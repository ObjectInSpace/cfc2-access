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

## ⭐ Route 5 (CURRENT): read the engine's own pad state

`src/pad_find.c`. **Four routes are closed with evidence — do not restart any
of them.** Detail in memory `cfc2-heapdiff` and `cfc2-gui-tree-walk`; the code
for those routes was deleted once closed and lives in git history.

### ⭐⭐ What the unpacked image gave up

MTFramework's reflection metadata is in the dump, in plain text, and it names
the input model directly (`sec_00fdc000_data.bin`):

- `sPad` class descriptor — size **0x3c4**, name hash `0x7db4fe86`
- `sPad::Pad` — the per-pad record, size **0xb8**
- `sPad::Map` — button mapping, size 0x50
- the five button words, in declaration order: **`Old Trg Rel Chg Rep`**
- `RepeatStartTime` / `RepeatTime` — the menu auto-repeat timing
- the 16 button bit names on a fixed stride, then the analog axes

That last one fixes the bit layout: `Rup Rright Rdown Rleft` (D-pad, bits 0-3)
then `Lup Lright Ldown Lleft` (stick, bits 4-7). So:

- **DOWN = 0x44, UP = 0x11, LEFT = 0x88, RIGHT = 0x22**
- Menu navigation (up/down) and **settings adjustment (left/right) are the same
  four bits in the same word** — one reader covers both.

Class descriptors are a uniform 0x38 bytes: `+0` name, `+40` property table,
`+48` size (low u32) + name hash (high u32). The singleton descriptors sit
together around `0x1422c8xxx`.

### ⭐⭐ Why this route is not a fifth variation on the closed ones

Every earlier route had to ARGUE that what it found was what it wanted, and
route 4 produced a monotonic counter that passed every filter it had. This one
is **self-validating**: the engine maintains an algebraic relationship between
the words every frame, and

     Prev == Old ^ Chg
     Trg  == Old & ~Prev
     Rel  == Prev & ~Old

is three words constrained by two equations — random data satisfies it with
probability ~2^-64 per eventful sample. That is a positive identification test,
not a correlation.

⚠ **`Old` holds the CURRENT state, not the previous one** — by the time the
record is observable it has already been updated. Reading it the other way
round makes the true window fail its own test; the scanner then arms a shifted
window whose last word runs past the record, and the real candidate dies a few
frames later. The self-test caught this before it ever ran in-game.

### Usage: none. It searches by itself.

The mod starts the search once the image unpacks and re-seeds every 250 ms
until a press makes the record visible. **The player just navigates.**

- **F6** = report status · **F7** = speak the direction currently held ·
  **F5** = restart the search.
- OFF switch: create `cfc2_pad.off` next to the exe.

⚠ An earlier design required "hold a direction while pressing F5". That worked
but pushed a protocol onto the player, and **a measurement that needs the user
to remember something is the wrong measurement** — instrument it instead.

### ⛔⛔ The all-zero rejection is load-bearing

All-zero windows satisfy the invariant trivially and a process is mostly
zeros: **measured 57k matches in 1.8 MB**, extrapolating to tens of millions
across this game's ~1.5 GB. Without rejecting them the candidate table fills in
the first few megabytes and never reaches the pad — the same "a fixed budget
silently becomes a tiny window" failure this project has now hit three times.

⭐ **The fix is a filter that DISCRIMINATES, never a bigger cap.** With zeros
rejected the sweep finds 5-8 candidates instead of 65536.

The consequence — a sweep only matches while a button is DOWN — is handled by
re-seeding, not by asking the player to hold a key.

### ⚠ The self-test is only a test because it was proven to FAIL

Five sabotages, each caught by the assertion that exists for it:

- `consistent()` forced TRUE → the decoy is accepted (cases 2/3/5)
- quiet samples counted as transitions → everything breaks (8 failures)
- a violating site not killed → the pad is not identified (cases 2/3/5)
- `PAD_CONFIRM_EVENTS = 1` → identifies on one transition (case 5)
- all-zero windows accepted → **the cap binds (case 1)**

⚠ **Two of these passed at first.** The cap sabotage was invisible because
nothing asserted on coverage — the log said "INCOMPLETE" while every check went
green. And case 5's boundary took two attempts to get right: seeding does not
count as a transition, and driving the full sequence gives many, so the case
must drive **exactly one** transition after seeding or it tests nothing.

### ⚠ MinHook is no longer linked

Routes 1-4 are gone and everything shipping is read-only, so the build does not
link MinHook. It stays vendored under `libs/` for a future in-game feature.
Hooking crashed this game once (`cfc2-seed-pointer-stack-smash`): the fix was
never a stack local as an out-param, and verification belongs in the CODEGEN
(`lea 0x...(%rip)`, not `lea -0x14(%rbp)`), auditing every site in the SHIPPED
binary rather than re-reading the sources you changed.

### ⛔ A false lead worth not repeating

`0x11/0x22/0x44/0x88` tested in that order appear ~24 times in
`0x14044d000-0x140453000` and look exactly like four-way menu dispatch. They
are **quadrant masks in SIMD collision code** — 0x1140-byte stack frames, heavy
`movaps`. Those four constants are just the 2-bit patterns and are not by
themselves evidence of input handling.

### The four closed routes

1. **hashwatch (text hook) — CLOSED, negative.** 381 calls across 5 game
   callers, **0 hashing text**, positive control passing. Label hashes are
   computed at LOAD time. Memory `cfc2-hashwatch-result`.
2. **GUI name-pool scan — CLOSED, negative.** All **25 of 25** layouts resident
   simultaneously, so residency is a constant. Memory `cfc2-gui-layout-finder`.
3. **Instantiated GUI tree — CLOSED, negative.** 1789 pool-referencing objects,
   full ±128-byte windows AND one hop out, two COMPLETE passes on different
   screens → **zero changes of any kind**. It is the layout TEMPLATE. Memory
   `cfc2-gui-tree-walk`.
4. **Differential heap scan — CLOSED.** 32 ms per sweep over 1.9M fields, both
   widths, full page coverage, control-subtracted and reversibility-tested:
   **nothing exceeded 4 distinct values**, where an 8-item list needs 7.
   ⭐ Consistent, not a miss — the GUI is animation-driven, so selection is
   *which node holds the SELECT state*, not a central integer. Memory
   `cfc2-heapdiff`.

### ⭐ Still banked and reusable
- **Text**: `gmd_find` locates and verifies the live GMD table, closed-loop.
- **⭐⭐ A CROSS-LAUNCH ANCHOR**: `[image+0x1208bd0]` reaches the live menu
  object, verified on four launches at four different heap addresses.
- Infrastructure: `memsafe` fault guard, region walking, page-bulk reads,
  deadline + partial reporting, `VirtualProtect` confirmed working.

⚠⚠ **`MEM_GUARDED` is inert unless `mem_guard_install()` ran.** `pad_find.c`
used the macro without arming it and the self-test died with `0xC0000005`. An
inert guard is worse than none: every read LOOKS protected.

⚠ **Anything living across `setjmp` must be `volatile`** or `longjmp` may
restore a stale value — GCC's `-Wclobbered` is right, and in the scan loop that
would rewind the cursor.

⚠ Even once focus is detected, joining a focused NODE to its TEXT is unsolved:
layouts contain no GMD label hashes (0 matches across all 4165), so the binding
happens somewhere not yet found.

Reading the text itself IS solved and robust — see the GMD notes below.

## Dumping and analysing the unpacked image

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
