#pragma once

#include "client/player_replica.hpp"
#include "session/movement_link.hpp"

namespace dsr
{
class ClientMovement
{
public:
    ClientMovement(SOCKET tcp, unsigned owner, const std::string &room, std::string library);
    std::string request() const;
    void control(const std::vector<std::string> &fields, TlsChannel &tls);
    void propose(const std::string &move_frame);
    void poll(PlayerReplica &replica);
    bool ready() const;
    void enable_metrics() { link_.metrics.enable(false, owner_); }
    void record_rtt(double ms) { link_.metrics.rtt(ms); }
    void control_queue(std::size_t bytes, bool rejected = false) { link_.metrics.control_queue(bytes, rejected); }
    void forget(unsigned entity) { link_.metrics.forget(entity); }
    void automatic(double seconds);
private:
    MovementLink link_;
    unsigned owner_;
    movement::Identity session_;
    std::string library_;
    bool host_ready_ = false, announced_ = false;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now(),
        last_state_ = started_;
};
} // namespace dsr
