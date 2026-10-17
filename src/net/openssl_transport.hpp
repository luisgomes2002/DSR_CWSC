#pragma once

#include "net/socket.hpp"

#include <memory>
#include <array>
#include <string>

namespace dsr
{
class OpenSslSession;

// OpenSSL 3 context; nonempty PFX means server. No Windows key store.
class OpenSslContext
{
public:
    OpenSslContext(const std::string &pfx, const std::string &library_directory);
    ~OpenSslContext();
    OpenSslContext(const OpenSslContext &) = delete;
    OpenSslContext &operator=(const OpenSslContext &) = delete;
    const std::string &fingerprint() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class OpenSslSession;
};

class OpenSslSession
{
public:
    explicit OpenSslSession(std::shared_ptr<OpenSslContext> context, std::string expected_pin = "");
    ~OpenSslSession();
    OpenSslSession(const OpenSslSession &) = delete;
    OpenSslSession &operator=(const OpenSslSession &) = delete;
    bool ready() const;
    bool wants_write() const;
    bool receive(SOCKET socket, std::string &plaintext);
    bool send(SOCKET socket, std::string &plaintext);
    std::array<unsigned char, 32> export_udp_key(const std::array<unsigned char, 32> &context);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dsr
