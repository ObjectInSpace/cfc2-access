# CFC2 Access

Screen reader accessibility mod for **Capcom Fighting Collection 2** (Steam
appid **2400430**). Goal: make the collection playable by a blind player —
NVDA primary, with braille and SAPI fallback via UniversalSpeech.

## Formatting rule

The user is blind and uses NVDA. **Never use tables** in replies or docs — use
lists. Keep line-based output linear and screen-reader friendly.

## Game

- Dir: `C:\Program Files (x86)\Steam\steamapps\common\CAPCOM FIGHTING COLLECTION 2`
- Launch **via Steam** (`steam://rungameid/2400430`).
- Engine: **MTFramework**, internal codename `DINGO_2400430`. x64,
  ImageBase `0x140000000`, **ASLR off, no relocations** — addresses found in a
  live dump are stable across runs forever.
- Exe is wrapped in **Enigma Protector**. Static analysis of the file on disk is
  useless (all code sections entropy 8.00, all strings garbage).

## ⭐ The two worlds — read this before choosing any technique

The game is two different things and they need different approaches:

- **The MTFramework shell** — title, main menu, game select, options, pause,
  move lists, museum, training, lobby. Real text, real GUI tree. 25 layouts in
  `arc/pc/menu.arc`; every interactive one has `DEFAULT`/`SELECT`/`DECIDE`/
  `DISABLE` animation states, so selection is modelled and hookable.
- **The 8 emulated arcade games** — CVS1, CVS2, CFJ, Z3U, PWS1, PWS2, MJUS,
  STG2. Each `arc/pc/game_XX.arc` holds one blob that inflates to an `IBIS`
  container wrapping an `AFS` archive: raw CPS-2/Naomi ROM data.
  **In-game text is drawn as sprites by the original arcade code. There is no
  string to hook.** Character names, gauges and round state must come from
  emulated machine RAM, not from a text hook.

## Scope (user's call — do not silently re-order)

**BUILD ORDER: the MTFramework shell FIRST**, then in-game state.

⚠ An earlier note here said "in-game first". That answered *what to
investigate* first, not *what to build* first, and the user corrected it
explicitly: "we should build the shell stuff first, I just wanted to get the
investigation done so our plan covers all the bases." The in-game investigation
is complete and recorded (audio design, prior-art RAM schemas); nothing is lost
by building the shell first. Do not re-invoke the old note to reorder.

The in-match audio design is fully settled — **everything in-game is an audio
cue, speech only on character select**; two per-player beacons with pan = X
position and pitch = a stance ladder. See memory `cfc2-audio-design-decision`
before touching that work.

## Build

- Toolchain: WinLibs MinGW-w64 GCC 16.1 (winget, user scope). Path in `build.ps1`.
- `.\build.ps1` builds `build\dinput8.dll`; `.\build.ps1 -Deploy` also copies
  the payload into the game dir.
- Log: **`CFC2Access.log`** in the game dir — truncated each run, so it always
  reflects the current run only. First thing to check.

## Architecture

- `src/dinput8_proxy.c` — the exe imports exactly one symbol from dinput8.dll
  (`DirectInput8Create`), and its own directory wins the DLL search order.
  DllMain stays minimal (loader lock): it only spawns the worker thread.
- `src/mod.c` — worker thread, probes, heartbeat.
- `src/image.c` — section walk, the unpack wait, and the image dumper.
  **Every read is guarded by `VirtualQuery`**: Enigma decommits its own
  unpacking scaffolding, so section headers describe memory that is gone.
- `src/speech.c` — UniversalSpeech, loaded dynamically so a missing DLL
  degrades instead of killing the process.
- `src/log.c` — timestamped logging.

## Phase 1.3 workflow — finding the shell's text chokepoint

Plan (user's call): **text hook first, then focus**, because the text hook can
be verified against ground truth and a focus hook cannot.

1. Launch the game once with the mod deployed. `image_wait_for_unpack()` polls
   for decryption, then `image_dump()` writes `cfc2_dump\` into the game dir
   (one `.bin` per section plus `manifest.txt`). The mod **speaks** the result,
   so no need to read the log for the headline.
2. `python tools/analyze_dump.py` — locates label hashes, the CRC-32 machinery
   and GMD artifacts in the dump, reporting **live addresses** (valid forever:
   ASLR off, no relocations).
3. `python tools/test_analyze_dump.py` — a self-test that plants known
   artifacts in a synthetic dump and requires the analyzer to find all of them.
   Run it after changing the analyzer: "found nothing" and "detector is broken"
   otherwise look identical.

⚠ The dump is large and game-owned; it is gitignored, not committed.

## ⭐ Injection facts (measured in-game, 2026-09-01)

- A **plain dinput8 proxy survives Enigma**. No bypass needed. `VirtualProtect`
  on `.text` succeeds, so MinHook detours are installable.
- **`.text` is still ENCRYPTED when DllMain runs.** It decrypts ~10 s later
  (protection flips RWX `0x80` → RX `0x20`). **Never scan, hook or dump at
  startup** — poll for unpack first. Readiness test already in `mod.c`: sample
  4 KB and count distinct byte values; >240 = still packed, ~85–235 = real code.
- Ship **`nvdaControllerClient.dll`** next to the exe. UniversalSpeech loads it
  by that exact unsuffixed name and **silently falls back to SAPI5** without it.
  A SAPI5 backend while NVDA is running is a bug, not a degraded-but-fine state.

## Data formats (parsed and verified — see memory `cfc2-arc-gmd-format`)

- `arc/pc/*.arc` = ARC v7, 80-byte entries, 64-byte names, zlib payloads.
- `arc/pc/msg.arc` holds 14 `menu_<lang>` **GMD** files, 4165 labelled strings
  each: full character rosters (233), 1696 move names, 254 `HELP_*` blurbs.
- ⚠ CFC2's GMD uses a **0x20-byte 64-bit label entry** with an int64 bucket
  table, not the 0x14 form public GMD tools assume — they misparse it. Ours
  is verified against all 4165 label hashes
  (`~crc32(label*2)` / `~crc32(label*3)`).
