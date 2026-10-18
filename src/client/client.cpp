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
#include <optional>

namespace dsr
{
using Clock = std::chrono::steady_clock;

int join(const Options &options, const std::string &room_password)
{
    Socket sock(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (sock.value == INVALID_SOCKET)
        net_error("socket");
    nonblocking(sock.value);
    auto server_address = address(options.ip, options.port);
    if (connect(sock.value, reinterpret_cast<sockaddr *>(&server_address), sizeof(server_address)) == SOCKET_ERROR)
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
            throw std::runtime_error("Connection timed out");
        int error = 0, size = sizeof(error);
        if (getsockopt(sock.value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &size))
            net_error("connect status");
        if (error || FD_ISSET(sock.value, &e))
            throw std::runtime_error("Connection was refused or failed: " + std::to_string(error));
    }
    std::string out, in;
    queue(out, std::string("HELLO\t") + ProtocolVersion + "\t" + options.name + "\t" + room_password + "\t" + BuildId);
    std::unique_ptr<TlsChannel> tls;
    if (!options.tls_pin.empty()) tls = std::make_unique<TlsChannel>(TlsCredentials::client(options.tls_library), options.tls_pin);
    ConsoleInput input;
    PlayerReplica replica;
    std::unique_ptr<ClientMovement> movement;
    bool welcomed = false, quitting = false, failed = false;
    const auto connected = Clock::now();
    auto last_received = connected, last_ping = connected, quit_start = connected;
    std::optional<Clock::time_point> automatic_start, ping_start;
    std::cout << "Commands: text, /who, /players, /move X Y Z, /send ID text, /quit. This prototype supports ASCII only." << std::endl;
    for (;;)
    {
        fd_set r, w;
        FD_ZERO(&r);
        FD_ZERO(&w);
        FD_SET(sock.value, &r);
        const bool writing = tls ? (tls->wants_write() || (tls->ready() && !out.empty())) : !out.empty();
        if (writing)
            FD_SET(sock.value, &w);
        timeval wait{0, options.udp ? 10000 : 50000};
        if (select(0, &r, &w, nullptr, &wait) == SOCKET_ERROR)
            net_error("client select");
        if (FD_ISSET(sock.value, &w) && !(tls ? tls->send(sock.value, out) : flush(sock.value, out)))
            throw std::runtime_error("Send failed");
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
                        throw std::runtime_error("Remote frame exceeds the limit");
                    break;
                }
                if (end > MaxFrame)
                    throw std::runtime_error("Remote frame exceeds the limit");
                auto line = in.substr(0, end);
                in.erase(0, end + 1);
                const auto fields = split(line);
                if (std::any_of(fields.begin(), fields.end(), [](const auto &f)
                                { return !printable(f); }))
                    throw std::runtime_error("Invalid remote frame");
                if (fields[0] == "WELCOME")
                {
                    if (welcomed || fields.size() != 5 || fields[1] != ProtocolVersion)
                        throw std::runtime_error("Incompatible WELCOME message");
                    welcomed = true;
                    if (options.udp)
                    {
                        movement = std::make_unique<ClientMovement>(sock.value,
                            number(fields[3], 1, 0xffffffffu), fields[2], options.tls_library);
                        if (options.metrics) movement->enable_metrics();
                        const bool accepted = queue(out, movement->request());
                        movement->control_queue(out.size(), !accepted);
                        if (!accepted) throw std::runtime_error("UDP: queue is full");
                    }
                }
                if (fields[0].rfind("UDP_", 0) == 0)
                {
                    if (!movement || !tls) throw std::runtime_error("UDP: unsolicited control message");
                    movement->control(fields, *tls);
                }
                if (welcomed) replica.consume(fields);
                if (movement && fields[0] == "DESPAWN" && fields.size() == 3)
                    movement->forget(number(fields[1], 1, 0xffffffffu));
                if (line == "PONG" && ping_start)
                {
                    if (movement) movement->record_rtt(std::chrono::duration<double, std::milli>(Clock::now() - *ping_start).count());
                    ping_start.reset();
                }
                if (line.rfind("ERROR\t", 0) == 0)
                    failed = true;
                if (line != "PONG")
                    std::cout << line << std::endl;
            }
        }
        const auto now = Clock::now();
        if (movement) movement->control_queue(out.size());
        if (movement && !quitting && !failed) movement->poll(replica);
        if (!welcomed && now - connected > std::chrono::seconds(7))
            throw std::runtime_error("Admission timed out");
        if (now - last_received > std::chrono::seconds(35))
            throw std::runtime_error("Host is not responding");
        if (quitting && now - quit_start > std::chrono::seconds(3))
            return failed ? 1 : 0;
        auto send_line = [&](const std::string &line)
        {
            const bool accepted = queue(out, line);
            if (movement) movement->control_queue(out.size(), !accepted);
            if (!accepted)
                throw std::runtime_error("Send queue exceeds the limit");
        };
        auto quit = [&]()
        { send_line("QUIT"); quitting = true; quit_start = now; };
        if (welcomed && !quitting)
        {
            if (options.auto_seconds && movement && movement->ready())
            {
                if (!automatic_start)
                {
                    automatic_start = now;
                    std::cout << "AUTO_START\t" << options.auto_seconds << std::endl;
                }
                const double seconds = std::chrono::duration<double>(now - *automatic_start).count();
                if (seconds >= options.auto_seconds) quit();
                else movement->automatic(seconds);
            }
            if (quitting) continue;
            for (const auto &line : input.poll())
            {
                if (line.empty())
                    continue;
                ClientCommand command;
                try { command = client_command(line); }
                catch (const std::exception &e)
                {
                    std::cerr << "Invalid command: " << e.what() << std::endl;
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
            if (input.eof() && !quitting && !options.auto_seconds)
                quit();
            if (!quitting && !ping_start && now - last_ping > std::chrono::seconds(options.metrics ? 1 : 5))
            {
                send_line("PING");
                last_ping = now;
                ping_start = now;
            }
        }
    }
}
} // namespace dsr


