# Static comparison of the two local game installations - 2026-10-18

The two `DarkSoulsRemastered.exe` files have matching SHA-256 hashes and sizes.
The surrounding installations differ, particularly their Seamless launchers,
Seamless DLLs and additional modules. This establishes a shared on-disk executable
fingerprint for investigation; runtime interoperability has not been demonstrated.

Evidence: [file inventory, SHA-256 and version resources](game-installations-2026-10-18.json).

User-supplied locations and labels:

- Steam: `E:\SteamLibrary\steamapps\common\DARK SOULS REMASTERED`
- Alternate: `E:\DARK SOULS REMASTERED\DARK SOULS REMASTERED`

The labels identify the supplied paths. No publisher manifest, provenance or
authenticity validation was performed. Both folders already contain Seamless
Co-op files; the Steam folder was not established as a clean vanilla baseline.

## Measured comparison

| File/group | Steam location | Alternate location | Result |
| --- | --- | --- | --- |
| `DarkSoulsRemastered.exe` | 50286344 bytes | 50286344 bytes | Matching SHA-256 |
| `steam_api64.dll` | 245536 bytes | 245536 bytes | Matching SHA-256 |
| `xinput1_3.dll` | 107368 bytes | 107368 bytes | Matching SHA-256 |
| Bink and three FMOD DLLs | Present | Present | Matching SHA-256 for each |
| Twelve files under `param/` | Present | Present | All twelve have matching SHA-256 |
| `ds1sc_launcher.exe` | 178688 bytes | 178688 bytes | Different SHA-256 |
| `SeamlessCoop/ds1sc.dll` | 2425344 bytes | 2426368 bytes | Different SHA-256 |
| `SeamlessCoop/ds1sc_settings.ini` | 1789 bytes | 1789 bytes | Different SHA-256; contents not exposed |
| `SeamlessCoop/locale/english.json` | 3488 bytes | 3303 bytes | Different SHA-256 |
| `SeamlessCoop/crashpad/crashpad_handler.exe` | 824832 bytes | 824832 bytes | Matching SHA-256 |
| `OnlineFix64.dll`, `winmm.dll`, `OnlineFix.ini`, `OnlineFix.url`, `dlllist.txt` | Absent at root | Present at root | Alternate-only files |

In the selected scope, there are 29 relative paths: 20 matching file hashes,
four differing file hashes, and five alternate-only files. Scope includes all
root files, `param/`, and `SeamlessCoop/` except crash dumps. Other game asset
folders and `_CommonRedist` were not hashed. This is not a full-install identity
or integrity check. No conclusions about the behavior of the additional DLLs
are drawn from their names alone.

The common executable fingerprint is:

```text
SHA-256: a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b
Size:   50286344 bytes
```

Windows version-resource strings in both executable files report file version
`1,0,0,0`, product version `1` and original filename `DSR.exe`. These generic
resource values do not independently confirm the previously user-reported game
menu versions app 1.03.1 / regulation 1.04. The two
`param/GameParam/GameParam.parambnd.dcx` files match by hash, but no displayed
regulation version was decoded in this comparison.

## Relationship to earlier static investigation

Steam-folder `SeamlessCoop/ds1sc.dll` currently hashes to
`5a375a5653da1186daffe339d5c5ec3df07b62c6ef7396de65edfe0944a54f70`, matching
the target fingerprint recorded in `dsr-analysis/RELATORIO.md`.
The alternate-folder DLL hashes to
`dd71ea19ebc6abf8667fafabdb3929a35f515cafb2df22ca35ea6552c3f88bb5`.
The earlier mod RVA tables therefore identify the Steam-folder mod build; they
must not be assumed to describe the alternate DLL. Mod RVAs are also not player
data offsets in `DarkSoulsRemastered.exe`.

## Implications and remaining work

Because the game executable hashes match, a static investigation of that exact
executable can begin from one shared file fingerprint. That does not validate
live addresses, pointer chains, ASLR-adjusted locations, installed hooks or
loaded modules. The surrounding module differences still require separate
runtime validation for each installation.

The next game-facing step is static identification of candidate local-player
position structures and a narrowly scoped reader design, with explicit executable
fingerprint checks and no assumed offsets. Controlled live reading, save isolation
and official-service isolation have not been implemented or validated here.
Compatibility between the installations and gameplay synchronization remain pending.

Only file metadata and hashes were read. Neither game, launcher nor third-party
DLL was executed or loaded by this comparison. No installation file, save,
firewall rule, authentication mechanism or game process was changed. The new
artifacts are documentation and JSON evidence inside the networking workspace.
