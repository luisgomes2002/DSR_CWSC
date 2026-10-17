#include "net/dtls.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>

namespace dsr
{
DatagramSocket::DatagramSocket(SOCKET control) : socket_(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))
{
    if (socket_.value == INVALID_SOCKET) net_error("UDP socket");
    sockaddr_in local{};
    int size = sizeof(local);
    if (getsockname(control, reinterpret_cast<sockaddr *>(&local), &size)) net_error("UDP local interface");
    size = sizeof(peer_);
    if (getpeername(control, reinterpret_cast<sockaddr *>(&peer_), &size)) net_error("UDP peer interface");
    local.sin_port = 0;
    const BOOL exclusive = TRUE;
    if (setsockopt(socket_.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive), sizeof(exclusive)) ||
        bind(socket_.value, reinterpret_cast<sockaddr *>(&local), sizeof(local))) net_error("UDP bind");
    nonblocking(socket_.value);
}

void DatagramSocket::connect_to(unsigned remote_port)
{
    if (remote_port < 1024 || remote_port > 65535) throw std::runtime_error("UDP: porta remota invalida");
    peer_.sin_port = htons(static_cast<u_short>(remote_port));
    if (connect(socket_.value, reinterpret_cast<sockaddr *>(&peer_), sizeof(peer_))) net_error("UDP connect");
}

unsigned DatagramSocket::port() const
{
    sockaddr_in local{};
    int size = sizeof(local);
    if (getsockname(socket_.value, reinterpret_cast<sockaddr *>(&local), &size)) net_error("UDP port");
    return ntohs(local.sin_port);
}

namespace
{
struct SSL;
struct SSL_CTX;
struct SSL_METHOD;
struct BIO;
using ClientCallback = unsigned (*)(SSL *, const char *, char *, unsigned, unsigned char *, unsigned);
using ServerCallback = unsigned (*)(SSL *, const char *, unsigned char *, unsigned);

struct Library
{
    HMODULE handle = nullptr;
    explicit Library(const std::filesystem::path &path)
        : handle(LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32))
    { if (!handle) throw std::runtime_error("DTLS: OpenSSL 3 x64 ausente/incompativel"); }
    ~Library() { if (handle) FreeLibrary(handle); }
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;
    template <typename T> T symbol(const char *name)
    {
        auto raw = GetProcAddress(handle, name);
        if (!raw) throw std::runtime_error(std::string("DTLS: API ausente: ") + name);
        T result; static_assert(sizeof(result) == sizeof(raw));
        std::memcpy(&result, &raw, sizeof(result)); return result;
    }
};

struct Api
{
    Library crypto, ssl;
#define S(ret, name, ...) ret (*name)(__VA_ARGS__) = ssl.symbol<decltype(name)>(#name)
#define C(ret, name, ...) ret (*name)(__VA_ARGS__) = crypto.symbol<decltype(name)>(#name)
    S(const SSL_METHOD *, DTLS_server_method, void);
    S(const SSL_METHOD *, DTLS_client_method, void);
    S(int, OPENSSL_init_ssl, std::uint64_t, const void *);
    S(SSL_CTX *, SSL_CTX_new, const SSL_METHOD *);
    S(void, SSL_CTX_free, SSL_CTX *);
    S(long, SSL_CTX_ctrl, SSL_CTX *, int, long, void *);
    S(std::uint64_t, SSL_CTX_set_options, SSL_CTX *, std::uint64_t);
    S(int, SSL_CTX_set_cipher_list, SSL_CTX *, const char *);
    S(SSL *, SSL_new, SSL_CTX *);
    S(void, SSL_free, SSL *);
    S(void, SSL_set_bio, SSL *, BIO *, BIO *);
    S(void, SSL_set_accept_state, SSL *);
    S(void, SSL_set_connect_state, SSL *);
    S(void, SSL_set_psk_client_callback, SSL *, ClientCallback);
    S(void, SSL_set_psk_server_callback, SSL *, ServerCallback);
    S(int, SSL_do_handshake, SSL *);
    S(int, SSL_get_error, const SSL *, int);
    S(int, SSL_read, SSL *, void *, int);
    S(int, SSL_write, SSL *, const void *, int);
    S(long, SSL_ctrl, SSL *, int, long, void *);
    C(unsigned long, OpenSSL_version_num, void);
    C(void, ERR_clear_error, void);
    C(void, OPENSSL_thread_stop, void);
    C(BIO *, BIO_new_dgram, int, int);
    C(int, BIO_free, BIO *);
    C(long, BIO_ctrl, BIO *, int, long, void *);
#undef S
#undef C
    explicit Api(const std::filesystem::path &directory)
        : crypto(directory / L"libcrypto-3-x64.dll"), ssl(directory / L"libssl-3-x64.dll")
    {
        if ((OpenSSL_version_num() >> 28) != 3 || OPENSSL_init_ssl(0x00080080, nullptr) != 1)
            throw std::runtime_error("DTLS: esperado OpenSSL 3 inicializado");
    }
    ~Api() { OPENSSL_thread_stop(); }
};

std::filesystem::path library_path(const std::string &directory)
{
    auto path = std::filesystem::u8path(directory.empty() ? "C:/Program Files/Git/mingw64/bin" : directory);
    if (!path.is_absolute()) throw std::runtime_error("DTLS: pasta de bibliotecas deve ser absoluta");
    return path;
}
} // namespace

struct DtlsChannel::Impl
{
    Api api;
    SSL_CTX *context = nullptr;
    SSL *session = nullptr;
    std::array<unsigned char, 32> key{};
    bool ready = false;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    // PSK callbacks are synchronous on this event-loop thread, never shared with workers.
    static thread_local std::map<SSL *, Impl *> callbacks;
    explicit Impl(const std::string &directory, const std::array<unsigned char, 32> &secret)
        : api(library_path(directory)), key(secret) {}
    ~Impl()
    {
        callbacks.erase(session);
        if (session) api.SSL_free(session);
        if (context) api.SSL_CTX_free(context);
        SecureZeroMemory(key.data(), key.size());
    }
    static unsigned client_key(SSL *session, const char *, char *identity, unsigned identity_size,
                               unsigned char *out, unsigned size)
    {
        auto it = callbacks.find(session);
        if (it == callbacks.end() || identity_size < 7 || size < 32) return 0;
        std::memcpy(identity, "dsrm-1", 7);
        std::memcpy(out, it->second->key.data(), 32); return 32;
    }
    static unsigned server_key(SSL *session, const char *identity, unsigned char *out, unsigned size)
    {
        auto it = callbacks.find(session);
        if (it == callbacks.end() || !identity || std::strcmp(identity, "dsrm-1") || size < 32) return 0;
        std::memcpy(out, it->second->key.data(), 32); return 32;
    }
    bool retry(int n)
    {
        const int error = api.SSL_get_error(session, n);
        if (error == 2 || error == 3) return true; // WANT_READ/WANT_WRITE
        throw std::runtime_error("DTLS: canal encerrado ou registro/handshake invalido");
    }
    void handshake()
    {
        if (ready) return;
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(7))
            throw std::runtime_error("DTLS: timeout de handshake");
        api.ERR_clear_error();
        if (api.SSL_ctrl(session, 74, 0, nullptr) < 0) // DTLS_CTRL_HANDLE_TIMEOUT
            throw std::runtime_error("DTLS: falha de retransmissao");
        api.ERR_clear_error();
        const int n = api.SSL_do_handshake(session);
        if (n == 1) ready = true;
        else retry(n);
    }
};
thread_local std::map<SSL *, DtlsChannel::Impl *> DtlsChannel::Impl::callbacks;

DtlsChannel::DtlsChannel(SOCKET socket, bool server, const std::array<unsigned char, 32> &key,
                         const std::string &directory) : impl_(std::make_unique<Impl>(directory, key))
{
    auto &p = *impl_; auto &a = p.api;
    if (socket > static_cast<SOCKET>((std::numeric_limits<int>::max)()))
        throw std::runtime_error("DTLS: socket fora do limite da ABI OpenSSL");
    p.context = a.SSL_CTX_new(server ? a.DTLS_server_method() : a.DTLS_client_method());
    if (!p.context || a.SSL_CTX_ctrl(p.context, 123, 0xfefd, nullptr) != 1 ||
        a.SSL_CTX_ctrl(p.context, 124, 0xfefd, nullptr) != 1 ||
        a.SSL_CTX_set_cipher_list(p.context, "PSK-AES128-GCM-SHA256") != 1)
        throw std::runtime_error("DTLS: configuracao DTLS 1.2/PSK falhou");
    // NO_QUERY_MTU, NO_RENEGOTIATION, NO_TICKET; default anti-replay remains enabled.
    a.SSL_CTX_set_options(p.context, (std::uint64_t{1} << 12) | (std::uint64_t{1} << 30) | (std::uint64_t{1} << 14));
    p.session = a.SSL_new(p.context);
    if (!p.session) throw std::runtime_error("DTLS: sessao falhou");
    BIO *bio = a.BIO_new_dgram(static_cast<int>(socket), 0); // socket owned by caller
    if (!bio) throw std::runtime_error("DTLS: BIO falhou");
    sockaddr_in peer{}; int size = sizeof(peer);
    if (getpeername(socket, reinterpret_cast<sockaddr *>(&peer), &size) ||
        a.BIO_ctrl(bio, 32, 0, &peer) != 1) // BIO_CTRL_DGRAM_SET_CONNECTED
    { a.BIO_free(bio); throw std::runtime_error("DTLS: endpoint nao conectado"); }
    a.SSL_set_bio(p.session, bio, bio);
    if (a.SSL_ctrl(p.session, 17, 1200, nullptr) <= 0) throw std::runtime_error("DTLS: MTU falhou");
    Impl::callbacks.emplace(p.session, &p);
    if (server) { a.SSL_set_psk_server_callback(p.session, Impl::server_key); a.SSL_set_accept_state(p.session); }
    else { a.SSL_set_psk_client_callback(p.session, Impl::client_key); a.SSL_set_connect_state(p.session); }
}
DtlsChannel::~DtlsChannel() = default;
bool DtlsChannel::ready() const { return impl_->ready; }

std::vector<std::vector<unsigned char>> DtlsChannel::poll()
{
    auto &p = *impl_;
    p.handshake();
    std::vector<std::vector<unsigned char>> messages;
    if (!p.ready) return messages;
    // Bounded work and full-record buffer: oversized payloads are rejected by the codec.
    std::array<unsigned char, 16384> buffer{};
    for (unsigned i = 0; i < 8; ++i)
    {
        p.api.ERR_clear_error();
        const int n = p.api.SSL_read(p.session, buffer.data(), static_cast<int>(buffer.size()));
        if (n <= 0) { p.retry(n); break; }
        messages.emplace_back(buffer.begin(), buffer.begin() + n);
    }
    return messages;
}

bool DtlsChannel::send(const unsigned char *data, std::size_t size)
{
    if (!ready() || !data || size == 0 || size > 1200) return false;
    // Best-effort application records. WANT_* does not enqueue/retry stale positions.
    // SSL_write retry requirements are avoided by failing closed on WANT_*.
    auto &p = *impl_; p.api.ERR_clear_error();
    if (p.api.SSL_write(p.session, data, static_cast<int>(size)) != static_cast<int>(size))
        throw std::runtime_error("DTLS: envio falhou; reconecte sem fallback");
    return true;
}
} // namespace dsr
