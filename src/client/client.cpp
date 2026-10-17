#include "client/client.hpp"
#include "app/options.hpp"
#include "net/socket.hpp"
#include "net/tls.hpp"
#include "protocol/protocol.hpp"
#include "ui/console_input.hpp"
#include "ui/client_commands.hpp"
#include "client/player_replica.hpp"
#include "client/client_movement.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace dsr
{
using Clock = std::chrono::steady_clock;

int join(const Options &o, const std::string &secret)
{
    Socket sock(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (sock.value == INVALID_SOCKET)
        net_error("socket");
    nonblocking(sock.value);
    auto a = address(o.ip, o.port);
    if (connect(sock.value, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
            net_error("connect");
        fd_set w, e;
        FD_ZERO(&w);
        FD_ZERO(&e);
        FD_SET(sock.value, &w);
        FD_SET(sock.value, &e);
        timeval wait{5, 0};
        const int n = select(0, nullptr, &w, &e, &wait);
        if (n == SOCKET_ERROR)
            net_error("connect select");
        if (!n)
            throw std::runtime_error("Timeout ao conectar");
        int error = 0, size = sizeof(error);
        if (getsockopt(sock.value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &size))
            net_error("connect status");
        if (error || FD_ISSET(sock.value, &e))
            throw std::runtime_error("Conexao recusada/falhou: " + std::to_string(error));
    }
    std::string out, in;
    queue(out, std::string("HELLO\t") + ProtocolVersion + "\t" + o.name + "\t" + secret + "\t" + BuildId);
    std::unique_ptr<TlsChannel> tls;
    if (!o.tls_pin.empty()) tls = std::make_unique<TlsChannel>(TlsCredentials::client(o.tls_library), o.tls_pin);
    ConsoleInput input;
    PlayerReplica replica;
    std::unique_ptr<ClientMovement> movement;
    bool welcomed = false, quitting = false, failed = false;
    const auto connected = Clock::now();
    auto last_received = connected, last_ping = connected, quit_start = connected;
    std::cout << "Comandos: texto, /who, /players, /move X Y Z, /send ID texto, /quit. ASCII neste prototipo." << std::endl;
    for (;;)
    {
        fd_set r, w;
        FD_ZERO(&r);
        FD_ZERO(&w);
        FD_SET(sock.value, &r);
        const bool writing = tls ? (tls->wants_write() || (tls->ready() && !out.empty())) : !out.empty();
        if (writing)
            FD_SET(sock.value, &w);
        timeval wait{0, o.udp ? 10000 : 50000};
        if (select(0, &r, &w, nullptr, &wait) == SOCKET_ERROR)
            net_error("client select");
        if (FD_ISSET(sock.value, &w) && !(tls ? tls->send(sock.value, out) : flush(sock.value, out)))
            throw std::runtime_error("Falha no envio");
        if (FD_ISSET(sock.value, &r))
        {
            if (tls)
            {
                if (!tls->receive(sock.value, in))
                {
                    std::cout << "DISCONNECTED" << std::endl;
                    return welcomed && !failed ? 0 : 1;
                }
                last_received = Clock::now();
            }
            else
            {
                std::array<char, 4096> buf{};
                const int n = recv(sock.value, buf.data(), static_cast<int>(buf.size()), 0);
                if (n == 0)
                {
                    std::cout << "DISCONNECTED" << std::endl;
                    return welcomed && !failed ? 0 : 1;
                }
                if (n == SOCKET_ERROR)
                {
                    if (WSAGetLastError() != WSAEWOULDBLOCK)
                        net_error("client recv");
                }
                else
                {
                    in.append(buf.data(), static_cast<size_t>(n));
                    last_received = Clock::now();
                }
            }
            for (;;)
            {
                auto end = in.find('\n');
                if (end == std::string::npos)
                {
                    if (in.size() > MaxFrame)
                        throw std::runtime_error("Frame remoto excede limite");
                    break;
                }
                if (end > MaxFrame)
                    throw std::runtime_error("Frame remoto excede limite");
                auto line = in.substr(0, end);
                in.erase(0, end + 1);
                const auto fields = split(line);
                if (std::any_of(fields.begin(), fields.end(), [](const auto &f)
                                { return !printable(f); }))
                    throw std::runtime_error("Frame remoto invalido");
                if (fields[0] == "WELCOME")
                {
                    if (welcomed || fields.size() != 5 || fields[1] != ProtocolVersion)
                        throw std::runtime_error("WELCOME incompativel");
                    welcomed = true;
                    if (o.udp)
                    {
                        movement = std::make_unique<ClientMovement>(sock.value,
                            number(fields[3], 1, 0xffffffffu), fields[2], o.tls_library);
                        if (!queue(out, movement->request())) throw std::runtime_error("UDP: fila cheia");
                    }
                }
                if (fields[0].rfind("UDP_", 0) == 0)
                {
                    if (!movement || !tls) throw std::runtime_error("UDP: controle nao solicitado");
                    movement->control(fields, *tls);
                }
                if (welcomed) replica.consume(fields);
                if (line.rfind("ERROR\t", 0) == 0)
                    failed = true;
                if (line != "PONG")
                    std::cout << line << std::endl;
            }
        }
        const auto now = Clock::now();
        if (movement && !quitting && !failed) movement->poll(replica);
        if (!welcomed && now - connected > std::chrono::seconds(7))
            throw std::runtime_error("Timeout na admissao");
        if (now - last_received > std::chrono::seconds(35))
            throw std::runtime_error("Host sem resposta");
        if (quitting && now - quit_start > std::chrono::seconds(3))
            return failed ? 1 : 0;
        auto send_line = [&](const std::string &line)
        {
            if (!queue(out, line))
                throw std::runtime_error("Fila de envio excede limite");
        };
        auto quit = [&]()
        { send_line("QUIT"); quitting = true; quit_start = now; };
        if (welcomed && !quitting)
        {
            for (const auto &line : input.poll())
            {
                if (line.empty())
                    continue;
                ClientCommand command;
                try { command = client_command(line); }
                catch (const std::exception &e)
                {
                    std::cerr << "Comando invalido: " << e.what() << std::endl;
                    continue;
                }
                if (movement && command.frame.rfind("MOVE\t", 0) == 0)
                    movement->propose(command.frame);
                else send_line(command.frame);
                if (command.quit)
                {
                    quitting = true;
                    quit_start = now;
                    break;
                }
            }
            if (input.eof() && !quitting)
                quit();
            if (!quitting && now - last_ping > std::chrono::seconds(5))
            {
                send_line("PING");
                last_ping = now;
            }
        }
    }
}
} // namespace dsr


