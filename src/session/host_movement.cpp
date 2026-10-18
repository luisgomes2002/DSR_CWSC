#include "session/host.hpp"
#include "protocol/entity_messages.hpp"
#include "protocol/protocol.hpp"

#include <algorithm>

namespace dsr
{
void Host::negotiate_movement(Peer &p, const std::vector<std::string> &fields)
{
    if (!udp_enabled || !p.tls || !p.tls->ready()) { reject(p, "UDP_UNAVAILABLE"); return; }
    if (fields.size() != 3 || fields[1] != "1" || p.movement) { reject(p, "UDP_NEGOTIATION"); return; }
    try
    {
        const auto port = number(fields[2], 1024, 65535);
        auto link = std::make_unique<MovementLink>(p.socket, p.id);
        if (metrics_enabled) link->metrics.enable(true, p.id);
        const auto connection = new_identity();
        link->establish(*p.tls, true, parse_identity(room), connection, port, udp_library);
        emit(p, "UDP_OFFER\t1\t" + room + "\t" + identity_hex(connection) + "\t" + std::to_string(link->port()));
        p.movement = std::move(link);
    }
    catch (const std::exception &) { reject(p, "UDP_NEGOTIATION"); }
}

void Host::publish_position(const PlayerState &player)
{
    // UDP opt-in clients get positions only as authenticated datagrams; legacy
    // clients retain the existing reliable POSITION stream, never both.
    for (auto &[s, other] : peers)
    {
        (void)s;
        if (other.id && !other.closing && !other.dead && !other.movement)
            emit(other, position_frame(player));
    }
}

void Host::service_movement(Peer &p)
{
    if (!p.movement || p.closing || p.dead) return;
    auto &link = *p.movement;
    try
    {
        for (const auto &packet : link.poll())
        {
            if (!link.accepts_input(packet)) continue;
            // Input sequence is not the world revision. Never accept ownership
            // from the payload, create entities, or resurrect a departed player.
            const auto current = world.players().find(p.id);
            if (current == world.players().end()) continue;
            const auto &before = current->second.position;
            if (before.x != packet.position.x || before.y != packet.position.y || before.z != packet.position.z)
                publish_position(world.move(p.id, packet.position));
            link.commit_input(packet);
        }
        if (link.ready() && !link.ready_announced)
        {
            emit(p, "UDP_READY\t1"); link.ready_announced = true;
        }
        if (link.state_due(active()) && !world.players().empty())
        {
            auto it = world.players().upper_bound(link.next_entity);
            if (it == world.players().end()) it = world.players().begin();
            const auto &player = it->second;
            if (link.send(movement::Kind::State, player.id, player.revision, player.position))
                link.next_entity = player.id;
        }
    }
    catch (const std::exception &) { reject(p, "UDP_CHANNEL"); }
}
} // namespace dsr
