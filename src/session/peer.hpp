#pragma once

#include "net/socket.hpp"
#include "net/tls.hpp"
#include "session/movement_link.hpp"

#include <chrono>
#include <string>

namespace dsr
{
using Clock = std::chrono::steady_clock;

// Connection-owned session state; socket lifetime is managed by Host.
struct Peer
{
    SOCKET socket;
    std::unique_ptr<TlsChannel> tls;
    std::unique_ptr<MovementLink> movement;
    unsigned id = 0;
    std::string name, input, output;
    bool closing = false, dead = false;
    Clock::time_point accepted = Clock::now(), last = accepted, rate_start = accepted, close_start = accepted;
    unsigned rate_count = 0;
};
} // namespace dsr


