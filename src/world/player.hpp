#pragma once

#include <cstdint>
#include <string>

namespace dsr
{
inline constexpr double CoordinateLimit = 100000.0;
inline constexpr unsigned MaxPlayers = 32;

struct Position
{
    double x = 0, y = 0, z = 0;
};

struct PlayerState
{
    unsigned id = 0;
    std::string name;
    Position position;
    std::uint64_t revision = 0;
};

bool valid_position(const Position &position);
} // namespace dsr
