#include "session/movement_link.hpp"

#include <bcrypt.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace dsr
{
movement::Identity parse_identity(const std::string &hex)
{
    if (hex.size() != 32) throw std::runtime_error("UDP: ID deve ter 32 digitos hexadecimais");
    auto nibble = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        throw std::runtime_error("UDP: ID hexadecimal invalido");
    };
    movement::Identity result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<unsigned char>((nibble(hex[i * 2]) << 4) | nibble(hex[i * 2 + 1]));
    if (std::all_of(result.begin(), result.end(), [](auto b) { return b == 0; }))
        throw std::runtime_error("UDP: ID nulo");
    return result;
}

std::string identity_hex(const movement::Identity &identity)
{
    std::string result;
    for (auto b : identity) { result += "0123456789abcdef"[b >> 4]; result += "0123456789abcdef"[b & 15]; }
    return result;
}

movement::Identity new_identity()
{
    movement::Identity result{};
    do
    {
        if (BCryptGenRandom(nullptr, result.data(), static_cast<ULONG>(result.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG))
            throw std::runtime_error("UDP: geracao de ID falhou");
    } while (std::all_of(result.begin(), result.end(), [](auto b) { return b == 0; }));
    return result;
}

MovementLink::MovementLink(SOCKET tcp, unsigned owner) : socket_(tcp), owner_(owner) {}

void MovementLink::establish(TlsChannel &tls, bool server, const movement::Identity &session,
                             const movement::Identity &connection, unsigned port, const std::string &library)
{
    if (channel_) throw std::runtime_error("UDP: renegociacao nao permitida; reconecte");
    session_ = session; connection_ = connection;
    input_ = std::make_unique<movement::Stream>(movement::Kind::Input, session, connection, owner_);
    std::array<unsigned char, 32> context{};
    std::copy(session.begin(), session.end(), context.begin());
    std::copy(connection.begin(), connection.end(), context.begin() + 16);
    auto key = tls.export_udp_key(context);
    try
    {
        socket_.connect_to(port);
        channel_ = std::make_unique<DtlsChannel>(socket_.native(), server, key, library);
    }
    catch (...) { SecureZeroMemory(key.data(), key.size()); throw; }
    SecureZeroMemory(key.data(), key.size());
}

std::vector<movement::Packet> MovementLink::poll()
{
    std::vector<movement::Packet> packets;
    if (!channel_) return packets;
    const auto now = Clock::now();
    if (now - rate_time_ >= std::chrono::seconds(1)) { rate_time_ = now; rate_count_ = 0; input_count_ = 0; }
    for (const auto &bytes : channel_->poll())
    {
        // Hard bound even for authenticated malformed records. Invalid records don't
        // refresh liveness or move sequence cursors. DTLS drops unauthenticated ones.
        if (++rate_count_ > 400) throw std::runtime_error("UDP: limite de registros excedido");
        try
        {
            auto packet = movement::decode(bytes.data(), bytes.size());
            if (packet.session != session_ || packet.connection != connection_) continue;
            if (packet.kind == movement::Kind::Input && ++input_count_ > 60) continue;
            packets.push_back(packet);
        }
        catch (const std::runtime_error &) { /* discard malformed payload */ }
    }
    // Before any position is proposed, DTLS can be ready without application input.
    // TCP PING remains the session liveness mechanism; no unsafe endpoint rebinding.
    return packets;
}

bool MovementLink::send(movement::Kind kind, unsigned entity, std::uint64_t sequence, Position position)
{
    if (!ready()) return false;
    const auto bytes = movement::encode({kind, session_, connection_, sequence, entity, position});
    return channel_->send(bytes.data(), bytes.size());
}

void MovementLink::publish_input()
{
    const auto now = Clock::now();
    if (!desired_ || !ready() || now - input_time_ < std::chrono::milliseconds(34)) return;
    if (sequence_ >= (std::numeric_limits<std::uint64_t>::max)() - 2)
        throw std::runtime_error("UDP: sequencia esgotada; reconecte");
    if (send(movement::Kind::Input, owner_, ++sequence_, *desired_)) input_time_ = now;
}

bool MovementLink::state_due(unsigned peers)
{
    // Fair per-recipient share: <=1000 application records/s across active peers,
    // <=320 per recipient. One latest-state packet per turn, no unbounded queue.
    const unsigned rate = (std::min)(320u, 1000u / (std::max)(1u, peers));
    const auto interval = std::chrono::microseconds((1000000u + rate - 1) / rate);
    const auto now = Clock::now();
    if (!ready() || now - state_time_ < interval) return false;
    state_time_ = now; return true;
}

bool MovementLink::accepts_input(const movement::Packet &packet) const
{ return input_ && input_->eligible(packet); }
void MovementLink::commit_input(const movement::Packet &packet)
{ if (!input_ || !input_->commit(packet)) throw std::runtime_error("UDP: cursor de entrada inconsistente"); }
} // namespace dsr
