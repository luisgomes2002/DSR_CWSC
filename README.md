# DSR Network Lab

A standalone, host-based networking prototype for a possible future private
co-op mod for **Dark Souls Remastered**. Written in C++17 for Windows x64.

**Current milestone: 0.4 — protected UDP movement.** This is a networking
laboratory with synthetic player entities, not a playable game mod.

The host runs on a player's PC. Guests connect to that host; guests do not
establish direct connections to each other. No rented game server or Steam
networking service is used by the current prototype.

A separate experimental [read-only player reader](docs/PLAYER_READER.md) now
probes the statically identified game-data candidates. Its thirteen tests include
real Windows reads of a synthetic child. Initial live captures of the alternative
installation cover rest, movement, stairs and menu/reload recovery; see the
[next-session test checklist](docs/RETOMAR_TESTES.md). These candidate samples
are not yet connected to the networking lab.

## Project status

Implemented and tested locally:

- Password-protected rooms, capacity limits, host-assigned player IDs and rosters.
- Chat and private messages routed through the host.
- Synthetic player positions, initial snapshots, movement updates and removal.
- TCP/TLS 1.2 for admission, room control, chat and snapshots.
- Optional UDP/DTLS 1.2 for movement, explicitly enabled with `--udp`.
- SHA-256 host certificate pinning and version/build compatibility checks.
- A DTLS key derived from the authenticated TLS connection, rather than the room password.
- Session/connection identifiers, per-entity sequencing and stale-update rejection.
- Bounded queues, rate limits, handshake timeouts and disconnect cleanup.
- Modular source code and automated unit/integration tests.
- Automatic synthetic trajectories and per-connection movement/RTT metrics, with
  repeatable loopback benchmarks for 2, 4 and 8 clients.
- Configurable longer room benchmarks with per-process working set/private-memory
  histories, TCP/DTLS queue occupancy, update-interval histograms and failure summaries.
- Bounded DTLS application-write retries and isolated bidirectional tests for
  delay, burst loss, duplication, reordering, blackouts and bandwidth limits.
- Complete native-room fault runner with eight encrypted-UDP profiles for 2, 4
  and 8 clients, per-connection relay counters and automatic recovery checks.

The configurable room capacity is 2–32 laboratory clients, with a default of 8.
Eight simultaneous synthetic UDP clients have been tested. This is **not evidence that
Dark Souls supports that player count**, or that this transport is ready for gameplay.

The `host` process is not a player. Run a separate `join` process to participate
on the host PC; that client occupies a slot like every other guest.

Not implemented or validated:

- Loading code into Dark Souls, game hooks, save handling or gameplay synchronization.
- Compatibility between different game installations.
- LAN/Internet connectivity tests, NAT traversal, relays or host migration.
- Automatic reconnection, persistent player identities, movement interpolation or prediction.
- Sustained load testing or adaptive congestion control.
- A security audit or any guarantee about future account-ban risk.

## Projects and technologies used

These are actual dependencies or development tools, not gameplay-mod dependencies.

| Project / technology | Role in this repository | When needed |
| --- | --- | --- |
| [OpenSSL 3](https://github.com/openssl/openssl) | TLS/DTLS, certificate/private-key loading, TLS exporters and protected datagrams; the CLI generates test identities | Runtime for TLS/UDP; CLI for identity generation and TLS/UDP integration tests |
| [CMake and CTest](https://cmake.org/) | Build configuration and automated test orchestration | Building and running the test suite |
| [GCC](https://gcc.gnu.org/) with [MinGW-w64](https://www.mingw-w64.org/) | The compiler and Windows-targeting toolchain used for the tested build | Building the Windows executable |
| [Python 3](https://www.python.org/) | Certificate-generation helper and process/socket integration tests, using standard-library modules | Identity generation and integration tests; not needed by the native executable itself |
| [Git for Windows](https://gitforwindows.org/) | Supplies the OpenSSL CLI and DLLs used in the current development environment | Optional provider of the required OpenSSL binaries; not the network transport |
| Windows APIs: [Winsock](https://learn.microsoft.com/en-us/windows/win32/winsock/windows-sockets-start-page-2), [CNG/BCrypt](https://learn.microsoft.com/en-us/windows/win32/seccng/cng-portal), [CryptoAPI](https://learn.microsoft.com/en-us/windows/win32/seccrypto/cryptography-portal) | Sockets, cryptographic randomness, certificate parsing/hash/time checks and Windows console support | Native Windows platform dependencies |

The tested development environment used GCC 15.2, CMake 4.1 and Python 3.13.
CMake requires version 3.20 or later and a C++17-capable compiler. MSVC build
settings are present, but MSVC has not been tested.

The application does not use the Steamworks SDK or Valve GameNetworkingSockets.
References to Steam APIs in earlier investigation notes are research references,
not libraries linked into this prototype. Python helpers do not require third-party
`pip` packages.

### OpenSSL runtime setup

The native executable dynamically loads OpenSSL 3 x64 DLLs through a narrow
adapter to its public C API. It does not require OpenSSL headers/import libraries
in the current build, and it does not bundle or download the DLLs.

The current default locations are:

```text
C:\Program Files\Git\mingw64\bin\libssl-3-x64.dll
C:\Program Files\Git\mingw64\bin\libcrypto-3-x64.dll
C:\Program Files\Git\usr\bin\openssl.exe
```

These paths describe the development machine, not a guarantee about every Git
installation. A different trusted OpenSSL DLL directory can be supplied with
`--tls-library 'C:\trusted\openssl\bin'` on both host and client. The identity
generator accepts `--openssl 'C:\trusted\openssl.exe'` for the CLI.

Use maintained binaries from a trusted source, not individual DLL download sites.
Third-party components retain their own licenses; this README does not relicense
them or authorize redistribution of another project's binaries.

## Inspiration and acknowledgements

The intended private co-op experience was inspired by Yui's Seamless Co-op work:

- [Seamless Co-op — Dark Souls Remastered](https://www.nexusmods.com/darksoulsremastered/mods/899):
  the main conceptual reference for this project's co-op goal and earlier local investigation.
- [Seamless Co-op — Elden Ring](https://www.nexusmods.com/eldenring/mods/510):
  a broader reference for the seamless co-op experience that motivated the project.

These are **inspirations, not dependencies**. DSR Network Lab is not a fork,
patch, replacement release or official extension of either mod. It does not
include Seamless source code, binaries, assets or its wire protocol, and its
laboratory protocol is not compatible with Seamless Co-op or Dark Souls multiplayer.

Earlier static investigation of locally installed Seamless files informed the
research questions; it did not provide an integrated gameplay implementation.

This is an independent project, not affiliated with or endorsed by Yui,
FromSoftware, Bandai Namco, Valve or the projects credited above.
The long-term focus is private co-op with friends, not invasions.

## Build

From the repository directory in PowerShell:

```powershell
cmake -S . -B build-v04 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-v04 --parallel 2
ctest --test-dir build-v04 --output-on-failure
```

The executable is `build-v04\dsr_net.exe`. Earlier build directories are
preserved. Use the same current executable on the host and all clients:

- TCP framing version: `3`.
- Compatibility build ID: `dsr-network-lab-0.4`.
- Binary movement payload version: `1`.

The MinGW build statically links its compiler runtimes. Windows system DLLs and,
for protected connections, the external OpenSSL runtime are still required.

## Quick start: protected UDP on one PC

Do not launch Dark Souls for this test. Open one terminal for the host and
another for each client. Start in the repository directory.

### 1. Create a host identity

Use a **new directory**, preferably outside cloud-synchronized folders.
The helper refuses to overwrite an existing identity.

```powershell
$dsrIdentity = Join-Path $env:LOCALAPPDATA 'DSRNetworkLab\host-v04'
$env:DSR_TLS_PFX_PASSWORD = 'choose-your-own-pfx-password'
python .\tools\create_tls_identity.py --output $dsrIdentity
```

Choose your own PFX password: 12–128 printable ASCII characters. The helper creates
an encrypted private key, a public certificate and `host.pfx`. The self-signed
certificate is valid for 30 days. If you already have a valid identity, reuse its
directory and the password originally used to create it instead of generating it again.

Never share the PFX, private key or PFX password. A new certificate changes the pin.

### 2. Start the host

In the same terminal, keeping the identity path and PFX password:

```powershell
$env:DSR_ROOM_PASSWORD = 'local-room-test-123'
.\build-v04\dsr_net.exe host --port 27034 --max-players 4 --tls-pfx "$dsrIdentity\host.pfx" --udp
```

The host prints `SERVER_READY` and `TLS_PIN`. Copy the 64 hexadecimal characters
of the pin to clients through a trusted channel. The room password is a separate
credential: 8–128 printable ASCII characters. Use disposable passwords for testing.

### 3. Join from another terminal

```powershell
$env:DSR_ROOM_PASSWORD = 'local-room-test-123'
$dsrPin = 'REPLACE_WITH_THE_REAL_64_CHARACTER_TLS_PIN'
.\build-v04\dsr_net.exe join --port 27034 --name Piero --tls-pin $dsrPin --udp
```

Replace the pin placeholder before running the command. Guests need the room
password and host pin, not the PFX password or private files.

Wait for `UDP_ACTIVE`, then enter:

```text
/move 10 0 5
/players
/who
hello everyone
/send 2 private message
/quit
```

After `/move`, expect `UDP_POSITION` with the player ID and position.
Wait for that update before checking `/players`, which requests a TCP snapshot.
For a second client, repeat the join setup in another terminal with `--name Friend`.
Use the actual roster ID for `/send`; names are not routing identifiers.

The client republishes its last proposed position after a movement command.
This is not an automatic trajectory generator. The host republishes authoritative
states, and clients discard duplicate/stale revisions. The positions remain synthetic.

### Optional: legacy local TCP mode

Without `--udp`, movement stays on TCP. A plaintext mode remains available
**only on loopback**:

```powershell
# Set the same DSR_ROOM_PASSWORD in each terminal.
.\build-v04\dsr_net.exe host --port 27030
.\build-v04\dsr_net.exe join --port 27030 --name LocalPlayer
```

Run host and client commands in separate terminals. This mode is not a substitute
for testing the protected UDP channel. `--udp` always requires TLS.

## Architecture and protocol

```text
src/
  main.cpp                 Small entry point: configuration and host/client startup
  app/                     CLI options and environment configuration
  net/                     Winsock, TLS facade, OpenSSL adapter and DTLS transport
  protocol/                TCP frames, entity messages and binary movement codec/streams
  world/                   Host-owned synthetic player state
  session/                 Peers, room control and connection-owned movement links
  client/                  Client event loop, world replica and UDP integration
  ui/                      Nonblocking console input and command parsing
tests/                     C++ unit/socket tests and Python integration scenarios
tools/                     Explicit host-identity generation
docs/                      Protocol specification, test guides and roadmap
```

CMake builds the internal `dsr_network_core` static library and links `dsr_net`
against it. Keep the entry point small; behavior belongs in the respective modules.

All guests communicate through the host. Reliable control uses newline-delimited
ASCII TCP frames with TAB-separated fields. Movement uses fixed 68-byte binary
payloads inside authenticated DTLS records. The host validates entity ownership
and assigns its own world revisions rather than trusting client sequence numbers.

TCP frames are limited to 2048 bytes before LF, text messages to 1024 bytes, and
the application output queue to 64 KiB per connection. Identifiers are local to
the laboratory session, not SteamIDs. ASCII names allow letters, digits, `_`
and `-`, with a length of 1–24 characters.

For exact field layouts, sequencing, channel binding, rate limits and timeouts,
see the [movement protocol](docs/MOVEMENT_PROTOCOL.md) and [UDP guide](docs/UDP_LAB.md).

## Tests and validation

The last full networking validation passed **99 cases across 9 CTest suites**.
The separate `player_reader_unit` suite also passed **13 reader checks**, using
synthetic memory and controlled Windows child processes. Live game data remains unvalidated.

| Suite | Cases | Coverage |
| --- | --- | --- |
| `entities_unit` | 8 | World state, replicas, entity messages and commands |
| `movement_unit` | 17 | Binary codec, identifiers, sequencing, UDP replica rules and metrics |
| `dtls_unit` | 22 | Protected transport, handshake loss, bounded write retries, fatal errors and eight adverse-network profiles |
| `loopback_integration` | 14 | Admission, chat, routing, framing, limits and disconnects |
| `entities_integration` | 11 | Synthetic entity synchronization |
| `tls_integration` | 12 | Native/Python TLS interoperability, certificate pinning and rejection paths |
| `udp_integration` | 9 | Native UDP clients, mixed TCP/UDP rooms, snapshots, reconnection and failure paths |
| `benchmark_integration` | 3 | Automatic 2/4/8-client trajectories, memory/queue/interval accounting, option validation and failed-launch diagnostics |
| `room_fault_integration` | 3 | Complete 2/4/8-client rooms under combined faults, UDP blackouts, bandwidth exhaustion and rejection of a bypassing executable |
| `player_reader_unit` | 13 | Candidate-reader guards, relocated/changing pointers, protected pages, actual child-process reads, module metadata and process exit |

```powershell
ctest --test-dir build-v04 --verbose
```

TLS/UDP Python integration suites are registered only if CMake finds the OpenSSL
CLI. If it cannot, configuration prints a warning; passing the remaining suites
does not validate those integrations. Set
`-DDSR_OPENSSL_EXECUTABLE="C:/trusted/openssl.exe"` when configuring if needed.
The DTLS socket tests also require the OpenSSL runtime DLLs.

Tests create temporary identities and local sockets. Successful runs were on
loopback, not separate PCs. They are not a security audit, sustained load test,
gameplay test or validation of Steam account safety.

## Safety and current limitations

- The prototype does not load the game, modify executables/saves, install
  certificates in Windows or change firewall/router settings.
- Host trust uses an explicitly supplied certificate pin plus certificate-time
  validation, not automatic first-use trust, system CA trust, hostname validation
  or online revocation checks.
- Protected UDP is tied to the authenticated TLS session. There is no silent
  fallback to plaintext UDP or legacy TCP movement after a channel failure.
- Outside loopback, `--lan` and TLS are required. The current UDP design uses
  per-peer ephemeral ports and fixed endpoints learned through TLS; it does not
  implement public-address discovery, hole punching or NAT rebinding.
- Traffic quotas are fixed laboratory limits, not adaptive congestion control.
  Complete TLS/DTLS shutdown and adaptive congestion control still need work.
- Synthetic position checks do not validate game collisions, speed, combat or cheating.
- Passwords/private keys need appropriate handling. Environment variables and
  encrypted PFX files do not protect against a compromised host or exposed terminal.
- Do not forward ports or expose this laboratory to the public Internet.
  Do not claim zero ban risk for a future game integration.

## Roadmap and documentation

Automatic synthetic movement and metrics for TCP RTT, update cadence/variation,
sequence observations and application traffic are implemented. See the
[metrics guide](docs/METRICS_LAB.md) for the reproducible benchmark and definitions.
Bounded DTLS retries and expanded adverse-network tests are also implemented;
see the [robustness guide](docs/ROBUSTNESS_LAB.md) and its recorded results.
The benchmark defaults to five minutes per room and records memory/queue histories;
see the metrics guide for longer runs and measurement limits.
For faults on complete rooms, use the separate
[room fault runner](docs/ROOM_FAULT_LAB.md). It prints phase/progress, applies
faults only to its own encrypted loopback UDP flows and checks every entity's recovery.
Next: establish performance budgets, validate LAN,
evaluate interpolation and decide an Internet-connectivity strategy before
attempting controlled game integration.

The [detailed roadmap](docs/NEXT_STEPS.md) distinguishes completed work,
pending phases and expected outcomes. Additional documentation:

- [Protected UDP test guide](docs/UDP_LAB.md)
- [Movement protocol specification](docs/MOVEMENT_PROTOCOL.md)
- [Historical TLS milestone](docs/TLS_LAB.md)
- [Historical entity synchronization milestone](docs/ENTITY_SYNCHRONIZATION.md)
