#include "client/player_replica.hpp"
#include "protocol/entity_messages.hpp"
#include "protocol/protocol.hpp"
#include "ui/client_commands.hpp"
#include "world/world.hpp"

#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
void check(bool condition)
{
    if (!condition) throw std::runtime_error("Verificacao falhou");
}
template <typename F> void must_throw(F action)
{
    try { action(); }
    catch (const std::runtime_error &) { return; }
    throw std::runtime_error("Entrada invalida foi aceita");
}
void consume(dsr::PlayerReplica &replica, const std::string &line)
{
    check(replica.consume(dsr::split(line)));
}
} // namespace

int main()
{
    using namespace dsr;
    const std::vector<std::pair<const char *, std::function<void()>>> tests{
        {"coordinates", [] {
            check(coordinate("-100000") == -CoordinateLimit);
            check(coordinate("1.25e2") == 125);
            for (const std::string text : {"nan", "inf", "-inf", "1e999", "100001", "1,5", " 1", "1 ", "+1", "1x", ""})
                must_throw([&] { (void)coordinate(text); });
            check(!valid_position({0, std::numeric_limits<double>::infinity(), 0}));
        }},
        {"world_lifecycle", [] {
            World world;
            check(world.spawn(1, "a").revision == 1);
            must_throw([&] { world.spawn(1, "duplicate"); });
            must_throw([&] { world.move(99, {}); });
            must_throw([&] { world.move(1, {100001, 0, 0}); });
            check(world.revision() == 1);
            check(world.move(1, {1, 2, 3}).revision == 2);
            check(world.remove(1) == 3);
            check(!world.remove(1));
            check(world.players().empty());
            check(world.spawn(2, "a").position.x == 0);
        }},
        {"serialization_roundtrip", [] {
            PlayerState p{7, "tester", {0.1, -2.5, 99999.125}, 23};
            const auto decoded = parse_player(split(player_frame(p)));
            check(decoded.id == p.id && decoded.name == p.name && decoded.revision == p.revision);
            check(decoded.position.x == p.position.x && decoded.position.y == p.position.y && decoded.position.z == p.position.z);
            check(position_fields({-0.0, 0, 0}) == "0\t0\t0");
        }},
        {"replica_snapshot_is_atomic", [] {
            PlayerReplica replica;
            consume(replica, "PLAYERS\t1\t1");
            consume(replica, "PLAYER\t1\ta\t0\t0\t0\t1");
            check(replica.players().empty());
            consume(replica, "PLAYERS_END\t1");
            consume(replica, "PLAYERS\t2\t1");
            consume(replica, "PLAYER\t2\tb\t1\t2\t3\t2");
            check(replica.players().count(1) == 1 && replica.revision() == 1);
            must_throw([&] { consume(replica, "PLAYERS_END\t3"); });
            check(replica.players().count(1) == 1 && replica.revision() == 1);
            consume(replica, "PLAYERS_END\t2");
            check(replica.players().count(2) == 1 && replica.players().count(1) == 0);
        }},
        {"replica_live_events", [] {
            PlayerReplica replica;
            consume(replica, "PLAYERS\t0\t0");
            consume(replica, "PLAYERS_END\t0");
            consume(replica, "SPAWN\t1\ta\t0\t0\t0\t1");
            consume(replica, "POSITION\t1\t5\t6\t7\t2");
            check(replica.players().at(1).position.x == 5);
            must_throw([&] { consume(replica, "POSITION\t1\t8\t8\t8\t1"); });
            must_throw([&] { consume(replica, "POSITION\t99\t8\t8\t8\t3"); });
            check(replica.players().at(1).position.x == 5 && replica.revision() == 2);
            consume(replica, "DESPAWN\t1\t3");
            check(replica.players().empty() && replica.revision() == 3);
        }},
        {"malformed_snapshot", [] {
            PlayerReplica replica;
            must_throw([&] { consume(replica, "PLAYERS\t0\t33"); });
            consume(replica, "PLAYERS\t2\t2");
            consume(replica, "PLAYER\t1\ta\t0\t0\t0\t1");
            must_throw([&] { consume(replica, "PLAYER\t1\ta\t0\t0\t0\t1"); });
            must_throw([&] { consume(replica, "PLAYERS_END\t2"); });
            must_throw([&] { consume(replica, "SPAWN\t2\tb\t0\t0\t0\t3"); });
            check(replica.players().empty());
        }},
        {"console_commands", [] {
            check(client_command("/move 10 -2.5 0").frame == "MOVE\t10\t-2.5\t0");
            check(client_command("/players").frame == "PLAYERS");
            check(client_command("/quit").quit);
            check(client_command("/send 2 hello there").frame == "SEND\t2\thello there");
            for (const std::string text : {"/move", "/move 1 2", "/move 1 2 3 4", "/move nan 0 0", "/move 1,5 0 0", "/unknown"})
                must_throw([&] { (void)client_command(text); });
        }},
        {"revisions", [] {
            check(revision_number("18446744073709551615") == std::numeric_limits<std::uint64_t>::max());
            for (const std::string text : {"18446744073709551616", "-1", " 1", "1x", ""})
                must_throw([&] { (void)revision_number(text); });
        }}
    };
    for (const auto &[name, test] : tests)
    {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception &e) { std::cerr << "FAIL " << name << ": " << e.what() << '\n'; return 1; }
    }
    return 0;
}
