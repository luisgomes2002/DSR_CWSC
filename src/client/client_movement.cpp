#include "client/client_movement.hpp"
#include "protocol/entity_messages.hpp"
#include "protocol/protocol.hpp"

#include <iostream>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace dsr
{
ClientMovement::ClientMovement(SOCKET tcp, unsigned owner, const std::string &room, std::string library)
    : link_(tcp, owner), owner_(owner), session_(parse_identity(room)), library_(std::move(library)) {}

void ClientMovement::automatic(double seconds)
{
    const double phase = seconds * 0.8 + static_cast<double>(owner_);
    link_.propose({10 * std::cos(phase), 2 * std::sin(phase * 0.5), 10 * std::sin(phase)});
}

std::string ClientMovement::request() const
{ return "UDP_REQUEST\t1\t" + std::to_string(link_.port()); }

bool ClientMovement::ready() const { return host_ready_ && link_.ready(); }

void ClientMovement::control(const std::vector<std::string> &f, TlsChannel &tls)
{
    if (f[0] == "UDP_OFFER")
    {
        if (f.size() != 5 || f[1] != "1" || parse_identity(f[2]) != session_ || link_.established())
            throw std::runtime_error("UDP: oferta incompativel/duplicada");
        link_.establish(tls, false, session_, parse_identity(f[3]), number(f[4], 1024, 65535), library_);
    }
    else if (f[0] == "UDP_READY")
    {
        if (f.size() != 2 || f[1] != "1" || !link_.established() || host_ready_)
            throw std::runtime_error("UDP: confirmacao invalida");
        host_ready_ = true;
    }
    else throw std::runtime_error("UDP: controle desconhecido");
}

void ClientMovement::propose(const std::string &move_frame)
{ link_.propose(parse_position(split(move_frame), 1)); }

void ClientMovement::poll(PlayerReplica &replica)
{
    const auto now = std::chrono::steady_clock::now();
    if (!announced_ && now - started_ > std::chrono::seconds(10))
        throw std::runtime_error("UDP: timeout de negociacao; sem fallback");
    for (const auto &p : link_.poll())
    {
        if (p.kind != movement::Kind::State) continue;
        last_state_ = now;
        if (replica.apply_movement(p.entity, p.position, p.sequence))
            std::cout << "UDP_POSITION\t" << p.entity << '\t' << position_fields(p.position) << '\t' << p.sequence << std::endl;
    }
    if (ready() && !announced_)
    { std::cout << "UDP_ACTIVE\t1" << std::endl; announced_ = true; last_state_ = now; }
    if (announced_ && now - last_state_ > std::chrono::seconds(10))
        throw std::runtime_error("UDP: host sent no states for 10 seconds; no fallback is available");
    if (ready()) link_.publish_input();
}
} // namespace dsr
