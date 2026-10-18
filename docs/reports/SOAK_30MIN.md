# Thirty-minute complete-room runs - 2026-10-17/18

All three rooms passed 1800 seconds per client, sequentially: 2, 4 and 8 clients,
about 90 minutes total. This completes the requested 5-30-minute local-room
measurement and analysis. Transport: loopback TCP/TLS + UDP/DTLS on Windows 11.

Evidence: [original schema-2 report](salas-30min.json), created at
2026-10-18 01:41:22 UTC (2026-10-17 22:41:22 America/Bahia).
Executable SHA-256: `2046db3d9f333e414853b6bfe8d000a97617d793aa506d727168b2afda7b8a45`.
Definitions: [metrics guide](../METRICS_LAB.md).

| Clients | Simultaneous overlap (s) | Peak host private memory (MiB) | Largest client private-memory peak (MiB) | Host private-memory median change (KiB) |
| --- | --- | --- | --- | --- |
| 2 | 1799.964 | 2.387 | 2.223 | +4 |
| 4 | 1799.935 | 2.840 | 2.230 | +8 |
| 8 | 1799.908 | 3.719 | 2.234 | 0 |

Memory analysis excludes the first 60 seconds for trend calculations. Median
change compares samples at 60-360 seconds with those at 1500 seconds onward.
Client median changes ranged from -32 to -16 KiB. Linear least-squares slopes
over all samples after 60 seconds were negative for every process: hosts about
-0.57 to -0.78 KiB/minute; clients about -1.23 to -1.75 KiB/minute. Thus there is
no observed sustained private-memory growth in these runs. This does not prove
the absence of leaks in other workloads. Peak working sets were 11.973, 12.328
and 13.055 MiB for the hosts, with all clients at or below 11.805 MiB.

| Clients | Largest TCP queue peak (bytes) | Largest DTLS pending write (bytes) | Mean client fresh-update interval (ms) | Largest client interval (ms) | Largest sampled client silence (ms) |
| --- | --- | --- | --- | --- | --- |
| 2 | 84 | 68 | 46.710 | 80.889 | 62.913 |
| 4 | 115 | 68 | 62.235 | 96.129 | 64.145 |
| 8 | 180 | 68 | 124.444 | 159.367 | 127.472 |

Queue peaks are per connection, across both host and clients, and captured at
enqueue. Periodic reports observed at most 5 current TCP queue bytes in each
scenario; every final queue was empty. There is no evidence of accumulating
application output. These measurements exclude TLS buffers and kernel queues.

Interval means pool final client counters weighted by sample count. Histograms
contain 154140, 462751 and 925661 fresh intervals respectively; none exceeded
200 ms. At eight clients, 925650 of 925661 intervals were in the (100, 200] ms
bucket. The cadence is consistent with the earlier one-minute measurements;
this is per-entity update spacing, not one-way latency or an accepted gameplay
target. Sampled silence is observed about once per second and is not an exact
maximum of all between-report pauses. Completed interval maxima cover gaps
between successive fresh updates.

All 14 clients exited normally, observed all trajectories moving, and produced
final reports. All 14 host connections also produced final reports. There were
zero DTLS send failures, DTLS send timeouts, control-queue rejections or process
diagnostics. Each host's exit code 1 is explicitly labeled `benchmark_cleanup`:
the runner terminated the otherwise still-running host after guests left.

The user reported opening another VS Code instance during the run, consuming
approximately 2 GB RAM. The collector does not measure total system pressure,
CPU or paging, so no causal impact can be attributed or ruled out. That memory
is not included in the child-process memory totals. No harmful effect is
apparent in the recorded failure, queue and update-interval measurements.

Remaining work: adverse conditions applied to complete rooms, validation on two
LAN PCs, and explicit performance budgets based on requirements and measured
behavior. These runs do not validate Internet connectivity or gameplay.
