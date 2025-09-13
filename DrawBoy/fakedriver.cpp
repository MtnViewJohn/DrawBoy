//
//  fakedriver.cpp
//  FakeLoom
//
//  Created by John Horigan on 12/13/24.
//

#include "driver.h"
#include "fakeargs.h"
#include "term.h"
#include <sys/select.h>
#include <unistd.h>
#include <csignal>
#include "ipc.h"
#include <cstring>
#include <chrono>

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
    
    int socketFD = -1;
    
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
    
    void sendToDrawBoy(const char *msg);
    
    void displayPrompt();
    LoopingState connect();
    LoopingState run(IPC::Server& server);
};

void
View::handleEvent(const Term::Event &ev)
{
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
                    sendToDrawBoy("\x61\x03");
                    loomState = Shed::Up;
                    break;
                    
                case Term::Key::Down:
                    if (loomState == Shed::Down)
                        std::fputs("\r\nArms were already lowered.\r\n", stdout);
                    else
                        std::fputs(opts.ascii ? "v" : "\xE2\x86\x93", stdout);
                    std::fflush(stdout);
                    sendToDrawBoy("\x62\x03");
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
View::sendToDrawBoy(const char *msg)
{
    size_t remaining = std::strlen(msg);
    size_t sent = 0;
    
    while (remaining > 0  &&  mode != Mode::Quit)
    {
        auto result = ::write(socketFD, msg + sent, remaining);
        if (result >= 0) {
            // sent partial or all the remaining data
            sent += (size_t)result;
            remaining -= (size_t)result;
        } else {
            int err = errno;
            if (err == EPIPE) {
                mode = Mode::Closed;
                return;
            }
            if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR) {
                timeval tv = {};
                fd_set fds = {};
                int selectresult;
                
                tv.tv_sec = 1;
                FD_ZERO(&fds);
                FD_SET(socketFD, &fds);
                selectresult = ::select(socketFD + 1, nullptr, &fds, nullptr, &tv);
                if (selectresult == -1 && errno != EINTR)
                    throw make_system_error("loom select failed");
            } else {
                throw make_system_error("loom write failed");
            }
        }
    }
}

LoopingState
View::connect()
{
    const char* shaftChar = opts.ascii ? "*" : "\xE2\x96\xA0";
    
    while (mode == Mode::Run) {
        fd_set rdset;
        FD_SET(STDIN_FILENO, &rdset);
        FD_SET(socketFD, &rdset);
        timeval onesec{term.pendingEvent() ? 0 : 1, 0};
        if (!autoInput.empty() && !autoReset && std::isdigit((unsigned char)autoInput.front())) {
            size_t pos = 0;
            int delay = std::stoi(autoInput, &pos);
            autoDelay = std::chrono::system_clock::now() + std::chrono::seconds(delay);
            autoInput.erase(0, pos);
            std::printf("\r\nInserting %d second delay. ", (int)onesec.tv_sec);
        }

        int nfds = ::select(socketFD + 1, &rdset, nullptr, nullptr, &onesec);
        
        if (nfds == -1 && errno != EINTR)
            throw make_system_error("select failed");

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

        if (FD_ISSET(STDIN_FILENO, &rdset) || term.pendingEvent() || nfds == -1) {
            Term::Event ev = term.getEvent();
            if (ev.type == Term::EventType::None) continue;
            handleEvent(ev);
        }
        
        if (FD_ISSET(socketFD, &rdset)) {
            char c;
            auto n = ::read(socketFD, &c, 1);
            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK)
                    continue;
                else
                    throw make_system_error("error in read");
            }
            if (n == 0) return LoopingState::ShouldWait;
            DrawBoyOutput.push_back(c);
            
            if (!DrawBoyOutput.empty() && DrawBoyOutput.back() == '\x07') {
                if (DrawBoyOutput == "\x0f\x07") {
                    if (autoReset) {
                        std::fputs("\r\nResponding to solenoid reset command.\r\n", stdout);
                        sendToDrawBoy("\x7f\03");
                        autoReset = false;
                    } else {
                        std::fputs("\r\nSolenoid reset command received.\r\n", stdout);
                        solenoidState = Solenoid::Reset;
                    }
                } else {
                    std::fputs(loomState == Shed::Down ? "\x1b[42;30m" : "\x1b[41;30m", stdout);
                    uint64_t lift = 0;
                    bool unexpected = false;
                    uint64_t shafts = 0;
                    std::fputs("\r\n", stdout);
                    for (size_t i = 0; i < DrawBoyOutput.length(); ++i) {
                        uint64_t uc = (unsigned char)DrawBoyOutput[i];
                        std::printf("0x%02x ", (int)uc);
                        if (uc >= 0x10 && uc <= 0xaf) {
                            lift |= (uc & 0xf) << (((uc >> 4) - 1) << 2);
                            uint64_t shaft = (uc & 0xf0) >> 2;
                            if (shaft > shafts) shafts = shaft;
                        } else if (uc != 0x07)
                            unexpected = true;
                    }
                    std::putchar('|');
                    bool tooMany = shafts > (uint64_t)opts.maxShafts;
                    for (uint64_t shaft = 0; shaft < shafts; ++shaft)
                        std::fputs((lift & (1 << shaft)) ? shaftChar : " ", stdout);
                    std::putchar('|');
                    std::printf("%s%s %s %s%s\r\n", Term::Style::reset, opts.ascii ? "" : Term::Style::bold,
                                tooMany ? "too many shafts!" : "",
                                unexpected ? "unexpected character!" : "",
                                opts.ascii ? "" : Term::Style::reset);
                }
                DrawBoyOutput.clear();
                displayPrompt();
            }
        }
    }
    
    return mode == Mode::Quit ? LoopingState::ShouldQuit : LoopingState::ShouldWait;
}

LoopingState
View::run(IPC::Server& server)
{
    std::fputs("\r\n\n\nWaiting for DrawBoy   q)uit", stdout);
    std::fflush(stdout);

    while (true) {
        fd_set rdset;
        FD_SET(STDIN_FILENO, &rdset);
        FD_SET(server.fd(), &rdset);
        timeval onesec{term.pendingEvent() ? 0 : 1, 0};
        
        int nfds = select(server.fd() + 1, &rdset, nullptr, nullptr, &onesec);
        
        if (nfds == -1 && errno != EINTR)
            throw make_system_error("select failed");

        if (FD_ISSET(STDIN_FILENO, &rdset) || term.pendingEvent() || nfds == -1) {
            Term::Event ev = term.getEvent();
            if (ev.type == Term::EventType::Char) {
                if (ev.character == '\x03' || ev.character == 'q' || ev.character == 'Q')
                    return LoopingState::ShouldQuit;
            }
        }
        
        if (FD_ISSET(server.fd(), &rdset)) {
            try {
                auto ac = server.accept();
                if (!ac.has_value()) continue;
                socketFD = ac->fd();
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
    IPC::Server server(opts.socketPath);
    LoopingState loop = LoopingState::ShouldWait;
    std::signal(SIGPIPE, SIG_IGN);
    
    do {
        Term term;
        
        if (!term.good())
            throw std::runtime_error("Could not open terminal.");
        
        View view(term, opts);
        loop = view.run(server);
        std::fputs("\r\n\nDrawBoy closed.\r\n", stdout);
    } while (loop != LoopingState::ShouldQuit);
}
