# Robustness run — 2026-10-17

**22 DTLS cases passed**, including eight adverse-network profiles and six
recorded application-write fault scenarios. The combined profile ran for
**30 seconds with eight independent bidirectional DTLS pairs**. All profiles
recovered to the final absolute state, rejected older revisions, and prevented
delayed states from recreating a removed entity.

Raw evidence: [JSON report](robustness-loopback-2026-10-17-final.json).
Methodology, definitions and commands: [robustness guide](../ROBUSTNESS_LAB.md).

| Profile | Pairs | Datagrams observed | Profile drops | Duplicates injected | Queue-overflow drops | Peak relay queue | Older input/state revisions rejected |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Fixed 40 ms delay | 1 | 46 | 0 | 0 | 0 | 4 | 1 / 1 |
| Variable delay, 10-70 ms | 1 | 46 | 0 | 0 | 0 | 6 | 5 / 5 |
| Burst loss, first 3 of every 10 | 1 | 46 | 18 | 0 | 0 | 2 | 0 / 0 |
| Duplicate every second datagram | 1 | 46 | 0 | 22 | 0 | 4 | 0 / 0 |
| Reorder every second datagram | 1 | 46 | 0 | 0 | 0 | 6 | 11 / 11 |
| 200 ms blackout | 1 | 46 | 14 | 0 | 0 | 2 | 0 / 0 |
| 1000 B/s, four-record queue | 1 | 46 | 0 | 0 | 30 | 4 | 2 / 2 |
| Combined faults, 30 seconds | 8 | 15440 | 4912 | 3532 | 0 | 10 | 1019 / 1028 |

Counts combine both directions. The last column includes delayed records rejected
after clean-path recovery/removal. Injected encrypted duplicates were not exposed
twice to the application. The fixed-delay profile measured a mean local delivery
delay of 46.76 ms; the configured 40 ms floor was checked on every received
impaired record. Scheduling and polling add to the configured delay. Timings and
datagram counts are observations from this run, not deterministic guarantees.

| Write fault | Completed records | SSL write attempts | Refused new payloads | Outcome |
| --- | --- | --- | --- | --- |
| Three WANT_WRITE results | 2 | 5 | 1000 | Original immutable record delivered once, then a new record sent |
| One WANT_READ result | 1 | 2 | 0 | Waited for readability, then recovered |
| Persistent WANT_READ | 0 | 1 | 0 | Two-second deadline; no repeated writes without readability |
| Persistent WANT_WRITE | 0 | 128 | 0 | Retry budget exhausted; channel terminal |
| Fatal initial write | 0 | 1 | 0 | Channel terminal; no retry |
| Fatal retry | 0 | 2 | 0 | Channel terminal; no further SSL operations |

Each scenario staged at most one 68-byte payload in the fixed 1200-byte buffer.
Pending or failed payloads were not counted as sent. Persistent/fatal faults
remained terminal on subsequent API calls. Handshake recovery after loss of the
first client flight and oversize/invalid-send rejection also passed.

The complete regression passed **95 cases across 8 CTest suites**, including the
existing native-room scenarios with 2, 4 and 8 clients. The fault report was run
separately before that regression.

Fault-test executable SHA-256:
`c03656984c22fe4c6ca7f0af2206457bc3718870810576a21cc78b82c3437cc1`.

Reproduce with a new report path:

```powershell
python tools/robustness_report.py --stress-seconds 30 --output docs/reports/my-robustness-run.json
```

The eight-pair test exercises protected transport and replicas, not an impaired
eight-client native room. Its nominal proposal interval is 20 ms; it is not a
like-for-like performance comparison with the earlier
[native-room baseline](MOVEMENT_BASELINE.md). RTT counts in pair metrics are zero
because these tests do not initiate TCP probes. This is an initial 30-second
exercise, not a long-duration load/memory audit or LAN/Internet validation.
