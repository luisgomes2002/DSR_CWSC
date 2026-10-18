#pragma once

#include "session/peer.hpp"
#include "world/world.hpp"

#include <map>
#include <string>

namespace dsr
{
struct Options;

class Host
{
public:
    Host(std::string key, unsigned limit);
    ~Host();
    Host(const Host &) = delete;
    Host &operator=(const Host &) = delete;
    void run(const Options &o);

private:
    std::map<SOCKET, Peer> peers;
    std::string secret, room;
    unsigned capacity, next_id = 1;
    World world;
    bool udp_enabled = false;
    bool metrics_enabled = false;
    std::string udp_library;

    void emit(Peer &p, const std::string &line);
    void reject(Peer &p, const std::string &reason);
    void broadcast(const std::string &line, unsigned except = 0);
    unsigned active() const;
    void roster(Peer &p);
    void players(Peer &p);
    void frame(Peer &p, const std::string &line);
    void receive(Peer &p);
    void negotiate_movement(Peer &p, const std::vector<std::string> &fields);
    void service_movement(Peer &p);
    void publish_position(const PlayerState &player);
};
} // namespace dsr


