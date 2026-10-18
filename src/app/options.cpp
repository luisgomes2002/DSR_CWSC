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
        throw std::runtime_error("Set DSR_ROOM_PASSWORD to 8-128 printable ASCII characters");
    return result;
}

Options options(int argc, char **argv)
{
    if (argc < 2)
        throw std::runtime_error("Usage: dsr_net host|join [--address IPv4] [--port N] [--name NAME] [--max-players N] [--tls-pfx FILE | --tls-pin SHA256] [--tls-library DIRECTORY] [--lan] [--udp] [--metrics] [--auto-seconds 1-3600]");
    Options parsed_options;
    const std::string mode = argv[1];
    if (mode != "host" && mode != "join")
        throw std::runtime_error("Mode must be host or join");
    parsed_options.host = mode == "host";
    for (int i = 2; i < argc; ++i)
    {
        const std::string option_name = argv[i];
        if (option_name == "--metrics") { parsed_options.metrics = true; continue; }
        if (option_name == "--auto-seconds")
        {
            if (parsed_options.host || ++i == argc) throw std::runtime_error("--auto-seconds requires join mode and duration 1-3600");
            parsed_options.auto_seconds = number(argv[i], 1, 3600);
            continue;
        }
        if (option_name == "--udp") { parsed_options.udp = true; continue; }
        if (option_name == "--lan")
        {
            parsed_options.lan = true;
            continue;
        }
        if (option_name != "--address" && option_name != "--port" && option_name != "--name" && option_name != "--max-players" && option_name != "--tls-pfx" && option_name != "--tls-pin" && option_name != "--tls-library")
            throw std::runtime_error("Unknown option: " + option_name);
        if (++i == argc)
            throw std::runtime_error("Missing value for " + option_name);
        const std::string option_value = argv[i];
        if (option_name == "--tls-library")
        {
            if (option_value.empty()) throw std::runtime_error("--tls-library requires a trusted directory");
            parsed_options.tls_library = option_value;
        }
        if (option_name == "--tls-pfx")
        {
            if (!parsed_options.host || option_value.empty()) throw std::runtime_error("--tls-pfx requires host mode and a file");
            parsed_options.tls_pfx = option_value;
        }
        if (option_name == "--tls-pin")
        {
            if (parsed_options.host || !valid_fingerprint(option_value)) throw std::runtime_error("--tls-pin requires join mode and a 64-digit SHA-256 hexadecimal value");
            parsed_options.tls_pin = option_value;
        }
        if (option_name == "--address")
            parsed_options.ip = option_value;
        if (option_name == "--port")
            parsed_options.port = number(option_value, parsed_options.host ? 0u : 1u, 65535);
        if (option_name == "--name")
        {
            if (parsed_options.host)
                throw std::runtime_error("--name is only valid in join mode");
            parsed_options.name = option_value;
        }
        if (option_name == "--max-players")
        {
            if (!parsed_options.host)
                throw std::runtime_error("--max-players is only valid in host mode");
            parsed_options.capacity = number(option_value, 2, 32);
        }
    }
    if (!name_ok(parsed_options.name))
        throw std::runtime_error("Name must contain 1-24 ASCII letters, digits, _ or -");
    if ((parsed_options.auto_seconds || parsed_options.metrics) && !parsed_options.udp)
        throw std::runtime_error("--auto-seconds and --metrics require --udp");
    if (parsed_options.udp && (parsed_options.host ? parsed_options.tls_pfx.empty() : parsed_options.tls_pin.empty()))
        throw std::runtime_error("--udp requires TCP/TLS (--tls-pfx for host, --tls-pin for client)");
    if (!parsed_options.tls_library.empty() && parsed_options.tls_pfx.empty() && parsed_options.tls_pin.empty()) throw std::runtime_error("--tls-library requires TLS");
    const auto endpoint = address(parsed_options.ip, parsed_options.port);
    const bool is_loopback = (ntohl(endpoint.sin_addr.s_addr) >> 24) == 127;
    if (!is_loopback && (!parsed_options.lan || (parsed_options.host ? parsed_options.tls_pfx.empty() : parsed_options.tls_pin.empty())))
        throw std::runtime_error("Non-loopback connections require --lan and TLS (--tls-pfx for host or --tls-pin for client)");
    return parsed_options;
}
} // namespace dsr


