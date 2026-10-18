#include "net/dtls.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>
#ifdef DSR_ROOM_TESTING
#include <cstdlib>
#endif

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
#ifdef DSR_ROOM_TESTING
    // Only the separately built full-room test executable contains this hook.
    // Register original endpoints with a loopback encrypted-datagram relay.
    if (const char *setting = std::getenv("DSR_LAB_RELAY_PORT"))
    {
        char *end = nullptr;
        const auto relay_port = std::strtoul(setting, &end, 10);
        sockaddr_in local{}; int size = sizeof(local);
        if (!*setting || *end || relay_port < 1024 || relay_port > 65535 ||
            getsockname(socket_.value, reinterpret_cast<sockaddr *>(&local), &size) ||
            local.sin_addr.s_addr != htonl(INADDR_LOOPBACK) || peer_.sin_addr.s_addr != htonl(INADDR_LOOPBACK))
            throw std::runtime_error("LAB: relay requires loopback endpoints and a valid port");
        Socket registration(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (registration.value == INVALID_SOCKET) net_error("LAB relay socket");
        const DWORD timeout = 2000;
        if (setsockopt(registration.value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout)) ||
            setsockopt(registration.value, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout)))
            net_error("LAB relay timeout");
        const auto controller = address("127.0.0.1", static_cast<unsigned>(relay_port));
        if (connect(registration.value, reinterpret_cast<const sockaddr *>(&controller), sizeof(controller)))
            net_error("LAB relay connect");
        const std::array<unsigned short, 2> endpoints{local.sin_port, htons(static_cast<u_short>(remote_port))};
        const char *request = reinterpret_cast<const char *>(endpoints.data());
        for (int sent = 0; sent < 4;)
        {
            const int n = ::send(registration.value, request + sent, 4 - sent, 0);
            if (n <= 0) throw std::runtime_error("LAB: relay registration send failed");
            sent += n;
        }
        unsigned short mapped = 0;
        for (int received = 0; received < 2;)
        {
            const int n = recv(registration.value, reinterpret_cast<char *>(&mapped) + received, 2 - received, 0);
            if (n <= 0) throw std::runtime_error("LAB: relay registration receive failed");
            received += n;
        }
        remote_port = ntohs(mapped);
        if (remote_port < 1024) throw std::runtime_error("LAB: relay registration rejected");
    }
#endif
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
#ifdef DSR_DTLS_TESTING
using BioCallback = long (*)(BIO *, int, const char *, std::size_t, int, long, int, std::size_t *);
#endif

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
#ifdef DSR_DTLS_TESTING
    C(void, BIO_set_callback_ex, BIO *, BioCallback);
    C(void, BIO_set_flags, BIO *, int);
    C(void, BIO_clear_flags, BIO *, int);
#endif
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
    using Clock = std::chrono::steady_clock;
    static constexpr auto RetryInterval = std::chrono::milliseconds(10);
    static constexpr auto WriteDeadline = std::chrono::seconds(2);
    static constexpr unsigned MaxWriteAttempts = 128;
    Api api;
    SSL_CTX *context = nullptr;
    SSL *session = nullptr;
    std::array<unsigned char, 32> key{};
    bool ready = false;
    bool failed = false;
    SOCKET socket = INVALID_SOCKET;
    std::array<unsigned char, 1200> pending{};
    DtlsSendStats send_stats;
    Clock::time_point queued{}, last_attempt{};
    unsigned attempts = 0;
    int write_want = 0;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    // PSK callbacks are synchronous on this event-loop thread, never shared with workers.
    static thread_local std::map<SSL *, Impl *> callbacks;
#ifdef DSR_DTLS_TESTING
    BIO *test_bio = nullptr;
    unsigned test_blocks = 0;
    bool test_read = false, test_fatal = false;
    static thread_local std::map<BIO *, Impl *> test_callbacks;
    static long test_callback(BIO *bio, int operation, const char *, std::size_t, int, long, int result, std::size_t *)
    {
        // BIO_CB_WRITE before the actual datagram write. This makes real
        // SSL_write/SSL_get_error report the requested retry condition.
        auto it = test_callbacks.find(bio);
        if (operation != 3 || it == test_callbacks.end()) return result;
        auto &p = *it->second;
        p.api.BIO_clear_flags(bio, 15);
        if (!p.test_blocks) return result;
        --p.test_blocks;
        if (!p.test_fatal) p.api.BIO_set_flags(bio, p.test_read ? 9 : 10);
        return -1;
    }
#endif
    explicit Impl(const std::string &directory, const std::array<unsigned char, 32> &secret)
        : api(library_path(directory)), key(secret) {}
    ~Impl()
    {
#ifdef DSR_DTLS_TESTING
        test_callbacks.erase(test_bio);
#endif
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
        failed = true;
        throw std::runtime_error("DTLS: channel closed or record/handshake is invalid");
    }
    [[noreturn]] void write_failure(const char *message, bool timeout = false)
    {
        failed = true;
        ++send_stats.failures;
        if (timeout) ++send_stats.timeouts;
        throw std::runtime_error(message);
    }
    void write_pending(bool initial = false)
    {
        if (!send_stats.pending_bytes) return;
        const auto now = Clock::now();
        if (now - queued >= WriteDeadline || attempts >= MaxWriteAttempts)
            write_failure("DTLS: pending send exceeded retry deadline/budget; reconnect", true);
        if (!initial)
        {
            if (now - last_attempt < RetryInterval) return;
            fd_set r, w; FD_ZERO(&r); FD_ZERO(&w);
            if (write_want == 2) FD_SET(socket, &r); else FD_SET(socket, &w);
            timeval wait{0, 0};
            const int n = select(0, &r, &w, nullptr, &wait);
            if (n == SOCKET_ERROR) write_failure("DTLS: pending send readiness failed");
            if (!n) return;
        }
        last_attempt = now; ++attempts; ++send_stats.write_attempts;
        api.ERR_clear_error();
        const int n = api.SSL_write(session, pending.data(), static_cast<int>(send_stats.pending_bytes));
        if (n == static_cast<int>(send_stats.pending_bytes))
        {
            ++send_stats.records_sent;
            send_stats.pending_bytes = 0;
            write_want = 0;
            return;
        }
        // Get the SSL error immediately, with no intervening OpenSSL call.
        const int error = n <= 0 ? api.SSL_get_error(session, n) : 0;
        if (error == 2 || error == 3)
        {
            write_want = error;
            if (error == 2) ++send_stats.want_read; else ++send_stats.want_write;
            return;
        }
        write_failure("DTLS: fatal send failure; reconnect without fallback");
    }
    void handshake()
    {
        if (ready) return;
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(7))
            throw std::runtime_error("DTLS: timeout de handshake");
        api.ERR_clear_error();
        if (api.SSL_ctrl(session, 74, 0, nullptr) < 0) // DTLS_CTRL_HANDLE_TIMEOUT
            throw std::runtime_error("DTLS: retransmission failed");
        api.ERR_clear_error();
        const int n = api.SSL_do_handshake(session);
        if (n == 1) ready = true;
        else retry(n);
    }
};
thread_local std::map<SSL *, DtlsChannel::Impl *> DtlsChannel::Impl::callbacks;
#ifdef DSR_DTLS_TESTING
thread_local std::map<BIO *, DtlsChannel::Impl *> DtlsChannel::Impl::test_callbacks;
#endif

DtlsChannel::DtlsChannel(SOCKET socket, bool server, const std::array<unsigned char, 32> &key,
                         const std::string &directory) : impl_(std::make_unique<Impl>(directory, key))
{
    auto &p = *impl_; auto &a = p.api;
    p.socket = socket;
    if (socket > static_cast<SOCKET>((std::numeric_limits<int>::max)()))
        throw std::runtime_error("DTLS: socket is outside the OpenSSL ABI limit");
    p.context = a.SSL_CTX_new(server ? a.DTLS_server_method() : a.DTLS_client_method());
    if (!p.context || a.SSL_CTX_ctrl(p.context, 123, 0xfefd, nullptr) != 1 ||
        a.SSL_CTX_ctrl(p.context, 124, 0xfefd, nullptr) != 1 ||
        a.SSL_CTX_set_cipher_list(p.context, "PSK-AES128-GCM-SHA256") != 1)
        throw std::runtime_error("DTLS: DTLS 1.2/PSK configuration failed");
    // NO_QUERY_MTU, NO_RENEGOTIATION, NO_TICKET; default anti-replay remains enabled.
    a.SSL_CTX_set_options(p.context, (std::uint64_t{1} << 12) | (std::uint64_t{1} << 30) | (std::uint64_t{1} << 14));
    p.session = a.SSL_new(p.context);
    if (!p.session) throw std::runtime_error("DTLS: session creation failed");
    BIO *bio = a.BIO_new_dgram(static_cast<int>(socket), 0); // socket owned by caller
    if (!bio) throw std::runtime_error("DTLS: BIO creation failed");
    sockaddr_in peer{}; int size = sizeof(peer);
    if (getpeername(socket, reinterpret_cast<sockaddr *>(&peer), &size) ||
        a.BIO_ctrl(bio, 32, 0, &peer) != 1) // BIO_CTRL_DGRAM_SET_CONNECTED
    { a.BIO_free(bio); throw std::runtime_error("DTLS: endpoint is not connected"); }
    a.SSL_set_bio(p.session, bio, bio);
#ifdef DSR_DTLS_TESTING
    p.test_bio = bio;
#endif
    if (a.SSL_ctrl(p.session, 17, 1200, nullptr) <= 0) throw std::runtime_error("DTLS: MTU configuration failed");
    Impl::callbacks.emplace(p.session, &p);
    if (server) { a.SSL_set_psk_server_callback(p.session, Impl::server_key); a.SSL_set_accept_state(p.session); }
    else { a.SSL_set_psk_client_callback(p.session, Impl::client_key); a.SSL_set_connect_state(p.session); }
}
DtlsChannel::~DtlsChannel() = default;
bool DtlsChannel::ready() const { return impl_->ready && !impl_->failed; }
const DtlsSendStats &DtlsChannel::send_stats() const { return impl_->send_stats; }
#ifdef DSR_DTLS_TESTING
void DtlsChannel::test_block_writes(unsigned attempts, bool want_read, bool fatal)
{
    auto &p = *impl_;
    p.test_blocks = attempts; p.test_read = want_read; p.test_fatal = fatal;
    Impl::test_callbacks[p.test_bio] = &p;
    p.api.BIO_set_callback_ex(p.test_bio, Impl::test_callback);
}
#endif

std::vector<std::vector<unsigned char>> DtlsChannel::poll()
{
    auto &p = *impl_;
    if (p.failed) throw std::runtime_error("DTLS: channel failed; reconnect");
    p.handshake();
    std::vector<std::vector<unsigned char>> messages;
    if (!p.ready) return messages;
    p.write_pending();
    // In particular WANT_WRITE must resume SSL_write before any other SSL I/O.
    if (p.send_stats.pending_bytes) return messages;
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
    auto &p = *impl_;
    if (p.failed) throw std::runtime_error("DTLS: channel failed; reconnect");
    if (!ready() || !data || size == 0 || size > 1200) return false;
    if (p.send_stats.pending_bytes) { ++p.send_stats.suppressed; return false; }
    // Copy before the first SSL_write: the address, bytes and length stay fixed
    // through all retries, even if the caller's buffer is changed or destroyed.
    std::copy_n(data, size, p.pending.begin());
    p.send_stats.pending_bytes = size;
    p.send_stats.peak_pending_bytes = (std::max)(p.send_stats.peak_pending_bytes, size);
    p.queued = Impl::Clock::now(); p.attempts = 0;
    p.write_pending(true);
    return true;
}
} // namespace dsr
