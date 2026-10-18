#pragma once

#include <string>

namespace dsr
{
struct Options
{
    bool host = false, lan = false, udp = false;
    std::string ip = "127.0.0.1", name = "player";
    std::string tls_pfx, tls_pin, tls_library;
    unsigned port = 27030, capacity = 8;
    bool metrics = false;
    unsigned auto_seconds = 0;
};

Options options(int argc, char **argv);
std::string password();
} // namespace dsr


