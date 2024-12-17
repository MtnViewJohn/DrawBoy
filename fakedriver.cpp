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
#include <signal.h>
#include "ipc.h"

enum class Shed {
    Open,
    Closed,
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

    View(Term& t, Options& o)
    : term(t), opts(o)
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
                    if (loomState == Shed::Open)
                        std::fputs("\r\nArms were already raised.\r\n", stdout);
                    else
                        std::fputs(opts.ascii ? "^" : "\xE2\x86\x91", stdout);
                    std::fflush(stdout);
                    sendToDrawBoy("\x61\x03");
                    loomState = Shed::Open;
                    break;
                    
                case Term::Key::Down:
                    if (loomState == Shed::Closed)
                        std::fputs("\r\nArms were already lowered.\r\n", stdout);
                    else
                        std::fputs(opts.ascii ? "v" : "\xE2\x86\x93", stdout);
                    std::fflush(stdout);
                    sendToDrawBoy("\x62\x03");
                    loomState = Shed::Closed;
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
    std::size_t remaining = std::strlen(msg);
    std::size_t sent = 0;
    
    while (remaining > 0  &&  mode != Mode::Quit)
    {
        auto result = write(socketFD, msg + sent, remaining);
        if (result >= 0) {
            // sent partial or all the remaining data
            sent += (std::size_t)result;
            remaining -= (std::size_t)result;
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
                selectresult = select(socketFD + 1, nullptr, &fds, nullptr, &tv);
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
        timeval onesec{1,0};
        
        int nfds = select(socketFD + 1, &rdset, nullptr, nullptr, &onesec);
        
        if (nfds == -1 && errno != EINTR)
            throw make_system_error("select failed");

        if (nfds == 0) continue;
        
        if (FD_ISSET(STDIN_FILENO, &rdset) || nfds == -1) {
            Term::Event ev = term.getEvent();
            if (ev.type == Term::EventType::None) continue;
            handleEvent(ev);
        }
        
        if (FD_ISSET(socketFD, &rdset)) {
            char c;
            auto n = read(socketFD, &c, 1);
            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK)
                    continue;
                else
                    throw make_system_error("error in read");
            }
            if (n == 0) return LoopingState::ShouldWait;
            DrawBoyOutput.push_back(c);
            
            if (!DrawBoyOutput.empty()) {
                if (DrawBoyOutput.back() == '\x07') {
                    if (DrawBoyOutput == "\x0f\x07") {
                        std::fputs("\r\nSolenoid reset command received.\r\n", stdout);
                        solenoidState = Solenoid::Reset;
                    } else {
                        std::fputs(loomState == Shed::Closed ? "\x1b[42;30m" : "\x1b[41;30m", stdout);
                        std::uint64_t lift = 0;
                        bool unexpected = false;
                        std::uint64_t shafts = 0;
                        std::fputs("\r\n", stdout);
                        for (std::size_t i = 0; i < DrawBoyOutput.length(); ++i) {
                            std::uint64_t uc = (unsigned char)DrawBoyOutput[i];
                            std::printf("0x%02x ", (int)uc);
                            if (uc >= 0x10 && uc <= 0xaf) {
                                lift |= (uc & 0xf) << (((uc >> 4) - 1) << 2);
                                std::uint64_t shaft = (uc & 0xf0) >> 2;
                                if (shaft > shafts) shafts = shaft;
                            } else if (uc != 0x07)
                                unexpected = true;
                        }
                        std::fputc('|', stdout);
                        bool tooMany = shafts > (std::uint64_t)opts.maxShafts;
                        for (std::uint64_t shaft = 0; shaft < shafts; ++shaft)
                            std::fputs((lift & (1 << shaft)) ? shaftChar : " ", stdout);
                        std::fputc('|', stdout);
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
        timeval onesec{1,0};
        
        int nfds = select(server.fd() + 1, &rdset, nullptr, nullptr, &onesec);
        
        if (nfds == -1 && errno != EINTR)
            throw make_system_error("select failed");

        if (nfds == 0) continue;

        if (FD_ISSET(STDIN_FILENO, &rdset) || nfds == -1) {
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
    signal(SIGPIPE, SIG_IGN);
    
    do {
        Term term;
        
        if (!term.good())
            throw std::runtime_error("Could not open terminal.");
        
        View view(term, opts);
        loop = view.run(server);
        std::fputs("\r\n\nDrawBoy closed.\r\n", stdout);
    } while (loop != LoopingState::ShouldQuit);
}
