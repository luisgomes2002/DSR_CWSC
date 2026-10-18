#pragma once

#include "net/socket.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dsr
{
struct DtlsSendStats
{
    std::uint64_t records_sent = 0, write_attempts = 0;
    std::uint64_t want_read = 0, want_write = 0, suppressed = 0, failures = 0, timeouts = 0;
    std::size_t pending_bytes = 0, peak_pending_bytes = 0;
};
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
    // True means admitted (sent or retained in the single pending slot).
    // False means not ready, invalid size, or busy. poll() advances retries.
    bool send(const unsigned char *data, std::size_t size);
    const DtlsSendStats &send_stats() const;
#ifdef DSR_DTLS_TESTING
    // Built only into dtls_unit's copy of dtls.cpp, never the application.
    void test_block_writes(unsigned attempts, bool want_read = false, bool fatal = false);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dsr
