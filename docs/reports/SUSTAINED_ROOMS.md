# Complete-room memory, queues and update measurements - 2026-10-17

All three native-room scenarios passed: 2, 4 and 8 automatic clients, each
running for 60 seconds on loopback TCP/TLS + UDP/DTLS, with a fresh host per room.
Memory was sampled once per second. The full CTest run completed before this
measurement; no regression suite was launched concurrently by the runner.

Raw evidence: [schema-2 JSON](movement-sustained-2026-10-17.json).
Definitions and repeat commands: [metrics guide](../METRICS_LAB.md).

| Clients | Simultaneous overlap (s) | Host peak working set (MiB) | Host peak private bytes (MiB) | Largest client peak working set (MiB) | Largest client peak private bytes (MiB) |
| --- | --- | --- | --- | --- | --- |
| 2 | 59.957 | 11.918 | 2.391 | 11.715 | 2.234 |
| 4 | 59.949 | 12.273 | 2.855 | 11.715 | 2.234 |
| 8 | 59.914 | 12.848 | 3.637 | 11.715 | 2.238 |

These are sampled peaks for individual processes, not summed room memory.
MiB means 1048576 bytes. The raw report includes each process's timestamped
samples, first/last/delta values and the OS lifetime peak working set. Startup
is included; these measurements do not establish a steady-state growth rate or
prove the absence of a memory leak.

| Clients | Largest TCP control queue (bytes) | Largest DTLS pending write (bytes) | Client fresh-update interval mean (ms) | Largest client fresh-update interval (ms) |
| --- | --- | --- | --- | --- |
| 2 | 84 | 68 | 46.630 | 82.307 |
| 4 | 116 | 68 | 62.170 | 83.057 |
| 8 | 149 | 68 | 124.337 | 142.434 |

Queue peaks are the largest per-connection high-water marks across the host and
clients. They measure application output after movement-link creation, excluding
TLS encryption buffers and kernel socket queues. Limits remain 65536 bytes for
the TCP output queue and one immutable movement payload for pending DTLS writes.
The interval mean is the arithmetic mean of each client's pooled per-entity
interval mean. It is fresh-state cadence, not network latency. Fixed interval
histograms, sampled update silence and periodic queue values remain in the JSON.

Every client completed its requested duration, observed all participants moving,
and emitted one final report with RTT samples. Every host connection emitted a
final report. There were zero recorded DTLS send failures, DTLS send timeouts or
control-queue rejections, and no process diagnostics or unexpected exits. The
host was terminated by benchmark cleanup after its guests left; that exit is
explicitly labeled in the report.

Validation: 96 cases across eight CTest suites passed, including actual Windows
memory sampling for all three room sizes, queue/histogram accounting, and failure
report preservation when the host executable rejects startup.

Build: existing MinGW Debug `build-v04`, Windows. Executable SHA-256:
`2046db3d9f333e414853b6bfe8d000a97617d793aa506d727168b2afda7b8a45`.

To repeat this run, use a new output path:

```powershell
python tools/movement_benchmark.py --seconds 60 --output docs/reports/my-room-run.json
```

Omit `--seconds` for five minutes per room; up to 3600 seconds is supported.
This one-minute measurement validates the new collection on complete native
rooms. Longer soak tests, faults applied to complete rooms, LAN tests and game
integration remain separate work. No gameplay capacity or latency budget is
established here.
