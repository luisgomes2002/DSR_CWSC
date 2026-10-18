#include "protocol/movement_packet.hpp"
#include "protocol/movement_stream.hpp"
#include "client/player_replica.hpp"
#include "protocol/protocol.hpp"
#include "session/movement_metrics.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
#include <sstream>

namespace
{
using namespace dsr::movement;
void check(bool ok) { if (!ok) throw std::runtime_error("Verification failed"); }
template <typename F> void must_throw(F action)
{
    try { action(); } catch (const std::runtime_error &) { return; }
    throw std::runtime_error("Invalid input was accepted");
}
Packet sample()
{
    Packet p;
    p.session[0] = 0xab;
    p.connection[15] = 0xcd;
    p.sequence = 0x0102030405060708ULL;
    p.entity = 0x10203040;
    p.position = {1, -2, 0.125};
    return p;
}
Packet unpack(const Bytes &b) { return decode(b.data(), b.size()); }
} // namespace

int main()
{
    const std::vector<std::pair<const char *, std::function<void()>>> tests{
        {"metric_samples_and_variation", [] {
            dsr::MetricSamples values;
            check(values.mean() == 0 && values.variation() == 0);
            values.add(10); values.add(30); values.add(20);
            check(values.count == 3 && values.mean() == 20 && values.maximum == 30);
            check(values.variation() == 15);
        }},
        {"transport_metrics_count_completed_not_pending_writes", [] {
            dsr::MovementMetrics metrics; metrics.enable(false, 7);
            dsr::DtlsSendStats stats;
            stats.pending_bytes = 68; stats.peak_pending_bytes = 68;
            stats.want_write = 1; stats.suppressed = 5; stats.write_attempts = 1;
            metrics.transport(stats);
            std::ostringstream output;
            auto *previous = std::cout.rdbuf(output.rdbuf());
            metrics.report(true);
            stats.pending_bytes = 0; stats.records_sent = 1; stats.write_attempts = 2;
            metrics.transport(stats); metrics.report(true);
            std::cout.rdbuf(previous);
            const auto text = output.str();
            check(text.find("\"payload_sent_bytes\":0") != std::string::npos);
            check(text.find("\"payload_sent_bytes\":68") != std::string::npos);
            check(text.find("\"dtls_send_suppressed\":5") != std::string::npos);
            check(text.find("\"dtls_pending_bytes\":68") != std::string::npos);
            check(text.find("\"dtls_pending_bytes\":0") != std::string::npos);
        }},
        {"movement_metrics_sequence_and_local_timing", [] {
            dsr::MovementMetrics metrics;
            metrics.enable(true, 7);
            auto packet = sample(); packet.entity = 7; packet.sequence = 1;
            const auto start = std::chrono::steady_clock::now();
            metrics.received(packet, start);
            packet.sequence = 3;
            metrics.received(packet, start + std::chrono::milliseconds(20));
            metrics.received(packet, start + std::chrono::milliseconds(30)); // duplicate
            packet.sequence = 2;
            metrics.received(packet, start + std::chrono::milliseconds(35)); // stale
            packet.sequence = 4;
            metrics.received(packet, start + std::chrono::milliseconds(60));
            metrics.sent(); metrics.rtt(10); metrics.rtt(30);
            metrics.control_queue(65536);
            metrics.control_queue(65536, true);
            metrics.control_queue(0);
            std::ostringstream output;
            auto *previous = std::cout.rdbuf(output.rdbuf());
            metrics.report(true);
            std::cout.rdbuf(previous);
            const auto json = output.str();
            for (const auto field : {"\"input_sequence_gaps_observed\":1", "\"duplicate_revision\":1",
                "\"stale_revision\":1", "\"update_interval_samples\":2", "\"update_interval_mean_ms\":30",
                "\"update_interval_variation_mean_ms\":20", "\"payload_sent_bytes\":68",
                "\"payload_received_bytes\":340", "\"tcp_rtt_samples\":2", "\"tcp_rtt_mean_ms\":20",
                "\"update_interval_histogram\":[1,1,0,0,0,0,0,0,0]",
                "\"control_queue_bytes\":0", "\"control_queue_peak_bytes\":65536", "\"control_queue_rejections\":1"})
                check(json.find(field) != std::string::npos);
        }},
        {"wire_golden_vector", [] {
            const Bytes expected{
                0x44,0x53,0x52,0x4d, 1,1,0,0, 0,0x44,0,0,
                0xab,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xcd,
                1,2,3,4,5,6,7,8, 0x10,0x20,0x30,0x40,
                0,0,3,0xe8, 0xff,0xff,0xf8,0x30, 0,0,0,0x7d};
            check(encode(sample()) == expected);
        }},
        {"roundtrip_both_directions", [] {
            for (auto kind : {Kind::Input, Kind::State})
            {
                auto p = sample(); p.kind = kind;
                auto q = unpack(encode(p));
                check(q.kind == p.kind && q.session == p.session && q.connection == p.connection);
                check(q.sequence == p.sequence && q.entity == p.entity);
                check(q.position.x == 1 && q.position.y == -2 && q.position.z == 0.125);
            }
        }},
        {"coordinate_limits_and_quantization", [] {
            auto p = sample(); p.position = {-100000, 100000, -0.0};
            auto q = unpack(encode(p));
            check(q.position.x == -100000 && q.position.y == 100000 && q.position.z == 0);
            p.position = {0.0006, -0.0006, 123.456789}; q = unpack(encode(p));
            check(q.position.x == 0.001 && q.position.y == -0.001);
            check(std::abs(q.position.z - p.position.z) <= 0.0005);
            for (double bad : {100000.001, -100000.001, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()})
            { p.position.x = bad; must_throw([&] { encode(p); }); }
        }},
        {"exact_packet_length", [] {
            auto b = encode(sample());
            for (std::size_t n = 0; n < PacketSize; ++n)
                must_throw([&] { decode(b.data(), n); });
            must_throw([&] { decode(b.data(), PacketSize + 1); });
            must_throw([&] { decode(nullptr, PacketSize); });
        }},
        {"strict_header", [] {
            const auto original = encode(sample());
            for (std::size_t offset = 0; offset < 12; ++offset)
            {
                auto b = original; b[offset] ^= 0x80;
                must_throw([&] { unpack(b); });
            }
        }},
        {"invalid_identifiers_and_sequence", [] {
            auto p = sample(); p.session = {}; must_throw([&] { encode(p); });
            p = sample(); p.connection = {}; must_throw([&] { encode(p); });
            p = sample(); p.entity = 0; must_throw([&] { encode(p); });
            for (auto sequence : {std::uint64_t{0}, (std::numeric_limits<std::uint64_t>::max)()})
            { p = sample(); p.sequence = sequence; must_throw([&] { encode(p); }); }
        }},
        {"reject_invalid_wire_fields", [] {
            auto original = encode(sample());
            for (const auto range : {std::pair<std::size_t, std::size_t>{12,28}, {28,44}, {44,52}, {52,56}})
            {
                auto b = original;
                for (auto i = range.first; i < range.second; ++i) b[i] = 0;
                must_throw([&] { unpack(b); });
            }
            for (std::size_t axis : {56u, 60u, 64u})
            {
                auto b = original; b[axis] = 0x7f;
                must_throw([&] { unpack(b); });
            }
            auto b = original;
            for (std::size_t i = 44; i < 52; ++i) b[i] = 255;
            must_throw([&] { unpack(b); });
        }},
        {"newest_wins_with_gaps", [] {
            auto p = sample(); Stream s(p.kind, p.session, p.connection, p.entity);
            p.sequence = 1; check(s.commit(p));
            p.sequence = 4; check(s.eligible(p) && s.latest() == 1); check(s.commit(p));
            check(!s.commit(p)); p.sequence = 2; check(!s.commit(p));
            p.sequence = 3; check(!s.commit(p)); check(s.latest() == 4);
            p.sequence = 5; check(s.commit(p));
        }},
        {"identity_direction_and_entity_isolation", [] {
            const auto p = sample(); Stream s(p.kind, p.session, p.connection, p.entity);
            auto q = p; q.session[1] = 1; check(!s.commit(q));
            q = p; q.connection[1] = 1; check(!s.commit(q));
            q = p; ++q.entity; check(!s.commit(q));
            q = p; q.kind = Kind::State; check(!s.commit(q));
            q = p; q.position.x = 100001; check(!s.commit(q));
            check(s.latest() == 0 && s.commit(p));
        }},
        {"snapshot_baseline_and_independent_entities", [] {
            auto p = sample(); p.kind = Kind::State; p.sequence = 100;
            Stream a(p.kind, p.session, p.connection, p.entity, 100);
            Stream b(p.kind, p.session, p.connection, p.entity + 1, 100);
            check(!a.commit(p)); p.sequence = 101; check(a.commit(p));
            ++p.entity; check(b.commit(p)); check(a.latest() == b.latest());
        }},
        {"reconnect_rejects_old_connection", [] {
            auto old = sample(); auto fresh = old; fresh.connection[0] = 1; fresh.sequence = 1;
            Stream s(fresh.kind, fresh.session, fresh.connection, fresh.entity);
            check(!s.commit(old)); check(s.commit(fresh));
        }},
        {"no_sequence_wrap_and_valid_stream_configuration", [] {
            auto p = sample(); Stream s(p.kind, p.session, p.connection, p.entity);
            p.sequence = (std::numeric_limits<std::uint64_t>::max)() - 1; check(s.commit(p));
            ++p.sequence; check(!s.commit(p)); p.sequence = 1; check(!s.commit(p));
            must_throw([&] { Stream invalid(p.kind, {}, p.connection, p.entity); });
            must_throw([&] { Stream invalid(p.kind, p.session, p.connection, 0); });
            must_throw([&] { Stream invalid(p.kind, p.session, p.connection, p.entity,
                (std::numeric_limits<std::uint64_t>::max)()); });
        }},
        {"udp_replica_does_not_advance_reliable_cursor", [] {
            dsr::PlayerReplica r;
            r.consume(dsr::split("PLAYERS\t2\t2"));
            r.consume(dsr::split("PLAYER\t1\ta\t0\t0\t0\t1"));
            r.consume(dsr::split("PLAYER\t2\tb\t0\t0\t0\t2"));
            r.consume(dsr::split("PLAYERS_END\t2"));
            check(r.apply_movement(1, {9, 0, 0}, 10));
            check(r.apply_movement(2, {8, 0, 0}, 9));
            check(r.revision() == 2);
            check(!r.apply_movement(1, {1, 0, 0}, 9));
            check(!r.apply_movement(1, {1, 0, 0}, 10));
            r.consume(dsr::split("SPAWN\t3\tc\t0\t0\t0\t3"));
            r.consume(dsr::split("DESPAWN\t1\t11"));
            check(!r.apply_movement(1, {2, 0, 0}, 12));
            check(r.apply_movement(2, {7, 0, 0}, 12));
        }},
        {"udp_replica_snapshot_baseline_and_unknown_entity", [] {
            dsr::PlayerReplica r;
            check(!r.apply_movement(99, {}, 1));
            r.consume(dsr::split("PLAYERS\t10\t1"));
            r.consume(dsr::split("PLAYER\t1\ta\t0\t0\t0\t5"));
            check(!r.apply_movement(1, {3, 0, 0}, 11));
            r.consume(dsr::split("PLAYERS_END\t10"));
            check(!r.apply_movement(1, {3, 0, 0}, 9));
            check(!r.apply_movement(1, {3, 0, 0}, 10));
            check(r.apply_movement(1, {3, 0, 0}, 11));
            check(!r.apply_movement(1, {100001, 0, 0}, 12));
        }}
    };
    unsigned failed = 0;
    for (const auto &[name, test] : tests)
    {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception &e)
        { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    return failed ? 1 : 0;
}
