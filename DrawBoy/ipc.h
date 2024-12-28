#ifndef _INCLUDE_IPC_H_
#define _INCLUDE_IPC_H_

#include <optional>
#include <stdexcept>
#include <system_error>
#include <string>

namespace IPC {
class Socket {
private:
    Socket(int fd);
    ~Socket();
    
    // Can't copy a Socket...
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    
    // ...but you can move it.
    Socket(Socket&&) noexcept;
    Socket& operator=(Socket&&) noexcept;
    
    bool valid();
    void close();
    void invalidate();
    
    int sockFD;

    friend class Client;
    friend class Server;
    friend class Connection;
public:
    int fd() { return sockFD; };
    int release();
};

class SocketError : public std::system_error {
public:
    SocketError(const char*);
};

class Client : public Socket {
public:
    Client(const std::string& path);
    ~Client();
};

class Connection : public Socket {
private:
    Connection(int fd);
public:
    ~Connection();
    
    // Can't copy a Connection...
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    
    // ...but you can move it.
    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;
    
    friend class Server;
};

class Server : public Socket {
public:
    Server(const std::string& path);
    ~Server();

    std::optional<Connection> accept();
    std::string socketPath;
};
}

#endif // _INCLUDE_IPC_H_
