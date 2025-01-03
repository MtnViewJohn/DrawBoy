/*
 *  driver.cpp
 *  DrawBoy
 */


#include "driver.h"
#include "args.h"
#include "term.h"
#include "wif.h"
#include <exception>
#include <sys/select.h>
#include <unistd.h>
#include <cstdio>
#include <map>
#include <string_view>

enum class Mode {
    Weave,
    Tabby,
    PickEntry,
    PickListEntry,
    Quit,
};

enum class Shed {
    Open,
    Closed,
    Unknown,
};

std::map<Mode, const char*> ModePrompt{
    {Mode::Weave, "Weaving"},
    {Mode::Tabby, "Tabby"},
    {Mode::PickEntry, "Select pick"},
    {Mode::PickListEntry, "Enter pick list"},
    {Mode::Quit, "Quitting"},
};

enum class TabbyPick {
    A,
    B,
};

static const uint64_t NotAShed = 0xffffffff;

enum class PickAction {
    DontSend,
    Send,
};

struct View
{
    Term& term;
    Options& opts;
    
    wif& wifContents;
    int pick;
    TabbyPick tabbyPick = TabbyPick::A;
    std::string pickValue;
    
    std::string loomOutput;
    Shed loomState = Shed::Unknown;
    bool pendingPick = false;
    bool waitingForSolenoids = false;

    Mode mode = Mode::Weave;
    Mode oldMode = Mode::Weave;
    bool weaveForward = true;
    
    View(Term& t, Options& o)
    : term(t), opts(o), wifContents(o.wifContents), pick(o.pick - 1)
    {}
    
    void handleEvent(const Term::Event& ev);
    bool handleGlobalEvent(const Term::Event& ev);
    bool handlePickEvent(const Term::Event& ev);
    bool handlePickEntryEvent(const Term::Event& ev);
    bool handlePickListEntryEvent(const Term::Event& ev);

    void sendPick(uint64_t _pick = NotAShed);
    void sendToLoom(std::string_view msg);
    
    void nextPick();
    void prevPick();
    void setPick(int newPick);
    void displayPick(PickAction sendToLoom);
    void displayPrompt();
    void run();
};

void
View::displayPick(PickAction _sendToLoom)
{
    // Compute liftplan for pick, inverting if dobby type does not match wif type
    uint64_t lift = 0;
    uint64_t liftMask = (1 << wifContents.maxShafts) - 1;
    color weftColor;

    if (mode == Mode::Tabby) {
        lift = tabbyPick == TabbyPick::A ? opts.tabbyA : opts.tabbyB;
        weftColor = opts.tabbyColor;
    } else {
        size_t zpick = (size_t)(pick) % opts.picks.size();
        int wifPick = opts.picks[zpick];

        if (wifPick < 0) {
            lift = wifPick == -1 ? opts.tabbyA : opts.tabbyB;
            weftColor = opts.tabbyColor;
        } else {
            lift = wifContents.liftplan[(size_t)wifPick];
            weftColor = wifContents.weftColor[(size_t)(wifPick)];
            
            if ((opts.dobbyType == DobbyType::Negative &&  wifContents.risingShed) ||
                (opts.dobbyType == DobbyType::Positive && !wifContents.risingShed))
            {
                lift ^= liftMask;
            }
        }
    }

    bool emptyLift =  (lift & liftMask) == 0 || (lift & liftMask) == liftMask;
    if (emptyLift) weftColor = color();
    
    // Output drawdown
    std::putchar('\r');
    int drawdownWidth = term.cols() - (wifContents.maxShafts + 24);
    if (drawdownWidth > wifContents.ends) drawdownWidth = wifContents.ends;
    if (drawdownWidth < 10) drawdownWidth = 10;
    for (size_t i = (size_t)drawdownWidth; i > 0 ; --i) {
        bool activated = wifContents.threading[i] & lift;
        bool raised = ( activated && opts.dobbyType == DobbyType::Positive) ||
                      (!activated && opts.dobbyType == DobbyType::Negative);
        
        if (opts.ansi != ANSIsupport::no) {
            color& c = raised ? wifContents.warpColor[i] : weftColor;
            std::fputs(Term::colorToStyle(c, opts.ansi == ANSIsupport::truecolor), stdout);
        }
        if (opts.ascii)
            std::putchar(raised ? '|' : '-');
        else
            std::fputs(raised ? "\xE2\x95\x91" : "\xE2\x95\x90", stdout);
    }
    
    // Output direction arrows and pick #
    if (opts.ansi != ANSIsupport::no) {
        std::fputs(Term::colorToStyle(weftColor, opts.ansi == ANSIsupport::truecolor), stdout);
    }
    const char *leftArrow = "", *rightArrow = "";
    if (weaveForward)
        rightArrow = opts.ascii ? " --> " : " \xE2\xAE\x95  ";
    else
        leftArrow = opts.ascii ? " <-- " : " \xE2\xAC\x85  ";
    if (mode == Mode::Tabby)
        std::printf(" %s   %c%s |", leftArrow, tabbyPick == TabbyPick::A ? 'A' : 'B', rightArrow);
    else
        std::printf(" %s%4d%s |", leftArrow, pick + 1, rightArrow);
    
    // Output liftplan
    for (uint64_t shaftMask = 1; shaftMask != (1ull << wifContents.maxShafts); shaftMask <<= 1)
        if (shaftMask & lift)
            std::fputs(opts.ascii ? "*" : "\xE2\x96\xA0", stdout);
        else
            std::putchar(' ');
    std::putchar('|');
    if (opts.ansi != ANSIsupport::no) std::fputs(Term::Style::reset, stdout);
    if (emptyLift) std::fputs(" EMPTY", stdout);
    if (loomState != Shed::Closed && _sendToLoom == PickAction::Send)
        std::printf(" %sPENDING%s", opts.ansi == ANSIsupport::no ? "" : Term::Style::bold,
                                    opts.ansi == ANSIsupport::no ? "" : Term::Style::reset);
    
    if (_sendToLoom == PickAction::Send)
        sendPick(lift);

    Term::clearToEOL();
    std::fputs("\r\n", stdout);
}

void
View::displayPrompt()
{
    std::putchar('\r');
    switch (mode) {
        case Mode::PickEntry:
            std::printf("Enter the new pick number: %s", pickValue.c_str());
            break;
        case Mode::PickListEntry:
            std::printf("Enter the new pick list: %s", pickValue.c_str());
            break;
        case Mode::Weave: {
            int wifPick = opts.picks[(size_t)(pick) % opts.picks.size()];
            if (wifPick < 0)
                std::printf("[%s:%c]  t)abby mode  l)iftplan mode  r)everse  s)elect next pick  P)ick list  q)uit   ",
                            ModePrompt[mode], wifPick == -1 ? 'A' : 'B');
            else
                std::printf("[%s:%d]  t)abby mode  l)iftplan mode  r)everse  s)elect next pick  P)ick list  q)uit   ",
                            ModePrompt[mode], wifPick);
            break;
        }
        case Mode::Tabby:
            std::printf("[%s:%c]  t)abby mode  l)iftplan mode  r)everse  s)elect next pick  P)ick list  q)uit   ",
                        ModePrompt[mode], tabbyPick == TabbyPick::A ? 'A' : 'B');
            break;
        default:
            std::printf("[%s]  t)abby mode  l)iftplan mode  r)everse  s)elect next pick  P)ick list  q)uit   ", ModePrompt[mode]);
            break;
    }
    Term::clearToEOL();
}

void
View::handleEvent(const Term::Event &ev)
{
    if (handleGlobalEvent(ev))
        return;
    
    switch (mode) {
        case Mode::Weave:
        case Mode::Tabby:
            if (handlePickEvent(ev))
                return;
            break;
        case Mode::PickEntry:
            if (handlePickEntryEvent(ev))
                return;
            break;
        case Mode::PickListEntry:
            if (handlePickListEntryEvent(ev))
                return;
            break;
        default:
            break;
    }
}

bool
View::handleGlobalEvent(const Term::Event& ev)
{
    switch (ev.type) {
        case Term::EventType::Char: {
            switch (ev.character) {
                case '\x03':      // control-c
                    mode = Mode::Quit;
                    return true;
                    
                case '\x0c':      // control-l  - like in vi!
                    displayPick(PickAction::DontSend);
                    displayPrompt();
                    return true;
                    
                case '\x1b':      // escape
                    mode = oldMode;
                    displayPrompt();
                    return true;
                    
                default:
                    break;
            }
            break;
        }
            
        case Term::EventType::Resize: {
            Term::moveCursorRel(-1, 0);
            displayPick(PickAction::DontSend);
            displayPrompt();
            return true;
        }
            
        default:
            break;
    }
    return false;
}

bool
View::handlePickEvent(const Term::Event &ev)
{
    if (ev.type == Term::EventType::Char) {
        switch (ev.character) {
            case 't':
            case 'T':
                if (mode == Mode::Tabby) return true;
                mode = Mode::Tabby;
                tabbyPick = weaveForward ? TabbyPick::A : TabbyPick::B;
                displayPick(PickAction::Send);
                displayPrompt();
                return true;
            case 'l':
            case 'L':
                if (mode == Mode::Weave) return true;
                mode = Mode::Weave;
                displayPick(PickAction::Send);
                displayPrompt();
                return true;
            case 'q':
            case 'Q':
                mode = Mode::Quit;
                return true;
            case 'r':
            case 'R':
                weaveForward = !weaveForward;
                displayPick(PickAction::DontSend);
                displayPrompt();
                return true;
            case 's':
            case 'S':
                oldMode = mode;
                mode = Mode::PickEntry;
                pickValue.clear();
                displayPrompt();
                return true;
            case 'p':
            case 'P':
                oldMode = mode;
                mode = Mode::PickListEntry;
                pickValue.clear();
                displayPrompt();
                return true;
            default:
                break;
        }
    }
    
    if (ev.type == Term::EventType::Key) {
        switch (ev.key) {
            case Term::Key::Up:
            case Term::Key::Left:
                prevPick();
                break;
            case Term::Key::Down:
            case Term::Key::Right:
                nextPick();
                break;
            default:
                return false;
        }
        displayPick(PickAction::Send);
        displayPrompt();
        return true;
    }
    return false;
}

bool
View::handlePickEntryEvent(const Term::Event &ev)
{
    if (ev.type == Term::EventType::Char) {
        if ('0' <= ev.character && ev.character <= '9') {
            if (pickValue.empty() && ev.character == '0')
                return false;
            pickValue.push_back(ev.character);
            std::putchar(ev.character);
            std::fflush(stdout);
            return true;
        }
        if (ev.character == '\b') {
            if (pickValue.empty()) {
                std::putchar('\a');
            } else {
                pickValue.pop_back();
                displayPrompt();
            }
            return true;
        }
        if (ev.character == '\r') {
            if (!pickValue.empty()) {
                errno = 0;
                long p = std::strtol(pickValue.c_str(), nullptr, 10);
                if (errno || p > 9999 || p < 1) {
                    std::putchar('\a');
                } else {
                    pick = (int)p - 1;
                    mode = Mode::Weave;
                    displayPick(PickAction::Send);
                }
            } else {
                mode = oldMode;
            }
            displayPrompt();
            return true;
        }
    }

    return false;
}

bool
View::handlePickListEntryEvent(const Term::Event &ev)
{
    if (ev.type == Term::EventType::Char) {
        if (std::strchr("0123456789ABab-,", ev.character)) {
            pickValue.push_back(ev.character);
            std::putchar(ev.character);
            std::fflush(stdout);
            return true;
        }
        if (ev.character == '\b') {
            if (pickValue.empty()) {
                std::putchar('\a');
            } else {
                pickValue.pop_back();
                displayPrompt();
            }
            return true;
        }
        if (ev.character == '\r') {
            try {
                opts.parsePicks(pickValue, wifContents.picks);
                pick = 0;
                mode = Mode::Weave;
                displayPick(PickAction::Send);
            } catch (std::exception& e) {
                std::printf("\r\n\a%s%s%s\r\n", opts.ansi == ANSIsupport::no ? "" : Term::Style::bold,
                                      e.what(), opts.ansi == ANSIsupport::no ? "" : Term::Style::reset);
            }
            displayPrompt();
            return true;
        }
    }

    return false;
}

void
View::setPick(int newPick)
{
    pick = newPick;
    int psize = (int)opts.picks.size();
    if (pick >= 9999)
        pick -= (pick / psize) * psize;
    while (pick < 0)
        pick += psize;
}

void
View::nextPick()
{
    switch (mode) {
        case Mode::Weave:
            setPick(pick + (weaveForward ? 1 : -1));
            break;
        case Mode::Tabby:
            tabbyPick = tabbyPick == TabbyPick::A ? TabbyPick::B : TabbyPick::A;
            break;
        default:
            break;
    }
}

void
View::prevPick()
{
    switch (mode) {
        case Mode::Weave:
            setPick(pick + (weaveForward ? -1 : 1));
            break;
        case Mode::Tabby:
            tabbyPick = tabbyPick == TabbyPick::A ? TabbyPick::B : TabbyPick::A;
            break;
        default:
            break;
    }
}

void
View::sendToLoom(std::string_view msg)
{
    while (!msg.empty() && mode != Mode::Quit)
    {
        auto result = ::write(opts.loomDeviceFD, msg.data(), msg.length());
        if (result >= 0) {
            if (result == 0) std::putchar('>');
            // sent partial or all the remaining data
            msg.remove_prefix((size_t)result);
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                timeval tv = {};
                fd_set fds = {};
                int selectresult;
                
                tv.tv_sec = 1;
                FD_ZERO(&fds);
                std::putchar('>');
                FD_SET(opts.loomDeviceFD, &fds);
                selectresult = ::select(opts.loomDeviceFD + 1, nullptr, &fds, nullptr, &tv);
                if (selectresult == -1 && errno != EINTR)
                    throw make_system_error("loom select failed");
            } else {
                // from the write
                throw make_system_error("loom write failed");
            }
        }
    }
}

void
View::sendPick(uint64_t lift)
{
    if (loomState != Shed::Closed) {
        pendingPick = true;
        return;
    }
    
    pendingPick = false;
    
    char shaftCmd = '\x10';
    std::string command;
    for (int shaft = 0; shaft < wifContents.maxShafts; shaft += 4) {
        command.push_back(shaftCmd | (char)(lift & 0xf));
        shaftCmd += '\x10';
        lift >>= 4;
    }
    command.push_back('\x07');
    sendToLoom(command);
}

void
View::run()
{
    sendToLoom("\x0f\x07");
    waitingForSolenoids = true;
    displayPick(PickAction::Send);    // initialize pending pick
    displayPrompt();

    while (mode != Mode::Quit) {
        fd_set rdset;
        FD_SET(STDIN_FILENO, &rdset);
        FD_SET(opts.loomDeviceFD, &rdset);
        timeval threesec{3,0};
        
        int nfds = ::select(opts.loomDeviceFD + 1, &rdset, nullptr, nullptr, &threesec);
        
        if (nfds == -1 && errno != EINTR)
            throw make_system_error("select failed");

        if (nfds == 0) {
            if (waitingForSolenoids) {
                sendToLoom("\x0f\x07");
                std::putchar('.');
            }
            continue;
        }
        
        if (FD_ISSET(STDIN_FILENO, &rdset) || nfds == -1) {
            Term::Event ev = term.getEvent();
            if (ev.type != Term::EventType::None)
                handleEvent(ev);
        }
        
        if (FD_ISSET(opts.loomDeviceFD, &rdset)) {
            char c;
            int count = 0;
            while (true) {
                auto n = ::read(opts.loomDeviceFD, &c, 1);
                if (n < 0) {
                    if (errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK)
                        break;
                    else
                        throw make_system_error("error in read");
                }
                if (n == 0) {
                    if (count == 0)
                        throw std::runtime_error("\r\nLoom connection was closed.");
                    break;
                }
                loomOutput.push_back(c);
                ++count;
            };
            
            if (!loomOutput.empty() && loomOutput.back() == '\x03') {
                if (loomOutput == "\x7f\x03") {
                    waitingForSolenoids = false;
                    std::fputs(" solenoid reset received", stdout);
                }
                if (loomOutput == "\x62\x03" && loomState == Shed::Closed) {
                    loomState = Shed::Open;
                    std::fputs(" open", stdout);
                }
                if (loomOutput == "\x61\x03") {
                    loomState = Shed::Closed;
                    if (pendingPick) {
                        // Redraw the last pending pick to erase the 'PENDING'
                        Term::moveCursorRel(-1, 0);
                        displayPick(PickAction::Send);
                        displayPrompt();
                        std::printf("%s SENT%s ",
                                    opts.ansi == ANSIsupport::no ? "" : Term::Style::bold,
                                    opts.ansi == ANSIsupport::no ? "" : Term::Style::reset);
                    } else {
                        nextPick();
                        displayPick(PickAction::Send);
                        displayPrompt();
                    }
                }
                loomOutput.clear();
                std::fflush(stdout);
            }
        }
    }
}

void driver(Options& opts)
{
    Term term;
    
    if (!term.good())
        throw std::runtime_error("Could not open terminal.");
    
    View view(term, opts);
    view.run();
}
