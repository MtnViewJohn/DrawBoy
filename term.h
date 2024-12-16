/*
 *  term.h
 *  DrawBoy
 */



#pragma once

#include <termios.h>
#include <string>

class color;

class Term {
  public:
    Term();
    ~Term();

    bool good() const { return _good; }

    int rows() const { return _rows; };
    int cols() const { return _cols; };

  private:
    bool _good;

    struct termios _original_termios;

    int _rows;
    int _cols;

  public:
    static void moveCursor(int row, int col, bool flush = false);
    static void clearLine(int row);
    static void clearToEOL();
    static void clearDisplay();
      // clear methods also reset style
    
    static const char* colorToStyle(const color &, bool truecolor);

    struct Style {
      static const char* reset;
      static const char* bold;
      static const char* dim;
      static const char* inverse;
    };

    enum class EventType {
      None,
      Char,
      Key,
      Resize,
      UnknownEscapeSequence,
    };

    enum class Key {
      Up,
      Down,
      Right,
      Left,
      Home,
      End,
    };

    struct Event {
      EventType type;

      char character;   // character from last Char event
      Key key;          // key from last Key event;

      bool shift;
      bool alt;
      bool control;
      bool meta;

      std::string unknown;
    };

    Event getEvent();

  private:
    std::string _pending_input;
    void remainingInput(const std::string& s, std::size_t pos = 0);
    std::string readInput();

    void fetchWindowSize();
};
