# Adverse conditions on complete native rooms

This runner starts a real host and 2, 4 or 8 real automatic clients, retaining
room admission, TLS exporter binding, DTLS handshake, host-owned world state,
replicas and normal disconnect cleanup. A loopback relay on every connection
delays/drops/reorders **encrypted UDP datagrams** in both directions. TCP/TLS
control, RTT probes and room liveness remain unaffected.

## Run

```powershell
cmake --build build-v04 --parallel 2
python tools/room_fault_benchmark.py --output docs/reports/my-room-faults.json
```

The default matrix is eight profiles times three room sizes: 24 rooms of
15 seconds each, run sequentially (about six minutes plus preparation/cleanup).
It prints the current room, phase and progress every ten seconds, and saves the
JSON after every room. A nonzero exit means at least one room failed. Existing
reports are preserved; always choose a new filename.

For a longer focused run:

```powershell
python tools/room_fault_benchmark.py --seconds 60 --profiles delay loss blackout combined --output docs/reports/my-longer-room-faults.json
```

Use `--clients 8` for one room size, `--seed INTEGER` for another fault seed,
`--sample-seconds 1` for memory polling, and `--openssl PATH` or
`--tls-library ABSOLUTE_DIRECTORY` for dependencies. Durations are 10-3600 seconds
per room. The duration includes clean startup/warmup, faults and clean recovery;
the fault window is 20%-60% of the duration after every participant is ready
and has observed all entities. Each client's native timer starts at UDP readiness.

| Profile | Conditions during the fault window, per direction |
| --- | --- |
| `delay` | 60 ms additional delay |
| `jitter` | Seeded delay from 10 to 100 ms |
| `loss` | Drop the first three of each ten encrypted datagrams |
| `duplication` | Inject an additional copy of each fourth datagram, 5 ms later |
| `reordering` | Add 250 ms to each third datagram |
| `blackout` | Drop all UDP traffic throughout the fault window |
| `bandwidth` | Serialize encrypted datagrams at 800 B/s, dropping when the relay queue is full |
| `combined` | Delay 20-80 ms; drop two of every twelve datagrams; duplicate every fifth; delay every fourth an extra 250 ms; 4000 B/s; blackout through the middle 30% of the fault window |

Every direction has a bounded queue of 16 datagrams. Delay and bandwidth clocks
use one local monotonic clock, and arrival scheduling happens after bandwidth
serialization so reordering remains possible in the combined profile. The seed
controls random draws; process scheduling, room registration order and datagram
counts remain dependent on the OS. This is a scoped simulation, not a full
network emulator or a congestion-control model.

## Checks and reports

The runner reuses [the movement benchmark's checks and memory/queue metrics](METRICS_LAB.md).
It additionally requires:

- Every original native UDP endpoint registered with the relay: two per client.
- Traffic affected in both directions of every connection, with actual counters
  for the profile's drops, duplication, reordering or queue overflows.
- Delivered datagrams respect the configured delay floor.
- Every client observes a changing strictly newer revision for **every entity**
  within three seconds of the end of the fault window. This is a functional
  recovery budget for these test profiles, not a gameplay latency target.
- Every applied `UDP_POSITION` strictly advances the entity's revision throughout
  the run, including late encrypted records delivered during recovery.
- No terminal DTLS write failure/timeout, control-queue rejection, process
  diagnostic, unexpected process exit or missing connection-final report.

`network_faults` records the phase schedule, recovery time for all entity/client
pairs and per-direction actual traffic/drop/reorder/queue/delivery-delay counters.
`process_runs` contains memory histories and queue/failure summaries. Periodic
and final connection metrics remain in `host_metrics` and `client_runs`.
The relay's encrypted-byte queue is separate from the native TCP control queue
and native DTLS pending-write slot. The Python runner/relay memory is not included
in the measured child-process memory. Per-direction counters include setup and
cleanup; `fault_received` identifies records arriving during the fault window.

Reordered encrypted records may be rejected by DTLS anti-replay before reaching
the application. A successful run proves no applied rollback was observed; it
does not imply every injected stale record reached the application's sequence
filter. Absolute positions make missing intermediate movement updates disposable.

## Isolation and limits

`dsr_net_fault_lab.exe` is built only with `BUILD_TESTING=ON`. It uses the same
host/client core, with a compile-time `DSR_ROOM_TESTING` hook in endpoint setup.
Only this executable accepts the runner's `DSR_LAB_RELAY_PORT` environment setting.
The ordinary `dsr_net.exe` has no relay hook. A regression verifies that selecting
the ordinary executable fails the benchmark instead of silently bypassing faults.

Both the native hook and relay require exact `127.0.0.1` endpoints. The relay
controller exchanges only original UDP port numbers and mapped ports; TLS keys,
control messages and decrypted movement never pass through it. UDP endpoint
negotiation and the wire payload formats are unchanged. The runner creates only
its own temporary identities, processes and local sockets, and cleans them up.
It does not configure the firewall, adapters, routers or other applications.

This covers temporary movement-channel interruptions while control stays alive.
TCP outages, host crashes, reconnecting after a terminal channel failure, LAN,
Internet connectivity and game integration are separate scenarios. The existing
isolated [DTLS robustness tests](ROBUSTNESS_LAB.md) remain useful for low-level
write backpressure and malformed/replayed records.

Regression command:

```powershell
ctest --test-dir build-v04 -R room_fault_integration --output-on-failure
```

The regression exercises combined faults at all three room sizes, full blackouts
and bandwidth exhaustion, plus rejection of the ordinary application binary.

The recorded complete eight-profile matrix passed all 24 scenarios; see
[the results and raw evidence](reports/ROOM_FAULT_RESULTS.md).
