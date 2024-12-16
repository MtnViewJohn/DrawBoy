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

enum class Mode {
    Weave,
    Unweave,
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
    {Mode::Unweave, "Reverse weaving"},
    {Mode::Tabby, "Tabby"},
    {Mode::PickEntry, "Select pick"},
    {Mode::PickListEntry, "Enter pick list"},
    {Mode::Quit, "Quitting"},
};

enum class TabbyPick {
    TabbyA,
    TabbyB,
};

static const std::uint64_t NotAShed = 0xffffffff;

enum class PickAction {
    DontSend,
    Send,
};

struct View
{
    Term& term;
    Options& opts;
    
    wif wifContents;
    int pick;
    TabbyPick tabbyPick = TabbyPick::TabbyA;
    std::string pickValue;
    
    std::string loomOutput;
    Shed loomState = Shed::Unknown;
    std::uint64_t pendingPick = NotAShed;
    bool waitingForSolenoids = false;

    Mode mode = Mode::Weave;
    
    View(Term& t, Options& o)
    : term(t), opts(o), wifContents(o.wifFileStream), pick(o.pick - 1)
    {}
    
    void handleEvent(const Term::Event& ev);
    bool handleGlobalEvent(const Term::Event& ev);
    bool handlePickEvent(const Term::Event& ev);
    bool handlePickEntryEvent(const Term::Event& ev);
    bool handlePickListEntryEvent(const Term::Event& ev);

    void sendPick(std::uint64_t _pick = NotAShed);
    void sendToLoom(const char *msg);
    
    void nextPick();
    void prevPick();
    void displayPick(PickAction sendToLoom = PickAction::DontSend);
    void displayPrompt();
    void run();
};

void
View::displayPick(PickAction sendToLoom)
{
    // Compute liftplan for pick, inverting if dobby type does not match wif type
    uint64_t lift = 0;
    color weftColor;
    if (mode == Mode::Tabby) {
        lift = tabbyPick == TabbyPick::TabbyA ? opts.tabbyA : opts.tabbyB;
        weftColor = wifContents.weftColor[(std::size_t)(opts.picks[0])];
    } else {
        std::size_t zpick = (std::size_t)(pick) % opts.picks.size();
        lift = wifContents.liftplan[(std::size_t)(opts.picks[zpick])];
        weftColor = wifContents.weftColor[(std::size_t)(opts.picks[zpick])];
        
        if ((opts.dobbyType == DobbyType::Negative &&  wifContents.risingShed) ||
            (opts.dobbyType == DobbyType::Positive && !wifContents.risingShed))
        {
            lift ^= (1 << wifContents.maxShafts) - 1;
        }
    }
    
    // Output drawdown
    std::putchar('\r');
    int drawdownWidth = term.cols() - wifContents.maxShafts - 24;
    if (drawdownWidth > wifContents.ends) drawdownWidth = wifContents.ends;
    for (std::size_t i = (std::size_t)drawdownWidth; i > 0 ; --i) {
        if (opts.ansi != ANSIsupport::no) {
            color c = wifContents.threading[i] & lift ? wifContents.warpColor[i] : weftColor;
            std::fputs(Term::colorToStyle(c, opts.ansi == ANSIsupport::truecolor), stdout);
        }
        if (opts.ascii)
            putchar(wifContents.threading[i] & lift ? '|' : '-');
        else
            std::fputs(wifContents.threading[i] & lift ? "\xE2\x95\x91" : "\xE2\x95\x90", stdout);
    }
    
    // Output direction arrows and pick #
    if (opts.ansi != ANSIsupport::no) {
        std::fputs(Term::colorToStyle(weftColor, opts.ansi == ANSIsupport::truecolor), stdout);
    }
    const char* arrow = opts.ascii ? "->" : "\xE2\xAE\x95 ";
    if (mode == Mode::Unweave)
        arrow = opts.ascii ? "<-" : "\xE2\xAC\x85 ";
    if (mode == Mode::Tabby)
        std::printf(" #%s%s%s#|", arrow, tabbyPick == TabbyPick::TabbyA ? "tabbyA" : "tabbyB", arrow);
    else
        std::printf(" #%s%6d%s#|", arrow, pick + 1, arrow);
    
    // Output liftplan
    for (uint64_t shaftMask = 1; shaftMask != 1 << wifContents.maxShafts; shaftMask <<= 1)
        if (shaftMask & lift)
            std::fputs(opts.ascii ? "*" : "\xE2\x96\xA0", stdout);
        else
            std::putchar(' ');
    putchar('|');
    if (opts.ansi != ANSIsupport::no) std::fputs(Term::Style::reset, stdout);
    if (loomState != Shed::Closed)
        std::fputs("pending ", stdout);
    
    Term::clearToEOL();
    std::fputs("\r\n", stdout);
    
    if (sendToLoom != PickAction::DontSend)
        sendPick(lift);
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
        default:
            std::printf("[%s]  t)abby mode  l)iftplan mode  r)everse  s)elect next pick  P)ick list  q)uit", ModePrompt[mode]);
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
        case Mode::Unweave:
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
                    displayPrompt();
                    return true;
                    
                case '\x1b':      // escape
                    mode = Mode::Weave;
                    displayPrompt();
                    return true;
                    
                default:
                    break;
            }
            break;
        }
            
        case Term::EventType::Resize: {
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
        switch (ev.character) {
            case 't':
            case 'T':
                if (mode != Mode::Tabby) {
                    mode = Mode::Tabby;
                    tabbyPick = TabbyPick::TabbyA;
                    displayPick();
                    displayPrompt();
                }
                return true;
            case 'l':
            case 'L':
                if (mode == Mode::Tabby) {
                    mode = Mode::Weave;
                    displayPick();
                    displayPrompt();
                }
                return true;
            case 'q':
            case 'Q':
                mode = Mode::Quit;
                return true;
            case 'r':
            case 'R':
                switch (mode) {
                    case Mode::Weave:
                        mode = Mode::Unweave;
                        break;
                    case Mode::Unweave:
                        mode = Mode::Weave;
                        break;
                    default:
                        return false;
                }
                displayPick();      // Redisplay current pick with
                displayPrompt();    // new direction marker
                return true;
            case 's':
            case 'S':
                mode = Mode::PickEntry;
                pickValue.clear();
                displayPrompt();
                return true;
            case 'p':
            case 'P':
                mode = Mode::PickListEntry;
                pickValue.clear();
                displayPrompt();
                return true;
            default:
                break;
        }
    }
    
    if (ev.type == Term::EventType::Key && mode != Mode::PickEntry) {
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
            std::putc(ev.character, stdout);
            return true;
        }
        if (ev.character == '\b') {
            if (pickValue.empty()) {
                std::putc('\a', stdout);
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
                if (errno || p > 999999 || p < 1) {
                    std::putc('\a', stdout);
                } else {
                    pick = (int)p - 1;
                    displayPick();
                }
            }
            mode = Mode::Weave;
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
        if (std::strchr("0123456789-,", ev.character)) {
            pickValue.push_back(ev.character);
            std::putc(ev.character, stdout);
            return true;
        }
        if (ev.character == '\b') {
            if (pickValue.empty()) {
                std::putc('\a', stdout);
            } else {
                pickValue.pop_back();
                displayPrompt();
            }
            return true;
        }
        if (ev.character == '\r') {
            if (!pickValue.empty()) {
                try {
                    opts.parsePicks(pickValue);
                    pick = 0;
                    displayPick(PickAction::Send);
                } catch (std::exception& e) {
                    std::printf(" \a%s%s%s\r\n", opts.ansi != ANSIsupport::no ? "" : Term::Style::bold,
                                e.what(), opts.ansi != ANSIsupport::no ? "" : Term::Style::reset);
                }
            }
            mode = Mode::Weave;
            displayPrompt();
            return true;
        }
    }

    return false;
}

void
View::nextPick()
{
    switch (mode) {
        case Mode::Weave:
            ++pick;
            break;
        case Mode::Unweave:
            --pick;
            if (pick < 0)
                pick = (int)opts.picks.size() - 1;
            break;
        case Mode::Tabby:
            tabbyPick = tabbyPick == TabbyPick::TabbyA ? TabbyPick::TabbyB : TabbyPick::TabbyA;
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
            --pick;
            if (pick < 0)
                pick = (int)opts.picks.size() - 1;
            break;
        case Mode::Unweave:
            ++pick;
            break;
        case Mode::Tabby:
            tabbyPick = tabbyPick == TabbyPick::TabbyA ? TabbyPick::TabbyB : TabbyPick::TabbyA;
            break;
        default:
            break;
    }
}

void
View::sendToLoom(const char *msg)
{
    std::size_t remaining = std::strlen(msg);
    std::size_t sent = 0;
    
    while (remaining > 0  &&  mode != Mode::Quit)
    {
        auto result = write(opts.loomDeviceFD, msg + sent, remaining);
        if (result >= 0) {
            if (result == 0) std::putchar('>');
            // sent partial or all the remaining data
            sent += (std::size_t)result;
            remaining -= (std::size_t)result;
        } else {
            int err = errno;
            if ((err == EAGAIN) || (err == EWOULDBLOCK) || (err == EINTR)) {
                timeval tv = {};
                fd_set fds = {};
                int selectresult;
                
                tv.tv_sec = 1;
                FD_ZERO(&fds);
                std::putchar('>');
                FD_SET(opts.loomDeviceFD, &fds);
                selectresult = select(opts.loomDeviceFD + 1, nullptr, &fds, nullptr, &tv);
                if (selectresult == -1 && errno != EINTR)
                    throw make_system_error("loom select failed");
            } else {
                throw make_system_error("loom write failed");
            }
        }
    }
}

void
View::sendPick(std::uint64_t lift)
{
    if (loomState != Shed::Closed) {
        pendingPick = lift;
        return;
    }
    
    if (lift == NotAShed) lift = pendingPick;
    pendingPick = NotAShed;
    if (lift == NotAShed) return;
    
    char shaftCmd = '\x10';
    std::string command;
    for (int i = 0; i < wifContents.maxShafts >> 2; ++i) {
        command.push_back(shaftCmd | (char)(lift & 0xf));
        shaftCmd += '\x10';
        lift >>= 4;
    }
    command.push_back('\x07');
    sendToLoom(command.c_str());
}

void
View::run()
{
    if (wifContents.maxShafts > opts.maxShafts)
        throw std::runtime_error("Wif file has more shafts than the loom.");
    
    // If no treadle list provided then treadle the whole liftplan
    if (opts.picks.empty()) {
        opts.picks.resize((std::size_t)wifContents.picks);
        for (int i = 0; i < wifContents.picks; ++i)
            opts.picks[(std::size_t)i] = i + 1;
    } else {
        for (auto _pick: opts.picks)
            if (_pick > wifContents.picks)
                throw std::runtime_error("Pick list includes picks that are not in the wif file.");
    }
    
    sendToLoom("\x0f\x07");
    waitingForSolenoids = true;
    displayPick(PickAction::Send);    // initialize pending pick
    displayPrompt();

    while (mode != Mode::Quit) {
        fd_set rdset;
        FD_SET(STDIN_FILENO, &rdset);
        FD_SET(opts.loomDeviceFD, &rdset);
        timeval threesec{3,0};
        
        int nfds = select(opts.loomDeviceFD + 1, &rdset, nullptr, nullptr, &threesec);
        
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
            if (ev.type == Term::EventType::None) continue;
            handleEvent(ev);
        }
        
        if (FD_ISSET(opts.loomDeviceFD, &rdset)) {
            char c;
            while (true) {
                auto n = read(opts.loomDeviceFD, &c, 1);
                if (n < 0) {
                    if (errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK)
                        break;
                    else
                        throw make_system_error("error in read");
                }
                if (n == 0) break;
                loomOutput.push_back(c);
            };
            
            if (!loomOutput.empty()) {
                if (loomOutput.back() == '\x03') {
                    if (loomOutput == "\x7f\x03") {
                        waitingForSolenoids = false;
                        std::fputs(" solenoid reset received", stdout);
                    }
                    if (loomOutput == "\x61\x03" && loomState == Shed::Closed) {
                        loomState = Shed::Open;
                        std::fputs(" open", stdout);
                    }
                    if (loomOutput == "\x62\x03") {
                        if (loomState == Shed::Open && pendingPick == NotAShed) {
                            loomState = Shed::Closed;
                            nextPick();
                            displayPick(PickAction::Send);
                            displayPrompt();
                        } else {
                            loomState = Shed::Closed;
                            sendPick(NotAShed);
                            std::printf("%s READY%s",
                                        opts.ansi != ANSIsupport::no ? "" : Term::Style::bold,
                                        opts.ansi != ANSIsupport::no ? "" : Term::Style::reset);
                        }
                    }
                    loomOutput.clear();
                }
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
