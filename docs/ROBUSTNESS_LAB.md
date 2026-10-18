# Transport robustness

The lab now handles temporary DTLS application-write backpressure and has an
expanded, isolated fault suite. Reliable control and movement wire formats are
unchanged. No firewall, router, system network settings or game process is changed.

## Pending DTLS writes

Each DTLS channel owns one fixed 1200-byte staging buffer. Before the first
`SSL_write`, it copies the payload into this buffer. If OpenSSL reports
`SSL_ERROR_WANT_READ` or `SSL_ERROR_WANT_WRITE`, the same pointer, bytes and length
are retained for retry. New payloads are refused while that write is pending;
there is no growing queue of positions and no replacement of an active write.

`poll()` checks socket readiness for the requested direction without blocking.
SSL write attempts are separated by at least 10 ms and limited to 128 attempts
(including the first). A pending write expires after 2 seconds, whichever bound
is reached first. The timer is checked even when readiness never arrives.
While the write remains pending, other SSL I/O is deferred. An OpenSSL fatal
error or exhausted deadline/budget makes the channel terminal; subsequent calls
cannot revive it. The existing host/client failure paths then terminate the
participant without falling back to plaintext or another movement transport.

`send()` returning true means **admitted**, either immediately completed or copied
into the pending slot. It does not mean the peer received the datagram. False
means invalid size, not ready or busy. The client advances its input sequence
only on admission; busy attempts therefore do not create artificial sequence
gaps. Once the pending write completes, the latest desired position can be
published normally. Retrying an unfinished SSL write is not retransmission of
application datagrams that were successfully sent and subsequently lost.

This follows the OpenSSL contracts for
[SSL_write](https://docs.openssl.org/3.0/man3/SSL_write/) and
[SSL_get_error](https://docs.openssl.org/3.0/man3/SSL_get_error/).

## Measurements

The existing movement reports now count completed SSL application writes,
including those completed later by `poll()`. Admission alone does not increment
sent payload bytes. Received payload definitions remain unchanged.

Additional cumulative fields in `METRICS`:

| Field | Meaning |
| --- | --- |
| `dtls_write_attempts` | Actual calls to SSL_write, including retries |
| `dtls_want_read/write` | Retryable SSL_write results in the respective direction |
| `dtls_send_suppressed` | New valid payloads refused because the single slot was occupied |
| `dtls_send_failures` | Terminal application-write errors or expired retry budgets |
| `dtls_send_timeouts` | Subset caused by the deadline or attempt budget |
| `dtls_pending_bytes` | Current staging-buffer occupancy; zero after a successful write |
| `dtls_peak_pending_bytes` | Largest staged payload, including immediate writes; at most 1200 |

A terminal failed channel can retain an unsent staged record until destruction;
that record is not counted as sent. These are application-write measurements,
not counters for handshake retransmission or all OpenSSL read/transport errors.

## Reproduce the tests and report

```powershell
cmake --build build-v04 --parallel 2
ctest --test-dir build-v04 -R 'dtls_unit|movement_unit|udp_integration|benchmark_integration' --output-on-failure
python tools/robustness_report.py --stress-seconds 30 --output docs/reports/my-robustness-run.json
```

The runner archives test names, parameters, transport/replica metrics, send-fault
counters, diagnostics and the test executable's SHA-256. Existing reports are
preserved. `--exe PATH` selects another locally built `dtls_unit.exe`.
The combined eight-channel scenario defaults to 2 seconds in CTest, and accepts
1-60 seconds through `--stress-seconds`. Other profiles run for 700 ms each.
The report for this change is summarized in
[the recorded robustness run](reports/ROBUSTNESS_RESULTS.md).

The DTLS suite has 22 cases, including:

- Recovery after dropping the first client handshake flight.
- Real OpenSSL WANT_WRITE recovery with an immutable caller-independent buffer;
  1000 additional send attempts cannot overwrite it or expand its queue.
- WANT_READ recovery after socket readability; persistent WANT_READ expires
  without repeatedly calling SSL_write while the socket remains unreadable.
- Persistent WANT_WRITE expires with bounded work; fatal errors on the initial
  write and during a retry remain terminal. Invalid payloads do not occupy a slot.
- Fixed 40 ms delay; seeded variable delay of 10-70 ms; three lost datagrams out
  of every ten; duplication of every second datagram; an 80 ms extra delay on
  every second datagram to force reordering; a 200 ms blackout; and a 1000 B/s
  per-direction limit with a four-record relay queue.
- Eight independent DTLS pairs under a combined seeded profile: 10-40 ms delay,
  loss of every seventh datagram and the first two of every ten, duplication,
  reordering and a 200 ms blackout.

Both directions carry moving absolute positions. A final clean state must recover
within 2 seconds while delayed records remain queued. Inputs and replica revisions
must never roll back; after DESPAWN, delayed states must not recreate the entity.
The suite checks that injected encrypted duplicates are not exposed twice to the
application, records actual stale-packet rejection, checks that measured delivery
delay respects the configured floor, and drains the bounded relay queue.

Profiles act only on the test's loopback relay sockets, after handshake (apart
from the dedicated handshake-loss test). The deterministic seed is `0x12345678`;
drop decisions depend on per-direction datagram indices. Timing and the number
of datagrams still depend on OS scheduling. The relay records actual drops,
duplicate injections and peak queue occupancy. Its bandwidth model serializes
encrypted datagrams per direction and drops on queue overflow; it is a test
model, not a full network emulator.

The BIO fault hook is compiled only into `dtls_unit`'s copy of the same transport
source. It uses OpenSSL's public
[BIO callback API](https://docs.openssl.org/3.0/man3/BIO_set_callback/)
to return retry/fatal conditions before an actual datagram write. The application
executable has neither this hook nor a CLI/environment option to enable it.

## Scope and remaining work

Eight independent test pairs are not an eight-client native room. Native-room
baseline regressions run separately in `benchmark_integration`. Adverse conditions
on actual 2/4/8-client rooms are now implemented in `room_fault_integration` and
[the separate complete-room fault runner](ROOM_FAULT_LAB.md).
Profile metrics reuse the movement counters and interval definitions; their
nominal 50 Hz proposals, short durations and simulated conditions differ from the
phase-1 baseline. There is no TCP RTT probe inside the DTLS pair tests, so their
RTT sample counts are zero. Delivery-delay measurements use a shared monotonic
clock inside one local test process, not clocks on separate PCs.

The 30-second run is an initial transport exercise, not a sustained-load or memory
audit. Thirty-minute native-room memory measurements are recorded in
[the soak report](reports/SOAK_30MIN.md). Further work includes expanded fault
scenarios, LAN validation and adaptive congestion control. Fixed traffic quotas and bounded
queues do not establish Internet readiness or game capacity.
