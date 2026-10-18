# External read-only player-candidate reader

`tools/player_reader.py` reads the candidate position/orientation chains from
[the static map](reports/PLAYER_STATIC_CANDIDATES.md). It requires an explicit PID
and executable path. It records JSONL samples and prints current values/status
approximately once per second. It is independent of the networking executable.

The implementation has been validated against a controlled synthetic Windows
child process. **Actual game values and compatibility with the two installations
remain unvalidated.** No game process was running during implementation/testing.

## Select an already-running game

Use 64-bit Python on Windows. Identify the PID and the exact installation:

```powershell
Get-Process -Name DarkSoulsRemastered | Select-Object Id, Path
```

Choose one PID explicitly. In the next example, replace `1234` with its actual
ID; the executable path must match that process:

```powershell
python tools/player_reader.py --pid 1234 --exe "E:\SteamLibrary\steamapps\common\DARK SOULS REMASTERED\DarkSoulsRemastered.exe" --seconds 60 --output docs/reports/player-read-01.jsonl
```

For the other installation, use its own PID and path:

```powershell
python tools/player_reader.py --pid 5678 --exe "E:\DARK SOULS REMASTERED\DARK SOULS REMASTERED\DarkSoulsRemastered.exe" --seconds 60 --output docs/reports/player-read-alternate-01.jsonl
```

The tool observes an existing process; it does not start the game. Default
sampling is 10 Hz; `--hz` accepts 0.5-30 Hz. Duration accepts 1-3600 seconds.
Use a new output filename each time. Reports inside the selected installation
are rejected. Ctrl+C ends capture cleanly and preserves the JSONL already written.

Exit codes: 0 after at least one candidate sample, or after an explicit Ctrl+C;
1 for attachment/build/anchor errors; 2 if the run ends without a candidate sample.
Process exit produces a `stopped` record. These codes describe reader operation,
not successful validation of position semantics or gameplay compatibility.

## What is checked

Before sampling:

1. The disk file must match SHA-256
   `a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`.
2. The PID's actual executable path must match `--exe`, even when both game
   installations contain the same executable bytes.
3. The module's real loaded base, image size and selected PE header fields must
   agree with the file. The process handle/creation time identify this capture;
   the tool never reattaches automatically to a recycled PID.
4. Live instruction anchors for the local-player predicate, position getter and
   orientation getter must match the static map. Anchors are rechecked every
   five seconds, and any failure stops capture. This is a check of selected
   anchors, not a full hash of every mapped code page or other loaded modules.

Every candidate sample checks world/player/controller vtables, the player's
local-predicate vtable slot, the controller's player back-reference, and pointer
ranges/alignment. It reads the two adjacent vectors twice and re-traverses the
pointer chain. Changing vectors/chains have at most three attempts; a failed
sample emits `unavailable` with a reason and **no previous coordinates**.
This is a best-effort consistency check without suspending or synchronizing the game.
Finite checks apply to the three exported components of each vector. Unknown
fourth lanes are omitted. No world boundaries or gameplay-valid coordinates are
inferred merely from finite values.

Every memory range is queried before reading. Inaccessible, uncommitted or
guarded pages and partial reads produce no values. The handle requests only
`PROCESS_QUERY_INFORMATION`, `PROCESS_VM_READ` and `SYNCHRONIZE` to inspect pages,
read bytes and observe exit. These roles follow Microsoft's
[VirtualQueryEx](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualqueryex),
[ReadProcessMemory](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-readprocessmemory)
and [process access-rights](https://learn.microsoft.com/en-us/windows/win32/procthread/process-security-and-access-rights)
documentation. The reader has no write, allocation, protection-changing, debug,
injection, thread-suspension or game-launching operation.

## Reading the output

The file begins with `starting` and, after verification, `attached`. Each sample
is either `candidate` or `unavailable`, followed eventually by `finished`,
`stopped` or `error`. Example candidate shape (illustrative values):

```json
{
  "status": "candidate",
  "generation": 1,
  "position_components": [10.0, 20.0, 30.0],
  "orientation_candidate_rad": [0.1, 0.2, 0.3],
  "addresses": {
    "world": "0x...",
    "player": "0x...",
    "controller": "0x...",
    "movement": "0x..."
  }
}
```

Components deliberately have no X/Y/Z or yaw/pitch/roll labels yet. The generation
counter changes when object pointers change or after sampling becomes unavailable.
It is capture-local, not a persistent/network player ID. Null world/player pointers
can occur before character creation or during transitions; the actual game
lifecycle still needs confirmation. Unexpected vtables can also indicate a
different object state, a wrong assumption in the static map, or installed hooks.
The reader does not relax those checks automatically.

`--include-script-id` optionally probes the separate GameDataMan chain. Both
GameDataMan and the nested PlayerGameData vtable are required; the latter field
association is still a hypothesis. ID failure has its own `unavailable` status
and does not turn the position sample into a persistent identity. The default
capture omits this optional chain entirely.

## Controlled validation still to perform

Use a game test session prepared by the user. For each installation separately,
record stationary samples, character movement, character turning and camera-only
movement; then observe normal loading/menu transitions and process exit.
Compare the component changes with observed behavior. Confirm axes/units and
which angular components follow the character before consuming them as game data.
An `unavailable` result is useful evidence; preserve its reason and the installed
module context instead of bypassing guards.

The reader does not implement or validate save isolation, blocking official game
services, gameplay hooks or game-network integration. Those remain separate
milestones. Nothing from these candidate samples is sent over the lab network.

## Automated verification

```powershell
python tests/player_reader_unit.py
# After configuring CMake with BUILD_TESTING=ON:
ctest --test-dir build-v04 -R player_reader_unit --output-on-failure
```

Thirteen tests cover ASLR-style relocation, unknown fourth lanes, null/corrupt
pointers, incorrect vtables/back-references, changing objects/vectors, bounded
retries, nonfinite values, optional ID handling and altered live anchors.
Windows tests read a synthetic child through the actual API, exercise unreadable
pages and ranges crossing page protection, detect process exit, inspect module
headers/path/creation time, reject a wrong executable build, and preserve existing
report files. The header test isolates game-specific anchor validation; the
actual game anchors are checked by separate tests. No game was launched by tests.

Recorded result, test names, environment and source hashes:
[reader validation evidence](reports/player-reader-validation-2026-10-18.json).
