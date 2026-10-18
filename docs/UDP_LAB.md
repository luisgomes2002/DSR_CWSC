# Movements in protected UDP – laboratory 0.4

Executable: `build-v04/dsr_net.exe`. Does not open or change Dark Souls, Steam,
saves, firewall or router. Previous binaries have been preserved. All
Participants must use build 0.4. Framing TCP 3, build `dsr-network-lab-0.4`.

## Channels and trust

- TCP/TLS: admission, password, roster, SPAWN/DESPAWN, chat, snapshots and QUIT.
- `--udp` on both sides: position proposals/states in DTLS 1.2/OpenSSL,
with the [68 byte v1 binary payload](MOVEMENT_PROTOCOL.md).
- PSK of 32 bytes by the TLS exporter of the authenticated connection. Exact label
`EXPORTER-dsr-network-movement-v1` (no NUL), context `session || connection`
(32 binary bytes), `use_context=1`. Requires TLS Extended Master Secret.
- The PSK is not the room password. It is not printed, recorded or transmitted as
protocol field. Local copies of work are deleted, without promising
lack of secrets in RAM. New entry has new connection and new key.
- DTLS uses `PSK-AES128-GCM-SHA256`, identity `dsrm-1`, MTU 1200, no tickets
or renegotiation. OpenSSL performs encryption, authentication and anti-replay.
We do not create our own cryptographic algorithms. There was no security audit.
- The certificate pin continues to be the TLS trust anchor; the DTLS
proves possession of the key derived from that TLS. IDs alone do not authenticate anything.

## Host: PowerShell first

## Host: PowerShell first
If you have not yet certified, first follow the generation section of that guide.
The generator requires a new folder and does not overwrite existing identities.

```powershell
cd 'C:\Users\luisg\OneDrive\Documents\dsr-network'
$dsrIdentity = Join-Path $env:LOCALAPPDATA 'DSRNetworkLab\host-v03'
$env:DSR_TLS_PFX_PASSWORD = 'PUT_THE_PASSWORD_USED_WHEN_GENERATING_O_PFX'
$env:DSR_ROOM_PASSWORD = 'test-room-123'
.\build-v04\dsr_net.exe host --port 27034 --max-players 4 --tls-pfx "$dsrIdentity\host.pfx" --udp
```

Replace the PFX password with the current value and check the path. She is different from
room password. Copy the 64 digits of `TLS_PIN` for clients per channel
reliable. Only the host needs PFX and PFX password: do not share these secrets.

## Client: other PowerShell

```powershell
cd 'C:\Users\luisg\OneDrive\Documents\dsr-network'
$env:DSR_ROOM_PASSWORD = 'test-room-123'
$dsrPin = 'COLE_AQUI_O_TLS_PIN_REAL_DE_64_DIGITOS'
.\build-v04\dsr_net.exe join --port 27034 --name Piero --tls-pin $dsrPin --udp
```

Replace the placeholder with the real pin, not the password. Wait for `UDP_ACTIVE`.
On a third terminal, connect another client with `--name Amigo`.
The host is not a player; each `join` occupies a space, even on the same PC.

In the Piero client, type `/move 10 0 5`. Customers must receive
`UDP_POSITION`, ID, coordinates and revision. Wait for this result and type
`/players`: the TCP snapshot must show the same accepted position on the host.
Chat, `/who`, `/send ID text` and `/quit` remain in TCP/TLS.

After `/move`, the client repeats the last desired position in new samples
(maximum approximately 29.4/s), even when stopped, until leaving or changing position.
This recovers the loss of the last datagram; It is not an automatic trajectory.
The host increments the revision only when the quantized position changes. Republic
round-robin absolute states; the terminal prints only new revisions.
Test exit, reconnection and host drop: there is no automatic reconnection/migration.

## Negotiation under TLS control

```text
client -> host: UDP_REQUEST<TAB>1<TAB>porta_udp_do_cliente
host -> client: UDP_OFFER<TAB>1<TAB>session_hex32<TAB>connection_hex32<TAB>host_udp_port
```

The two derive the PSK and initiate DTLS. Host sends `UDP_READY<TAB>1` over TLS
after your handshake. The client announces `UDP_ACTIVE<TAB>1` only when the DTLS
location and host confirmation are ready. Duplicate and declined trade.

One UDP socket per participant, on the effective local TCP interface and port
ephemera. The pair is fixed to the TCP IP and the port received within TLS.
Remote ports below 1024 are refused. We don't learn endpoint with
unauthenticated datagram. There is no single fixed UDP port in this framework.
No NAT rebinding, hole punching or public port discovery: this design
is intended for loopback/LAN and may not work over VPNs/NAT.

is intended for loopback/LAN and may not work over VPNs/NAT.
host review. The client compares by entity, without advancing the global cursor
TCP: UDP can bypass a SPAWN/DESPAWN that still arrives via TCP.
During snapshot, discard UDP; When finished, use its global review as a
baseline. UDP does not create unknown entities or resurrect removed entities.
Republicacao recovers states that arrived before SPAWN reliably.

0.4 clients without `--udp` can use the host in legacy TCP mode. Each recipient
receives movements on only one channel. `MOVE` by TCP after negotiating UDP returns
`ERROR USE_UDP` and does not modify the world. Builds 0.3/earlier are rejected.

## Limits and failures

- Client: one proposal every 34 ms, without a growing queue of positions.
- Host: discards Inputs beyond 60/s/connection; more than 400 application records
DTLS/s/conexao terminates the participant, including invalid payload.
- Reading: up to 8 application records per polling call.
- Output: quota of up to `min(320, floor(1000/participants))` states/s per
recipient, one player per turn in round-robin; in the backlog. Frequency
effective depends on the loop. Fixed quotas are not adaptive congestion control.
- Handshake: 7 s timeout; negotiation at the client: 10 s. No valid states
from the host for 10 s after activation, the client fails without fallback.
- Fatal failure terminates the participant without falling back to plaintext UDP
or TCP movement. WANT_READ/WANT_WRITE on application writes retain one immutable
payload, with socket-readiness retries at least 10 ms apart, at most 128 attempts
and a 2-second deadline. New writes are refused while pending; the queue cannot
grow. Handshakes retain OpenSSL retransmissions. See [robustness](ROBUSTNESS_LAB.md).
- Full DTLS `close_notify` closure was not implemented. Close TCP
invalidates session and destroys UDP socket/channel; UDP alone does not maintain the room.
- No speed, collision, combat or anticheat validation. In metrics,
interpolation, audit or proof of DoS resistance/playability.

Uses the same OpenSSL 3 x64 DLLs as TLS, loaded from an absolute trusted folder;
`--tls-library` applies to both channels. We do not install/download DLLs.
Keep the runtime updated from its trusted source, not from separate DLL sites.

## Compile and test

```powershell
cmake -S . -B build-v04 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-v04 --parallel 2
ctest --test-dir build-v04 --output-on-failure
```

`dtls_unit` uses local proxy for encrypted roundtrip, tampering, replay, loss,
reordering and wrong key. `udp_integration` exercises native clients,
snapshot, mixed UDP/TCP clients, four UDP, reconnection, TLS requirement, negotiation
duplicate, clear text, timeout and host crash. Replicate tests cover baseline
and ordering between channels. Test identities are temporary.

Successful tests only in loopback, not LAN/Internet. Metrics and isolated adverse
profiles are implemented. Extended native-room load, adaptive congestion and DTLS interoperability
with another stack. There is no conclusion of aptitude for gameplay.
For LAN, add `--lan --address IP_LOCAL_DO_HOST` to the commands of both
sides; firewall can block ephemeral ports. No rules were created.
Do not forward ports or expose this laboratory on the Internet.

References: [PSK callbacks](https://docs.openssl.org/3.0/man3/SSL_CTX_set_psk_client_callback/),
[TLS exporter](https://docs.openssl.org/3.0/man3/SSL_export_keying_material/),
[BIO datagram](https://docs.openssl.org/3.0/man3/BIO_s_datagram/).
