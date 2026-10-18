#include "protocol/movement_packet.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace dsr::movement
{
namespace
{
bool nonzero(const Identity &identity)
{
    return std::any_of(identity.begin(), identity.end(), [](auto b) { return b != 0; });
}

void put(Bytes &bytes, std::size_t offset, std::uint64_t value, std::size_t width)
{
    for (std::size_t i = 0; i < width; ++i)
    {
        bytes[offset + width - 1 - i] = static_cast<std::uint8_t>(value & 255);
        value >>= 8;
    }
}

std::uint64_t get(const std::uint8_t *data, std::size_t offset, std::size_t width)
{
    std::uint64_t result = 0;
    for (std::size_t i = 0; i < width; ++i) result = (result << 8) | data[offset + i];
    return result;
}

std::uint32_t quantize(double value)
{
    const auto signed_value = static_cast<std::int64_t>(std::round(value * 1000.0));
    // Signed-to-unsigned conversion defines the wire's two's-complement bits.
    return static_cast<std::uint32_t>(signed_value);
}

double unquantize(const std::uint8_t *data, std::size_t offset)
{
    const auto raw = get(data, offset, 4);
    const auto signed_value = raw < 0x80000000ULL ? static_cast<std::int64_t>(raw)
        : static_cast<std::int64_t>(raw) - 0x100000000LL;
    return static_cast<double>(signed_value) / 1000.0;
}
} // namespace

bool valid(const Packet &p)
{
    return (p.kind == Kind::Input || p.kind == Kind::State) && nonzero(p.session) &&
        nonzero(p.connection) && p.entity != 0 && p.sequence != 0 &&
        p.sequence != (std::numeric_limits<std::uint64_t>::max)() && valid_position(p.position);
}

Bytes encode(const Packet &p)
{
    if (!valid(p)) throw std::runtime_error("Movement: invalid fields");
    Bytes bytes{};
    bytes[0] = 'D'; bytes[1] = 'S'; bytes[2] = 'R'; bytes[3] = 'M';
    bytes[4] = Version;
    bytes[5] = static_cast<std::uint8_t>(p.kind);
    put(bytes, 8, PacketSize, 2);
    std::copy(p.session.begin(), p.session.end(), bytes.begin() + 12);
    std::copy(p.connection.begin(), p.connection.end(), bytes.begin() + 28);
    put(bytes, 44, p.sequence, 8);
    put(bytes, 52, p.entity, 4);
    put(bytes, 56, quantize(p.position.x), 4);
    put(bytes, 60, quantize(p.position.y), 4);
    put(bytes, 64, quantize(p.position.z), 4);
    return bytes;
}

Packet decode(const std::uint8_t *data, std::size_t size)
{
    if (!data || size != PacketSize) throw std::runtime_error("Movement: invalid size");
    if (data[0] != 'D' || data[1] != 'S' || data[2] != 'R' || data[3] != 'M' ||
        data[4] != Version || get(data, 6, 2) != 0 || get(data, 8, 2) != PacketSize ||
        get(data, 10, 2) != 0)
        throw std::runtime_error("Movement: incompatible header");
    Packet p;
    p.kind = static_cast<Kind>(data[5]);
    std::copy_n(data + 12, 16, p.session.begin());
    std::copy_n(data + 28, 16, p.connection.begin());
    p.sequence = get(data, 44, 8);
    p.entity = static_cast<std::uint32_t>(get(data, 52, 4));
    p.position = {unquantize(data, 56), unquantize(data, 60), unquantize(data, 64)};
    if (!valid(p)) throw std::runtime_error("Movement: invalid fields");
    return p;
}
} // namespace dsr::movement
