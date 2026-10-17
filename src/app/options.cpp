#include "app/options.hpp"
#include "net/socket.hpp"
#include "net/tls.hpp"
#include "protocol/protocol.hpp"

#include <cstdlib>
#include <stdexcept>

namespace dsr
{
std::string password()
{
    const char *p = std::getenv("DSR_ROOM_PASSWORD");
    std::string result = p ? p : "";
    if (result.size() < 8 || result.size() > 128 || !printable(result))
        throw std::runtime_error("Defina DSR_ROOM_PASSWORD com 8 a 128 caracteres ASCII imprimiveis");
    return result;
}

Options options(int argc, char **argv)
{
    if (argc < 2)
        throw std::runtime_error("Use: dsr_net host|join [--address IPv4] [--port N] [--name NOME] [--max-players N] [--tls-pfx ARQUIVO | --tls-pin SHA256] [--tls-library PASTA] [--lan] [--udp]");
    Options o;
    const std::string mode = argv[1];
    if (mode != "host" && mode != "join")
        throw std::runtime_error("Modo deve ser host ou join");
    o.host = mode == "host";
    for (int i = 2; i < argc; ++i)
    {
        const std::string k = argv[i];
        if (k == "--udp") { o.udp = true; continue; }
        if (k == "--lan")
        {
            o.lan = true;
            continue;
        }
        if (k != "--address" && k != "--port" && k != "--name" && k != "--max-players" && k != "--tls-pfx" && k != "--tls-pin" && k != "--tls-library")
            throw std::runtime_error("Opcao desconhecida: " + k);
        if (++i == argc)
            throw std::runtime_error("Falta valor para " + k);
        const std::string v = argv[i];
        if (k == "--tls-library")
        {
            if (v.empty()) throw std::runtime_error("--tls-library exige pasta confiavel");
            o.tls_library = v;
        }
        if (k == "--tls-pfx")
        {
            if (!o.host || v.empty()) throw std::runtime_error("--tls-pfx exige host e arquivo");
            o.tls_pfx = v;
        }
        if (k == "--tls-pin")
        {
            if (o.host || !valid_fingerprint(v)) throw std::runtime_error("--tls-pin exige join e SHA-256 com 64 digitos hexadecimais");
            o.tls_pin = v;
        }
        if (k == "--address")
            o.ip = v;
        if (k == "--port")
            o.port = number(v, o.host ? 0u : 1u, 65535);
        if (k == "--name")
        {
            if (o.host)
                throw std::runtime_error("--name somente em join");
            o.name = v;
        }
        if (k == "--max-players")
        {
            if (!o.host)
                throw std::runtime_error("--max-players somente em host");
            o.capacity = number(v, 2, 32);
        }
    }
    if (!name_ok(o.name))
        throw std::runtime_error("Nome: 1 a 24 letras ASCII, numeros, _ ou -");
    if (o.udp && (o.host ? o.tls_pfx.empty() : o.tls_pin.empty()))
        throw std::runtime_error("--udp exige TCP/TLS (--tls-pfx no host, --tls-pin no cliente)");
    if (!o.tls_library.empty() && o.tls_pfx.empty() && o.tls_pin.empty()) throw std::runtime_error("--tls-library exige TLS");
    auto a = address(o.ip, o.port);
    const bool loopback = (ntohl(a.sin_addr.s_addr) >> 24) == 127;
    if (!loopback && (!o.lan || (o.host ? o.tls_pfx.empty() : o.tls_pin.empty())))
        throw std::runtime_error("Fora de loopback exige --lan e TLS (--tls-pfx no host ou --tls-pin no cliente)");
    return o;
}
} // namespace dsr


