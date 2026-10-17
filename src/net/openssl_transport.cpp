#include "net/openssl_transport.hpp"

#include <bcrypt.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace dsr
{
    namespace
    {
        // Narrow dynamic interface to the public OpenSSL 3 C ABI. All objects opaque.
        // Signatures/constants: openssl/ssl.h, bio.h, pkcs12.h, x509.h and crypto.h.
        struct SSL_CTX;
        struct SSL;
        struct SSL_METHOD;
        struct BIO;
        struct BIO_METHOD;
        struct PKCS12;
        struct X509;
        struct EVP_PKEY;
        struct X509Stack;
        struct InitSettings;
        constexpr std::size_t CipherLimit = 256 * 1024;
        constexpr int WantRead = 2, WantWrite = 3, ZeroReturn = 6;
        constexpr int SetMinVersion = 123, SetMaxVersion = 124, Tls12 = 0x0303;

        struct Module
        {
            HMODULE handle;
            explicit Module(const std::filesystem::path &path)
                : handle(LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32))
            {
                if (!handle)
                    throw std::runtime_error("TLS: DLL OpenSSL 3 x64 ausente/incompativel; use --tls-library PASTA confiavel");
            }
            ~Module()
            {
                if (handle)
                    FreeLibrary(handle);
            }
            Module(const Module &) = delete;
            Module &operator=(const Module &) = delete;
            template <typename T>
            T get(const char *name) const
            {
                auto proc = GetProcAddress(handle, name);
                if (!proc)
                    throw std::runtime_error(std::string("TLS: API OpenSSL ausente: ") + name);
                T result;
                static_assert(sizeof(result) == sizeof(proc));
                std::memcpy(&result, &proc, sizeof(result));
                return result;
            }
        };

        struct Api
        {
            // Dependencies loaded by absolute path, never from cwd or untrusted PATH.
            Module crypto, ssl;
#define DSR_SSL(ret, name, ...) ret (*name)(__VA_ARGS__) = ssl.get<decltype(name)>(#name)
#define DSR_CRYPTO(ret, name, ...) ret (*name)(__VA_ARGS__) = crypto.get<decltype(name)>(#name)
            DSR_SSL(const SSL_METHOD *, TLS_server_method, void);
            DSR_SSL(const SSL_METHOD *, TLS_client_method, void);
            DSR_SSL(int, OPENSSL_init_ssl, std::uint64_t, const InitSettings *);
            DSR_SSL(SSL_CTX *, SSL_CTX_new, const SSL_METHOD *);
            DSR_SSL(void, SSL_CTX_free, SSL_CTX *);
            DSR_SSL(long, SSL_CTX_ctrl, SSL_CTX *, int, long, void *);
            DSR_SSL(long, SSL_ctrl, SSL *, int, long, void *);
            DSR_SSL(std::uint64_t, SSL_CTX_set_options, SSL_CTX *, std::uint64_t);
            DSR_SSL(int, SSL_CTX_set_cipher_list, SSL_CTX *, const char *);
            DSR_SSL(int, SSL_CTX_use_certificate, SSL_CTX *, X509 *);
            DSR_SSL(int, SSL_CTX_use_PrivateKey, SSL_CTX *, EVP_PKEY *);
            DSR_SSL(int, SSL_CTX_check_private_key, const SSL_CTX *);
            DSR_SSL(SSL *, SSL_new, SSL_CTX *);
            DSR_SSL(void, SSL_free, SSL *);
            DSR_SSL(void, SSL_set_bio, SSL *, BIO *, BIO *);
            DSR_SSL(void, SSL_set_accept_state, SSL *);
            DSR_SSL(void, SSL_set_connect_state, SSL *);
            DSR_SSL(X509 *, SSL_get1_peer_certificate, const SSL *);
            DSR_SSL(int, SSL_do_handshake, SSL *);
            DSR_SSL(int, SSL_get_error, const SSL *, int);
            DSR_SSL(int, SSL_read, SSL *, void *, int);
            DSR_SSL(int, SSL_write, SSL *, const void *, int);
            DSR_SSL(int, SSL_export_keying_material, SSL *, unsigned char *, std::size_t,
                    const char *, std::size_t, const unsigned char *, std::size_t, int);
            DSR_CRYPTO(unsigned long, OpenSSL_version_num, void);
            DSR_CRYPTO(void, ERR_clear_error, void);
            DSR_CRYPTO(void, OPENSSL_thread_stop, void);
            DSR_CRYPTO(PKCS12 *, d2i_PKCS12, PKCS12 **, const unsigned char **, long);
            DSR_CRYPTO(int, PKCS12_parse, PKCS12 *, const char *, EVP_PKEY **, X509 **, X509Stack **);
            DSR_CRYPTO(void, PKCS12_free, PKCS12 *);
            DSR_CRYPTO(void, X509_free, X509 *);
            DSR_CRYPTO(void, EVP_PKEY_free, EVP_PKEY *);
            DSR_CRYPTO(int, i2d_X509, const X509 *, unsigned char **);
            DSR_CRYPTO(const BIO_METHOD *, BIO_s_mem, void);
            DSR_CRYPTO(BIO *, BIO_new, const BIO_METHOD *);
            DSR_CRYPTO(int, BIO_free, BIO *);
            DSR_CRYPTO(int, BIO_read, BIO *, void *, int);
            DSR_CRYPTO(int, BIO_write, BIO *, const void *, int);
            DSR_CRYPTO(std::size_t, BIO_ctrl_pending, BIO *);
#undef DSR_SSL
#undef DSR_CRYPTO
            explicit Api(const std::filesystem::path &directory)
                : crypto(directory / L"libcrypto-3-x64.dll"), ssl(directory / L"libssl-3-x64.dll")
            {
                if ((OpenSSL_version_num() >> 28) != 3)
                    throw std::runtime_error("TLS: esperado OpenSSL 3");
                // Avoid external configuration and callbacks into an unloaded dynamic library.
                constexpr std::uint64_t init_flags = 0x00000080 | 0x00080000;
                if (OPENSSL_init_ssl(init_flags, nullptr) != 1)
                    throw std::runtime_error("TLS: inicializacao OpenSSL falhou");
            }
            ~Api() { OPENSSL_thread_stop(); }
        };

        std::string hash_and_validate(Api &api, X509 *cert)
        {
            const int length = api.i2d_X509(cert, nullptr);
            if (length <= 0 || length > 1024 * 1024)
                throw std::runtime_error("TLS: certificado invalido");
            std::vector<unsigned char> der(static_cast<std::size_t>(length));
            auto ptr = der.data();
            if (api.i2d_X509(cert, &ptr) != length)
                throw std::runtime_error("TLS: certificado invalido");
            PCCERT_CONTEXT parsed = CertCreateCertificateContext(X509_ASN_ENCODING, der.data(), static_cast<DWORD>(der.size()));
            if (!parsed)
                throw std::runtime_error("TLS: certificado invalido");
            const bool valid = CertVerifyTimeValidity(nullptr, parsed->pCertInfo) == 0;
            CertFreeCertificateContext(parsed);
            if (!valid)
                throw std::runtime_error("TLS: certificado fora da validade");
            std::array<unsigned char, 32> digest{};
            DWORD count = static_cast<DWORD>(digest.size());
            if (!CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, nullptr, der.data(), static_cast<DWORD>(der.size()), digest.data(), &count))
                throw std::runtime_error("TLS: hash do certificado falhou");
            std::string pin;
            for (auto b : digest)
            {
                pin += "0123456789abcdef"[b >> 4];
                pin += "0123456789abcdef"[b & 15];
            }
            return pin;
        }
    } // namespace

    struct OpenSslContext::Impl
    {
        Api api;
        SSL_CTX *context = nullptr;
        bool server = false;
        std::string pin;
        explicit Impl(const std::filesystem::path &directory) : api(directory) {}
        ~Impl()
        {
            if (context)
                api.SSL_CTX_free(context);
        }
    };

    OpenSslContext::OpenSslContext(const std::string &pfx, const std::string &library_directory)
    {
        const auto directory = std::filesystem::u8path(library_directory.empty() ? "C:/Program Files/Git/mingw64/bin" : library_directory);
        if (!directory.is_absolute())
            throw std::runtime_error("TLS: --tls-library exige pasta absoluta confiavel");
        impl_ = std::make_unique<Impl>(directory);
        auto &a = impl_->api;
        impl_->server = !pfx.empty();
        impl_->context = a.SSL_CTX_new(impl_->server ? a.TLS_server_method() : a.TLS_client_method());
        if (!impl_->context)
            throw std::runtime_error("TLS: contexto OpenSSL falhou");
        auto *ctx = impl_->context;
        if (a.SSL_CTX_ctrl(ctx, SetMinVersion, Tls12, nullptr) != 1 || a.SSL_CTX_ctrl(ctx, SetMaxVersion, Tls12, nullptr) != 1 ||
            a.SSL_CTX_set_cipher_list(ctx, "ECDHE+AESGCM:ECDHE+CHACHA20") != 1)
            throw std::runtime_error("TLS: configuracao OpenSSL falhou");
        a.SSL_CTX_set_options(ctx, std::uint64_t{1} << 30); // SSL_OP_NO_RENEGOTIATION
        if (!impl_->server)
            return;
        std::ifstream file(std::filesystem::u8path(pfx), std::ios::binary | std::ios::ate);
        if (!file || file.tellg() <= 0 || file.tellg() > 1024 * 1024)
            throw std::runtime_error("TLS: PFX ausente ou invalido");
        const auto length = static_cast<std::size_t>(file.tellg());
        file.seekg(0);
        std::vector<unsigned char> bytes(length);
        if (!file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(length)))
            throw std::runtime_error("TLS: leitura PFX falhou");
        const char *env = std::getenv("DSR_TLS_PFX_PASSWORD");
        std::string password = env ? env : "";
        if (password.size() < 12 || password.size() > 128 || !std::all_of(password.begin(), password.end(), [](unsigned char c)
                                                                          { return c >= 32 && c <= 126; }))
            throw std::runtime_error("Defina DSR_TLS_PFX_PASSWORD com 12 a 128 caracteres ASCII");
        const auto *cursor = bytes.data();
        auto *bundle = a.d2i_PKCS12(nullptr, &cursor, static_cast<long>(bytes.size()));
        if (!bundle)
            throw std::runtime_error("TLS: arquivo PFX invalido");
        EVP_PKEY *raw_key = nullptr;
        X509 *raw_cert = nullptr;
        const auto parsed = a.PKCS12_parse(bundle, password.c_str(), &raw_key, &raw_cert, nullptr);
        SecureZeroMemory(password.data(), password.size());
        a.PKCS12_free(bundle);
        std::unique_ptr<EVP_PKEY, decltype(a.EVP_PKEY_free)> key(raw_key, a.EVP_PKEY_free);
        std::unique_ptr<X509, decltype(a.X509_free)> cert(raw_cert, a.X509_free);
        if (!parsed || !key || !cert)
            throw std::runtime_error("TLS: PFX/senha invalido");
        impl_->pin = hash_and_validate(a, cert.get());
        if (a.SSL_CTX_use_certificate(ctx, cert.get()) != 1 || a.SSL_CTX_use_PrivateKey(ctx, key.get()) != 1 ||
            a.SSL_CTX_check_private_key(ctx) != 1)
            throw std::runtime_error("TLS: configuracao OpenSSL falhou");
    }
    OpenSslContext::~OpenSslContext() = default;
    const std::string &OpenSslContext::fingerprint() const { return impl_->pin; }

    struct OpenSslSession::Impl
    {
        std::shared_ptr<OpenSslContext> server;
        SSL *session = nullptr;
        BIO *input = nullptr, *output = nullptr;
        bool ready = false;
        std::string wire_output;
        std::string expected_pin;
        Impl(std::shared_ptr<OpenSslContext> owner, std::string pin) : server(std::move(owner)), expected_pin(std::move(pin)) {}
        ~Impl()
        {
            if (session)
                server->impl_->api.SSL_free(session);
        }
        Api &api() { return server->impl_->api; }
        void drain()
        {
            std::array<char, 4096> bytes{};
            while (api().BIO_ctrl_pending(output))
            {
                const int n = api().BIO_read(output, bytes.data(), static_cast<int>(bytes.size()));
                if (n <= 0)
                    throw std::runtime_error("TLS: falha de buffer cifrado");
                if (wire_output.size() + static_cast<std::size_t>(n) > CipherLimit)
                    throw std::runtime_error("TLS: limite de saida excedido");
                wire_output.append(bytes.data(), static_cast<std::size_t>(n));
            }
        }
        bool process(std::string &plaintext)
        {
            if (!ready)
            {
                api().ERR_clear_error();
                const int result = api().SSL_do_handshake(session);
                const int error = result == 1 ? 0 : api().SSL_get_error(session, result);
                drain();
                if (result == 1)
                {
                    if (!server->impl_->server)
                    {
                        auto *raw = api().SSL_get1_peer_certificate(session);
                        if (!raw)
                            throw std::runtime_error("TLS: host sem certificado");
                        std::unique_ptr<X509, decltype(api().X509_free)> cert(raw, api().X509_free);
                        if (hash_and_validate(api(), cert.get()) != expected_pin)
                            throw std::runtime_error("TLS: impressao digital incorreta; senha nao enviada");
                    }
                    ready = true;
                }
                else if (error == WantRead || error == WantWrite)
                    return true;
                else
                    throw std::runtime_error("TLS: handshake do host falhou");
            }
            std::array<char, 4096> data{};
            for (;;)
            {
                api().ERR_clear_error();
                const int n = api().SSL_read(session, data.data(), static_cast<int>(data.size()));
                const int error = n > 0 ? 0 : api().SSL_get_error(session, n);
                drain();
                if (n > 0)
                    plaintext.append(data.data(), static_cast<std::size_t>(n));
                else if (error == WantRead || error == WantWrite)
                    return true;
                else if (error == ZeroReturn)
                    return false;
                else
                    throw std::runtime_error("TLS: registro recebido invalido");
            }
        }
    };

    OpenSslSession::OpenSslSession(std::shared_ptr<OpenSslContext> server, std::string pin)
        : impl_(std::make_unique<Impl>(std::move(server), std::move(pin)))
    {
        auto &p = *impl_;
        auto &a = p.api();
        p.session = a.SSL_new(p.server->impl_->context);
        if (!p.session)
            throw std::runtime_error("TLS: sessao OpenSSL falhou");
        p.input = a.BIO_new(a.BIO_s_mem());
        p.output = a.BIO_new(a.BIO_s_mem());
        if (!p.input || !p.output)
        {
            if (p.input)
                a.BIO_free(p.input);
            if (p.output)
                a.BIO_free(p.output);
            throw std::runtime_error("TLS: buffers OpenSSL falharam");
        }
        a.SSL_set_bio(p.session, p.input, p.output); // SSL owns both BIOs from now on.
        if (p.server->impl_->server)
            a.SSL_set_accept_state(p.session);
        else
        {
            a.SSL_set_connect_state(p.session);
            std::string unused;
            p.process(unused); // Initial ClientHello; application data waits for pin validation.
        }
    }
    OpenSslSession::~OpenSslSession() = default;
    bool OpenSslSession::ready() const { return impl_->ready; }
    bool OpenSslSession::wants_write() const { return !impl_->wire_output.empty(); }
    std::array<unsigned char, 32> OpenSslSession::export_udp_key(const std::array<unsigned char, 32> &context)
    {
        if (!ready()) throw std::runtime_error("UDP: TLS ainda nao autenticado");
        if (impl_->api().SSL_ctrl(impl_->session, 122, 0, nullptr) != 1) // Extended Master Secret
            throw std::runtime_error("UDP: TLS exige Extended Master Secret para o exporter");
        constexpr char label[] = "EXPORTER-dsr-network-movement-v1";
        std::array<unsigned char, 32> key{};
        if (impl_->api().SSL_export_keying_material(impl_->session, key.data(), key.size(),
            label, sizeof(label) - 1, context.data(), context.size(), 1) != 1)
        {
            SecureZeroMemory(key.data(), key.size());
            throw std::runtime_error("UDP: derivacao da chave falhou");
        }
        return key;
    }
    bool OpenSslSession::receive(SOCKET socket, std::string &plaintext)
    {
        auto &p = *impl_;
        std::array<char, 16384> data{};
        const int n = recv(socket, data.data(), static_cast<int>(data.size()), 0);
        if (n == 0)
            return false;
        if (n == SOCKET_ERROR)
            return WSAGetLastError() == WSAEWOULDBLOCK;
        if (p.api().BIO_ctrl_pending(p.input) + static_cast<std::size_t>(n) > CipherLimit)
            throw std::runtime_error("TLS: limite de entrada excedido");
        if (p.api().BIO_write(p.input, data.data(), n) != n)
            throw std::runtime_error("TLS: buffer de entrada falhou");
        return p.process(plaintext);
    }
    bool OpenSslSession::send(SOCKET socket, std::string &plaintext)
    {
        auto &p = *impl_;
        if (p.wire_output.empty() && p.ready && !plaintext.empty())
        {
            p.api().ERR_clear_error();
            // Parentheses also prevent expansion of the Windows min macro.
            const auto size = (std::min)(plaintext.size(), std::size_t{16384});
            const int n = p.api().SSL_write(p.session, plaintext.data(), static_cast<int>(size));
            if (n <= 0)
                throw std::runtime_error("TLS: envio cifrado falhou");
            plaintext.erase(0, static_cast<std::size_t>(n));
            p.drain();
        }
        return flush(socket, p.wire_output);
    }
} // namespace dsr
