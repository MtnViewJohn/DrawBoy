//
// serial-posix.cpp
// DrawBoy
//
// Created by John Horigan on 9/8/2026
//

#include "serial.h"

#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "ipc.h"
#include "argscommon.h"
#include <iostream>
#include <cassert>

namespace {
    struct addr_deleter {
        void operator()(addrinfo* ap) { ::freeaddrinfo(ap); }
    };
    struct dir_deleter {
        void operator()(DIR* dp) { ::closedir(dp); }
    };
}

using unique_ai = std::unique_ptr<addrinfo, addr_deleter>;
using unique_dir = std::unique_ptr<DIR, dir_deleter>;

serial::serial()
: _fd(-1), _type(CxnType::Serial) {}

serial::serial(int fd, serial::CxnType type)
: _fd(fd), _type(type) {}

serial::serial(const std::string& name, serial::CxnType type)
: _fd(-1), _type(type)
{
    switch (type) {
        case CxnType::Serial: {
            _fd = ::open(name.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
            int modemBits = 0;
            if (_fd == -1)
                return;
            if (!::isatty(_fd) || ::ioctl(_fd, TIOCMGET, &modemBits) == -1) {
                ::close(_fd);
                _fd = -2;
            }
            break;
        }
        case CxnType::Telnet: {
            addrinfo hint{AI_ADDRCONFIG, PF_INET, SOCK_STREAM, IPPROTO_TCP,
                          0, nullptr, nullptr, nullptr};
            addrinfo* results;

            if (::getaddrinfo(name.c_str(), "telnet", &hint, &results) < 0) {
                _fd = -3;
                return;
            }

            auto aiList = unique_ai(results);

            for (addrinfo* ai = results; ai; ai = ai->ai_next) {
                _fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
                if (_fd < 0)
                    continue;

                if (::connect(_fd, ai->ai_addr, ai->ai_addrlen) < 0) {
                    ::close(_fd);
                    continue;
                }

                return;
            }
            _fd = -1;
            break;
        }
        case CxnType::Socket: {
            IPC::Client fakeLoom(name);
            _fd = fakeLoom.release();
            break;
        }
    }
}

serial::~serial()
{
    this->close();
}

serial::serial(serial&& other)
{
    _fd = other._fd;
    other._fd = -1;
}

serial& serial::operator=(serial&& other)
{
    if (this != &other) {
        _fd = other._fd;
        other._fd = -1;
    }
    return *this;
}

void
serial::close()
{
    if (_fd >= 0)
        ::close(_fd);
    _fd = -1;
}

serial::Status
serial::getStatus() const
{
    switch (_fd) {
    case -1:
        return Status::NotOpen;
    case -2:
        return Status::NotSerial;
    case -3:
        return Status::NotFound;
    default:
        return Status::Valid;
    }
}

serial::SerialSet
serial::enumSerial(const SerialSet& exclude)
{
    std::set<std::string> result;
    
    auto devDir = unique_dir(::opendir("/dev"));
    
    if (devDir) {
        while (struct dirent *entry = ::readdir(devDir.get())) {
            if (entry->d_type == DT_CHR) {
                std::string dname = "/dev/";
                dname.append(entry->d_name);
                if (exclude.contains(dname))
                    continue;
                std::putchar('.');
                serial test(dname, CxnType::Serial);
                if (test.getStatus() == Status::Valid) {
                    std::cout << '\n' << dname << std::endl;
                    result.emplace(std::move(dname));
                }
            }
        }
    }
    return result;
}

void
serial::initLoomPort(int cdgen)
{
    struct termios term;

    if (::tcgetattr(_fd, &term) < 0)
        throw make_system_error("Cannot communicate with loom device");
    
    ::cfmakeraw(&term);
    
    if (cdgen == 1) {
        ::cfsetispeed(&term, B1200);            // set 1200 baud
        ::cfsetospeed(&term, B1200);
        term.c_cflag |= PARENB;                 // set 7E2
        term.c_cflag &= (tcflag_t)(~PARODD);
        term.c_cflag |= CSTOPB;
        term.c_cflag = (term.c_cflag & (tcflag_t)(~CSIZE)) | CS7;
    } else {
        ::cfsetispeed(&term, B9600);            // set 9600 baud
        ::cfsetospeed(&term, B9600);
        term.c_cflag &= (tcflag_t)(~PARENB);    // set 8N1
        term.c_cflag &= (tcflag_t)(~CSTOPB);
        term.c_cflag = (term.c_cflag & (tcflag_t)(~CSIZE)) | CS8;
    }
    term.c_cflag |= CLOCAL;
    term.c_cc[VMIN] = 0;
    term.c_cc[VTIME] = 1;

    if (::tcsetattr(_fd, TCSAFLUSH, &term) < 0)
        throw make_system_error("Cannot communicate with loom device");
}

WaitInfo
serial::waitRead(int time)
{
    assert(_fd >= 0);
    fd_set rdfds;
    timeval tv{time, 0};

    FD_ZERO(&rdfds);
    FD_SET(STDIN_FILENO, &rdfds);
    FD_SET(_fd, &rdfds);
    
    int nfds = ::select(_fd + 1, &rdfds, nullptr, nullptr, &tv);
    
    if (nfds == -1 && errno != EINTR)
        throw make_system_error("select failed(3)");
    return {(bool)FD_ISSET(STDIN_FILENO, &rdfds), (bool)FD_ISSET(_fd, &rdfds), nfds};
}

WaitInfo
serial::waitWrite(int time)
{
    assert(_fd >= 0);
    fd_set wrfds = {};
    fd_set rdfds = {};
    timeval tv{time, 0};
    
    FD_ZERO(&wrfds);
    FD_ZERO(&rdfds);
    FD_SET(_fd, &wrfds);
    FD_SET(STDIN_FILENO, &rdfds);

    int nfds = ::select(_fd + 1, &rdfds, &wrfds, nullptr, &tv);

    if (nfds == -1 && errno != EINTR)
        throw make_system_error("select failed(4)");
    return {(bool)FD_ISSET(STDIN_FILENO, &rdfds), (bool)FD_ISSET(_fd, &wrfds), nfds};
}

int
serial::read(char& c)
{
    return (int)::read(_fd, &c, 1);
}

int
serial::write(std::string_view msg)
{
    return (int)::write(_fd, msg.data(), msg.length());
}
