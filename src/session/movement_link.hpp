#pragma once

#include "net/dtls.hpp"
#include "net/tls.hpp"
#include "protocol/movement_packet.hpp"
#include "protocol/movement_stream.hpp"
#include "session/movement_metrics.hpp"

#include <chrono>
#include <optional>

namespace dsr
{
movement::Identity parse_identity(const std::string &hex);
std::string identity_hex(const movement::Identity &identity);
movement::Identity new_identity();

// One connection-owned protected datagram channel. Destroy on TCP disconnect.
class MovementLink
{
public:
    explicit MovementLink(SOCKET tcp, unsigned owner);
    ~MovementLink() { metrics.report(true); }
    MovementMetrics metrics;
    unsigned port() const { return socket_.port(); }
    SOCKET native() const { return socket_.native(); }
    void establish(TlsChannel &tls, bool server, const movement::Identity &session,
                   const movement::Identity &connection, unsigned port, const std::string &library);
    bool established() const { return !!channel_; }
    bool ready() const { return channel_ && channel_->ready(); }
    std::vector<movement::Packet> poll();
    bool send(movement::Kind kind, unsigned entity, std::uint64_t sequence, Position position);
    void propose(Position position) { desired_ = position; }
    void publish_input();
    bool state_due(unsigned peers);
    bool accepts_input(const movement::Packet &packet) const;
    void commit_input(const movement::Packet &packet);
    bool ready_announced = false;
    unsigned next_entity = 0;

private:
    using Clock = std::chrono::steady_clock;
    DatagramSocket socket_;
    std::unique_ptr<DtlsChannel> channel_;
    movement::Identity session_{}, connection_{};
    std::unique_ptr<movement::Stream> input_;
    std::optional<Position> desired_;
    unsigned owner_, rate_count_ = 0, input_count_ = 0;
    std::uint64_t sequence_ = 0;
    Clock::time_point input_time_{}, state_time_{}, rate_time_ = Clock::now();
};
} // namespace dsr
