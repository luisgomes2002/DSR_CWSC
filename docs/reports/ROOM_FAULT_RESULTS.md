# Complete native-room adverse-network results - 2026-10-18

All **24 scenarios passed**: eight network profiles, each with 2, 4 and 8 native
clients connected to a native host. Every room ran for 15 seconds per client,
with a 20% clean warmup, 40% fault window and 40% clean recovery. A relay on each
connection affected encrypted UDP datagrams in both directions; TCP/TLS control
was unchanged. Handshakes completed before faults started.

Evidence: [raw JSON](room-faults-2026-10-18.json).
Parameters, checks, isolation and commands: [full-room fault guide](../ROOM_FAULT_LAB.md).

| Profile | All entities recovered, 2 clients (ms) | 4 clients (ms) | 8 clients (ms) | Largest completed update interval across room sizes (ms) |
| --- | --- | --- | --- | --- |
| Delay | 83.539 | 116.973 | 116.934 | 187.124 |
| Jitter | 57.309 | 121.890 | 104.892 | 343.143 |
| Burst loss | 131.670 | 123.306 | 168.435 | 396.587 |
| Duplication | 122.153 | 126.993 | 176.729 | 142.853 |
| Reordering | 143.407 | 150.161 | 176.908 | 265.246 |
| UDP blackout | 51.609 | 159.875 | 189.569 | 6160.880 |
| Bandwidth limit | 133.699 | 126.285 | 186.103 | 2360.420 |
| Combined | 64.794 | 142.789 | 174.991 | 2406.750 |

Recovery is observed with 50 ms polling: after the fault window ends, every
entity at every client must advance from its recorded revision to a different
position. These observed times are not precise network latency measurements.
The three-second recovery check passed throughout. The six-second total UDP
blackout intentionally produced gaps above six seconds; TCP control kept rooms
alive, and movement recovered automatically once UDP delivery resumed.

Applied UDP revisions strictly increased for every entity throughout every run,
including recovery with delayed encrypted datagrams still in flight. There were
no application rollbacks. DTLS anti-replay can reject old encrypted records
before the application's revision filter sees them; the result does not claim
that every injected stale record reached that filter.

Actual relay counters across the matrix:

- 3069 burst-loss drops and 9415 blackout drops.
- 6857 queue-overflow drops, 2246 admitted duplicate copies and 6970 out-of-order
  forwards. Out-of-order counts include interactions between jitter, duplication,
  reordering and queued records delivered during recovery.
- Peak relay occupancy of 16 datagrams per direction, matching the configured cap.

Native application metrics recorded zero terminal DTLS write failures, write
timeouts, control-queue rejections or process diagnostics. The largest native
TCP control queue was 183 bytes; the native DTLS staging peak was 68 bytes.
Peak private memory was 3.754 MiB for a host and 2.234 MiB for a client. Histories
are archived per process; these short fault runs do not establish memory stability
over long periods. The earlier [thirty-minute soak](SOAK_30MIN.md) is separate.
Relay/runner memory is excluded from native-process memory measurements.

All 112 client runs and 112 host connections emitted final metrics. Hosts were
terminated by normal benchmark cleanup after clients left. No unexpected exit
or missing final report was recorded.

Validation before this measurement: **99 cases across nine CTest suites passed**.
The new regression covers combined faults at all room sizes, full blackouts,
bandwidth/queue exhaustion, memory sampling, and rejection of a production
executable that bypasses the relay. No regression suite was launched concurrently
by the measurement runner.

Environment: Windows 11, existing MinGW Debug build. Fault executable SHA-256:
`bda940ccab7f094014f5ae847588f3203fd936699b0fa546f2916326050418af`.
The fault hook exists only in `dsr_net_fault_lab.exe`; ordinary `dsr_net.exe`
retains its direct endpoint setup and ignores the relay environment setting.

Repeat with a new report path:

```powershell
python tools/room_fault_benchmark.py --output docs/reports/my-room-faults.json
```

This is a local synthetic movement test. TCP outages, reconnecting after terminal
failures, two-PC LAN validation, performance budgets and gameplay remain separate work.
