//
// serial.h
// DrawBoy
//
// Created by John Horigan on 9/8/2026
//

#pragma once
#include <string>
#include <set>

struct WaitInfo {
    bool _stdinReady;
    bool _loomReady;
    int _nfds;
};

class serial {
public:
    enum class CxnType { Serial, Telnet, Socket };
    serial();
    serial(const std::string& name, CxnType type);
    serial(int fd, CxnType type);
    serial(serial&& other);
    serial(const serial&) = delete;
    serial& operator=(serial&& other);
    serial& operator=(const serial&) = delete;
    ~serial();

    WaitInfo waitRead(int time);
    WaitInfo waitWrite(int time);
    int read(char& c);
    int write(std::string_view msg);
    void close();

    enum class Status { Valid, NotOpen, NotSerial, NotFound };
    Status getStatus() const;

    using SerialSet = std::set<std::string>;
    static SerialSet enumSerial(const SerialSet& exclude);

    void initLoomPort(int cdgen);

private:
    int _fd = -1;
    CxnType _type;
};
