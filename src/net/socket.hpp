#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <string>

namespace dsr
{
// Owns the process-local Winsock lifetime. Never copy resource owners.
struct Winsock
{
    Winsock();
    ~Winsock();
    Winsock(const Winsock &) = delete;
    Winsock &operator=(const Winsock &) = delete;
};

struct Socket
{
    SOCKET value = INVALID_SOCKET;
    explicit Socket(SOCKET s = INVALID_SOCKET);
    ~Socket();
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
};

[[noreturn]] void net_error(const char *action);
void nonblocking(SOCKET s);
sockaddr_in address(const std::string &ip, unsigned port);
// Performs one nonblocking send and keeps any unsent bytes in the queue.
bool flush(SOCKET s, std::string &out);
} // namespace dsr


