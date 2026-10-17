#pragma once

#include "world/player.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace dsr
{
// Strict, locale-independent numeric parsing and typed entity messages.
double coordinate(std::string_view text);
std::uint64_t revision_number(std::string_view text);
Position parse_position(const std::vector<std::string> &fields, std::size_t offset);
PlayerState parse_player(const std::vector<std::string> &fields);
std::string position_fields(const Position &position);
std::string player_frame(const PlayerState &player, std::string_view verb = "PLAYER");
std::string position_frame(const PlayerState &player);
} // namespace dsr
