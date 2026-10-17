#include "protocol/entity_messages.hpp"
#include "protocol/protocol.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>

namespace dsr
{
namespace
{
std::string decimal(double value)
{
    if (value == 0) return "0";
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) throw std::runtime_error("Falha ao serializar coordenada");
    return {buffer, result.ptr};
}
} // namespace

double coordinate(std::string_view text)
{
    if (text.empty() || text.size() > 32) throw std::runtime_error("Coordenada invalida");
    double value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        !std::isfinite(value) || std::abs(value) > CoordinateLimit)
        throw std::runtime_error("Coordenada deve ser finita e estar entre -100000 e 100000");
    return value;
}

std::uint64_t revision_number(std::string_view text)
{
    if (text.empty() || text.size() > 20) throw std::runtime_error("Revisao invalida");
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::runtime_error("Revisao invalida");
    return value;
}

Position parse_position(const std::vector<std::string> &fields, std::size_t offset)
{
    if (offset > fields.size() || fields.size() - offset < 3)
        throw std::runtime_error("Faltam coordenadas");
    return {coordinate(fields[offset]), coordinate(fields[offset + 1]), coordinate(fields[offset + 2])};
}

PlayerState parse_player(const std::vector<std::string> &fields)
{
    if (fields.size() != 7 || !name_ok(fields[2])) throw std::runtime_error("Jogador remoto invalido");
    PlayerState p{number(fields[1], 1, 0xffffffffu), fields[2], parse_position(fields, 3), revision_number(fields[6])};
    if (!p.revision) throw std::runtime_error("Revisao de jogador invalida");
    return p;
}

std::string position_fields(const Position &p)
{
    if (!valid_position(p)) throw std::runtime_error("Posicao invalida");
    return decimal(p.x) + "\t" + decimal(p.y) + "\t" + decimal(p.z);
}

std::string player_frame(const PlayerState &p, std::string_view verb)
{
    return std::string(verb) + "\t" + std::to_string(p.id) + "\t" + p.name + "\t" +
           position_fields(p.position) + "\t" + std::to_string(p.revision);
}

std::string position_frame(const PlayerState &p)
{
    return "POSITION\t" + std::to_string(p.id) + "\t" + position_fields(p.position) + "\t" + std::to_string(p.revision);
}
} // namespace dsr
