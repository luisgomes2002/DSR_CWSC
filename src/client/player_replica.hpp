#pragma once

#include "world/player.hpp"

#include <map>
#include <string>
#include <vector>

namespace dsr
{
// Client copy of the host world. Snapshots commit only after a valid end marker.
class PlayerReplica
{
public:
    bool consume(const std::vector<std::string> &fields);
    const std::map<unsigned, PlayerState> &players() const { return players_; }
    std::uint64_t revision() const { return revision_; }
    bool apply_movement(unsigned entity, const Position &position, std::uint64_t revision);

private:
    std::map<unsigned, PlayerState> players_, pending_;
    std::uint64_t revision_ = 0, snapshot_revision_ = 0;
    unsigned expected_ = 0;
    bool snapshot_ = false;
    std::uint64_t movement_baseline_ = 0;
};
} // namespace dsr
