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

## ▶ START HERE (next session)
**Solved:** reading the shell's text (`gmd_find.c`, closed-loop verified).
**Not solved:** knowing which menu item is selected.
**Immediate next step, offline, no game launch needed:** disassemble the
function containing `0x14002cce0` (the `sKeyboard` DTI registration) and follow
what it does with its result, to find where the sKeyboard **instance pointer**
is stored. Everything else about sKeyboard is already known — DTI
`0x1422c9c38`, size `0xa78`, name `0x14104e6f8`.
**First, rebuild the cross-reference index** (~55 s):
    python tools/build_riprefs.py
⚠ It refuses to load when stale rather than answering wrongly — the game
rewrites `cfc2_dump/` on every launch, and `.text` is stable but the DATA
sections are not.
**⭐⭐ The method that works, and the one that does not:**
- ✅ Find the routine that **writes** the state; read the offsets out of the
  instructions. This cracked sPad completely.
- ⛔ Searching memory for something input-shaped. Three false positives; there
  is an endless supply of look-alikes and they are indistinguishable as data.
⚠ **Do not spend the user's game launches on unverified models.** Five launches
went on tools built from inferences that turned out wrong. Verify offline
first; a launch should test a claim, not discover that one was mistaken.

## ⭐⭐ Route 5 (CURRENT): read the pad record the ENGINE writes
`src/pad_watch.c` + `tools/analyze_pad_dumps.py`. Routes 1-4 are closed with
evidence; **route 5's own first four attempts also failed**, and the reason is
the single most useful thing in this file.

### ⛔⛔ The four failures, and the one cause
1-3. **Three scanners** searched 1.4 GB for a struct recognisable only by its
shape. A shadow-copy pair, an alternating boolean, and a site frozen at one
value each passed every filter; each fix was another filter that a different
look-alike then passed. **Look-alikes are abundant and indistinguishable as
data** — recognising a pad record by its shape cannot work.
4. **A two-state diff with no same-state control.** Captures eight seconds
apart measured elapsed time, not the keypress: two runs holding *different*
keys produced the identical 109 changed bytes. Its "find" was also selected by
a filter searching for exactly the values it then reported — circular.
⭐ **The real cause of all four: I had the WRONG POINTER.** `0x22c82c8` is a
UI/dialog manager, not sPad, which is exactly why its bytes changed identically
whichever key was held. The error was reading a **0x38-byte DTI table 8 bytes
out of alignment**; with the alignment fixed the declared sizes decode cleanly
(`sPad::Pad` 0xb8, `sPad::Map` 0x50, `sPad` 0x3c4) and the boundaries are
unambiguous.

### ⭐⭐ What is now READ OFF THE MACHINE CODE (not inferred)

- **`[image + 0x22c8218]` -> the sPad instance.** Two independent sites resolve
  to it (`0x140036de1`, `0x1401880e3`), and `0x1401880ea` tests `[instance+0x38]`
  as a live enable flag.
- **Field offsets**, from the update routine at `0x14018449e-0x1401844e7`, which
  literally computes:
  `[+0x164]=old On` · `[+0x160]=new On` · `[+0x170]=On^Old` (Chg) ·
  `[+0x168]=Chg&On` (Trg) · `[+0x16c]=Chg&~On` (Rel) · `[+0x174]=0` (Rep).
  That is the textbook trigger/release derivation at exactly the offsets the
  reflection metadata names.
- **Array stride 0x2e0**, from `imul rax, rdi, 0x2e0` at `0x140036df0` plus
  three more sites, corroborated by flat offsets 0x1b8/0x498/0x778/0xa58/0xd38
  whose successive differences are all exactly 0x2e0.
  ⚠ **NOT 0xb8** — that is the DTI's *serialized* size; chasing a 0xb8 grid gave
  a 6/14 fit, which was noise.
- ⚠ **Inferred, not measured**: pad[0] at instance+0x8, and ~5 records. No count
  constant was found.

### ⛔⛔ RESULT: sPad IS A DEAD END -- and this negative is trustworthy
The layout was **proven in live memory before the negative was accepted**,
which is what distinguishes this from the session's earlier null results.
From the update loop at `0x140187b50-0x140187be9`
(`lea rbx,[r14+0x1e5]` · call with `[rbx-0x18d]` · `add rbx,0x2e0` · `cmp edi,5`):
    pad[i] = instance + 0x58 + i*0x2e0,  exactly 5 records
⚠ Base is **+0x58**, not the +0x8 inferred from the reflection registrar. The
live dump settles it: **`Pad_no` reads 0,1,2,3,4** at exactly those five
records, with `Be_flag = 1` on each. The structure identifies itself.
**But `On/Old/Trg/Rel/Chg/Rep` are ZERO in every capture**, across three
directions with clean press/release edges. Because every record has
**`Kind = 0`**, and the dispatcher at `0x1401880a0` opens with
`movzx eax,[rcx+0xb]` / `test al,al` / `je` — it returns immediately. **The pad
update never runs**; the records are allocated and numbered but no device is
bound to them.
⭐ **So the shell menus do not read the keyboard through sPad.** That is a fact
about the game, not a tooling limitation.

### ⭐ Where to look next (not yet tried)
`sKeyboard` (DTI `0x1422c9c40`) and `sMouse` are separate singleton classes
with their own null instance slots — the shell likely reads the keyboard
directly rather than through the pad abstraction. Their instance pointers can
be found the same way this one was: locate the slot, then find the routine
that WRITES the state and read its offsets, rather than searching memory.

### The tool: dump only, analyse offline
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

