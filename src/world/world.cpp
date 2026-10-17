#include "world/world.hpp"
#include "protocol/protocol.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace dsr
{
namespace
{
void ensure_revision(std::uint64_t revision)
{
    if (revision >= (std::numeric_limits<std::uint64_t>::max)() - 1)
        throw std::runtime_error("Revisoes esgotadas; abra nova sessao");
}
} // namespace

bool valid_position(const Position &p)
{
    auto valid = [](double n) { return std::isfinite(n) && std::abs(n) <= CoordinateLimit; };
    return valid(p.x) && valid(p.y) && valid(p.z);
}

PlayerState World::spawn(unsigned id, const std::string &name)
{
    if (!id || !name_ok(name) || players_.count(id) || players_.size() >= MaxPlayers)
        throw std::runtime_error("Criacao de jogador invalida");
    ensure_revision(revision_);
    PlayerState player{id, name, {}, revision_ + 1};
    players_.emplace(id, player);
    ++revision_;
    return player;
}

PlayerState World::move(unsigned id, const Position &position)
{
    auto it = players_.find(id);
    if (it == players_.end() || !valid_position(position))
        throw std::runtime_error("Movimento invalido");
    ensure_revision(revision_);
    it->second.position = position;
    it->second.revision = ++revision_;
    return it->second;
}

std::optional<std::uint64_t> World::remove(unsigned id)
{
    if (!players_.count(id)) return std::nullopt;
    ensure_revision(revision_);
    players_.erase(id);
    return ++revision_;
}
} // namespace dsr
