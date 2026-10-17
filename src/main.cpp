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
        const auto o = options(argc, argv);
        const auto secret = password();
        Winsock network;
        if (o.tls_pfx.empty() && o.tls_pin.empty())
            std::cerr << "AVISO: modo local sem criptografia, restrito a loopback.\n";
        if (o.host)
        {
            Host server(secret, o.capacity);
            server.run(o);
            return 0;
        }
        return join(o, secret);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Erro: " << e.what() << std::endl;
        return 1;
    }
}
