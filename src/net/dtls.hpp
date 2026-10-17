#pragma once

#include "net/socket.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace dsr
{
// Binds to the actual local TCP interface, ephemeral UDP port. No NAT discovery.
class DatagramSocket
{
public:
    explicit DatagramSocket(SOCKET control);
    void connect_to(unsigned remote_port);
    unsigned port() const;
    SOCKET native() const { return socket_.value; }
private:
    Socket socket_;
    sockaddr_in peer_{};
};

// DTLS 1.2 PSK-AES128-GCM-SHA256. PSK must come from the authenticated TLS exporter.
// Owns SSL/BIO, not the caller's socket. Single event-loop/thread lifetime.
class DtlsChannel
{
public:
    DtlsChannel(SOCKET socket, bool server, const std::array<unsigned char, 32> &key,
                const std::string &library_directory);
    ~DtlsChannel();
    DtlsChannel(const DtlsChannel &) = delete;
    DtlsChannel &operator=(const DtlsChannel &) = delete;
    bool ready() const;
    std::vector<std::vector<unsigned char>> poll();
    bool send(const unsigned char *data, std::size_t size);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dsr
