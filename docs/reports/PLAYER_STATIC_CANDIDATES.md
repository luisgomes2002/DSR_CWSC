# Local player, position and orientation: initial static map

Date: 2026-10-18. Scope: read-only static analysis of the common game executable.
No game/launcher execution, DLL loading, process attachment or live-memory access.

**Result:** concrete candidates for the local-player pointer, movement-position
vector and angular-orientation vector were traced through the executable's own
code. Their pointer operations are statically established; live values, axis
conventions, lifecycle and behavior with each installation's modules remain unvalidated.

## Build and evidence

- SHA-256: `a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`.
- Preferred image base in the PE: `0x140000000`. All addresses below are **RVAs**,
  unless a field offset is explicitly specified. Live addresses require the
  actual loaded module base; the preferred base is not an ASLR guarantee.
- Both supplied installations were re-read and matched this executable hash.
- [RTTI, selected strings and candidate RIP references](player-static-probe-2026-10-18.json).
- [Exact-byte checks, constants and 23 bounded objdump ranges](player-static-evidence-2026-10-18.json).
- [Earlier installation comparison and module differences](GAME_INSTALLATIONS.md).
- Reusable probe: [static_game_probe.py](../../tools/static_game_probe.py).

## 1. Types and identification of the local character

The PE's RTTI descriptors, locator self-references, inheritance arrays and vtable
code pointers agree on these candidate object types:

| Type | Type descriptor RVA | Primary vtable RVA | Base type at offset zero |
| --- | --- | --- | --- |
| `PlayerIns` | `0x1B00708` | `0x13251F0` | `ChrIns`, then `FieldInsBase` |
| `ChrIns` | `0x1AFFD18` | `0x131E008` | `FieldInsBase` |
| `WorldChrManImp` | `0x1B00C08` | `0x1328210` | Self |
| `PlayerCtrl` | `0x1B00EA0` | `0x132C258` | `ChrCtrl` |
| `ChrCtrl` | `0x1B00D10` | `0x132AAB0` | Self |
| `GameDataMan` | `0x1B2B108` | `0x13DBF70` | Self |
| `PlayerGameData` | `0x1B2B3E0` | `0x13DD620` | `IGameDataElem` |

The routine at `0x27BF20` allocates `0x1F30` bytes and calls `0x36D8D0`.
That constructor installs the RTTI-linked `WorldChrManImp` vtable. The caller
stores the resulting pointer in global storage at **RVA `0x1C77E50`**.

The routine at **`0x322730`** reads that global, compares the caller's character
pointer against **`world + 0x68`**, and returns equality. Both `ChrIns` and
`PlayerIns` vtables reference this routine at slot byte offset `0x150`.
Separately, registration routine `0x2BD10` names `IsLocalPlayer` and references
the binding table at `0x1A314C0`, whose first function pointer is `0x5B62A0`.
That wrapper resolves a character and dispatches virtual slot `0x150`.
This links the pointer comparison to a named local-player predicate, rather
than relying on a guessed global address.

Static confidence: **strong** for the role of this global/field pair. The pointer
is populated at runtime; no current pointer value was read from the file or a process.

## 2. Position and movement-controller chain

Routine **`0x321B70`** loads a pointer from **`character + 0x68`** and calls
**`0x37A500`**. The latter loads **`controller + 0x28`** and copies 16 bytes
starting at **`movement_block + 0x10`** to its output.

Independent consumers/setters support the position interpretation:

- `0x37A4E0` forwards the movement block to `0x2BBBF0`.
- `0x2BBBF0` copies its supplied vector to block offsets `+0x10` and `+0xB0`,
  then updates dirty/state flags. `0x2BBC20` uses the same `+0x10` storage and
  forwards three float components to another subsystem.
- The controller initialization path at `0x3786E0` uses a character
  back-reference at controller `+0x10`; it also reads the movement block's
  `+0x10` vector during the setup path at `0x37BDB0`.

Candidate read layout, expressed as pointer operations only:

```text
world      = read_u64(module_base + 0x1C77E50)
local_chr  = read_u64(world + 0x68)
controller = read_u64(local_chr + 0x68)
movement   = read_u64(controller + 0x28)
position   = read_float32x4(movement + 0x10)
```

Static confidence: **strong candidate** for character movement position. The
meaning/order of the first three components as world X/Y/Z, coordinate units,
map/origin transformations and the fourth lane still need controlled validation.
The fourth lane must not be exposed as a fourth position coordinate by assumption.

Routine `0x321C70` returns `character + 0x100`; `0x37A540` copies three 16-byte
vectors from that block. These are alternative/cached transform candidates,
not established substitutes for the active-controller position. Likewise,
`0x37A520` reads another vector at movement `+0x20`; its semantics are unresolved.

## 3. Angular orientation and basis candidates

At **`0x321E30`**, the character path loads its `+0x68` controller and calls
**`0x37A630`**. That getter reads the movement-block pointer at controller `+0x28`
and copies the first 16 bytes of the block to its output.

The corresponding setter path is `0x37A590` -> `0x2BBD20`. It clamps one input
component, then normalizes vector lanes using constants approximately
**`1/(2*pi) = 0.1591549367`** and **`2*pi = 6.283185482`** before storing at
movement-block offset zero. The clamp constants at `0x132BD04`/`0x132BD0C`
are approximately `+1.22173047` and `-1.22173047` radians. This is substantial
evidence for angular values in radians, rather than an arbitrary four-float
position or an assumed normalized quaternion.

```text
orientation_candidate = read_float32x4(movement + 0x00)
```

Static confidence: **strong angular-vector candidate**. Which components denote
yaw, pitch or roll, angle sign/order, padding and whether this fully describes
the player's visible facing direction remain unconfirmed.

Controller offsets `+0xA0..+0xCF` are an additional orientation-basis candidate:
the constructor calls `0x1E1780`, whose constants form the three identity rows
`(1,0,0,0)`, `(0,1,0,0)`, `(0,0,1,0)`. Routines `0x37A650`, `0x37A6A0` and
`0x37A6F0` extract column-like combinations. It is not yet established whether
these are the desired rendered-character basis, physical orientation or another
controller transform. Camera matrices were not substituted for player orientation.

## 4. Script-facing ID: separate from pointer identity

Registration `0x18780` names `GetLocalPlayerId`, passing the binding table at
`0x1A2E4A0`. Its first function pointer is `0x4AADC0`; that getter reads the
global at **`0x1C8A530`**, follows **`+0x10`**, then reads a 32-bit value at
**`+0x10`**, returning zero when the nested pointer is null.

The creator at `0x7578B0` calls `0x754B10`, which installs the RTTI-linked
`GameDataMan` vtable, then publishes the result to `0x1C8A530`.

```text
game_data = read_u64(module_base + 0x1C8A530)
entry     = read_u64(game_data + 0x10)
script_id_candidate = read_u32(entry + 0x10)
```

The operations and named binding association are static evidence. Whether this
value is suitable as a character/event ID, its lifetime and special values remain
pending. It is **not established as a SteamID, persistent player identity, room
participant ID or network entity ID**. The laboratory's session identities must
retain their existing independent meaning. The local-pointer predicate above
is currently the stronger way to distinguish the locally controlled character.

## 5. Verification and limitations

Performed on both installation files: expected SHA-256 and preferred base checks;
exact bytes for the local-pointer predicate, position getter, orientation getter
and cached-block accessor; `PlayerIns` inheritance/vtable consistency; confirmation
that an incorrect expected fingerprint is rejected by the probe.

The helper parses PE bytes/RTTI and invokes `objdump` only on requested ranges.
RIP-reference scanning produces **candidates**, which are checked against
disassembly before being used here. It is not a complete disassembler or
decompiler. Many locations contain jumps/thunks and intervening non-linear code;
arbitrary linear decoding can produce invalid instructions. `.pdata` ranges are
reported as unwind regions, not guaranteed complete semantic functions.

There is enough evidence for a narrowly scoped reader specification, but no
live-memory reader has been implemented here. Remaining checks include null
pointers on menus/loading/death, object/vtable identity, finite numeric values,
axes/units and facing-direction behavior, resampling across object recreation,
and separate validation with each installation's loaded modules. The shared
on-disk executable hash does not validate patches/hooks made by those modules.

## Reproduce selected probes

From the networking workspace:

```powershell
python tools/static_game_probe.py --rtti '.?AVPlayerIns@NS_FRPG@@' '.?AVWorldChrManImp@NS_FRPG@@'
python tools/static_game_probe.py --range 322730 32273f
python tools/static_game_probe.py --range 321b70 321b8b
python tools/static_game_probe.py --range 37a500 37a510
python tools/static_game_probe.py --range 37a630 37a63f
python tools/static_game_probe.py --range 2bbd20 2bbd70
```

The default target is the supplied Steam-folder executable, guarded by its
expected SHA-256. `--target PATH` selects the other matching installation;
`--output NEW_PATH` preserves JSON probe evidence without overwriting an existing
report. This tool has no game-launching or process-reading mode.
