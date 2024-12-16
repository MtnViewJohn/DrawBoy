#include "ipc.h"

#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include "argscommon.h"
#include <fcntl.h>

namespace {

void
removeSocketFile(const std::string& path)
{
    int err = remove(path.c_str());
    if (err == -1 && (errno != 0 && errno != ENOENT))
        std::fprintf(stderr, "Couldn't remove socket %s\n", path.c_str());
    // This doesn't throw as we don't want to obscure any error that is
    // causing the code to shutdown.
}


int makeSocket(const std::string& path, bool server)
{
    int sockFD = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockFD == -1)
        throw make_system_error("Couldn't create IPC socket");
    
    int flags = fcntl(sockFD, F_GETFL);
    if (flags == -1)
        throw make_system_error("Could not get socket flags.");
    if (fcntl(sockFD, F_SETFL, flags | O_NONBLOCK) == -1)
        throw make_system_error("Could not set socket flags.");
    
    if (server) removeSocketFile(path);
    
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (path.length() > sizeof(addr.sun_path) - 1)
        throw make_runtime_error({"Socket path is too long: ", path});
    
    if (server) {
        auto oldmask = umask(0007); // allow group access (usually audio)
        if (bind(sockFD, (const struct sockaddr*)&addr, sizeof(addr)) != 0)
            throw make_runtime_error({"Couldn't bind socket to path ", path});
        umask(oldmask);
        
        if (listen(sockFD, 2) != 0)   // don't need a long backlog
            throw make_runtime_error({"Couldn't listen to socket path ", path});
    } else {
        if (connect(sockFD, (const struct sockaddr*)&addr, sizeof(addr)) != 0) {
            throw make_runtime_error({
                "Couldn't connect to the fakeloom server\n"
                "\n"
                "(While trying to connect to the socket path:\n"
                "    ", path, "\n"
                "    got the error: ", strerror(errno), ")"});
        }
    }
    
    return sockFD;
}
}



namespace IPC {

Socket::Socket(int fd) : sockFD(fd) { }
Socket::~Socket()                   { close(); }

Socket::Socket(Socket&& other) noexcept : sockFD(other.sockFD)
{ other.invalidate(); }
Socket& Socket::operator=(Socket&& other) noexcept {
    if (&other != this) {
        close();
        sockFD = other.sockFD;
        other.invalidate();
    }
    return *this;
}

bool Socket::valid()      { return sockFD != -1; }
void Socket::invalidate() { sockFD = -1; }
void Socket::close() {
    if (valid()) ::close(sockFD);
    invalidate();
}
int Socket::release() {
    int _fd = -1;
    std::swap(_fd, sockFD);
    return _fd;
}


SocketError::SocketError(const char* what)
: std::system_error(errno, std::generic_category(), what)
{ }


Client::Client(const std::string& path)
: Socket(makeSocket(path, false))  { }
Client::~Client()                  { }


Connection::Connection(int fd) : Socket(fd) { }
Connection::~Connection()                   { }

Connection::Connection(Connection&& other) noexcept : Socket(std::move(other)) { }
Connection& Connection::operator=(Connection&& other) noexcept
{
    if (this != &other)
        *this = std::move(other);
    return *this;
}

Server::Server(const std::string& path)
: Socket(makeSocket(path, true)), socketPath(path) { }
Server::~Server()                { removeSocketFile(socketPath); }


std::optional<Connection>
Server::accept()
{
    int connFD = ::accept(sockFD, NULL, NULL);
    if (connFD == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return {};
        }
        
        throw make_system_error("Accepting a connection failed");
    }
    
    return Connection(connFD);
}
}
