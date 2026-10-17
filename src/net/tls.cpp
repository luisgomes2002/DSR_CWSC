#include "net/tls.hpp"
#include "net/openssl_transport.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace dsr
{
bool valid_fingerprint(const std::string &pin)
{
    return pin.size() == 64 && std::all_of(pin.begin(), pin.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

struct TlsCredentials::Impl
{
    std::shared_ptr<OpenSslContext> context;
};

TlsCredentials::TlsCredentials() : impl_(std::make_unique<Impl>()) {}
TlsCredentials::~TlsCredentials() = default;

std::shared_ptr<TlsCredentials> TlsCredentials::server(const std::string &path, const std::string &directory)
{
    auto result = std::shared_ptr<TlsCredentials>(new TlsCredentials);
    result->impl_->context = std::make_shared<OpenSslContext>(path, directory);
    return result;
}

std::shared_ptr<TlsCredentials> TlsCredentials::client(const std::string &directory)
{
    auto result = std::shared_ptr<TlsCredentials>(new TlsCredentials);
    result->impl_->context = std::make_shared<OpenSslContext>("", directory);
    return result;
}

std::string TlsCredentials::fingerprint() const
{
    return impl_->context->fingerprint();
}

struct TlsChannel::Impl
{
    OpenSslSession session;
    Impl(std::shared_ptr<OpenSslContext> context, std::string pin) : session(std::move(context), std::move(pin)) {}
};

TlsChannel::TlsChannel(std::shared_ptr<TlsCredentials> credentials, std::string pin)
{
    if (credentials->fingerprint().empty())
    {
        if (!valid_fingerprint(pin)) throw std::runtime_error("TLS: pin SHA-256 obrigatorio");
        std::transform(pin.begin(), pin.end(), pin.begin(), [](unsigned char c) {
            return c >= 'A' && c <= 'F' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
        });
    }
    impl_ = std::make_unique<Impl>(credentials->impl_->context, std::move(pin));
}
TlsChannel::~TlsChannel() = default;
bool TlsChannel::ready() const { return impl_->session.ready(); }
bool TlsChannel::wants_write() const { return impl_->session.wants_write(); }
bool TlsChannel::receive(SOCKET socket, std::string &plaintext) { return impl_->session.receive(socket, plaintext); }
bool TlsChannel::send(SOCKET socket, std::string &plaintext) { return impl_->session.send(socket, plaintext); }
std::array<unsigned char, 32> TlsChannel::export_udp_key(const std::array<unsigned char, 32> &context)
{ return impl_->session.export_udp_key(context); }
} // namespace dsr
