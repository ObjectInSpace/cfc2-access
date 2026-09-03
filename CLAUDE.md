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

## ⚠ hashwatch CRASHED the game (2026-09-02) — read before enabling it

`hash_watch.c` is **off by default** behind an opt-in file `cfc2_hashwatch.on`
next to the exe. Delete that file to disable; that is the OFF switch.

Its first live run killed the process. Cause: the positive control passed
`&seed` — **a stack local** — to the game's CRC primitive, which writes back
through that pointer wider than the 4 bytes declared, overwriting the return
address. The call logged OK and the function's `ret` then faulted.

Diagnosis method worth repeating: the log just stops, which proves nothing.
The Windows **Application event log (Id 1000)** gave `0xc0000005` and **fault
offset 0x3dfc in DINPUT8.dll**; `objdump -d` on the deployed DLL mapped that
offset to the exact instruction. Symbols are stripped — match on RVA.

**Fixed and CONFIRMED in-game 2026-09-02** — it now installs and runs without
crashing (oversized `static volatile` scratch, never a stack local; call
wrapped in the fault guard; hooks self-uninstall if the control faults).

⚠ The fix was declared done while still INCOMPLETE. `probe_hash.c` had the same
bug in **three** places, and the third was found only by disassembling the
DEPLOYED dll and classifying every call site into `0x1402ef1e0` by how `%rcx`
is loaded — it had never crashed because `GUARDED_CALL` absorbed the fault. A
guard that catches a fault makes latent memory corruption look like a working
experiment. **Audit the shipped binary for ALL sites; re-reading the source of
the sites you changed is not verification.**

Verify such a fix in the CODEGEN, not the source: the pointer argument must
load RIP-relative (`lea 0x...(%rip),%rcx`), not `lea -0x14(%rbp),%rcx`.

## ⛔⛔ Focus detection: FOUR routes closed. Next is a CODE HOOK. (2026-09-02)

**Status: reading the shell's text is SOLVED. Finding which item is selected is
NOT. Four approaches have been closed with evidence — do not restart any of
them.** Full detail in memory `cfc2-heapdiff` and `cfc2-gui-tree-walk`.

### ⭐ What is banked and reusable
- **Text**: `gmd_find` locates and verifies the live GMD table; text reads
  correctly, closed-loop.
- **⭐⭐ A CROSS-LAUNCH ANCHOR**: `[image+0x1208bd0]` points at the live menu
  object. Verified on four launches at four different heap addresses. ASLR is
  off and there are no relocations, so image-relative offsets are stable
  forever. This is the single most valuable artifact produced.
- Infrastructure: `memsafe` fault guard, region walking, page-bulk reads,
  deadline + partial reporting, MinHook vendored, `VirtualProtect` confirmed.

### ⛔ Route 4 — differential heap scan (`src/heapdiff.c`) — CLOSED
The scanner works: **32 ms per sweep over 1.9M fields**, full page coverage,
u32 *and* u8/u16 widths, control-subtracted, reversibility-tested.
**Nothing exceeded 4 distinct values.** An 8-item list needs a field reaching 7.

⭐ **That is consistent, not a miss.** MTFramework's GUI is animation-driven:
selection is *which node holds the SELECT state* — a per-node flag, not a
central integer. Route 3 saw the same shape (0/1 toggles per item plus a few
2-3 state fields). **A scan for a flat index cannot succeed when the game does
not keep one.**

⚠ Route 4 also produced a monotonic-counter FALSE POSITIVE that passed every
filter (±1 steps, reversed, quiet in the control) — see
`anchor-on-what-changes`: controls prove a field is not noise; only
**reversibility plus plausibility** prove it is state.

### ⭐ NEXT: hook the input handler
Find the routine that reacts to Down/Up on a menu and read the selection from
the instruction that writes it. Mostly offline work on the dumped image, so far
fewer game runs. ⚠ Hooking crashed this game once
(`cfc2-seed-pointer-stack-smash`); the fault guard and opt-in gating from that
episode already exist and must be used.

### The three earlier closed routes

1. **hashwatch (text hook) — CLOSED, negative.** It runs clean now (no crash),
   and observed 381 calls across 5 game callers with **0 hashing text**, while
   its positive control passed. The label hashes are computed at LOAD time;
   lookups afterwards never re-hash the label, so there is no text chokepoint
   on this primitive. See memory `cfc2-hashwatch-result`.
2. **GUI name-pool scan — CLOSED, negative.** All **25 of 25** layouts are
   resident simultaneously, so residency is a constant and says nothing about
   the displayed screen. The focus-state counts it reported are each layout's
   STATIC file property, not live state. `src/gui_find.c` is kept as a one-shot
   inventory and as a way to locate a layout's pool. See memory
   `cfc2-gui-layout-finder`.
3. **The instantiated GUI tree — CLOSED, negative.** `src/gui_tree.c` ran
   clean: 1789 pool-referencing objects, their full ±128-byte windows AND one
   hop out, two COMPLETE passes on different screens → **zero changes of any
   kind**. The pool-anchored subtree is the loaded layout TEMPLATE and is
   static by design. See memory `cfc2-gui-tree-walk`.

   ⭐ **The entry point is a pointer INTO the name pool.** A layout does not
   reference its names by offset — searching the whole shipped file for every
   deep pool offset (u32/u64, pool-relative and file-absolute) gives **zero**
   hits. So a `char*` into the pool cannot have come from the file; it is
   positive evidence of an instantiated object. That is the exact inverse of
   the residency scan, which only found bytes memcpy'd out of a file.

   ⛔ **`gui_find`'s `pool_base`/`pool_span` are the 256 KB CHUNK, not the
   pool.** Anchoring on them tests the wrong range: the first harness run found
   **0 of 12 planted nodes while reporting a clean "complete scan"**. Use
   `bound_pool()`, which grows outward from the anchor's real hit offset.

   ⚠ **A too-WIDE span passes every end-to-end check.** Sabotaging the bounds
   back to the chunk window still found 12/12 nodes and still diffed correctly;
   only the direct assertions on `pool_base`/`pool_span` caught it. Assert the
   bounds, not just the answer.

   Run `.\tools\run_gui_tree_test.ps1` after any change to `gui_tree.c`.

   ⚠ **Hotkeys: use ordinary keys.** An earlier pass excluded all 70 keys in
   the game's `KB_*` table and landed on Scroll Lock, a TOGGLE key that never
   registered in-game — two runs wasted. That table lists what the game CAN
   bind, not what it DOES; the shell menus use arrows/Enter/Escape. **F5-F10
   all work fine.** The real hazard is narrower: NumLock-off numpad VKs
   literally ARE the arrow VKs.

⚠ Even once focus is detected, joining a focused NODE to its TEXT is unsolved:
layouts contain no GMD label hashes (0 matches across all 4165), so the binding
happens somewhere not yet found.

Reading the text itself IS solved and robust — see the GMD notes below.

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
