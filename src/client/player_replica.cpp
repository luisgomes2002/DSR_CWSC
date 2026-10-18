#include "client/player_replica.hpp"
#include "protocol/entity_messages.hpp"
#include "protocol/protocol.hpp"

#include <stdexcept>
#include <limits>

namespace dsr
{
bool PlayerReplica::consume(const std::vector<std::string> &fields)
{
    if (fields.empty()) return false;
    const auto &verb = fields[0];
    if (verb == "PLAYERS")
    {
        if (snapshot_ || fields.size() != 3) throw std::runtime_error("Invalid snapshot start");
        const auto snapshot_revision = revision_number(fields[1]);
        if (snapshot_revision < revision_) throw std::runtime_error("Stale snapshot");
        expected_ = number(fields[2], 0, MaxPlayers);
        snapshot_revision_ = snapshot_revision;
        pending_.clear();
        snapshot_ = true;
    }
    else if (verb == "PLAYER")
    {
        if (!snapshot_) throw std::runtime_error("PLAYER outside snapshot");
        const auto player = parse_player(fields);
        if (player.revision > snapshot_revision_ || pending_.size() >= expected_ || !pending_.emplace(player.id, player).second)
            throw std::runtime_error("Invalid snapshot content");
    }
    else if (verb == "PLAYERS_END")
    {
        if (!snapshot_ || fields.size() != 2 || revision_number(fields[1]) != snapshot_revision_ || pending_.size() != expected_)
            throw std::runtime_error("Invalid snapshot end");
        players_.swap(pending_);
        pending_.clear();
        revision_ = snapshot_revision_;
        movement_baseline_ = snapshot_revision_;
        snapshot_ = false;
    }
    else if (verb == "SPAWN")
    {
        if (snapshot_) throw std::runtime_error("Event received during snapshot");
        const auto player = parse_player(fields);
        if (player.revision <= revision_ || players_.size() >= MaxPlayers || !players_.emplace(player.id, player).second)
            throw std::runtime_error("Invalid SPAWN");
        revision_ = player.revision;
    }
    else if (verb == "POSITION")
    {
        if (snapshot_ || fields.size() != 6) throw std::runtime_error("Invalid POSITION");
        const auto player_id = number(fields[1], 1, 0xffffffffu);
        const auto position = parse_position(fields, 2);
        const auto position_revision = revision_number(fields[5]);
        auto player = players_.find(player_id);
        if (player == players_.end() || position_revision <= revision_) throw std::runtime_error("Invalid player update");
        player->second.position = position;
        player->second.revision = revision_ = position_revision;
    }
    else if (verb == "DESPAWN")
    {
        if (snapshot_ || fields.size() != 3) throw std::runtime_error("Invalid DESPAWN");
        const auto player_id = number(fields[1], 1, 0xffffffffu);
        const auto removal_revision = revision_number(fields[2]);
        if (removal_revision <= revision_ || !players_.erase(player_id)) throw std::runtime_error("Invalid player removal");
        revision_ = removal_revision;
    }
    else return false;
    return true;
}

bool PlayerReplica::apply_movement(unsigned entity, const Position &position, std::uint64_t revision)
{
    auto it = players_.find(entity);
    if (snapshot_ || it == players_.end() || !valid_position(position) ||
        revision <= movement_baseline_ || revision <= it->second.revision ||
        revision == (std::numeric_limits<std::uint64_t>::max)()) return false;
    it->second.position = position;
    it->second.revision = revision;
    // UDP may overtake reliable SPAWN/DESPAWN. Do NOT advance the TCP event cursor.
    return true;
}
} // namespace dsr
