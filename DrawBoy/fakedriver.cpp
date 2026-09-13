//
//  fakedriver.cpp
//  FakeLoom
//
//  Created by John Horigan on 12/13/24.
//

#include "driver.h"
#include "fakeargs.h"
#include "term.h"
#include <csignal>
#include "ipc.h"
#include <cstring>
#include <chrono>
#include <charconv>
#include <string_view>
#include <cassert>
#include "serial.h"

enum class Shed {
    Up,
    Down,
    Unknown,
};

enum class Mode {
    Run,
    Quit,
    Closed,
};

enum class Solenoid {
    Normal,
    Reset,
};

enum class LoopingState {
    ShouldQuit,
    ShouldWait,
};

struct View
{
    Term& term;
    Options& opts;
    
    serial socket;
    
    Mode mode = Mode::Run;
    
    std::string DrawBoyOutput;
    Shed loomState = Shed::Unknown;
    Solenoid solenoidState = Solenoid::Normal;
    
    std::string autoInput;
    bool autoReset;
    std::chrono::time_point<std::chrono::system_clock> autoDelay;

    View(Term& t, Options& o)
    : term(t), opts(o), autoInput(o.autoInput), autoReset(opts.autoReset),
      autoDelay(std::chrono::system_clock::now())
    {}
    
    void handleEvent(const Term::Event& ev);
    
    void sendToDrawBoy(std::string_view msg);
    
    void displayPrompt();
    LoopingState connect();
    LoopingState run(IPC::Server& serverIPC, serial& server);
};

void
View::handleEvent(const Term::Event &ev)
{
    const char* armsDown = opts.cd4 ? "<down>" : "\x62\x03";
    const char* armsUp = opts.cd4 ? "<up>" : "\x61\x03";
    const char* armsNeutral = "<arm null>";    // CD IV only

    switch (ev.type) {
        case Term::EventType::Char: {
            switch (ev.character) {
                case 'q':
                case 'Q':
                case '\x03':      // control-c
                    mode = Mode::Quit;
                    return;
                    
                case '\x0c':      // control-l  - like in vi!
                    displayPrompt();
                    return;
                    
                case ' ':
                    sendToDrawBoy(armsNeutral);
                    loomState = Shed::Unknown;
                    std::fputs("0", stdout);
                    std::fflush(stdout);
                    return;

                case 's':
                case 'S':
                    if (solenoidState != Solenoid::Reset)
                        std::fputs("\r\nNot in solenoid reset state.\r\n", stdout);
                    else
                        std::fputs("  sending solenoid reset response.", stdout);
                    sendToDrawBoy("\x7f\03");
                    displayPrompt();
                    solenoidState = Solenoid::Normal;
                    return;
                    
                default:
                    break;
            }
            break;
        }
            
        case Term::EventType::Key: {
            switch (ev.key) {
                case Term::Key::Up:
                    if (loomState == Shed::Up)
                        std::fputs("\r\nArms were already raised.\r\n", stdout);
                    else
                        std::fputs(opts.ascii ? "^" : "\xE2\x86\x91", stdout);
                    std::fflush(stdout);
                    sendToDrawBoy(armsUp);
                    loomState = Shed::Up;
                    break;
                    
                case Term::Key::Down:
                    if (loomState == Shed::Down)
                        std::fputs("\r\nArms were already lowered.\r\n", stdout);
                    else
                        std::fputs(opts.ascii ? "v" : "\xE2\x86\x93", stdout);
                    std::fflush(stdout);
                    sendToDrawBoy(armsDown);
                    loomState = Shed::Down;
                    break;
                    
                default:
                    break;
            }
            break;
        }
            
        case Term::EventType::Resize: {
            displayPrompt();
            return;
        }
            
        default:
            break;
    }
}

void
View::displayPrompt()
{
    std::fputs("\r\ns)end solenoid reset   up/down arrows raise/lower arms   q)uit", stdout);
    std::fflush(stdout);
}

void
View::sendToDrawBoy(std::string_view msg)
{
    while (!msg.empty() && mode != Mode::Quit) {
        auto sent = socket.write(msg);
        if (sent >= 0) {
            // sent partial or all the remaining data
            assert(sent > 0);
            msg.remove_prefix((size_t)sent);
        } else {
            switch (errno) {
                case EPIPE:
                    mode = Mode::Closed;
                    return;
                case EAGAIN:
#if EAGAIN != EWOULDBLOCK
                case EWOULDBLOCK:
#endif
                case EINTR:
                    socket.waitWrite(1);
                    break;
                default:
                    throw make_system_error("loom write failed");
            }
        }
    }
}

LoopingState
View::connect()
{
    const char* shaftChar = opts.ascii ? "*" : "\xE2\x96\xA0";
    char termChar = opts.cd4 ? '\r' : '\x07';
    const char* loomReset = opts.cd4 ? "\r" : "\x0f\x03";

    while (mode == Mode::Run) {
        int tdelay = term.pendingEvent() ? 0 : 1;
        if (!autoInput.empty() && !autoReset && std::isdigit((unsigned char)autoInput.front())) {
            size_t pos = 0;
            int delay = std::stoi(autoInput, &pos);
            autoDelay = std::chrono::system_clock::now() + std::chrono::seconds(delay);
            autoInput.erase(0, pos);
            std::printf("\r\nInserting %d second delay. ", tdelay);
        }

        auto ready = socket.waitRead(tdelay);
        
        if (ready._nfds == -1 && errno != EINTR)
            throw make_system_error("select failed(1)");

        if (!autoInput.empty() && !autoReset &&
            std::chrono::system_clock::now() > autoDelay &&
            std::isalpha((unsigned char)autoInput.front()))
        {
            auto c = autoInput.front();
            autoInput.erase(0, 1);
            switch (c) {
                case 'u':
                case 'd': {
                    Term::Event ev{
                        Term::EventType::Key,
                        ' ',
                        c == 'u' ? Term::Key::Up : Term::Key::Down,
                        false, false, false, false, ""
                    };
                    std::printf("\r\nInserting %s event. ", c == 'u' ? "UP" : "DOWN");
                    handleEvent(ev);
                    break;
                }
                default: {
                    Term::Event ev{
                        Term::EventType::Char,
                        c,
                        Term::Key::Up,
                        false, false, false, false, ""
                    };
                    std::printf("\r\nInserting '%c' event. ", c);
                    handleEvent(ev);
                    break;
                }
            }
        }

        if (ready._stdinReady || term.pendingEvent() || ready._nfds == -1) {
            Term::Event ev = term.getEvent();
            if (ev.type == Term::EventType::None) continue;
            handleEvent(ev);
        }
        
        if (ready._loomReady) {
            char c;
            auto n = socket.read(c);
            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK)
                    continue;
                else
                    throw make_system_error("error in read");
            }
            if (n == 0) return LoopingState::ShouldWait;
            DrawBoyOutput.push_back(c);
            
            if (!DrawBoyOutput.empty() && DrawBoyOutput.back() == termChar) {
                if (DrawBoyOutput == loomReset) {
                    if (autoReset && !opts.cd4) {
                        std::fputs("\r\nResponding to solenoid reset command.\r\n", stdout);
                        sendToDrawBoy("\x7f\03");
                        autoReset = false;
                    } else if (autoReset && opts.cd4) {
                        std::fputs("\r\nSending loom greeting.\r\n", stdout);
                        std::string greeting = std::format("<Compu-Dobby IV, {}H, {} Dobby, HW A.1, FW 0.1.0>\n\r<Password:>",
                                                           opts.maxShafts, opts.dobbyType == DobbyType::Positive ? "Pos" : "Neg");
                        sendToDrawBoy(greeting);
                    } else {
                        std::fputs("\r\nSolenoid reset command received.\r\n", stdout);
                        solenoidState = Solenoid::Reset;
                    }
                } else if (DrawBoyOutput == "chico\r") {
                    sendToDrawBoy("<ready>");
                    std::fputs("\r\nPassword received.\r\n", stdout);
                } else if (!opts.cd4 || DrawBoyOutput.starts_with("pick ")) {
                    std::fputs(loomState == Shed::Down ? "\x1b[42;30m" : "\x1b[41;30m", stdout);
                    uint64_t lift = 0;
                    bool unexpected = false;
                    uint64_t shafts = 0;
                    std::fputs("\r\n", stdout);
                    if (opts.cd4) {
                        auto str = DrawBoyOutput.c_str() + 5;
                        uint64_t shaft;
                        while (*str != '\r') {
                            auto res = std::from_chars(str, DrawBoyOutput.c_str() + DrawBoyOutput.size(), shaft, 10);
                            if (res.ec != std::errc()) {
                                unexpected = true;
                                str = "\r";
                            } else {
                                str = *(res.ptr) == ',' ? res.ptr + 1 : res.ptr;
                                lift |= 1ull << (shaft - 1);
                                if (shaft > shafts) shafts = shaft;
                            }
                        }
                    } else {
                        for (size_t i = 0; i < DrawBoyOutput.length(); ++i) {
                            uint64_t uc = (unsigned char)DrawBoyOutput[i];
                            std::printf("0x%02x ", (int)uc);
                            if (uc >= 0x10 && uc <= 0xaf) {
                                lift |= (uc & 0xf) << (((uc >> 4) - 1) << 2);
                                uint64_t shaft = (uc & 0xf0) >> 2;
                                if (shaft > shafts) shafts = shaft;
                            } else if (uc != 0x07) {
                                unexpected = true;
                            }
                        }
                    }
                    std::putchar('|');
                    bool tooMany = shafts > (uint64_t)opts.maxShafts;
                    shafts = (uint64_t)opts.maxShafts;
                    for (uint64_t shaft = 0; shaft < shafts; ++shaft)
                        std::fputs((lift & (1ull << shaft)) ? shaftChar : " ", stdout);
                    std::putchar('|');
                    std::printf("%s%s %s %s%s\r\n", Term::Style::reset, opts.ascii ? "" : Term::Style::bold,
                                tooMany ? "too many shafts!" : "",
                                unexpected ? "unexpected character!" : "",
                                opts.ascii ? "" : Term::Style::reset);
                    if (opts.cd4)
                        sendToDrawBoy("<ready>");
                } else if (DrawBoyOutput == "clear\r" || DrawBoyOutput == "close\r") {
                    std::printf("\r\n%s\n", DrawBoyOutput.c_str());
                    sendToDrawBoy("<ready>");
                } else {
                    DrawBoyOutput.pop_back();
                    std::printf("\r\n%s%sUnexpected input from driver: %s%s",
                                Term::Style::reset, opts.ascii ? "" : Term::Style::bold,
                                DrawBoyOutput.c_str(),
                                opts.ascii ? "" : Term::Style::reset);
                }
                DrawBoyOutput.clear();
                displayPrompt();
            }
        }
    }
    
    socket.close();
    return mode == Mode::Quit ? LoopingState::ShouldQuit : LoopingState::ShouldWait;
}

LoopingState
View::run(IPC::Server& serverIPC, serial& server)
{
    std::fputs("\r\n\n\nWaiting for DrawBoy   q)uit", stdout);
    std::fflush(stdout);

    while (true) {
        auto ready = server.waitRead(term.pendingEvent() ? 0 : 1);
        
        if (ready._nfds == -1 && errno != EINTR)
            throw make_system_error("select failed(2)");

        if (ready._stdinReady || term.pendingEvent() || ready._nfds == -1) {
            Term::Event ev = term.getEvent();
            if (ev.type == Term::EventType::Char) {
                if (ev.character == '\x03' || ev.character == 'q' || ev.character == 'Q')
                    return LoopingState::ShouldQuit;
            }
        }
        
        if (ready._loomReady) {
            try {
                auto ac = serverIPC.accept();
                if (!ac.has_value()) continue;
                socket = serial(ac->fd(), serial::CxnType::Socket);
                return connect();
            } catch (IPC::SocketError& se) {
                std::printf("\r\nClient connection failed: %s, ignoring\r\n", se.what());
                std::fflush(stdout);
            }
        }
    }
}

void
driver(Options& opts)
{
    IPC::Server serverIPC(opts.socketPath);
    serial server(serverIPC.fd(), serial::CxnType::Socket);
    LoopingState loop = LoopingState::ShouldWait;
    std::signal(SIGPIPE, SIG_IGN);
    
    do {
        Term term;
        
        if (!term.good())
            throw std::runtime_error("Could not open terminal.");
        
        View view(term, opts);
        loop = view.run(serverIPC, server);
        std::fputs("\r\n\nDrawBoy closed.\r\n", stdout);
    } while (loop != LoopingState::ShouldQuit);
    serverIPC.release();    // server object will close socket descriptor
}
