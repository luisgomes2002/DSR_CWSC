#include "client/player_replica.hpp"
#include "protocol/entity_messages.hpp"
#include "protocol/protocol.hpp"

#include <stdexcept>
#include <limits>

namespace dsr
{
bool PlayerReplica::consume(const std::vector<std::string> &f)
{
    if (f.empty()) return false;
    const auto &verb = f[0];
    if (verb == "PLAYERS")
    {
        if (snapshot_ || f.size() != 3) throw std::runtime_error("Inicio de snapshot invalido");
        const auto rev = revision_number(f[1]);
        if (rev < revision_) throw std::runtime_error("Snapshot antigo");
        expected_ = number(f[2], 0, MaxPlayers);
        snapshot_revision_ = rev;
        pending_.clear();
        snapshot_ = true;
    }
    else if (verb == "PLAYER")
    {
        if (!snapshot_) throw std::runtime_error("PLAYER fora de snapshot");
        const auto p = parse_player(f);
        if (p.revision > snapshot_revision_ || pending_.size() >= expected_ || !pending_.emplace(p.id, p).second)
            throw std::runtime_error("Conteudo de snapshot invalido");
    }
    else if (verb == "PLAYERS_END")
    {
        if (!snapshot_ || f.size() != 2 || revision_number(f[1]) != snapshot_revision_ || pending_.size() != expected_)
            throw std::runtime_error("Fim de snapshot invalido");
        players_.swap(pending_);
        pending_.clear();
        revision_ = snapshot_revision_;
        movement_baseline_ = snapshot_revision_;
        snapshot_ = false;
    }
    else if (verb == "SPAWN")
    {
        if (snapshot_) throw std::runtime_error("Evento durante snapshot");
        const auto p = parse_player(f);
        if (p.revision <= revision_ || players_.size() >= MaxPlayers || !players_.emplace(p.id, p).second)
            throw std::runtime_error("SPAWN invalido");
        revision_ = p.revision;
    }
    else if (verb == "POSITION")
    {
        if (snapshot_ || f.size() != 6) throw std::runtime_error("POSITION invalido");
        const auto id = number(f[1], 1, 0xffffffffu);
        const auto position = parse_position(f, 2);
        const auto rev = revision_number(f[5]);
        auto it = players_.find(id);
        if (it == players_.end() || rev <= revision_) throw std::runtime_error("Atualizacao de jogador invalida");
        it->second.position = position;
        it->second.revision = revision_ = rev;
    }
    else if (verb == "DESPAWN")
    {
        if (snapshot_ || f.size() != 3) throw std::runtime_error("DESPAWN invalido");
        const auto id = number(f[1], 1, 0xffffffffu);
        const auto rev = revision_number(f[2]);
        if (rev <= revision_ || !players_.erase(id)) throw std::runtime_error("Remocao de jogador invalida");
        revision_ = rev;
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
