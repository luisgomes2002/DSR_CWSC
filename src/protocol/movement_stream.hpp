#pragma once

#include "protocol/movement_packet.hpp"

namespace dsr::movement
{
// One authorized entity/direction per connection. Not an authentication mechanism.
// Caller must authenticate, authorize, validate and commit the world update FIRST,
// then call commit(). Serialize access on the future network event loop.
class Stream
{
public:
    Stream(Kind kind, Identity session, Identity connection, std::uint32_t entity,
           std::uint64_t baseline = 0);
    bool eligible(const Packet &packet) const;
    bool commit(const Packet &packet);
    std::uint64_t latest() const { return latest_; }

private:
    Kind kind_;
    Identity session_, connection_;
    std::uint32_t entity_;
    std::uint64_t latest_;
};
} // namespace dsr::movement
