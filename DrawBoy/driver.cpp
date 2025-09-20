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
#include <cassert>
#include <charconv>
#include <print>
#include <set>

namespace {
std::string pickString(int pick, bool padded)
{
    if (pick == TabbyA) return padded ? "   A" : "A";
    if (pick == TabbyB) return padded ? "   B" : "B";
    if (pick < 0) return padded ? "   X" : "X";
    
    if (padded)
        return std::format("{:4}", pick);
    else
        return std::format("{}", pick);
}
}

enum class Mode {
    Weave,
    Tabby,
    PickEntry,
    PickListEntry,
    Quit,
};

enum class Arms {
    Up,
    Down,
    Unknown,
};

std::map<Mode, const char*> ModePrompt{
    {Mode::Weave, "Weaving"},
    {Mode::Tabby, "Tabby"},
    {Mode::PickEntry, "Select pick"},
    {Mode::PickListEntry, "Enter pick list"},
    {Mode::Quit, "Quitting"},
};

struct View
{
    Term& term;
    Options& opts;
    
    draft& draftContent;
    int currentPick = 0, nextPick = 1;
    std::string pickValue;
    int parenLevel = 0;
    
    std::array<color, 4> weftColors;
    size_t weftIndex = 0;
    bool lastBell = false;
    
    std::string loomOutput;
    Arms loomState = Arms::Unknown;

    Mode mode = Mode::Weave;
    Mode oldMode = Mode::Weave;
    int oldPick = -1;
    bool weaveForward = true;
    
    View(Term& t, Options& o)
    : term(t), opts(o), draftContent(*o.draftContents), nextPick(o.pick - 1)
    {}
    
    void handleEvent(const Term::Event& ev);
    bool handleGlobalEvent(const Term::Event& ev);
    bool handlePickEvent(const Term::Event& ev);
    bool handlePickEntryEvent(const Term::Event& ev);
    bool handlePickListEntryEvent(const Term::Event& ev);

    void sendPick();
    void sendToLoom(std::string_view msg);
    std::pair<uint64_t, color> calculateLift(int pick);
    void advancePick(bool forward);
    void setPick(int newPick);
    color displayPick(const char* message = "");
    void colorCheck(color currentColor);
    void displayPrompt();
    void erasePrompt();
    void run();
    
    const char* toColor(const color& c)
    { return opts.ansi == ANSIsupport::no ? "" : Term::colorToStyle(c, opts.ansi == ANSIsupport::truecolor); }
    const char* bold()
    { return opts.ansi == ANSIsupport::no ? "" : Term::Style::bold; }
    const char* reset()
    { return opts.ansi == ANSIsupport::no ? "" : Term::Style::reset; }
};

std::pair<uint64_t, color>
View::calculateLift(int pick)
{
    // Compute liftplan for pick, inverting if dobby type does not match wif type
    uint64_t lift = 0;
    uint64_t liftMask = (1 << draftContent.maxShafts) - 1;
    color weftColor;
    
    if (opts.treadleThreading) {
        size_t zpick = (size_t)(pick % draftContent.ends + 1);
        return {draftContent.threading[zpick], draftContent.warpColor[zpick]};
    }

    if (pick < 0) {
        assert(pick == TabbyA || pick == TabbyB);
        lift = pick == TabbyA ? opts.tabbyA : opts.tabbyB;
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

    return {lift, weftColor};
}

color
View::displayPick(const char* message)
{
    auto [lift, weftColor] = calculateLift(currentPick);

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
    
    int cpick = currentPick < 0 ? currentPick : currentPick + 1;
    std::print(" {}{}{} |", leftArrow, pickString(cpick, true), rightArrow);
    
    // Output liftplan
    for (uint64_t shaftMask = 1; shaftMask != (1ull << draftContent.maxShafts); shaftMask <<= 1)
        if (shaftMask & lift)
            std::fputs(opts.ascii ? "*" : "\xE2\x96\xA0", stdout);
        else
            std::putchar(' ');
    std::putchar('|');
    std::fputs(reset(), stdout);
    std::fputs(message, stdout);

    Term::clearToEOL();
    std::fputs("\r\n", stdout);
    return weftColor;
}

void
View::colorCheck(color currentColor)
{
    weftColors[weftIndex & 3] = currentColor;
    bool bell;
    switch (opts.colorAlert) {
        case ColorAlert::None:
            bell = false;
            break;
        case ColorAlert::Simple:
        case ColorAlert::Pulse:
            bell = weftColors[(weftIndex + 3) & 3] != currentColor;
            break;
        case ColorAlert::Alternating:
            bell = weftColors[(weftIndex + 2) & 3] != currentColor;
            break;
    }
    bell = bell && weftIndex > (opts.colorAlert == ColorAlert::Alternating ? 1 : 0);
    if (bell && !(lastBell && opts.colorAlert == ColorAlert::Pulse))
        std::putchar('\a');
    lastBell = bell;
    ++weftIndex;
}

void
View::displayPrompt()
{
    static const char* menu = "T)abby  L)iftplan  R)everse  S)elect pick  P)ick list  Q)uit   ";
    std::putchar('\r');
    switch (mode) {
        case Mode::PickEntry:
            std::print("Enter the new pick number: {}", pickValue);
            break;
        case Mode::PickListEntry:
            std::print("Enter the new pick list: {}", pickValue);
            break;
        case Mode::Tabby:
        case Mode::Weave: {
            int cpick = currentPick < 0 ? currentPick : opts.picks[(size_t)(currentPick) % opts.picks.size()];
            int npick = nextPick < 0 ? nextPick : opts.picks[(size_t)(nextPick) % opts.picks.size()];
            const char* rightArrow = opts.ascii ? " --> " : " \xE2\xAE\x95  ";
            std::print("[{}:{}{}{}] {}", ModePrompt[mode], pickString(cpick, false),
                       rightArrow, pickString(npick, false), menu);
            break;
        }
        default:
            std::print("[{}] {}", ModePrompt[mode], menu);
            break;
    }
    Term::clearToEOL();
}

void
View::erasePrompt()
{
    std::putchar('\r');
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
                    displayPick();
                    if (loomState == Arms::Down)
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
            displayPick();
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
        char evChar = (char)std::tolower((int)ev.character);
        if (loomState != Arms::Down && evChar != 'q') {
            std::putchar('\a');
            std::fflush(stdout);
            return true;
        }
        switch (evChar) {
            case 't':
                if (mode == Mode::Tabby) return true;
                mode = Mode::Tabby;
                oldPick = nextPick;
                nextPick = weaveForward ? TabbyA : TabbyB;
                sendPick();
                displayPrompt();
                return true;
            case 'l':
                if (mode == Mode::Weave) return true;
                mode = Mode::Weave;
                nextPick = oldPick;
                sendPick();
                displayPrompt();
                return true;
            case 'q':
                mode = Mode::Quit;
                std::putchar('q');
                std::fflush(stdout);
                return true;
            case 'r':
                weaveForward = !weaveForward;
                nextPick = currentPick;
                advancePick(true);
                sendPick();
                displayPrompt();
                return true;
            case 's':
                oldMode = mode;
                mode = Mode::PickEntry;
                pickValue.clear();
                displayPrompt();
                return true;
            case 'p':
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
        if (loomState != Arms::Down) {
            std::putchar('\a');
            std::fflush(stdout);
            return true;
        }
        switch (ev.key) {
            case Term::Key::Up:
            case Term::Key::Left:
                advancePick(false);
                break;
            case Term::Key::Down:
            case Term::Key::Right:
                advancePick(true);
                break;
            default:
                return false;
        }
        sendPick();
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
                    nextPick = (int)p - 1;
                    mode = Mode::Weave;
                    sendPick();
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
                nextPick = 0;       // Current pick is from old pick list
                currentPick = -10;  // it is meaningless in new pick list
                mode = Mode::Weave;
                sendPick();
            } catch (std::exception& e) {
                std::print("\r\n\a{}{}{}\r\n", bold(), e.what(), reset());
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
    nextPick = newPick;
    int psize = opts.treadleThreading ? draftContent.ends : (int)opts.picks.size();
    if (nextPick >= 9999)
        nextPick -= (nextPick / psize) * psize;
    while (nextPick < 0)
        nextPick += psize;
}

void
View::advancePick(bool forward)
{
    switch (mode) {
        case Mode::Weave:
                                    //   VV     logical XNOR
            setPick(nextPick + ((forward == weaveForward) ? 1 : -1));
            break;
        case Mode::Tabby:
            nextPick = nextPick == TabbyA ? TabbyB : TabbyA;
            break;
        default:
            break;
    }
}

void
View::sendToLoom(std::string_view msg)
{
    while (!msg.empty())
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
View::sendPick()
{
    auto [lift, weftColor] = calculateLift(nextPick);
    std::string command;

    if (opts.compuDobbyGen < 4) {
        char shaftCmd = '\x10';
        for (int shaft = 0; shaft < draftContent.maxShafts; shaft += 4) {
            command.push_back(shaftCmd | (char)(lift & 0xf));
            shaftCmd += '\x10';
            lift >>= 4;
        }
        command.push_back('\x07');
    } else {
        bool first = true;
        if (lift == 0)
            return;
        command = "clear\rpick ";
        for (int shaft = 1; lift; lift >>= 1, ++shaft)
            if (lift & 1) {
                if (!first) command.push_back(',');
                command.append(std::to_string(shaft));
                first = false;
            }
        command.push_back('\r');
    }
    sendToLoom(command);
}

void
View::run()
{
    // Drain input queue
    char c;
    char termChar = opts.compuDobbyGen < 4 ? '\x03' : '>';
    const char* armsDown = opts.compuDobbyGen < 4 ? "\x62\x03" : "<down>";
    const char* armsUp = opts.compuDobbyGen < 4 ? "\x61\x03" : "<up>";
    const char* armsNeutral = "<arm null>";    // CD IV only
    const char* loomReset = opts.compuDobbyGen < 4 ? "\x0f\x03" : "\r";

    while (true) {
        auto n = ::read(opts.loomDeviceFD, &c, 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else
                throw make_system_error("error in read");
        }
        if (n == 0)
            break;
    }
    
    sendToLoom(loomReset);
    
    int AVLstate = 1;
    bool doAdvancePick = opts.compuDobbyGen < 4;

    while (mode != Mode::Quit) {
        fd_set rdset;
        FD_SET(STDIN_FILENO, &rdset);
        FD_SET(opts.loomDeviceFD, &rdset);
        timeval threesec{term.pendingEvent() || !loomOutput.empty() ? 0 : 3, 0};
        
        int nfds = ::select(opts.loomDeviceFD + 1, &rdset, nullptr, nullptr, &threesec);
        
        if (nfds == -1 && errno != EINTR)
            throw make_system_error("select failed");

        if (nfds == 0 && AVLstate == 1) {
            sendToLoom(loomReset);
            std::putchar('.');
        }
        
        if (FD_ISSET(STDIN_FILENO, &rdset) || term.pendingEvent() || nfds == -1) {
            Term::Event ev = term.getEvent();
            if (ev.type != Term::EventType::None)
                handleEvent(ev);
            if (mode == Mode::Quit)
                break;
        }
        
        if (FD_ISSET(opts.loomDeviceFD, &rdset)) {
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
                if (opts.compuDobbyGen == 4) {
                    if (c == '\r' || c == '\n')
                        continue;
                    c = (char)std::tolower((int)c);
                }
                loomOutput.push_back(c);
                ++count;
            };
        }

        if (loomOutput.empty())
            continue;
        
        if (auto termPos = loomOutput.find(termChar); termPos != std::string::npos) {
            std::string_view loomLine(loomOutput.begin(), loomOutput.begin() + (ssize_t)termPos + 1);
            switch (AVLstate) {
                case 1:
                    // waiting for reset, sending first pick
                    if (opts.compuDobbyGen < 4 && loomLine == "\x7f\x03") {
                        sendPick();     // initialize pending pick
                        displayPick(" reset");
                        AVLstate = 3;
                    } else if (opts.compuDobbyGen == 4 && loomLine.starts_with("<compu-dobby iv,")) {
                        static const std::set<int> legalShafts = {4, 8, 12, 16, 20, 24, 28, 32, 36, 40};
                        if (loomLine.contains("neg dobby"))
                            opts.dobbyType = DobbyType::Negative;
                        if (loomLine.contains("pos dobby"))
                            opts.dobbyType = DobbyType::Positive;
                        auto fcres = std::from_chars(loomLine.begin() + 17, loomLine.begin() + 19, opts.maxShafts, 10);
                        if (fcres.ec != std::errc() || !legalShafts.contains(opts.maxShafts))
                            throw std::runtime_error("Illegal shaft count in loom greeting.");
                        if (opts.draftContents->maxShafts > opts.maxShafts)
                            throw std::runtime_error("Draft file requires more shafts than the loom possesses.");
                        std::print("Greeting received: {} shafts, {} dobby\n", opts.maxShafts,
                                   opts.dobbyType == DobbyType::Positive ? "Positive" : "Negative");
                        AVLstate = 2;
                    } else {
                        //std::fputs(" ?", stdout);
                        std::print("??{}\n", loomLine.begin());
                    }
                    break;
                case 2:
                    if (loomLine == "<password:>") {
                        sendToLoom("chico\r");
                        std::putchar('\n');
                        AVLstate = 3;
                    } else {
                        std::fputs(" ?", stdout);
                    }
                    break;
                case 3:
                    // process switch (5 or nothing), arm up (4) or arm down (7)
                    if (loomLine == armsDown && loomState != Arms::Down) {
                        // Shed is open, OK to send to solenoids
                        loomState = Arms::Down;
                        if (doAdvancePick)
                            advancePick(true);
                        sendPick();
                        displayPrompt();
                        doAdvancePick = true;
                    }
                    if (loomLine == armsUp && loomState != Arms::Up) {
                        // Shed is closed, next shed is fixed
                        loomState = Arms::Up;
                        const char* msg = "";
                        if (mode == Mode::PickEntry || mode == Mode::PickListEntry) {
                            msg = " \aCancelled";
                            mode = oldMode;
                        }
                        currentPick = nextPick;
                        colorCheck(displayPick(msg));
                    }
                    if (loomLine == armsNeutral) {
                        loomState = Arms::Unknown;
                        erasePrompt();
                    }
                    break;
                default:
                    
            }
            loomOutput.erase(0, termPos + 1);
            std::fflush(stdout);
        }
    }
    if (opts.compuDobbyGen < 4) {
        sendToLoom("\x0f\x07");
    } else {
        sendToLoom("close\r");
    }
    sleep(1);
}

void driver(Options& opts)
{
    Term term(opts.ansi != ANSIsupport::no);
    
    if (!term.good())
        throw std::runtime_error("Could not open terminal.");
    
    View view(term, opts);
    view.run();
}
