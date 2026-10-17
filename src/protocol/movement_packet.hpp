#pragma once

#include "world/player.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace dsr::movement
{
// Application payload only. MUST be carried by an authenticated datagram channel.
inline constexpr std::uint8_t Version = 1;
inline constexpr std::size_t PacketSize = 68;
using Identity = std::array<std::uint8_t, 16>;
using Bytes = std::array<std::uint8_t, PacketSize>;

enum class Kind : std::uint8_t { Input = 1, State = 2 };

struct Packet
{
    Kind kind = Kind::Input;
    Identity session{};
    Identity connection{};
    std::uint64_t sequence = 0;
    std::uint32_t entity = 0;
    Position position;
};

bool valid(const Packet &packet);
Bytes encode(const Packet &packet);
Packet decode(const std::uint8_t *data, std::size_t size);
} // namespace dsr::movement
