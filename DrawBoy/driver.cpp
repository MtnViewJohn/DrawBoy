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
#include <deque>

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

static const int ClearPick = -42;
}

enum class Mode {
    Weave,
    Tabby,
    PickEntry,
    PickListEntry,
    Quit,
};

enum class LogDirection {
    Unknown,
    Reading,
    Writing
};

enum class Arms {
    Up,
    Down,
    Unknown,
};

enum class Commands {
    Null,
    Tabby,
    Liftplan,
    Reverse,
    SetPick,
    AdvancePick,
    SetPickList,
    DoSetPick,
    DoSetPickList,
    Quit
};

std::map<Commands, const char*> CommandNames = {
    {Commands::Tabby, "Tabby"},
    {Commands::Liftplan, "Liftplan"},
    {Commands::Reverse, "Reverse"},
    {Commands::AdvancePick, "Next/Previous pick"},
    {Commands::DoSetPick, "Pick set"},
    {Commands::DoSetPickList, "Pick list set"}
};

struct Command
{
    Commands command = Commands::Null;
    int argument = 0;
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
    DobbyType HWdobbyType = DobbyType::Positive;

    draft& draftContent;
    int currentPick = 0, nextPick = 1;
    bool pickSent = true;
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
    
    LogDirection logdirn = LogDirection::Unknown;
    
    View(Term& t, Options& o)
    : term(t), opts(o), draftContent(*o.draftContents),
      currentPick(o.pick - 2), nextPick(o.pick - 1)
    {
        if (currentPick < 0)
            currentPick += (int)opts.picks.size();
    }
    
    void handleEvent(const Term::Event& ev);
    bool handleGlobalEvent(const Term::Event& ev);
    bool handlePickEvent(const Term::Event& ev);
    bool handlePickEntryEvent(const Term::Event& ev);
    bool handlePickListEntryEvent(const Term::Event& ev);
    
    std::deque<Command> pendingCommands;
    void doCommand(Command cmd, bool deferPick = false);

    void sendPick();
    void sendToLoom(std::string_view msg, bool waitReady);
    std::pair<uint64_t, color> calculateLift(int pick);
    void advancePick(bool forward);
    void setPick(int newPick);
    color displayPick();
    void colorCheck(color currentColor);
    void displayPrompt();
    int listenToLoom();
    void run();
    
    ssize_t readLoom(char &c);
    ssize_t writeLoom(std::string_view msg);
    void prettyPrint(char c);
    
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
        assert(pick == TabbyA || pick == TabbyB || pick == ClearPick);
        lift = pick == TabbyA ? opts.tabbyA : opts.tabbyB;
        weftColor = opts.tabbyColor;
        if (pick == ClearPick)
            lift = (1 << opts.maxShafts) - 1;   // loom shafts, not draft shafts
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
View::displayPick()
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
    const char* menuPrefix = (loomState == Arms::Down && !opts.ascii) ?
                                Term::Style::inverse : "";
    const char* menuSuffix = loomState == Arms::Down ?
                                (opts.ascii ? ")" : Term::Style::reset) : "";
    auto menu = std::format("{0}T{1}abby  {0}L{1}iftplan  {0}R{1}everse  {0}S{1}elect pick  {0}P{1}ick list  {0}Q{1}uit   ", menuPrefix, menuSuffix);
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
            int cwifpick = currentPick < 0 ? currentPick : opts.picks[(size_t)(currentPick) % opts.picks.size()];
            int nwifpick = nextPick < 0 ? nextPick : opts.picks[(size_t)(nextPick) % opts.picks.size()];
            int cpick = currentPick < 0 ? currentPick : currentPick + 1;
            int npick = nextPick < 0 ? nextPick : nextPick + 1;
            const char* rightArrow = opts.ascii ? " --> " : " \xE2\xAE\x95  ";
            if (cwifpick == cpick && nwifpick == npick)
                std::print("[{}:{}{}{}] {}", ModePrompt[mode], pickString(cwifpick, false),
                           rightArrow, pickString(nwifpick, false), menu);
            else
                std::print("[{}:{}({}){}{}({})] {}", ModePrompt[mode],
                           pickString(cwifpick, false), pickString(cpick, false),
                           rightArrow, pickString(nwifpick, false),
                           pickString(npick, false), menu);
            break;
        }
        default:
            std::print("[{}] {}", ModePrompt[mode], menu);
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
        switch (evChar) {
            case 't':
                doCommand({Commands::Tabby});
                return true;
            case 'l':
                doCommand({Commands::Liftplan});
                return true;
            case 'q':
                mode = Mode::Quit;
                std::putchar('q');
                std::fflush(stdout);
                return true;
            case 'r':
                doCommand({Commands::Reverse});
                return true;
            case 's':
                doCommand({Commands::SetPick});
                return true;
            case 'p':
                doCommand({Commands::SetPickList});
                return true;
            default:
                break;
        }
    }
    
    if (ev.type == Term::EventType::Key) {
        switch (ev.key) {
            case Term::Key::Up:
            case Term::Key::Left:
                doCommand({Commands::AdvancePick, -1});
                break;
            case Term::Key::Down:
            case Term::Key::Right:
                doCommand({Commands::AdvancePick, 1});
                break;
            default:
                return false;
        }
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
                    mode = Mode::Weave;
                    doCommand({Commands::DoSetPick, (int)p});
                }
            } else {
                mode = oldMode;
                doCommand({Commands::DoSetPick, 0});
            }
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
            mode = Mode::Weave;
            doCommand({Commands::DoSetPickList});
            return true;
        }
    }

    return false;
}


void
View::doCommand(Command cmd, bool deferPick)
{
    if (cmd.command == Commands::Null)
        return;
    
    if (cmd.command == Commands::Quit) {
        mode = Mode::Quit;
        return;
    }
    
    if (auto cmdName = CommandNames.find(cmd.command);
        loomState != Arms::Down && cmdName != CommandNames.end())
    {
        std::print(" {} command queued.\r\n", cmdName->second);
        displayPrompt();
        std::fflush(stdout);
        
        if (cmd.command == Commands::AdvancePick && !pendingCommands.empty() &&
            pendingCommands.front().command == Commands::AdvancePick)
        {   // Merge together AdvancePick commands
            pendingCommands.front().argument += cmd.argument;
        } else {
            pendingCommands.push_front(cmd);
        }
        return;
    }
    
    switch (cmd.command) {
        case Commands::Null:
        case Commands::Quit:
            break;      // already handled
        case Commands::Tabby:
            if (opts.treadleThreading) {
                std::putchar('\a');
                std::fflush(stdout);
                return;
            }
            if (mode == Mode::Tabby) return;
            mode = Mode::Tabby;
            oldPick = nextPick;
            nextPick = weaveForward ? TabbyA : TabbyB;
            if (!deferPick) {
                sendPick();
                displayPrompt();
            }
            break;
        case Commands::Liftplan:
            if (opts.treadleThreading) {
                std::putchar('\a');
                std::fflush(stdout);
                return;
            }
            if (mode == Mode::Weave) return;
            mode = Mode::Weave;
            nextPick = oldPick;
            if (!deferPick) {
                sendPick();
                displayPrompt();
            }
            break;
        case Commands::Reverse:
            weaveForward = !weaveForward;
            nextPick = currentPick;
            advancePick(true);
            if (!deferPick) {
                sendPick();
                displayPrompt();
            }
            break;
        case Commands::AdvancePick:
            if (cmd.argument == 0)
                return;
            for (int i = 0; i < std::abs(cmd.argument); ++i)
                advancePick(cmd.argument > 0);
            if (!deferPick) {
                sendPick();
                displayPrompt();
            }
            break;
        case Commands::SetPick:
            oldMode = mode;
            mode = Mode::PickEntry;
            pickValue.clear();
            displayPrompt();
            break;
        case Commands::SetPickList:
            oldMode = mode;
            mode = Mode::PickListEntry;
            pickValue.clear();
            parenLevel = 0;
            displayPrompt();
            break;
        case Commands::DoSetPick:
            if (cmd.argument > 0) {
                nextPick = cmd.argument - 1;
                mode = Mode::Weave;
                if (!deferPick) {
                    sendPick();
                    displayPrompt();
                }
            } else {
                if (!deferPick) {
                    mode = oldMode;
                    displayPrompt();
                }
            }
            break;
        case Commands::DoSetPickList:
            if (parenLevel != 0) {
                std::putchar('\a');
                std::fflush(stdout);
                return;
            }
            try {
                opts.parsePicks(pickValue, draftContent.picks);
                nextPick = 0;       // Current pick is from old pick list
                currentPick = -10;  // it is meaningless in new pick list
                mode = Mode::Weave;
                if (!deferPick)
                    sendPick();
            } catch (std::exception& e) {
                std::print("\r\n\a{}{}{}\r\n", bold(), e.what(), reset());
            }
            if (!deferPick)
                displayPrompt();
            break;
    }
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
View::sendToLoom(std::string_view msg, bool waitReady)
{
    while (!msg.empty())
    {
        auto result = writeLoom(msg);
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
    if (opts.compuDobbyGen == 4 && waitReady) {
        while (mode != Mode::Quit) {
            listenToLoom();
            if (loomOutput.contains("<ready>")) {
                return;
            }
            if (loomOutput.contains("<what>")) {
                std::print("\nloom protocol confusion\n");
                return;
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
        if (pickSent)
            sendToLoom("clear\r", true);
        bool first = true;
        if (lift == 0)
            return;
        command = "pick ";
        for (int shaft = 1; lift; lift >>= 1, ++shaft)
            if (lift & 1) {
                if (!first) command.push_back(',');
                command.append(std::to_string(shaft));
                first = false;
            }
        command.push_back('\r');
        pickSent = true;
    }
    sendToLoom(command, true);
}

int
View::listenToLoom()
{
    fd_set rdset;
    char c;
    FD_SET(STDIN_FILENO, &rdset);
    FD_SET(opts.loomDeviceFD, &rdset);
    timeval threesec{term.pendingEvent() || !loomOutput.empty() ? 0 : 3, 0};
    
    int nfds = ::select(opts.loomDeviceFD + 1, &rdset, nullptr, nullptr, &threesec);
    
    if (nfds == -1 && errno != EINTR)
        throw make_system_error("select failed");

    if (FD_ISSET(STDIN_FILENO, &rdset) || term.pendingEvent() || nfds == -1) {
        Term::Event ev = term.getEvent();
        if (ev.type != Term::EventType::None)
            handleEvent(ev);
        if (mode == Mode::Quit)
            return nfds;
    }
    
    if (FD_ISSET(opts.loomDeviceFD, &rdset)) {
        int count = 0;
        while (true) {
            auto n = readLoom(c);
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
    
    if (opts.compuDobbyGen == 4 && loomOutput.starts_with("<error"))
        make_system_error({loomOutput});
    return nfds;
}

ssize_t
View::readLoom(char &c)
{
    ssize_t n = ::read(opts.loomDeviceFD, &c, 1);
    
    if (n == 1 && opts.logFile) {
        if (logdirn != LogDirection::Reading) {
            std::fputs("\nloom: ", opts.logFile);
            logdirn = LogDirection::Reading;
        }
        prettyPrint(c);
    }

    return n;
}

ssize_t
View::writeLoom(std::string_view msg)
{
    ssize_t n = ::write(opts.loomDeviceFD, msg.data(), msg.length());
    
    if (n > 0 && opts.logFile) {
        if (logdirn != LogDirection::Writing) {
            std::fputs("\ndrawboy: ", opts.logFile);
            logdirn = LogDirection::Writing;
        }
        for (size_t i = 0; i < (size_t)n; ++i)
            prettyPrint(msg[i]);
    }

    return n;
}

void
View::prettyPrint(char c)
{
    if (opts.compuDobbyGen < 4) {
        std::print(opts.logFile, "{:#04x}", (int)c);
    } else {
        if (std::isprint((int)c) && c != '\\') {
            std::fputc((int)c, opts.logFile);
        } else {
            switch (c) {
                case '\r':
                    std::fputs("\\r", opts.logFile);
                    break;
                case '\n':
                    std::fputs("\\n", opts.logFile);
                    break;
                case '\\':
                    std::fputs("\\\\", opts.logFile);
                    break;
                default:
                    std::print(opts.logFile, "\\x{:02x}", (int)c);
                    break;
            }
        }
    }
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
        auto n = readLoom(c);
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
    
    sendToLoom(loomReset, false);
    
    int AVLstate = 1;
    bool atLeastOnce = false;
    bool doAdvancePick = false;

    while (mode != Mode::Quit) {
        int nfds = listenToLoom();

        if (nfds == 0 && AVLstate == 1) {
            sendToLoom(loomReset, false);
            std::putchar('.');
        }

        if (loomOutput.empty())
            continue;
        
        if (auto termPos = loomOutput.find(termChar); termPos != std::string::npos) {
            std::string loomLine(loomOutput.begin(), loomOutput.begin() + (ssize_t)termPos + 1);
            loomOutput.erase(0, termPos + 1);
            switch (AVLstate) {
                case 1:
                    // waiting for reset, sending first pick
                    if (opts.compuDobbyGen < 4 && loomLine == "\x7f\x03") {
                        opts.tabbyA &= ((1 << opts.maxShafts) - 1);
                        opts.tabbyB &= ((1 << opts.maxShafts) - 1);
                        AVLstate = 3;
                    } else if (opts.compuDobbyGen == 4 && loomLine.starts_with("<compu-dobby iv,")) {
                        static const std::set<int> legalShafts = {4, 8, 12, 16, 20, 24, 28, 32, 36, 40};
                        if (loomLine.contains("neg dobby"))
                            HWdobbyType = DobbyType::Negative;
                        if (loomLine.contains("pos dobby"))
                            HWdobbyType = DobbyType::Positive;
                        auto fcres = std::from_chars(loomLine.data() + 17, loomLine.data() + 19, opts.maxShafts, 10);
                        if (fcres.ec != std::errc() || !legalShafts.contains(opts.maxShafts))
                            throw std::runtime_error("Illegal shaft count in loom greeting.");
                        if (opts.draftContents->maxShafts > opts.maxShafts)
                            throw std::runtime_error("Draft file requires more shafts than the loom possesses.");
                        opts.tabbyA &= ((1 << opts.maxShafts) - 1);
                        opts.tabbyB &= ((1 << opts.maxShafts) - 1);
                        std::print("\r\nGreeting received: {} shafts, {} dobby\r\n",
                            opts.maxShafts,
                            opts.dobbyType == DobbyType::Positive ? "Positive" :"Negative");
                        AVLstate = 2;
                    } else {
                        //std::fputs(" ?", stdout);
                        std::print("\r\n??{}\r\n", loomLine);
                    }
                    break;
                case 2:
                    if (loomLine == "<password:>") {
                        sendToLoom("chico\r", true);
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
                        pickSent = false;
                        if (pendingCommands.empty() && doAdvancePick)
                            advancePick(true);
                        while (!pendingCommands.empty()) {
                            doCommand(pendingCommands.back(), true);
                            pendingCommands.pop_back();
                        }
                        sendPick();
                        displayPrompt();
                        doAdvancePick = true;
                        atLeastOnce = true;
                    }
                    if (loomLine == armsUp && loomState != Arms::Up) {
                        // Shed is closed, next shed is fixed
                        loomState = Arms::Up;
                        currentPick = nextPick;
                        colorCheck(displayPick());
                        displayPrompt();
                    }
                    if (loomLine == armsNeutral) {
                        loomState = Arms::Unknown;
                        displayPrompt();
                    }
                    break;
                default:
                    break;
            }
            std::fflush(stdout);
        }
    }
    if (atLeastOnce) {
        int cpick = currentPick >= 0 ? currentPick + 1 : oldPick + 1;
        bool success = false;
        if (std::FILE* pickf{std::fopen(opts.pickFile.c_str(), "w")}) {
            try {
                std::print(pickf, "{}\n", cpick);
                success = true;
            } catch (...) {}
            std::fclose(pickf);
        }
        if (success)
            std::print("\r\nNext pick saved: {}\r\n", cpick);
        else
            std::print("\r\nFailed to save next pick.\r\n");
    }
    if (opts.compuDobbyGen < 4) {
        sendToLoom("\x0f\x07", false);
    } else {
        if (HWdobbyType == DobbyType::Positive) {
            sendToLoom("clear\r", true);
        } else {
            nextPick = ClearPick;
            sendPick();
        }
        sendToLoom("close\r", false);
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
