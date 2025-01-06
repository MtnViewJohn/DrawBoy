/*
 *  term.cpp
 *  DrawBoy
 */



#include "term.h"

#include <csignal>
#include <exception>
#include <system_error>
#include <cstdio>
#include <sys/ioctl.h>
#include <unistd.h>
#include "color.h"

#define TermStyleReset     "\x1b[0m"

namespace {
    volatile std::sig_atomic_t pendingResize = false;
    void sigwinch_handler(int /* signal */) { pendingResize = true; }
    int flushwrite(const char* str, std::FILE* f = stdout)
        { auto ret = std::fputs(str, f); std::fflush(f); return ret; }
}

Term::Term()
{
    _good = false;
    _rows = 0;
    _cols = 0;

    if (!::isatty(STDIN_FILENO))  throw std::runtime_error("stdin isn't a tty");
    if (!::isatty(STDOUT_FILENO)) throw std::runtime_error("stdout isn't a tty");

    int err;

    struct termios t;
    err = ::tcgetattr(STDOUT_FILENO, &t);
    if (err != 0) throw std::system_error(errno, std::generic_category(), "getting term attr");
    _original_termios = t;
    ::cfmakeraw(&t);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 1;
    err = ::tcsetattr(STDOUT_FILENO, TCSAFLUSH, &t);
    if (err != 0) throw std::system_error(errno, std::generic_category(), "setting term attr");

    pendingResize = false;
    std::signal(SIGWINCH, sigwinch_handler);
    fetchWindowSize();

    flushwrite(
        //"\x1b[?1049h"   // use alternate buffer, saving cursor first
        //"\x1b[2J"       // erase whole screen
        "\x1b[?7l"      // turn off wrapping
        TermStyleReset  // reset style
    );

    _good = true;
}

Term::~Term()
{
    if (!_good) return;

    std::signal(SIGWINCH, SIG_DFL);

    flushwrite(
        //"\x1b[?1049l"   // use original buffer, restore cursor
        "\x1b[?7h"      // turn on wrapping
        TermStyleReset  // reset style
    );

    ::tcsetattr(STDOUT_FILENO, TCSAFLUSH, &_original_termios);
}

void Term::fetchWindowSize()
{
    struct winsize w;
    int err = ::ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    if (err != 0)
        throw std::system_error(errno, std::generic_category(), "getting term size");
    _rows = w.ws_row;
    _cols = w.ws_col;
}

void Term::moveCursor(int row, int col, bool flush)
{
    std::printf("\x1b[%d;%dH", row, col);
    if (flush) std::fflush(stdout);
}

void Term::moveCursorRel(int row, int col, bool flush)
{
    if (row < 0)
        std::printf("\x1b[%dA", -row);
    if (row > 0)
        std::printf("\x1b[%dB",  row);
    if (col < 0)
        std::printf("\x1b[%dD", -col);
    if (col > 0)
        std::printf("\x1b[%dC",  col);
    if (flush) std::fflush(stdout);
}

void Term::clearToEOL()
{
    flushwrite("\x1b[0K" TermStyleReset);
}

void Term::clearLine(int row)
{
    moveCursor(row, 1);
    flushwrite("\x1b[2K"      // clear whole line
              TermStyleReset);
}

void Term::clearDisplay()
{
    moveCursor(1, 1);
    flushwrite("\x1b[2J"      // clear whole display
              TermStyleReset);
}


const char* Term::Style::reset    = TermStyleReset;
const char* Term::Style::bold     = "\x1b[1m";
const char* Term::Style::dim      = "\x1b[2m";
const char* Term::Style::inverse  = "\x1b[7m";

const char* Term::colorToStyle(const color &c, bool truecolor)
{
    static char buf[32];
    
    const char* foregnd = c.useWhiteText() ? "1;37" : "0;30";
    if (truecolor) {
        auto c24bit = c.convert(256);
        std::snprintf(buf, 32, "\x1b[%s;48;2;%d;%d;%dm", foregnd,
                      std::get<0>(c24bit),
                      std::get<1>(c24bit),
                      std::get<2>(c24bit));
    } else {
        auto c666 = c.convert(6);
        int gray = c.convertGray(24);
        int bkgnd = gray >= 0 ? gray + 232 :
            std::get<0>(c666) * 36 + std::get<1>(c666) * 6 + std::get<2>(c666) + 16;
        std::snprintf(buf, 32, "\x1b[%s;48;5;%dm", foregnd, bkgnd);
    }
    
    return buf;
}

void Term::remainingInput(const std::string& s, std::size_t pos)
{
    _pending_input = s.substr(pos);
}

std::string Term::readInput()
{
    std::string o = std::move(_pending_input);
    _pending_input.clear();

    char c;
    while (true) {
        auto n = ::read(STDIN_FILENO, &c, 1);
        if (n < 0) throw std::system_error(errno, std::generic_category(), "error in read");
        if (n == 0) break;
        o.push_back(c);
    };

    return o;
}

bool Term::pendingEvent()
{
    return (!_pending_input.empty()) || pendingResize;
}

Term::Event Term::getEvent()
{
    Event ev;
    ev.type = EventType::None;
    ev.shift = ev.alt = ev.control = ev.meta = false;

    if (pendingResize) {
        pendingResize = false;
        fetchWindowSize();
        ev.type = EventType::Resize;
        return ev;
    }

    std::string s = readInput();

    if (s.empty()) {
        ev.type = EventType::None;
        return ev;
    }

    // We know: s.length() > 0
    if (s.length() == 1 || s[0] != '\x1b') {
        remainingInput(s, 1);
        ev.type = EventType::Char;
        ev.character = s[0];
        return ev;
    }

    // We know: s.length() > 1 && s[0] == '\x1b'
    if (s == "\x1b\x1b") {
        remainingInput(s, 2);
        ev.type = EventType::Char;
        ev.character = '\x1b';
        return ev;
    };

    // We know: s.length() > 1 && s[0] == '\x1b && s != "\x1b\x1b"
    if (s.length() == 2 || s[1] != '[') {
        remainingInput(s, 2);
        ev.type = EventType::Char;
        ev.alt = true;
        ev.character = s[1];
        return ev;
    }

    // We know: s.length() > 2 && s[0] == '\x1b && s[1] == '['
    if (s.length() == 3) {
        switch (s[2]) {
            case 'A':   ev.key = Term::Key::Up;     break;
            case 'B':   ev.key = Term::Key::Down;   break;
            case 'C':   ev.key = Term::Key::Right;  break;
            case 'D':   ev.key = Term::Key::Left;   break;
            case 'H':   ev.key = Term::Key::Home;   break;
            case 'F':   ev.key = Term::Key::End;    break;
            default:
                goto unknown;
        }
        ev.type = EventType::Key;
        return ev;
    }

unknown:
    ev.type = EventType::UnknownEscapeSequence;
    ev.unknown = s;
    return ev;
}

