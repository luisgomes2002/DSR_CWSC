# Automatic movement and metrics

The v0.4 transport now supports deterministic synthetic trajectories and optional
per-connection measurements. No game process is involved. Wire formats remain
unchanged; RTT is measured using the existing TCP/TLS PING/PONG exchange.

## Reproduce the benchmark

Build the current executable, then run from the repository root:

```powershell
cmake --build build-v04 --parallel 2
python tools/movement_benchmark.py --seconds 15 --output docs/reports/my-movement-run.json
```

The script creates a temporary identity outside the repository and random test
passwords, starts a fresh loopback host for each of 2, 4 and 8 clients, and cleans
up its own processes and private files. It does not overwrite existing reports.
Use `--exe PATH`, `--openssl PATH` and `--tls-library ABSOLUTE_DIRECTORY` for
alternate local dependencies. `--clients 8 --seconds 60` selects a longer single
scenario; durations accepted by the runner are 4-3600 seconds.

For a sustained run of all three room sizes (five minutes per room by default):

```powershell
python tools/movement_benchmark.py --output docs/reports/my-sustained-run.json
# Explicit duration and memory sampling interval, in seconds:
python tools/movement_benchmark.py --seconds 600 --sample-seconds 1 --output docs/reports/my-ten-minute-run.json
```

Rooms run sequentially. Budget about three times `--seconds`, plus startup and
cleanup. Memory sampling accepts intervals from 0.1 to 10 seconds (default 1).
Each report is saved after each room, preserving completed scenarios if a later
room is interrupted. Choose a new output filename on each invocation.

Every client must finish successfully, reach UDP_ACTIVE, collect at least two
RTT samples, and observe changing trajectories from every participant. The
simultaneous-client overlap must exceed half the requested duration. The runner
also checks that every host connection produces a final report and that payload
byte counts agree with packet counts. These are functional checks, not latency
or capacity approval thresholds. A failed scenario returns a nonzero exit code
and records diagnostics in the JSON report.
Early completion, missing memory samples, memory-query errors, DTLS send
failures/timeouts and rejected control writes also fail a scenario. A nonzero
host exit caused by the runner's normal cleanup is labeled `benchmark_cleanup`;
the host is expected to stay alive after clients leave and has no automatic exit.

The report contains all periodic and final metrics, final observed positions,
client exit codes, overlap duration, parameters, OS/Python versions and the SHA-256
of the executable. Compare results only after considering machine load and build
settings. Instrumentation and console output affect these measurements.

The short automated regression is included in CTest as `benchmark_integration`.
It uses temporary reports and runs all three counts for four seconds each.

Schema 2 adds `process_runs` for the host and every client: PID, exit status,
bounded diagnostics, periodic `memory_samples`, `memory_summary` and
`metrics_summary`. Memory uses Windows `GetProcessMemoryInfo` on the child process
handle: working set is resident memory, and private bytes are private committed
memory. Reports include first/last values, sampled peaks and last-minus-first
growth, plus the OS lifetime peak working set in each sample. Initial samples
include startup and do not establish a steady-state baseline. Growth alone does
not prove a leak; working sets can be trimmed by Windows. Processes can exit
between polls, so sampled peaks may miss short spikes. Memory histories and
periodic connection metrics grow with run duration in the Python runner; runner
memory is outside the measured child processes.

Metric summaries aggregate the latest counters per connection, including partial
reports on failure, without summing repeated cumulative snapshots. Histograms
pool per-entity intervals and keep fixed buckets rather than individual samples.
No automatic latency or memory budget is imposed: use the recorded measurements
to choose targets. Diagnostic lists retain at most 100 lines per process.

## Manual use

Add `--metrics` to both the existing UDP host and client commands to emit local
`METRICS<TAB>{JSON}` lines about once per second and a final cumulative report
when a connection object is destroyed. The host reports each client separately.
For example, after creating the identity and setting the passwords as described
in the README:

```powershell
.\build-v04\dsr_net.exe host --port 27034 --max-players 8 --tls-pfx "$dsrIdentity\host.pfx" --udp --metrics
# In a separate terminal with the room password and actual host pin:
.\build-v04\dsr_net.exe join --port 27034 --name Auto1 --tls-pin $dsrPin --udp --metrics --auto-seconds 15
```

`--auto-seconds` is client-only, requires UDP/TLS, and accepts 1-3600 seconds.
The timer starts after channel readiness, independently for each client. EOF on
stdin does not end an automatic run; `/quit` can still end one early. At the end
of its duration the client sends QUIT normally. Movement follows
`phase = 0.8 * elapsed_seconds + player_id`, with
`(x,y,z) = (10*cos(phase), 2*sin(phase/2), 10*sin(phase))`.
The existing publisher sends the latest proposed position no more often than
every 34 ms; the event loop and OS scheduling can make this slower.

## Meaning of the measurements

All timing uses each process's monotonic clock. No comparison of absolute clocks
between machines is made, and no one-way latency is inferred.

| Field | Definition |
| --- | --- |
| `tcp_rtt_*` | Client-local time from queueing PING until consuming PONG, sampled about once per second with at most one request outstanding. Includes local scheduling, queueing, host processing and both TCP/TLS directions. Host samples are zero: the host does not initiate probes. A zero mean with zero samples means unavailable. |
| `tcp_rtt_variation_mean_ms` | Mean absolute difference between successive RTT samples. This describes observed RTT variation, not isolated network jitter. |
| `udp_payload_sent/received` | Completed SSL application writes (immediate or retried) and decoded, session-bound received movement records. Admission into a pending slot is not counted as sent. Reception is counted after codec/identity and input rate checks, before replica/ownership decisions. These are not raw socket or DTLS datagram counters. |
| `payload_*_bytes` | Those counts multiplied by the fixed 68-byte movement payload size. Excludes control messages, handshakes, record headers, tags and IP/UDP/TCP overhead. |
| `payload_tx/rx_Bps` | Cumulative average payload bytes per second since construction of the connection's movement link, including negotiation. |
| `payload_tx/rx_window_Bps`, `window_s` | Payload rate since the preceding report, with its actual interval. The final interval can be shorter than one second. |
| `update_interval_*` | Intervals between strictly newer received sequences for the same entity, pooled across entities. Client measurements use state revisions; host measurements use owner inputs. Repeated/older revisions do not refresh the timer. These reflect fresh-state cadence, including scheduling and publication choices. |
| `update_interval_histogram` | Cumulative counts with inclusive upper bounds 20, 50, 100, 200, 500, 1000, 2000 and 5000 ms; ninth bucket is above 5000 ms. Counts sum to `update_interval_samples`. No exact percentiles are inferred from buckets. |
| `control_queue_bytes`, `control_queue_peak_bytes`, `control_queue_rejections` | Current application TCP output bytes, exact high-water mark at enqueue, and rejected enqueue attempts since movement-link creation. Queue limit is 65536 bytes. Current size is refreshed each event-loop turn; peak persists after draining. Excludes encrypted TLS buffers and OS socket queues; admission before link creation is outside this measurement. |
| `dtls_pending_bytes`, `dtls_peak_pending_bytes` | Current and peak pending application DTLS write bytes, separate from the reliable-control queue. |
| `update_interval_variation_mean_ms` | Mean absolute difference of consecutive fresh-update intervals, calculated within each entity and then pooled. |
| `current_silence_max_ms` | Largest current time since a tracked entity's last newer sequence. Tracking starts with its first decoded packet; client DESPAWN removes its cursor. Late/unknown decoded states can create a measurement cursor even though the replica rejects them. At most 32 entities are retained. This exposes pauses even before another update arrives. |
| `duplicate_revision`, `stale_revision` | Equality or decrease relative to the highest observed sequence, for the relevant direction/owner. Equality can simply mean the host republished an unchanged state; it does not prove duplicated network packets. DTLS may discard replayed records before this layer. |
| `input_sequence_gaps_observed` | Host-only sum of jumps above the input high-water mark, excluding the first packet. There is no waiting/recovery window: later lower sequences remain classified as stale and do not subtract from this count. Gaps can reflect reordering or discarding, so this is not a packet-loss estimate. State revision gaps are never counted, since world revisions are shared across entities. |

Only recognized movement-direction packets affect interval/sequence statistics.
RTT and interval aggregates retain counts, sums, maxima and previous samples;
they do not store an unbounded history. All reports are cumulative unless a field
explicitly refers to a window or current silence. Abruptly terminating a process
may prevent a final report; the runner treats missing finals as failure.

## Baseline and next measurements

The recorded baseline is summarized in
[the loopback report](reports/MOVEMENT_BASELINE.md). It measures short local runs,
not LAN/Internet connectivity or game capacity. The existing host event-loop
polling and round-robin publication can dominate per-entity update intervals as
client counts rise.

The extended collector was also validated with one minute per complete room;
see [memory, queue and interval results](reports/SUSTAINED_ROOMS.md) and its raw
schema-2 report. This is longer than the initial baseline but is not a prolonged soak test.

Subsequent 30-minute runs for all three room sizes are analyzed in
[the soak report](reports/SOAK_30MIN.md), including memory trends, queue peaks,
update pauses and the user's reported concurrent VS Code activity.

Isolated adverse-network simulation and pending-write/failure instrumentation
are now implemented; see [the robustness guide](ROBUSTNESS_LAB.md).
Sustained native-room runs now record process memory, application queue occupancy,
interval distributions and failures. Complete-room fault injection is also implemented;
see [the full-room fault guide](ROOM_FAULT_LAB.md) and
[recorded matrix](reports/ROOM_FAULT_RESULTS.md). Next work is expanded fault scenarios,
LAN validation and selection of performance targets using the measurements and
future synchronization needs. No loss percentage, congestion-control validation
or playable-game capacity is claimed by this benchmark.
