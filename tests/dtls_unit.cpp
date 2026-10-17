#include "net/dtls.hpp"
#include "protocol/movement_packet.hpp"
#include "protocol/movement_stream.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace dsr;
using Clock = std::chrono::steady_clock;
using Record = std::vector<unsigned char>;
void check(bool ok) { if (!ok) throw std::runtime_error("Verificacao falhou"); }
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
    Socket client_socket, server_socket, upstream, downstream;
    std::unique_ptr<DtlsChannel> client, server;
    sockaddr_in client_addr{}, server_addr{};
    bool hold_client = false;
    std::vector<Record> captured, held;
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
                if (from_client)
                {
                    captured.push_back(record);
                    if (hold_client) { held.push_back(record); continue; }
                }
                raw_send(from_client ? downstream.value : upstream.value, record, from_client ? server_addr : client_addr);
            }
        }
    }
    template <typename F> void until(F done, unsigned timeout_ms = 6000)
    {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        while (!done())
        {
            if (Clock::now() >= deadline) throw std::runtime_error("Timeout do teste DTLS");
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
} // namespace
int main()
{
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
