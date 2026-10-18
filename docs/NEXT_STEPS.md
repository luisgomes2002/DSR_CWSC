# Next steps for DSR Network Lab

Last updated: 2026-10-18, after live read-only player captures and menu/reload recovery.

Next session: follow the Portuguese [test resumption checklist](RETOMAR_TESTES.md).
The next live test is death/respawn, followed by area loading and normal process exit.

## Current state – version 0.4

- Code separated into modules, executable in `build-v04/dsr_net.exe`.
- Control of the room, password, chat and snapshots via TCP/TLS.
- Dummy movements via UDP protected with DTLS, activated with `--udp`.
- Session/connection IDs, sequences and discarding old positions.
- Automatic synthetic trajectories and movement/RTT reports, with native-room
  loopback benchmarks for 2, 4 and 8 clients.
- Bounded DTLS write retries, plus bidirectional adverse-network profiles.
- Complete native rooms with 2, 4 and 8 clients passed one-minute runs with
  per-process memory histories, queue occupancy, update histograms and failures
  recorded in [the complete-room report](reports/SUSTAINED_ROOMS.md).
- 99 cases passed across nine automated suites, including complete native-room
  faults and recovery. The earlier recorded robustness run
  exercised eight independent DTLS pairs for 30 seconds under combined faults;
  this was separate from the native-room benchmarks.
- The user confirmed operation in the local manual test.

This isn't Dark Souls multiplayer yet. There was no integration with the game,
cross-computer validation, Internet testing or proof of capability
for gameplay. Compatibility between different DSR installations has not been
demonstrated. There is no guarantee of zero risk of banning in future integration.

## Immediate next work

The 30-minute runs for 2, 4 and 8 clients and their memory/queue/pause analysis
are complete; see [the soak report](reports/SOAK_30MIN.md). No sustained memory
growth, output accumulation or recorded transport failures were observed.

Complete-room adverse conditions are implemented in the
[room fault runner](ROOM_FAULT_LAB.md): eight bidirectional encrypted-UDP profiles,
bounded relay queues, memory/queue/interval metrics and recovery checks for every entity.
All 24 scenarios passed in the [recorded full matrix](reports/ROOM_FAULT_RESULTS.md).

1. Extend adverse-room runs as requirements evolve, including separate control-channel
   outages and reconnection scenarios; current faults affect UDP movement only.
2. Establish explicit performance budgets from the completed measurements and
   synchronization requirements; default benchmark duration is five minutes per room.
3. Validate between two PCs on the LAN using the same measurements.
4. Continue with visualization/interpolation, Internet connectivity, controlled
   game integration and gameplay synchronization, in that order.

The sections below distinguish completed deliverables from remaining work.

## 1. Metrics and automatic movements

Status: initial baseline, instrumentation and thirty-minute complete-room runs completed.

Implemented: deterministic synthetic trajectories, optional per-connection JSON
reports, TCP/TLS RTT and RTT variation, fresh UDP update intervals/variation,
current update silence, application payload traffic and observed sequence counters.
The reproducible runner validates changing trajectories with 2, 4 and 8 clients.
The extended runner supports configurable 4-3600-second rooms, Windows working
set/private-byte histories, reliable-control queue current/peak/rejection metrics,
fixed interval histograms, DTLS failure summaries and process diagnostics.
Recorded results: [complete-room memory and queue report](reports/SUSTAINED_ROOMS.md).
See [the metrics guide](METRICS_LAB.md) for exact definitions and commands, and
[the baseline report](reports/MOVEMENT_BASELINE.md) for recorded results.

Remaining work:

- Establish performance budgets from the recorded thirty-minute soak data.
- Broaden failure instrumentation beyond the implemented pending-write counters.
- Optionally measure actual transport bytes separately from application payload.
- Define performance targets from measurements and synchronization requirements.

Do not compare absolute times of different PCs to infer latency.
Missing sequences may also represent local reordering or discarding;
define the window and meaning of each counter before calling it a loss.

Delivered: automatic execution with reports and recorded parameters, using the
current transport as a reference. This does not establish game capacity.

## 2. Adverse conditions and robustness of transport

Status: requested write-backpressure fix and adverse-network test expansion implemented.

DTLS writes now retain one immutable payload on WANT_READ/WANT_WRITE, retry on
socket readiness with a 10 ms minimum interval, and fail terminally after 2 seconds
or 128 attempts. Completed writes and pending/suppressed/error counters are reported.
The expanded suite covers both directions under delay, jitter, burst loss,
duplication, reordering, a temporary blackout and bandwidth/queue limits, plus
eight independent DTLS channels under combined faults. See
[the robustness guide](ROBUSTNESS_LAB.md) and
[the recorded run](reports/ROBUSTNESS_RESULTS.md).
Complete host/client rooms now also run with encrypted-UDP delay, jitter, loss,
duplication, reordering, blackouts, bandwidth limits and combined faults; see
[the full-room guide](ROOM_FAULT_LAB.md). The regression exercises combined faults
at all three room sizes, blackouts and bandwidth exhaustion, and rejects an
executable that bypasses the relay. Applied revisions must never roll back;
every entity at every client must recover a newer changing state within three seconds.

Remaining work:

- Extend fault durations and scenario coverage as synchronization requirements evolve.
- Add control-channel outages and explicit reconnection scenarios separately from
  temporary UDP interruptions.
- Implement complete TLS/DTLS shutdown and extend backpressure coverage as the
  transport evolves.
- Evolve fixed quotas for traffic control appropriate to measurements and
  to congestion conditions before considering Internet exposure.

Delivered: reproducible isolated and complete-room fault scenarios using the phase-1
metric definitions. Workloads differ, so the reports are not a like-for-like
performance comparison. Future simulations should also affect only the lab.

## 3. Validation between computers on the local network

Status: pending.

- A PC hosts; another connects by IP, password and certificate pin.
- Check the OpenSSL dependencies on the second PC.
- Test multiple clients, normal exit, client drop, reconnection and host drop.
- Observe firewall blocks without automatically creating rules.
- Repeat the metrics in a real network and compare with loopback.

The current design uses ephemeral UDP ports per participant, communicated over TLS.
Do not assume that releasing just the TCP port will resolve UDP connectivity.
A virtual machine can help, but it must be registered as this type of test.

Expected delivery: LAN roadmap validated and limitations documented.

## 4. Smoothing player representation

Status: pending.

- Create a minimal visual representation of the fictional entities if necessary
  to evaluate movement before integration with the game.
- Interpolate received positions with a limited buffer.
- Handle pauses, sudden changes and removal/recreation of entities.
- Measure the additional delay introduced by smoothing.
- Separate authoritative status received from the position used in the presentation.

Expected delivery: visually evaluable movements under the previous scenarios.
Prediction and reconciliation should only be added when there is a model
inputs/simulation that allows them to be defined correctly.

## 5. Internet Connectivity

Status: pending; do not expose the current laboratory publicly.

- Define how to locate the host and traverse NAT/CGNAT.
- Review the design of ports and the endpoint link, currently focused on loopback/LAN.
- Evaluate connectivity options and their costs/dependencies on the user.
- Review authentication, resource limits, behavior under abuse and maintenance
  of libraries before releasing external tests.

Preserved requirement: the session world must be hosted on the host PC.
Hosting on PC does not guarantee direct access through any NAT/CGNAT.
Do not assume authorization to hire a server, add an external service,
use relay, forward ports or change firewall. If necessary, present
options and get a decision before changing those requirements.

Expected delivery: explicitly chosen strategy and controlled external testing.

## 6. Dark Souls Remastered Minimal Integration and Isolation

Status: pending; requires its own framework, separate from the network.
The initial static comparison of the two user-supplied installations is complete;
see [the executable and module comparison](reports/GAME_INSTALLATIONS.md).
Game executables and all twelve parameter files match by SHA-256, while launchers,
Seamless DLLs and additional modules differ. Runtime compatibility remains pending.
The [external read-only candidate reader](PLAYER_READER.md) is implemented and
passes thirteen tests, including actual Windows reads of a synthetic child.
It checks executable hash/path, real module base, code anchors, object types,
pointer stability and page readability. Initial live reads of the alternative
installation now cover [rest](reports/PLAYER_LIVE_INITIAL.md),
[camera-only instructions](reports/PLAYER_LIVE_CAMERA_ONLY.md),
[flat movement](reports/PLAYER_LIVE_FLAT_MOVEMENT.md) and
[stairs](reports/PLAYER_LIVE_STAIRS.md), plus a successful
[menu/reload recovery](reports/PLAYER_LIVE_RELOAD.md) with a new object chain
and no stale values in unavailable records. These support the candidate position
and facing fields; full axis conventions, other loading/death recovery and Steam
runtime compatibility remain unvalidated. No gameplay integration is implemented.
Initial static player-data candidates have now been located and traced through
RTTI, getters/setters and the named local-player predicate; see
[the player-data static map](reports/PLAYER_STATIC_CANDIDATES.md). This supplies
a candidate reader specification, supplemented by the scoped live observations above.

### 6.1. Investigate without executing – combat and authority

In the Seamless DLL and DSR executable:

- Locate the points that receive and process messages.
- Follow calls to routines that alter life, death or control status
  of entities.
- Look for checks that differentiate host, guest and owner of the entity.
- Identify fields that may represent entity, attack, sequence or result.

Reverse engineering tools like a decompiler help reconstruct
this flow. The initial interpretation is a hypothesis, not a confirmation.
Separate the behavior of the DSR itself from the adaptations made by Seamless;
a message passing through the host does not prove authority over combat.

Expected delivery: map of handlers and relevant calls, with addresses/versions,
evidence, hypotheses and doubts recorded. Start down the path of a scam
up to altering an enemy's life. This step is just static analysis:
do not run or modify the game or mod, and do not copy code to the laboratory.

### 6.2. Minimal integration and isolation validation

- Identify and compare the versions of executables that will be supported;
  the version reported by the user was app 1.03.1 / regulation 1.04.
  The current common executable SHA-256 is recorded in the comparison report;
  its generic Windows version resources do not independently confirm those menu versions.
- Start with identification and controlled reading of player data,
  no memory writing or saves.
- Validate isolation of saves and blocking of official services before
  experience changes to the game. Not treating previous investigations
  as proof that these protections already work.
- Use backups and test data, with explicit authorization before running
  tools that load code or change the game process.
- Confirm separately the compatibility of each supported installation;
  the laboratory without Steam does not, in itself, prove compatibility between them.

Expected delivery: validated minimum reading layer and isolation evidence
before moving on to running modifications. Do not promise no ban.

## 7. Gameplay synchronization in stages

Status: pending; depends on network and integration validations.

Suggested starting order, subject to what game investigation allows:

1. Remote player representation and movement.
2. Orientation, animations and equipment.
3. Combat actions, damage and death.
4. Enemies, bosses and authority over the world.
5. Items, events, progress and area transitions.
6. Recovery from inconsistencies, late entry and prolonged testing.

Define reliability, order and prevention of duplicate application for each
event. Do not carry items, damage or progress as if they were positions
disposable. The host needs to validate relevant world changes.

Expected Delivery: Each feature demonstrated and tested in isolation before
be considered part of a playable cooperative experience. No invasions
as the objective of the project.

## Related documentation

- [Current test of protected UDP channel](UDP_LAB.md)
- [Movement Protocol Agreement](MOVEMENT_PROTOCOL.md)
- [TLS Previous Milestone](TLS_LAB.md)
- [Previous feasibility opinion](../../dsr-analysis/PARECER_VIIBILIDADE.md)

Update this document at each milestone, distinguishing implemented, tested and
still proposed. This list does not authorize the execution of future phases in advance.
