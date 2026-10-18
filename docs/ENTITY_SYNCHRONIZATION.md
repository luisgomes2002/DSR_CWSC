# Dummy entities – milestone 0.2

This laboratory synchronizes player IDs, names and numerical positions
test. It doesn't create characters in DSR, it doesn't read game memory, it doesn't use its
packages and does not access saves or official services.

## Manual testing of the new version

The previous version was open during compilation and was not closed.
The new executable is in `build-v02\dsr_net.exe`. Use this executable as much
on the host and on the clients. The protocol is now **2**; version 1 is rejected.

Open three PowerShell terminals and run in each:

```powershell
cd 'C:\Users\luisg\OneDrive\Documents\dsr-network'
$env:DSR_ROOM_PASSWORD = 'lab-local-123'
```

Host, using another port so as not to conflict with the previous laboratory:

```powershell
.\build-v02\dsr_net.exe host --port 27031 --max-players 4
```

First customer:

```powershell
.\build-v02\dsr_net.exe join --port 27031 --name Luis
```

Second client:

```powershell
.\build-v02\dsr_net.exe join --port 27031 --name Piero
```

On the Luis client:

```text
/move 10 0 5
/players
```

Both clients should receive something like:

```text
POSITION 1 10 0 5 3
```

The fields are: type, ID of who moved, X, Y, Z and global world review.
IDs and revisions vary with the sequence of inputs, movements and outputs.
`/move` defines the absolute position; does not add displacement. Use point to
decimals, for example `/move 10.5 -2 5`.

`/players` requests a complete picture and prints `PLAYERS`, one line
`PLAYER` per entity and `PLAYERS_END`. A third customer receives this
photography automatically upon entering, with Luis in his current position.

Also test:

- Move Piero: only the player corresponding to his connection changes.
- `/move nan 0 0`, `/move 100001 0 0` or `/move 1.5 0 0`: the client warns
`Invalid command`, remains connected and does not send the invalid update.
- `/quit`: other clients receive `LEFT` and `DESPAWN`; the entity leaves the list.
- Enter again: new ID and initial position `(0, 0, 0)`, without recovering the old entity.

Clear text password and lack of encryption remain: testing only
locations. Do not open ports on the router. The host alone does not create a player;
To participate on the host PC, also open a client.

To participate on the host PC, also open a client.

- Each admitted connection creates exactly one test entity.
- The host chooses the ID and origin of the movement; `MOVE` has no ID field.
- Each coordinate is finite and lies in the inclusive range `[-100000, 100000]`.
- Parsing and numerical serialization are independent of the regional configuration.
- Each creation, valid move and removal increases the global revision.
- The host redistributes movements to everyone, including the sender.
- The client keeps a replica in memory. A snapshot only replaces the replica
after checking count, unique IDs, values â€‹â€‹and corresponding final marker.
- Events with an old revision, non-existent entity or invalid format are
treated as a protocol error by the client, not applied silently.
- Snapshot and deltas traverse the same ordered TCP stream. There is no protocol
UDP, local prediction, interpolation, reconciliation, physics, top speed,
persistence or combat. Numerical limits are not an anti-cheat system.

## New protocol 2 messages

In addition to the room/chat commands, there are still ASCII lines with LF and TAB fields:

| Direction | Fields |
| --- | --- |
| Client -> host | `MOVE X Y Z` |
| Client -> host | `PLAYERS` |
| Host -> client | `PLAYERS global_revision amount` |
| Host -> client | `PLAYER id name
| Host -> client | `PLAYERS_END global_revision` |
| Host -> other clients | `SPAWN id name X Y Z player_revision` |
| Host -> all clients | `POSITION id
| Host -> other clients | `DESPAWN id global_revision` |

The spaces in the table represent TAB separators. The `/move` command
terminal is converted to this representation; the user types spaces.
`PLAYER`/`SPAWN` have seven fields and `POSITION` have six, including the verb.

The new client receives `WELCOME`, roster and snapshot before processing the
following events. Customers already present receive `JOIN` and `SPAWN`.
On removal, they receive `LEFT` and `DESPAWN`.

`MOVE` with four fields and invalid coordinates produces `ERROR BAD_POSITION`
without disconnecting or changing position/revision. Command with quantity of
incorrect fields or attempting to send host-unique messages is
refused as `BAD_COMMAND` and closes the connection. Movement without admission
is rejected as `EXPECTED_HELLO`. There is no entity created for those who make a wrong password.

## Code organization

| Module | Responsibility |
| --- | --- |
| `world/player.hpp`, `world/world.hpp/.cpp` | Data and authoritative state of entities, without sockets or gaming |
| `protocol/entity_messages.hpp/.cpp` | Typed parsing, limits and serialization of entity messages |
| `client/player_replica.hpp/.cpp` | Validated and atomic application of snapshots and deltas on the client |
| `ui/client_commands.hpp/.cpp` | Conversion of terminal commands, including local validation |
| `session/host.hpp/.cpp` | Connects the accepted connection to the entity and coordinates transport/events |

## Compile and test

```powershell
cmake -S . -B build-v02 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-v02 --parallel 2
ctest --test-dir build-v02 --verbose
```

The suite contains three groups: the 14 existing network scenarios, 11
new loopback synchronization and 8 C++ unitary cases for state,
serialization, replication, revisions and commands. It is not a load test or proof
of compatibility between DSR executables.