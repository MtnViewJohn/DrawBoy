/*
 *  driver.cpp
 *  DrawBoy
 */


#include "driver.h"
#include "args.h"
#include "term.h"
#include "draft.h"
#include <exception>
#include <sys/select.h>
#include <unistd.h>
#include <cstdio>
#include <map>
#include <string_view>
#include <array>

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

enum class PickAction {
    DontSend,
    Send,
    SendFinally,
};

struct View
{
    Term& term;
    Options& opts;
    
    draft& draftContent;
    int pick;
    TabbyPick tabbyPick = TabbyPick::A;
    std::string pickValue;
    int parenLevel = 0;
    
    std::array<color, 4> weftColors;
    size_t weftIndex = 0;
    bool lastBell = false;
    
    std::string loomOutput;
    Shed loomState = Shed::Unknown;
    bool pendingPick = false;
    bool waitingForSolenoids = false;

    Mode mode = Mode::Weave;
    Mode oldMode = Mode::Weave;
    bool weaveForward = true;
    
    View(Term& t, Options& o)
    : term(t), opts(o), draftContent(*o.draftContents), pick(o.pick - 1)
    {}
    
    void handleEvent(const Term::Event& ev);
    bool handleGlobalEvent(const Term::Event& ev);
    bool handlePickEvent(const Term::Event& ev);
    bool handlePickEntryEvent(const Term::Event& ev);
    bool handlePickListEntryEvent(const Term::Event& ev);

    void sendPick(uint64_t _pick);
    void sendToLoom(std::string_view msg);
    
    void nextPick(bool forward);
    void setPick(int newPick);
    void displayPick(PickAction sendToLoom);
    void displayPrompt();
    void run();
    
    const char* toColor(const color& c)
    { return opts.ansi == ANSIsupport::no ? "" : Term::colorToStyle(c, opts.ansi == ANSIsupport::truecolor); }
    const char* bold()
    { return opts.ansi == ANSIsupport::no ? "" : Term::Style::bold; }
    const char* reset()
    { return opts.ansi == ANSIsupport::no ? "" : Term::Style::reset; }
};

void
View::displayPick(PickAction _sendToLoom)
{
    // Compute liftplan for pick, inverting if dobby type does not match wif type
    uint64_t lift = 0;
    uint64_t liftMask = (1 << draftContent.maxShafts) - 1;
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
            lift = draftContent.liftplan[(size_t)wifPick];
            weftColor = draftContent.weftColor[(size_t)(wifPick)];
            
            if ((opts.dobbyType == DobbyType::Negative &&  draftContent.risingShed) ||
                (opts.dobbyType == DobbyType::Positive && !draftContent.risingShed))
            {
                lift ^= liftMask;
            }
        }
    }

    bool emptyLift =  (lift & liftMask) == 0 || (lift & liftMask) == liftMask;
    if (emptyLift) weftColor = color();
    
    // Output drawdown
    std::putchar('\r');
    int drawdownWidth = term.cols() - (draftContent.maxShafts + 24);
    if (drawdownWidth > draftContent.ends) drawdownWidth = draftContent.ends;
    if (drawdownWidth < 10) drawdownWidth = 10;
    for (size_t i = (size_t)drawdownWidth; i > 0 ; --i) {
        bool activated = draftContent.threading[i] & lift;
        bool raised = ( activated && opts.dobbyType == DobbyType::Positive) ||
                      (!activated && opts.dobbyType == DobbyType::Negative);
        
        color& c = raised ? draftContent.warpColor[i] : weftColor;
        std::fputs(toColor(c), stdout);
        if (opts.ascii)
            std::putchar(raised ? '|' : '-');
        else
            std::fputs(raised ? "\xE2\x95\x91" : "\xE2\x95\x90", stdout);
    }
    
    // Output direction arrows and pick #
    std::fputs(toColor(weftColor), stdout);
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
    for (uint64_t shaftMask = 1; shaftMask != (1ull << draftContent.maxShafts); shaftMask <<= 1)
        if (shaftMask & lift)
            std::fputs(opts.ascii ? "*" : "\xE2\x96\xA0", stdout);
        else
            std::putchar(' ');
    std::putchar('|');
    std::fputs(reset(), stdout);
    if (emptyLift) std::fputs(" EMPTY", stdout);
    const char* endMessage = nullptr;
    if (loomState != Shed::Closed && _sendToLoom == PickAction::Send) endMessage = "PENDING";
    if (loomState == Shed::Closed && _sendToLoom == PickAction::SendFinally) endMessage = "SENT";
    if (endMessage)
        std::printf(" %s%s%s", bold(), endMessage, reset());
    
    if (loomState == Shed::Closed && _sendToLoom != PickAction::DontSend) {
        weftColors[weftIndex & 3] = weftColor;
        bool bell;
        switch (opts.colorAlert) {
            case ColorAlert::None:
                bell = false;
                break;
            case ColorAlert::Simple:
            case ColorAlert::Pulse:
                bell = weftColors[(weftIndex + 3) & 3] != weftColor;
                break;
            case ColorAlert::Alternating:
                bell = weftColors[(weftIndex + 2) & 3] != weftColor;
                break;
        }
        bell = bell && weftIndex > (opts.colorAlert == ColorAlert::Alternating ? 1 : 0);
        if (bell && !(lastBell && opts.colorAlert == ColorAlert::Pulse))
            std::putchar('\a');
        lastBell = bell;
        ++weftIndex;
    }
    
    if (_sendToLoom != PickAction::DontSend)
        sendPick(lift);

    Term::clearToEOL();
    std::fputs("\r\n", stdout);
}

void
View::displayPrompt()
{
    static const char* menu = "T)abby  L)iftplan  R)everse  S)elect pick  P)ick list  Q)uit   ";
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
                std::printf("[%s:%c] %s", ModePrompt[mode], wifPick == -1 ? 'A' : 'B', menu);
            else
                std::printf("[%s:%d] %s", ModePrompt[mode], wifPick, menu);
            break;
        }
        case Mode::Tabby:
            std::printf("[%s:%c] %s", ModePrompt[mode], tabbyPick == TabbyPick::A ? 'A' : 'B', menu);
            break;
        default:
            std::printf("[%s] %s", ModePrompt[mode], menu);
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
                std::putchar('q');
                std::fflush(stdout);
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
                parenLevel = 0;
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
                nextPick(false);
                break;
            case Term::Key::Down:
            case Term::Key::Right:
                nextPick(true);
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
        if (ev.character == '\x7f') {
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
        if (std::strchr("0123456789ABabx-~(),", ev.character)) {
            if (ev.character == ')' && parenLevel == 0) {
                std::putchar('\a');
                std::fflush(stdout);
                return true;
            }
            if (ev.character == '(') ++parenLevel;
            if (ev.character == ')') --parenLevel;
            pickValue.push_back(ev.character);
            std::putchar(ev.character);
            std::fflush(stdout);
            return true;
        }
        if (ev.character == '\x7f') {
            if (pickValue.empty()) {
                std::putchar('\a');
            } else {
                pickValue.pop_back();
                displayPrompt();
            }
            return true;
        }
        if (ev.character == '\r') {
            if (parenLevel != 0) {
                std::putchar('\a');
                std::fflush(stdout);
                return true;
            }
            try {
                opts.parsePicks(pickValue, draftContent.picks);
                pick = 0;
                mode = Mode::Weave;
                displayPick(PickAction::Send);
            } catch (std::exception& e) {
                std::printf("\r\n\a%s%s%s\r\n", bold(), e.what(), reset());
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
View::nextPick(bool forward)
{
    switch (mode) {
        case Mode::Weave:
                                //   VV     logical XNOR
            setPick(pick + ((forward == weaveForward) ? 1 : -1));
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
                fd_set wrfds = {};
                fd_set rdfds = {};
                int selectresult;
                
                tv.tv_sec = term.pendingEvent() ? 0 : 1;
                FD_ZERO(&wrfds);
                FD_ZERO(&rdfds);
                std::putchar('>');
                std::fflush(stdout);
                FD_SET(opts.loomDeviceFD, &wrfds);
                FD_SET(STDIN_FILENO, &rdfds);
                selectresult = ::select(opts.loomDeviceFD + 1, &rdfds, &wrfds, nullptr, &tv);
                if (selectresult == -1 && errno != EINTR)
                    throw make_system_error("loom select failed");
                if (FD_ISSET(STDIN_FILENO, &rdfds) || term.pendingEvent()) {
                    Term::Event ev = term.getEvent();
                    if (ev.type != Term::EventType::None)
                        handleEvent(ev);
                }
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
    for (int shaft = 0; shaft < draftContent.maxShafts; shaft += 4) {
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
        timeval threesec{term.pendingEvent() ? 0 : 3, 0};
        
        int nfds = ::select(opts.loomDeviceFD + 1, &rdset, nullptr, nullptr, &threesec);
        
        if (nfds == -1 && errno != EINTR)
            throw make_system_error("select failed");

        if (nfds == 0 && waitingForSolenoids) {
            sendToLoom("\x0f\x07");
            std::putchar('.');
        }
        
        if (FD_ISSET(STDIN_FILENO, &rdset) || term.pendingEvent() || nfds == -1) {
            Term::Event ev = term.getEvent();
            if (ev.type != Term::EventType::None)
                handleEvent(ev);
        }
        
        if (FD_ISSET(opts.loomDeviceFD, &rdset) && mode != Mode::Quit) {
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
                        if (opts.ansi != ANSIsupport::no)
                            Term::moveCursorRel(-1, 0);
                        displayPick(PickAction::SendFinally);
                        displayPrompt();
                    } else {
                        nextPick(true);
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
    Term term(opts.ansi != ANSIsupport::no);
    
    if (!term.good())
        throw std::runtime_error("Could not open terminal.");
    
    View view(term, opts);
    view.run();
}
