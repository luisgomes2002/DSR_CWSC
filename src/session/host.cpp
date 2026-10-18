#include "session/host.hpp"
#include "app/options.hpp"
#include "protocol/protocol.hpp"
#include "protocol/entity_messages.hpp"

#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dsr
{
namespace
{
constexpr int MaxPending = 8;

std::string room_id()
{
    std::array<unsigned char, 16> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("Room ID generation failed");
    std::string result;
    for (auto b : bytes)
    {
        result += "0123456789abcdef"[b >> 4];
        result += "0123456789abcdef"[b & 15];
    }
    return result;
}
} // namespace

void Host::emit(Peer &p, const std::string &line)
{
    const bool accepted = queue(p.output, line);
    if (p.movement) p.movement->metrics.control_queue(p.output.size(), !accepted);
    if (!accepted)
        p.dead = true;
}

void Host::reject(Peer &p, const std::string &reason)
{
    if (p.closing)
        return;
    emit(p, "ERROR\t" + reason);
    p.closing = true;
    p.close_start = Clock::now();
}

void Host::broadcast(const std::string &line, unsigned except)
{
    for (auto &[s, p] : peers)
    {
        (void)s;
        if (p.id && !p.closing && !p.dead && p.id != except)
            emit(p, line);
    }
}

unsigned Host::active() const
{
    return static_cast<unsigned>(std::count_if(peers.begin(), peers.end(), [](const auto &entry)
                                               { return entry.second.id && !entry.second.closing && !entry.second.dead; }));
}

void Host::roster(Peer &p)
{
    emit(p, "ROSTER\t" + std::to_string(active()));
    for (const auto &[s, member] : peers)
    {
        (void)s;
        if (member.id && !member.closing && !member.dead)
            emit(p, "MEMBER\t" + std::to_string(member.id) + "\t" + member.name);
    }
    emit(p, "END");
}

void Host::players(Peer &p)
{
    const auto revision = std::to_string(world.revision());
    emit(p, "PLAYERS\t" + revision + "\t" + std::to_string(world.players().size()));
    for (const auto &[id, player] : world.players())
    {
        (void)id;
        emit(p, player_frame(player));
    }
    emit(p, "PLAYERS_END\t" + revision);
}

void Host::frame(Peer &p, const std::string &line)
{
    const auto fields = split(line);
    const auto now = Clock::now();
    if (now - p.rate_start >= std::chrono::seconds(1))
    {
        p.rate_start = now;
        p.rate_count = 0;
    }
    if (++p.rate_count > 100)
    {
        reject(p, "RATE_LIMIT");
        return;
    }
    if (std::any_of(fields.begin(), fields.end(), [](const auto &f)
                    { return !printable(f); }))
    {
        reject(p, "BAD_FRAME");
        return;
    }
    p.last = now;
    if (!p.id)
    {
        if (fields.empty() || fields[0] != "HELLO")
        {
            reject(p, "EXPECTED_HELLO");
            return;
        }
        if (fields.size() < 2 || fields[1] != ProtocolVersion)
        {
            reject(p, "VERSION");
            return;
        }
        if (fields.size() != 5) { reject(p, "EXPECTED_HELLO"); return; }
        if (fields[4] != BuildId) { reject(p, "BUILD"); return; }
        if (!name_ok(fields[2]))
        {
            reject(p, "NAME");
            return;
        }
        if (fields[3] != secret)
        {
            reject(p, "AUTH");
            return;
        }
        if (active() >= capacity || world.players().size() >= capacity)
        {
            reject(p, "FULL");
            return;
        }
        if (!next_id) { reject(p, "ID_EXHAUSTED"); return; }
        p.id = next_id++;
        p.name = fields[2];
        const auto player = world.spawn(p.id, p.name);
        emit(p, std::string("WELCOME\t") + ProtocolVersion + "\t" + room + "\t" + std::to_string(p.id) + "\t" + std::to_string(capacity));
        roster(p);
        players(p);
        broadcast("JOIN\t" + std::to_string(p.id) + "\t" + p.name, p.id);
        broadcast(player_frame(player, "SPAWN"), p.id);
    }
    else if (fields.size() == 1 && fields[0] == "WHO")
    {
        roster(p);
    }
    else if (fields.size() == 1 && fields[0] == "PLAYERS")
    {
        players(p);
    }
    else if (fields[0] == "UDP_REQUEST")
    {
        negotiate_movement(p, fields);
    }
    else if (fields.size() == 4 && fields[0] == "MOVE")
    {
        if (p.movement) { emit(p, "ERROR\tUSE_UDP"); return; }
        Position position;
        try { position = parse_position(fields, 1); }
        catch (const std::exception &) { emit(p, "ERROR\tBAD_POSITION"); return; }
        // No sender ID is accepted on the wire: ownership comes from this socket.
        publish_position(world.move(p.id, position));
    }
    else if (fields.size() == 1 && fields[0] == "PING")
    {
        emit(p, "PONG");
    }
    else if (fields.size() == 1 && fields[0] == "QUIT")
    {
        emit(p, "BYE");
        p.closing = true;
        p.close_start = now;
    }
    else if (fields.size() == 2 && fields[0] == "SAY" && !fields[1].empty() && fields[1].size() <= MaxText)
    {
        broadcast("CHAT\t" + std::to_string(p.id) + "\t" + fields[1]);
    }
    else if (fields.size() == 3 && fields[0] == "SEND" && !fields[2].empty() && fields[2].size() <= MaxText)
    {
        unsigned target = 0;
        try
        {
            target = number(fields[1], 1, 0xffffffffu);
        }
        catch (const std::exception &)
        {
            reject(p, "BAD_DESTINATION");
            return;
        }
        for (auto &[s, other] : peers)
        {
            (void)s;
            if (other.id == target && !other.closing && !other.dead)
            {
                emit(other, "DATA\t" + std::to_string(p.id) + "\t" + fields[2]);
                return;
            }
        }
        emit(p, "ERROR\tNO_SUCH_PEER");
    }
    else
    {
        reject(p, "BAD_COMMAND");
    }
}

void Host::receive(Peer &p)
{
    if (p.tls)
    {
        try { if (!p.tls->receive(p.socket, p.input)) { p.dead = true; return; } }
        catch (const std::exception &) { p.dead = true; return; }
        if (!p.tls->ready()) return;
    }
    else
    {
        std::array<char, 4096> buf{};
        const int n = recv(p.socket, buf.data(), static_cast<int>(buf.size()), 0);
        if (n == 0)
        {
            p.dead = true;
            return;
        }
        if (n == SOCKET_ERROR)
        {
            if (WSAGetLastError() != WSAEWOULDBLOCK)
                p.dead = true;
            return;
        }
        p.input.append(buf.data(), static_cast<size_t>(n));
    }
    for (;;)
    {
        const auto end = p.input.find('\n');
        if (end == std::string::npos)
        {
            if (p.input.size() > MaxFrame)
                reject(p, "FRAME_TOO_LARGE");
            return;
        }
        if (end > MaxFrame)
        {
            reject(p, "FRAME_TOO_LARGE");
            return;
        }
        auto line = p.input.substr(0, end);
        p.input.erase(0, end + 1);
        frame(p, line);
        if (p.closing || p.dead)
        {
            p.input.clear();
            return;
        }
    }
}

Host::Host(std::string key, unsigned limit) : secret(std::move(key)), room(room_id()), capacity(limit) {}

Host::~Host()
{
    for (const auto &[s, p] : peers)
    {
        (void)p;
        closesocket(s);
    }
}

void Host::run(const Options &o)
{
    udp_enabled = o.udp;
    metrics_enabled = o.metrics;
    udp_library = o.tls_library;
    const auto tls = o.tls_pfx.empty() ? nullptr : TlsCredentials::server(o.tls_pfx, o.tls_library);
    Socket listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (listener.value == INVALID_SOCKET)
        net_error("socket");
    const BOOL exclusive = TRUE;
    if (setsockopt(listener.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive), sizeof(exclusive)))
        net_error("exclusive bind");
    auto a = address(o.ip, o.port);
    if (bind(listener.value, reinterpret_cast<sockaddr *>(&a), sizeof(a)))
        net_error("bind");
    if (listen(listener.value, SOMAXCONN))
        net_error("listen");
    nonblocking(listener.value);
    int size = sizeof(a);
    if (getsockname(listener.value, reinterpret_cast<sockaddr *>(&a), &size))
        net_error("getsockname");
    std::cout << "SERVER_READY\t" << o.ip << '\t' << ntohs(a.sin_port) << '\t' << room << '\t' << capacity << std::endl;
    if (tls) std::cout << "TLS_PIN\t" << tls->fingerprint() << std::endl;
    for (;;)
    {
        fd_set reads, writes;
        FD_ZERO(&reads);
        FD_ZERO(&writes);
        FD_SET(listener.value, &reads);
        for (const auto &[s, p] : peers)
        {
            if (!p.closing && !p.dead)
                FD_SET(s, &reads);
            const bool writing = p.tls ? (p.tls->wants_write() || (p.tls->ready() && !p.output.empty())) : !p.output.empty();
            if (writing && !p.dead)
                FD_SET(s, &writes);
        }
        timeval timeout{0, udp_enabled ? 10000 : 50000};
        if (select(0, &reads, &writes, nullptr, &timeout) == SOCKET_ERROR)
            net_error("select");
        // Only service descriptors present before select; accept after processing.
        for (auto &[s, p] : peers)
        {
            if (FD_ISSET(s, &reads))
                receive(p);
            if (!p.dead && FD_ISSET(s, &writes))
            {
                try { if (!(p.tls ? p.tls->send(s, p.output) : flush(s, p.output))) p.dead = true; }
                catch (const std::exception &) { p.dead = true; }
            }
            if (p.movement) p.movement->metrics.control_queue(p.output.size());
            service_movement(p);
            const auto now = Clock::now();
            if (!p.id && !p.closing && now - p.accepted > std::chrono::seconds(5))
                reject(p, "HANDSHAKE_TIMEOUT");
            if (p.id && !p.closing && now - p.last > std::chrono::seconds(30))
                reject(p, "IDLE_TIMEOUT");
            if (p.closing && ((p.output.empty() && (!p.tls || !p.tls->wants_write())) || now - p.close_start > std::chrono::seconds(2)))
                p.dead = true;
        }
        std::vector<unsigned> departed;
        for (auto it = peers.begin(); it != peers.end();)
        {
            if (!it->second.dead)
            {
                ++it;
                continue;
            }
            if (it->second.id)
                departed.push_back(it->second.id);
            closesocket(it->first);
            it = peers.erase(it);
        }
        for (auto id : departed)
        {
            const auto revision = world.remove(id);
            broadcast("LEFT\t" + std::to_string(id));
            if (revision) broadcast("DESPAWN\t" + std::to_string(id) + "\t" + std::to_string(*revision));
        }
        if (FD_ISSET(listener.value, &reads))
        {
            Socket incoming(accept(listener.value, nullptr, nullptr));
            if (incoming.value == INVALID_SOCKET)
            {
                if (WSAGetLastError() != WSAEWOULDBLOCK)
                    net_error("accept");
                continue;
            }
            if (peers.size() >= static_cast<size_t>(capacity + MaxPending))
                continue;
            nonblocking(incoming.value);
            const SOCKET s = incoming.value;
            Peer p{};
            p.socket = s;
            if (tls) p.tls = std::make_unique<TlsChannel>(tls);
            peers.emplace(s, std::move(p));
            incoming.value = INVALID_SOCKET;
        }
    }
}
} // namespace dsr


