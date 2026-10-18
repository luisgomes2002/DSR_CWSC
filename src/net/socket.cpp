#include "net/socket.hpp"

#include <stdexcept>

namespace dsr
{
Winsock::Winsock()
{
    WSADATA w{};
    if (WSAStartup(MAKEWORD(2, 2), &w))
        throw std::runtime_error("WSAStartup failed");
}

Winsock::~Winsock() { WSACleanup(); }

Socket::Socket(SOCKET s) : value(s) {}

Socket::~Socket()
{
    if (value != INVALID_SOCKET)
        closesocket(value);
}

[[noreturn]] void net_error(const char *action)
{
    throw std::runtime_error(std::string(action) + ": Winsock " + std::to_string(WSAGetLastError()));
}
void nonblocking(SOCKET s)
{
    u_long one = 1;
    if (ioctlsocket(s, FIONBIO, &one))
        net_error("ioctlsocket");
}

sockaddr_in address(const std::string &ip, unsigned port)
{
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1)
        throw std::runtime_error("Use um endereco IPv4 numerico");
    return a;
}

bool flush(SOCKET s, std::string &out)
{
    if (out.empty())
        return true;
    const int n = send(s, out.data(), static_cast<int>(out.size()), 0);
    if (n == SOCKET_ERROR)
        return WSAGetLastError() == WSAEWOULDBLOCK;
    if (n <= 0)
        return false;
    out.erase(0, static_cast<size_t>(n));
    return true;
}
} // namespace dsr


