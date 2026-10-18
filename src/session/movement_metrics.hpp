#pragma once

#include "protocol/movement_packet.hpp"
#include "net/dtls.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <array>

namespace dsr
{
    // All times use the local monotonic clock. Constant memory per connection.
    struct MetricSamples
    {
        unsigned long long count = 0;
        double sum = 0, maximum = 0, previous = 0, variation_sum = 0;
        void add(double value)
        {
            if (count)
                variation_sum += std::abs(value - previous);
            ++count;
            sum += value;
            maximum = (std::max)(maximum, value);
            previous = value;
        }
        double mean() const { return count ? sum / static_cast<double>(count) : 0; }
        double variation() const { return count > 1 ? variation_sum / static_cast<double>(count - 1) : 0; }
    };

    class MovementMetrics
    {
        using Clock = std::chrono::steady_clock;
        struct Cursor
        {
            std::uint64_t sequence;
            Clock::time_point last;
            double previous_interval = -1;
        };
        std::map<unsigned, Cursor> cursors_;
        Clock::time_point started_ = Clock::now(), reported_ = started_;
        bool enabled_ = false, server_ = false;
        unsigned owner_ = 0;
        std::uint64_t sent_ = 0, received_ = 0, stale_ = 0, duplicates_ = 0, input_gaps_ = 0;
        std::uint64_t reported_sent_ = 0, reported_received_ = 0;
        MetricSamples intervals_, interval_variation_, rtt_;
        DtlsSendStats transport_;
        std::size_t control_queue_bytes_ = 0, control_queue_peak_ = 0;
        std::uint64_t control_queue_rejections_ = 0;
        // Fixed upper bounds in milliseconds; last bucket is overflow.
        std::array<std::uint64_t, 9> interval_buckets_{};

    public:
        void enable(bool server, unsigned owner)
        {
            enabled_ = true;
            server_ = server;
            owner_ = owner;
        }
        void sent() { ++sent_; }
        void transport(const DtlsSendStats &stats)
        {
            transport_ = stats;
            sent_ = stats.records_sent;
        }
        void rtt(double ms) { rtt_.add(ms); }
        void control_queue(std::size_t bytes, bool rejected = false)
        {
            control_queue_bytes_ = bytes;
            control_queue_peak_ = (std::max)(control_queue_peak_, bytes);
            if (rejected)
                ++control_queue_rejections_;
        }
        void received(const movement::Packet &p, Clock::time_point now = Clock::now())
        {
            ++received_;
            if (!enabled_)
                return;
            if (server_ ? (p.kind != movement::Kind::Input || p.entity != owner_) : p.kind != movement::Kind::State)
                return;
            auto it = cursors_.find(p.entity);
            if (it == cursors_.end())
            {
                if (cursors_.size() >= 32)
                    cursors_.erase(cursors_.begin());
                cursors_.emplace(p.entity, Cursor{p.sequence, now});
                return;
            }
            auto &c = it->second;
            if (p.sequence == c.sequence)
            {
                ++duplicates_;
                return;
            }
            if (p.sequence < c.sequence)
            {
                ++stale_;
                return;
            }
            if (server_ && p.kind == movement::Kind::Input)
                input_gaps_ += p.sequence - c.sequence - 1;
            const double ms = std::chrono::duration<double, std::milli>(now - c.last).count();
            intervals_.add(ms);
            constexpr double bounds[] = {20, 50, 100, 200, 500, 1000, 2000, 5000};
            std::size_t bucket = 0;
            while (bucket < 8 && ms > bounds[bucket])
                ++bucket;
            ++interval_buckets_[bucket];
            if (c.previous_interval >= 0)
                interval_variation_.add(std::abs(ms - c.previous_interval));
            c = {p.sequence, now, ms};
        }
        void forget(unsigned entity) { cursors_.erase(entity); }
        void report(bool final = false)
        {
            const auto now = Clock::now();
            if (!enabled_ || (!final && now - reported_ < std::chrono::seconds(1)))
                return;
            const double window_seconds = (std::max)(0.001, std::chrono::duration<double>(now - reported_).count());
            const double seconds = (std::max)(0.001, std::chrono::duration<double>(now - started_).count());
            double silence = 0;
            for (const auto &entry : cursors_)
                silence = (std::max)(silence, std::chrono::duration<double, std::milli>(now - entry.second.last).count());
            std::cout << "METRICS\t{\"side\":\"" << (server_ ? "host" : "client")
                      << "\",\"player\":" << owner_ << ",\"final\":" << (final ? "true" : "false")
                      << ",\"elapsed_s\":" << seconds << ",\"udp_payload_sent\":" << sent_
                      << ",\"udp_payload_received\":" << received_
                      << ",\"payload_sent_bytes\":" << sent_ * movement::PacketSize
                      << ",\"payload_received_bytes\":" << received_ * movement::PacketSize
                      << ",\"payload_tx_Bps\":" << static_cast<double>(sent_ * movement::PacketSize) / seconds
                      << ",\"payload_rx_Bps\":" << static_cast<double>(received_ * movement::PacketSize) / seconds
                      << ",\"window_s\":" << window_seconds
                      << ",\"payload_tx_window_Bps\":" << static_cast<double>((sent_ - reported_sent_) * movement::PacketSize) / window_seconds
                      << ",\"payload_rx_window_Bps\":" << static_cast<double>((received_ - reported_received_) * movement::PacketSize) / window_seconds
                      << ",\"dtls_write_attempts\":" << transport_.write_attempts
                      << ",\"dtls_want_read\":" << transport_.want_read
                      << ",\"dtls_want_write\":" << transport_.want_write
                      << ",\"dtls_send_suppressed\":" << transport_.suppressed
                      << ",\"dtls_send_failures\":" << transport_.failures
                      << ",\"dtls_send_timeouts\":" << transport_.timeouts
                      << ",\"dtls_pending_bytes\":" << transport_.pending_bytes
                      << ",\"dtls_peak_pending_bytes\":" << transport_.peak_pending_bytes
                      << ",\"control_queue_bytes\":" << control_queue_bytes_
                      << ",\"control_queue_peak_bytes\":" << control_queue_peak_
                      << ",\"control_queue_rejections\":" << control_queue_rejections_
                      << ",\"duplicate_revision\":" << duplicates_ << ",\"stale_revision\":" << stale_
                      << ",\"input_sequence_gaps_observed\":" << input_gaps_
                      << ",\"update_interval_samples\":" << intervals_.count
                      << ",\"update_interval_mean_ms\":" << intervals_.mean()
                      << ",\"update_interval_max_ms\":" << intervals_.maximum
                      << ",\"update_interval_variation_mean_ms\":" << interval_variation_.mean()
                      << ",\"current_silence_max_ms\":" << silence
                      << ",\"tcp_rtt_samples\":" << rtt_.count << ",\"tcp_rtt_mean_ms\":" << rtt_.mean()
                      << ",\"tcp_rtt_max_ms\":" << rtt_.maximum
                      << ",\"tcp_rtt_variation_mean_ms\":" << rtt_.variation()
                      << ",\"update_interval_histogram\":[";
            for (std::size_t i = 0; i < interval_buckets_.size(); ++i)
                std::cout << (i ? "," : "") << interval_buckets_[i];
            std::cout << "]}" << std::endl;
            reported_ = now;
            reported_sent_ = sent_;
            reported_received_ = received_;
        }
    };
} // namespace dsr
