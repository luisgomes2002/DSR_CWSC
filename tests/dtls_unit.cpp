#include "net/dtls.hpp"
#include "protocol/movement_packet.hpp"
#include "protocol/movement_stream.hpp"
#include "client/player_replica.hpp"
#include "session/movement_metrics.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <set>
#include <string>

namespace
{
using namespace dsr;
using Clock = std::chrono::steady_clock;
using Record = std::vector<unsigned char>;
void check(bool ok) { if (!ok) throw std::runtime_error("Verification failed"); }
template <typename F> void must_throw(F action)
{
    try { action(); } catch (const std::runtime_error &) { return; }
    throw std::runtime_error("Expected channel failure");
}
void bind_udp(Socket &s)
{
    s.value = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s.value == INVALID_SOCKET) net_error("test socket");
    auto local = address("127.0.0.1", 0);
    if (bind(s.value, reinterpret_cast<sockaddr *>(&local), sizeof(local))) net_error("test bind");
    nonblocking(s.value);
}
sockaddr_in endpoint(SOCKET s)
{
    sockaddr_in a{}; int size = sizeof(a);
    if (getsockname(s, reinterpret_cast<sockaddr *>(&a), &size)) net_error("test endpoint");
    return a;
}
void raw_send(SOCKET s, const Record &record, const sockaddr_in &to)
{
    if (sendto(s, reinterpret_cast<const char *>(record.data()), static_cast<int>(record.size()), 0,
        reinterpret_cast<const sockaddr *>(&to), sizeof(to)) != static_cast<int>(record.size())) net_error("test relay");
}
struct Pair
{
    struct Profile
    {
        unsigned delay_ms = 0, jitter_ms = 0, drop_every = 0;
        unsigned burst_period = 0, burst_length = 0, duplicate_every = 0, reorder_every = 0;
        unsigned blackout_at_ms = 0, blackout_ms = 0, bytes_per_second = 0, queue_limit = 128;
    } profile;
    struct Scheduled { Clock::time_point due; bool from_client; Record record; };
    std::vector<Scheduled> scheduled;
    bool impaired = false;
    Clock::time_point profile_start{}, next_delivery[2]{};
    unsigned direction_count[2]{}, random_state = 0x12345678;
    unsigned seen = 0, dropped = 0, queue_drops = 0, duplicates = 0, delivered = 0, queue_peak = 0;
    Socket client_socket, server_socket, upstream, downstream;
    std::unique_ptr<DtlsChannel> client, server;
    sockaddr_in client_addr{}, server_addr{};
    bool hold_client = false;
    std::vector<Record> captured, held;
    void configure(Profile settings)
    { profile = settings; impaired = true; profile_start = Clock::now(); captured.clear(); }
    void schedule(Record record, bool from_client)
    {
        ++seen;
        const unsigned direction = from_client ? 0u : 1u;
        const unsigned n = ++direction_count[direction];
        const auto now = Clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - profile_start).count();
        if ((profile.drop_every && n % profile.drop_every == 0) ||
            (profile.burst_period && (n - 1) % profile.burst_period < profile.burst_length) ||
            (profile.blackout_ms && elapsed >= profile.blackout_at_ms && elapsed < profile.blackout_at_ms + profile.blackout_ms))
        { ++dropped; return; }
        if (scheduled.size() >= profile.queue_limit) { ++queue_drops; return; }
        random_state = random_state * 1664525u + 1013904223u;
        const unsigned jitter = profile.jitter_ms ? random_state % (profile.jitter_ms + 1) : 0;
        auto due = now + std::chrono::milliseconds(profile.delay_ms + jitter);
        if (profile.reorder_every && n % profile.reorder_every == 0) due += std::chrono::milliseconds(80);
        if (profile.bytes_per_second)
        {
            next_delivery[direction] = (std::max)(now, next_delivery[direction]) +
                std::chrono::microseconds(record.size() * 1000000 / profile.bytes_per_second);
            due = (std::max)(due, next_delivery[direction]);
        }
        const auto enqueue = [&](const Record &bytes, Clock::time_point time)
        {
            if (scheduled.size() >= profile.queue_limit) { ++queue_drops; return false; }
            scheduled.push_back({time, from_client, bytes});
            queue_peak = (std::max)(queue_peak, static_cast<unsigned>(scheduled.size())); return true;
        };
        enqueue(record, due);
        if (profile.duplicate_every && n % profile.duplicate_every == 0 && enqueue(record, due + std::chrono::milliseconds(3)))
            ++duplicates;
    }
    void flush_scheduled()
    {
        const auto now = Clock::now();
        for (;;)
        {
            auto first = std::min_element(scheduled.begin(), scheduled.end(), [](const auto &a, const auto &b) { return a.due < b.due; });
            if (first == scheduled.end() || first->due > now) break;
            raw_send(first->from_client ? downstream.value : upstream.value, first->record,
                     first->from_client ? server_addr : client_addr);
            ++delivered; scheduled.erase(first);
        }
    }
    explicit Pair(bool wrong_key = false)
    {
        bind_udp(client_socket); bind_udp(server_socket); bind_udp(upstream); bind_udp(downstream);
        client_addr = endpoint(client_socket.value); server_addr = endpoint(server_socket.value);
        auto a = endpoint(upstream.value), b = endpoint(downstream.value);
        if (connect(client_socket.value, reinterpret_cast<sockaddr *>(&a), sizeof(a)) ||
            connect(server_socket.value, reinterpret_cast<sockaddr *>(&b), sizeof(b))) net_error("test connect");
        std::array<unsigned char, 32> key{}; key.fill(0x42);
        server = std::make_unique<DtlsChannel>(server_socket.value, true, key, "");
        if (wrong_key) key[0] ^= 1;
        client = std::make_unique<DtlsChannel>(client_socket.value, false, key, "");
        SecureZeroMemory(key.data(), key.size());
    }
    void relay()
    {
        for (bool from_client : {true, false})
        {
            const auto source = from_client ? upstream.value : downstream.value;
            for (unsigned i = 0; i < 16; ++i)
            {
                std::array<unsigned char, 65536> buffer{};
                const int n = recvfrom(source, reinterpret_cast<char *>(buffer.data()), static_cast<int>(buffer.size()), 0, nullptr, nullptr);
                if (n == SOCKET_ERROR)
                { if (WSAGetLastError() != WSAEWOULDBLOCK) net_error("test recv"); break; }
                Record record(buffer.begin(), buffer.begin() + n);
                if (impaired) { schedule(std::move(record), from_client); continue; }
                if (from_client)
                {
                    captured.push_back(record);
                    if (hold_client) { held.push_back(record); continue; }
                }
                raw_send(from_client ? downstream.value : upstream.value, record, from_client ? server_addr : client_addr);
            }
        }
        flush_scheduled();
    }
    template <typename F> void until(F done, unsigned timeout_ms = 6000)
    {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        while (!done())
        {
            if (Clock::now() >= deadline) throw std::runtime_error("DTLS test timed out");
            relay(); Sleep(1);
        }
    }
    void handshake()
    {
        until([&] { client->poll(); server->poll(); return client->ready() && server->ready(); });
        relay(); captured.clear();
    }
    void deliver(const Record &r) { raw_send(downstream.value, r, server_addr); }
};
movement::Packet packet(std::uint64_t sequence)
{
    movement::Packet p;
    p.session[0] = 1; p.connection[0] = 2; p.entity = 7;
    p.sequence = sequence; p.position = {123.125, -4.5, 9}; return p;
}
void transmit(Pair &pair, std::uint64_t sequence)
{
    const auto bytes = movement::encode(packet(sequence));
    check(pair.client->send(bytes.data(), bytes.size())); pair.relay();
}
std::vector<Record> receive(Pair &pair)
{
    std::vector<Record> records;
    pair.until([&] { auto r = pair.server->poll(); records.insert(records.end(), r.begin(), r.end()); return !records.empty(); });
    return records;
}
void expect_silence(Pair &pair)
{
    const auto deadline = Clock::now() + std::chrono::milliseconds(60);
    while (Clock::now() < deadline)
    { pair.relay(); check(pair.server->poll().empty()); Sleep(1); }
}

void exercise_profile(const char *name, Pair::Profile profile, unsigned channels = 1, unsigned duration_ms = 700)
{
    struct Connection
    {
        Pair pair;
        movement::Stream input{movement::Kind::Input, packet(1).session, packet(1).connection, 7};
        PlayerReplica replica;
        MovementMetrics host_metrics, client_metrics;
        std::map<std::uint64_t, Clock::time_point> sent_at;
        std::set<std::uint64_t> input_seen, state_seen;
        unsigned stale_inputs = 0, stale_states = 0;
        MetricSamples delivery_delay;
        std::uint64_t input_sequence = 1, state_sequence = 1;
        unsigned input_updates = 0, state_updates = 0;
        Connection()
        {
            pair.handshake();
            replica.consume({"PLAYERS", "1", "1"});
            replica.consume({"PLAYER", "7", "remote", "0", "0", "0", "1"});
            replica.consume({"PLAYERS_END", "1"});
            host_metrics.enable(true, 7); client_metrics.enable(false, 7);
        }
        void send(std::uint64_t sequence)
        {
            sent_at[sequence] = Clock::now();
            auto p = packet(sequence); p.position.x = static_cast<double>(sequence);
            auto bytes = movement::encode(p); check(pair.client->send(bytes.data(), bytes.size()));
            p.kind = movement::Kind::State;
            bytes = movement::encode(p); check(pair.server->send(bytes.data(), bytes.size()));
        }
        void timing(const movement::Packet &p)
        {
            if (p.sequence == 10000) return; // Clean recovery probe, not impaired.
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - sent_at.at(p.sequence)).count();
            check(ms >= pair.profile.delay_ms);
            delivery_delay.add(ms);
        }
        void poll()
        {
            pair.relay();
            for (const auto &r : pair.server->poll())
            {
                auto p = movement::decode(r.data(), r.size()); host_metrics.received(p);
                check(input_seen.insert(p.sequence).second); timing(p);
                if (input.commit(p))
                { check(p.sequence > input_sequence); input_sequence = p.sequence; ++input_updates; }
                else ++stale_inputs;
                check(input.latest() == input_sequence);
            }
            for (const auto &r : pair.client->poll())
            {
                auto p = movement::decode(r.data(), r.size()); client_metrics.received(p);
                check(state_seen.insert(p.sequence).second); timing(p);
                if (replica.apply_movement(p.entity, p.position, p.sequence))
                { check(p.sequence > state_sequence); state_sequence = p.sequence; ++state_updates; }
                else ++stale_states;
                check(replica.players().at(7).revision == state_sequence);
                check(replica.players().at(7).position.x == static_cast<double>(state_sequence));
            }
        }
    };
    std::vector<std::unique_ptr<Connection>> connections;
    for (unsigned i = 0; i < channels; ++i)
    {
        auto c = std::make_unique<Connection>(); c->pair.configure(profile);
        connections.push_back(std::move(c));
    }
    const auto start = Clock::now(); auto next_send = start;
    std::uint64_t sequence = 1;
    while (Clock::now() - start < std::chrono::milliseconds(duration_ms))
    {
        if (Clock::now() >= next_send)
        {
            ++sequence;
            for (auto &c : connections) c->send(sequence);
            next_send = Clock::now() + std::chrono::milliseconds(20);
        }
        for (auto &c : connections) c->poll();
        Sleep(1);
    }
    // Resume a clean path but retain delayed records. New absolute states must
    // recover promptly; late old states must not undo them or resurrect entities.
    for (auto &c : connections) { c->pair.impaired = false; c->send(10000); }
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    for (;;)
    {
        bool done = true;
        for (auto &c : connections)
        {
            c->poll();
            done = done && c->input_sequence == 10000 && c->state_sequence == 10000;
        }
        if (done) break;
        check(Clock::now() < deadline); Sleep(1);
    }
    unsigned seen = 0, dropped = 0, duplicated = 0, queue_drops = 0, peak = 0, stale_inputs = 0, stale_states = 0;
    double delay_sum = 0, delay_max = 0; unsigned long long delay_samples = 0;
    for (auto &c : connections)
    {
        check(c->input_updates > 1 && c->state_updates > 1);
        check(c->pair.queue_peak <= profile.queue_limit);
        c->replica.consume({"DESPAWN", "7", "10001"});
        const auto until = Clock::now() + std::chrono::seconds(3);
        while (!c->pair.scheduled.empty())
        {
            check(Clock::now() < until);
            c->pair.relay();
            for (const auto &r : c->pair.client->poll())
            {
                const auto p = movement::decode(r.data(), r.size());
                check(c->state_seen.insert(p.sequence).second); c->timing(p);
                c->client_metrics.received(p);
                check(!c->replica.apply_movement(p.entity, p.position, p.sequence));
                ++c->stale_states;
            }
            for (const auto &r : c->pair.server->poll())
            {
                const auto p = movement::decode(r.data(), r.size());
                check(c->input_seen.insert(p.sequence).second); c->timing(p);
                c->host_metrics.received(p); check(!c->input.commit(p)); ++c->stale_inputs;
            }
            check(c->replica.players().empty()); Sleep(1);
        }
        seen += c->pair.seen; dropped += c->pair.dropped; duplicated += c->pair.duplicates;
        queue_drops += c->pair.queue_drops; peak = (std::max)(peak, c->pair.queue_peak);
        stale_inputs += c->stale_inputs; stale_states += c->stale_states;
        delay_sum += c->delivery_delay.sum; delay_samples += c->delivery_delay.count;
        delay_max = (std::max)(delay_max, c->delivery_delay.maximum);
        c->host_metrics.transport(c->pair.server->send_stats());
        c->client_metrics.transport(c->pair.client->send_stats());
        c->host_metrics.report(true); c->client_metrics.report(true);
    }
    if (profile.drop_every || profile.burst_period || profile.blackout_ms) check(dropped > 0);
    if (profile.duplicate_every) check(duplicated > 0);
    if (profile.bytes_per_second) check(queue_drops > 0);
    if (profile.reorder_every) check(stale_inputs > 0 && stale_states > 0);
    std::cout << "NETWORK_PROFILE\t{\"name\":\"" << name << "\",\"channels\":" << channels
        << ",\"duration_ms\":" << duration_ms << ",\"seed\":305419896,\"delay_ms\":" << profile.delay_ms
        << ",\"jitter_ms\":" << profile.jitter_ms << ",\"drop_every\":" << profile.drop_every
        << ",\"burst_period\":" << profile.burst_period << ",\"burst_length\":" << profile.burst_length
        << ",\"duplicate_every\":" << profile.duplicate_every << ",\"reorder_every\":" << profile.reorder_every
        << ",\"blackout_at_ms\":" << profile.blackout_at_ms << ",\"blackout_ms\":" << profile.blackout_ms
        << ",\"bytes_per_second\":" << profile.bytes_per_second << ",\"queue_limit\":" << profile.queue_limit
        << ",\"seen\":" << seen << ",\"dropped_by_profile\":" << dropped << ",\"duplicates_injected\":" << duplicated
        << ",\"queue_drops\":" << queue_drops << ",\"queue_peak\":" << peak
        << ",\"stale_inputs_rejected\":" << stale_inputs << ",\"stale_states_rejected\":" << stale_states
        << ",\"delivery_delay_samples\":" << delay_samples
        << ",\"delivery_delay_mean_ms\":" << (delay_samples ? delay_sum / static_cast<double>(delay_samples) : 0)
        << ",\"delivery_delay_max_ms\":" << delay_max
        << ",\"recovered\":true,\"no_rollback_or_resurrection\":true}" << std::endl;
}
void report_backpressure(const char *name, const DtlsChannel &channel)
{
    const auto &s = channel.send_stats();
    std::cout << "BACKPRESSURE\t{\"name\":\"" << name << "\",\"records_sent\":" << s.records_sent
        << ",\"write_attempts\":" << s.write_attempts << ",\"want_read\":" << s.want_read
        << ",\"want_write\":" << s.want_write << ",\"suppressed\":" << s.suppressed
        << ",\"failures\":" << s.failures << ",\"timeouts\":" << s.timeouts
        << ",\"pending_bytes\":" << s.pending_bytes << ",\"peak_pending_bytes\":" << s.peak_pending_bytes
        << "}" << std::endl;
}
} // namespace
int main(int argc, char **argv)
{
    unsigned stress_ms = 2000;
    if (argc != 1)
    {
        try
        {
            if (argc != 3 || std::string(argv[1]) != "--stress-seconds") throw std::runtime_error("Invalid arguments");
            const std::string value = argv[2];
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("Invalid duration");
            const auto seconds = std::stoul(value);
            if (seconds < 1 || seconds > 60) throw std::runtime_error("Invalid duration");
            stress_ms = static_cast<unsigned>(seconds) * 1000;
        }
        catch (const std::exception &)
        { std::cerr << "Usage: dtls_unit [--stress-seconds 1-60]" << std::endl; return 1; }
    }
    Winsock network;
    const std::vector<std::pair<const char *, std::function<void()>>> tests{
        {"encrypted_roundtrip_and_no_cleartext", [] {
            Pair p; p.handshake(); transmit(p, 1);
            const auto bytes = movement::encode(packet(1));
            auto received = receive(p); check(received.size() == 1);
            check(received[0] == Record(bytes.begin(), bytes.end())); check(!p.captured.empty());
            for (const auto &record : p.captured)
                check(std::search(record.begin(), record.end(), bytes.begin(), bytes.end()) == record.end());
            check(p.server->send(bytes.data(), bytes.size())); p.relay();
            p.until([&] { return !p.client->poll().empty(); });
        }},
        {"handshake_recovers_after_first_client_flight_loss", [] {
            Pair p; p.hold_client = true;
            p.client->poll(); p.relay(); check(!p.held.empty());
            p.hold_client = false; // Held first flight is deliberately never sent.
            p.handshake(); transmit(p, 1); receive(p);
            check(p.client->ready() && p.server->ready());
        }},
        {"tampered_record_rejected_channel_survives", [] {
            Pair p; p.handshake(); p.hold_client = true; transmit(p, 1);
            check(p.held.size() == 1); auto broken = p.held[0]; broken.back() ^= 1;
            p.deliver(broken); expect_silence(p); p.hold_client = false; transmit(p, 2);
            auto records = receive(p); check(movement::decode(records[0].data(), records[0].size()).sequence == 2);
        }},
        {"replay_record_rejected", [] {
            Pair p; p.handshake(); transmit(p, 1); receive(p);
            check(p.captured.size() == 1); p.deliver(p.captured[0]); expect_silence(p);
            transmit(p, 2); receive(p);
        }},
        {"loss_does_not_block_next_position", [] {
            Pair p; p.handshake(); p.hold_client = true; transmit(p, 1);
            p.hold_client = false; transmit(p, 2);
            auto records = receive(p); check(movement::decode(records[0].data(), records[0].size()).sequence == 2);
        }},
        {"reordered_positions_newest_wins", [] {
            Pair p; p.handshake(); p.hold_client = true; transmit(p, 1); transmit(p, 2);
            check(p.held.size() == 2); p.deliver(p.held[1]); p.deliver(p.held[0]);
            const auto initial = packet(1);
            movement::Stream stream(initial.kind, initial.session, initial.connection, initial.entity);
            unsigned accepted = 0;
            p.until([&] {
                for (const auto &r : p.server->poll())
                    if (stream.commit(movement::decode(r.data(), r.size()))) ++accepted;
                return stream.latest() == 2;
            }); check(accepted == 1);
        }},
        {"wrong_key_cannot_authenticate", [] {
            Pair p(true); bool refused = false;
            try { p.handshake(); } catch (const std::runtime_error &) { refused = true; }
            check(refused && !p.server->ready() && !p.client->ready());
        }},
        {"want_write_recovers_immutable_single_pending_record", [] {
            Pair p; p.handshake();
            auto bytes = movement::encode(packet(1)); const auto original = bytes;
            p.client->test_block_writes(3);
            check(p.client->send(bytes.data(), bytes.size()));
            check(p.client->send_stats().want_write == 1 && p.client->send_stats().records_sent == 0);
            bytes.fill(0); // The original caller's buffer may disappear/change.
            for (unsigned i = 0; i < 1000; ++i) check(!p.client->send(bytes.data(), bytes.size()));
            check(p.client->send_stats().pending_bytes == movement::PacketSize);
            check(p.client->send_stats().suppressed == 1000);
            p.until([&] { p.client->poll(); return p.client->send_stats().records_sent == 1; });
            auto records = receive(p);
            check(records.size() == 1 && records[0] == Record(original.begin(), original.end()));
            check(p.client->send_stats().write_attempts == 4 && p.client->send_stats().pending_bytes == 0);
            transmit(p, 2); receive(p); expect_silence(p);
            report_backpressure("want_write_recovery", *p.client);
        }},
        {"want_read_waits_for_readability_then_recovers", [] {
            Pair p; p.handshake(); const auto bytes = movement::encode(packet(1));
            p.client->test_block_writes(1, true);
            check(p.client->send(bytes.data(), bytes.size()));
            check(p.client->send_stats().want_read == 1);
            const auto until = Clock::now() + std::chrono::milliseconds(40);
            while (Clock::now() < until) { p.client->poll(); Sleep(1); }
            check(p.client->send_stats().write_attempts == 1); // No busy polling of SSL_write.
            check(p.server->send(bytes.data(), bytes.size())); p.relay();
            bool received_reply = false;
            p.until([&] {
                auto replies = p.client->poll(); received_reply = received_reply || !replies.empty();
                return p.client->send_stats().records_sent == 1 && received_reply;
            });
            auto records = receive(p); check(records.size() == 1);
            check(records[0] == Record(bytes.begin(), bytes.end()));
            check(p.client->send_stats().write_attempts == 2);
            report_backpressure("want_read_recovery", *p.client);
        }},
        {"persistent_want_read_expires_without_spinning", [] {
            Pair p; p.handshake(); const auto bytes = movement::encode(packet(1));
            p.client->test_block_writes((std::numeric_limits<unsigned>::max)(), true);
            check(p.client->send(bytes.data(), bytes.size()));
            const auto start = Clock::now();
            must_throw([&] { p.until([&] { p.client->poll(); return false; }, 3000); });
            check(Clock::now() - start >= std::chrono::seconds(2));
            check(!p.client->ready() && p.client->send_stats().timeouts == 1);
            check(p.client->send_stats().write_attempts == 1);
            must_throw([&] { p.client->send(bytes.data(), bytes.size()); });
            must_throw([&] { p.client->poll(); });
            report_backpressure("want_read_timeout", *p.client);
        }},
        {"persistent_want_write_is_bounded_and_terminal", [] {
            Pair p; p.handshake(); const auto bytes = movement::encode(packet(1));
            p.client->test_block_writes((std::numeric_limits<unsigned>::max)());
            check(p.client->send(bytes.data(), bytes.size()));
            must_throw([&] { p.until([&] { p.client->poll(); return false; }, 3000); });
            check(!p.client->ready() && p.client->send_stats().timeouts == 1);
            check(p.client->send_stats().write_attempts > 1 && p.client->send_stats().write_attempts <= 128);
            check(p.client->send_stats().pending_bytes == movement::PacketSize);
            check(p.client->send_stats().records_sent == 0);
            must_throw([&] { p.client->send(bytes.data(), bytes.size()); });
            report_backpressure("want_write_timeout", *p.client);
        }},
        {"fatal_initial_write_is_not_retried", [] {
            Pair p; p.handshake(); const auto bytes = movement::encode(packet(1));
            p.client->test_block_writes(1, false, true);
            must_throw([&] { p.client->send(bytes.data(), bytes.size()); });
            check(!p.client->ready() && p.client->send_stats().failures == 1);
            must_throw([&] { p.client->poll(); });
            check(p.client->send_stats().write_attempts == 1);
            report_backpressure("fatal_initial_write", *p.client);
        }},
        {"fatal_retry_stays_terminal", [] {
            Pair p; p.handshake(); const auto bytes = movement::encode(packet(1));
            p.client->test_block_writes(1);
            check(p.client->send(bytes.data(), bytes.size()));
            p.client->test_block_writes(1, false, true);
            must_throw([&] { p.until([&] { p.client->poll(); return false; }, 1000); });
            check(!p.client->ready() && p.client->send_stats().failures == 1 && p.client->send_stats().timeouts == 0);
            must_throw([&] { p.client->poll(); });
            check(p.client->send_stats().write_attempts == 2);
            report_backpressure("fatal_retry", *p.client);
        }},
        {"invalid_send_does_not_allocate_pending_slot", [] {
            Pair p; p.handshake(); std::array<unsigned char, 1201> bytes{};
            check(!p.client->send(nullptr, 68)); check(!p.client->send(bytes.data(), 0));
            check(!p.client->send(bytes.data(), bytes.size()));
            check(p.client->send_stats().pending_bytes == 0 && p.client->send_stats().write_attempts == 0);
        }},
        {"fixed_delay_both_directions", [] {
            Pair::Profile profile; profile.delay_ms = 40;
            exercise_profile("fixed_delay", profile);
        }},
        {"seeded_variable_delay_both_directions", [] {
            Pair::Profile profile; profile.delay_ms = 10; profile.jitter_ms = 60;
            exercise_profile("variable_delay", profile);
        }},
        {"burst_loss_both_directions", [] {
            Pair::Profile profile; profile.burst_period = 10; profile.burst_length = 3;
            exercise_profile("burst_loss", profile);
        }},
        {"duplicated_records_both_directions", [] {
            Pair::Profile profile; profile.duplicate_every = 2;
            exercise_profile("duplicates", profile);
        }},
        {"reordering_both_directions", [] {
            Pair::Profile profile; profile.reorder_every = 2;
            exercise_profile("reordering", profile);
        }},
        {"temporary_blackout_both_directions", [] {
            Pair::Profile profile; profile.blackout_at_ms = 150; profile.blackout_ms = 200;
            exercise_profile("blackout", profile);
        }},
        {"bandwidth_limit_and_bounded_relay_queue", [] {
            Pair::Profile profile; profile.bytes_per_second = 1000; profile.queue_limit = 4;
            exercise_profile("bandwidth", profile);
        }},
        {"eight_channels_combined_seeded_adversity", [stress_ms] {
            Pair::Profile profile; profile.delay_ms = 10; profile.jitter_ms = 30;
            profile.drop_every = 7; profile.burst_period = 10; profile.burst_length = 2;
            profile.duplicate_every = 3; profile.reorder_every = 4;
            profile.blackout_at_ms = 600; profile.blackout_ms = 200;
            exercise_profile("combined_eight_channels", profile, 8, stress_ms);
        }}
    };
    unsigned failed = 0;
    for (const auto &[name, test] : tests)
    {
        try { test(); std::cout << "PASS " << name << std::endl; }
        catch (const std::exception &e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << std::endl; }
    }
    return failed ? 1 : 0;
}
