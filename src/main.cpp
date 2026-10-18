#include "app/options.hpp"
#include "client/client.hpp"
#include "net/socket.hpp"
#include "session/host.hpp"

#include <exception>
#include <iostream>

int main(int argc, char **argv)
{
    using namespace dsr;
    try
    {
        const auto parsed_options = options(argc, argv);
        const auto room_password = password();
        Winsock network;
        if (parsed_options.tls_pfx.empty() && parsed_options.tls_pin.empty())
            std::cerr << "WARNING: unencrypted local mode is restricted to loopback.\n";
        if (parsed_options.host)
        {
            Host server(room_password, parsed_options.capacity);
            server.run(parsed_options);
            return 0;
        }
        return join(parsed_options, room_password);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
