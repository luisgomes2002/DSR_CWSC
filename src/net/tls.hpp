#pragma once

#include "net/socket.hpp"

#include <memory>
#include <array>
#include <string>

namespace dsr
{
// TLS 1.2 via OpenSSL 3. Trust is an explicit SHA-256 certificate pin,
// not a system-wide CA installation or an automatic trust-on-first-use rule.
class TlsCredentials
{
public:
    static std::shared_ptr<TlsCredentials> server(const std::string &pfx_path, const std::string &library_directory = "");
    static std::shared_ptr<TlsCredentials> client(const std::string &library_directory = "");
    ~TlsCredentials();
    std::string fingerprint() const;
    TlsCredentials(const TlsCredentials &) = delete;
    TlsCredentials &operator=(const TlsCredentials &) = delete;

private:
    TlsCredentials();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class TlsChannel;
};

class TlsChannel
{
public:
    explicit TlsChannel(std::shared_ptr<TlsCredentials> credentials,
                        std::string expected_pin = "");
    ~TlsChannel();
    TlsChannel(const TlsChannel &) = delete;
    TlsChannel &operator=(const TlsChannel &) = delete;

    bool ready() const;
    bool wants_write() const;
    // Reads/decrypts or writes/encrypts without blocking. False means closed/error.
    bool receive(SOCKET socket, std::string &plaintext);
    bool send(SOCKET socket, std::string &plaintext);
    std::array<unsigned char, 32> export_udp_key(const std::array<unsigned char, 32> &context);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

bool valid_fingerprint(const std::string &pin);
} // namespace dsr
