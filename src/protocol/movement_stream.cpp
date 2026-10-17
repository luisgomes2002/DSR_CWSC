#include "protocol/movement_stream.hpp"

#include <limits>
#include <stdexcept>

namespace dsr::movement
{
Stream::Stream(Kind kind, Identity session, Identity connection, std::uint32_t entity,
               std::uint64_t baseline)
    : kind_(kind), session_(session), connection_(connection), entity_(entity), latest_(baseline)
{
    if (!valid(Packet{kind, session, connection, 1, entity, {}}) ||
        baseline == (std::numeric_limits<std::uint64_t>::max)())
        throw std::runtime_error("Movimento: stream invalido");
}

bool Stream::eligible(const Packet &p) const
{
    return valid(p) && p.kind == kind_ && p.session == session_ && p.connection == connection_ &&
        p.entity == entity_ && p.sequence > latest_;
}

bool Stream::commit(const Packet &p)
{
    if (!eligible(p)) return false;
    latest_ = p.sequence;
    return true;
}
} // namespace dsr::movement
