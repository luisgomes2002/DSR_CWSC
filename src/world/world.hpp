#pragma once

#include "world/player.hpp"

#include <map>
#include <optional>

namespace dsr
{
// Host-owned test world. No sockets, console, Steam or game dependencies.
class World
{
public:
    PlayerState spawn(unsigned id, const std::string &name);
    PlayerState move(unsigned id, const Position &position);
    std::optional<std::uint64_t> remove(unsigned id);
    const std::map<unsigned, PlayerState> &players() const { return players_; }
    std::uint64_t revision() const { return revision_; }

private:
    std::map<unsigned, PlayerState> players_;
    std::uint64_t revision_ = 0;
};
} // namespace dsr
