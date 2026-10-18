# Synthetic movement baseline — 2026-10-17

Automatic movement and measurements passed with **2, 4 and 8 clients**, using
the current TCP/TLS + UDP/DTLS transport on one Windows 11 PC. Each scenario ran
for 15 seconds per client, with a fresh host and deterministic trajectories.
Every client observed every participant's trajectory changing. All processes
completed normally and all host connections emitted final reports.

Raw evidence: [JSON report](movement-loopback-baseline-2026-10-17.json).
Definitions and usage: [metrics guide](../METRICS_LAB.md).

| Clients | Simultaneous overlap (s) | TCP RTT mean (ms) | RTT variation (ms) | Fresh update interval per entity (ms) | Update interval variation (ms) | Largest update interval (ms) |
| --- | --- | --- | --- | --- | --- | --- |
| 2 | 14.99 | 0.445 | 0.099 | 46.747 | 27.748 | 79.970 |
| 4 | 14.95 | 0.538 | 0.154 | 62.252 | 3.702 | 114.286 |
| 8 | 14.94 | 0.764 | 0.155 | 124.583 | 2.952 | 151.308 |

Means and variations above are arithmetic averages of the final per-client
aggregates. The maximum is the largest observed client interval. Update
intervals are measured within each entity before pooling; they are not the
spacing between arbitrary UDP records. Variation is the mean absolute difference
between consecutive observations, not a network-only delay estimate.

| Clients | Mean client TX payload (B/s) | Mean client RX payload (B/s) | Sum host TX payload (B/s) | Observed input sequence gaps |
| --- | --- | --- | --- | --- |
| 2 | 1451.04 | 4344.06 | 8697.51 | 0 |
| 4 | 1456.97 | 4367.51 | 17489.62 | 0 |
| 8 | 1457.15 | 4368.60 | 34978.13 | 0 |

Rates are cumulative payload-only averages since movement-link construction,
including negotiation. Their denominators differ slightly between processes.
Periodic window rates and current silence are included in the raw report.
These counts exclude encryption/protocol overhead and control traffic. Zero
observed sequence gaps is not proof of zero network packet loss.

The increase in per-entity update interval is relevant to the next robustness
and smoothing work: eight clients received new states for a given entity about
every 125 ms on average despite sub-millisecond TCP RTT. The current host polls
with a 10 ms timeout and publishes one entity per recipient per turn, cycling
through the players. Publication cadence, scheduling and unchanged revisions
can therefore dominate these intervals. The measurements do not isolate the
cost of each factor and do not establish a gameplay performance target.

Validation of the final build: **78 cases across 8 CTest suites passed**, including
two metric unit cases and the automatic 2/4/8-client regression. The recorded
15-second baseline ran after CTest completed, with no regression suite launched
in parallel by the benchmark. Build: MinGW Debug, existing `build-v04` toolchain.

Executable SHA-256:
`1a93cd110b61ff91a06c3a41a8d18573d23a3b09da127c6e936d2ce6ea33518c`.

To repeat with a new report path:

```powershell
python tools/movement_benchmark.py --seconds 15 --output docs/reports/my-movement-run.json
```

These are short synthetic loopback runs. LAN, adverse-network behavior,
sustained load and game integration remain separate milestones.
