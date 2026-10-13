# CFC2 Access

## Implementation update, 2026-10-13

The user wants the required reusable mechanisms completed before handing their
ports to Opus, not an infrastructure demonstration. See the
[implemented interfaces and remaining shared work](docs/collection-porting-interfaces.md). CVS2 now uses the
worker-owned selection channel, shared presenter and existing speech backend
in the DLL build; its native acquisition is retained. Earlier descriptions of
those selection components as offline-only are historical. Opus should reuse
this implemented selection path instead of rebuilding it. Shared gameplay,
transition and arena-audio work still has explicit gaps; do not claim all
feature families are ready. Finish the mechanism needed for the next feature
without reopening the optional protected-reader redesign. This update governs
conflicting implementation status and next actions below. Live acceptance of
the changed DLL is not yet claimed.

## Current direction, 2026-10-13: deliver CVS1 selection speech

The [corrected Opus execution plan](docs/port-tasks/opus-shared-selection-execution.md) supersedes conflicting sequencing
and model assignments below. Preserve the existing CVS2 native acquisition
path. Reuse shared presentation and implement only what the scoped CVS1 feature
needs. The optional protected CVS2 reader replacement, global atomic output
dispatcher and complete collection-wide lifetime investigation are deferred;
they are not prerequisites for this pilot or sharing already-copied data.
CVS1 still needs evidence-backed names and an appropriately justified acquisition
path. Resolve concrete feature blockers without claiming unproved safety.
Existing stronger-design contracts remain valid for those designs if pursued,
but do not govern a smaller compatibility-preserving presentation integration.
Historical research and captures remain evidence, not automatic next actions.

Current collection-port direction: follow [the Astra-to-Opus workflow](docs/collection-workflow.md)
and [staged port plan](docs/collection-port-plan.md). Astra leads upfront tracing,
shared-system proof and difficult reviews; Opus implements evidence-backed task
packets. Every integration path needs tracing, and shared-system changes must
propagate across games unless a documented game-specific reason applies.
The [next Astra session brief](docs/collection-astra-brief.md) records the starting
point. This workflow does not authorize assuming an untraced field or claiming
an offline test proves live behavior.

The optional [collection boundary probe](docs/collection-boundary-run-card.md)
is implemented for the next lifecycle capture. It is off unless
`cfc2_collection_probe.on` is present, respects `cfc2_hook.off`, and is not a
production session router. Its new runner brings the regression set to 29.
Do not treat synthetic probe CSVs under `build/test` as live evidence.

For CVS2 in-game research, read [the 2026-09-18 prior-art audit](docs/cvs2-prior-art-audit.md)
before relying on the historical conclusions below. It corrects the NAOMI RAM
size, the claimed coverage of SH-4 searches, and the guest-pointer mapping
experiment. CVS2's native character-highlight state is now traced and live
checked; see [the state-access trace](docs/cvs2-state-access-trace.md).
`src/cvs2_select.c` reads that state on the existing speech worker and calls
`speech_say`. Groove and ratio handlers are also traced and included in the
pending combined build; the alternate native stage-selection handler is now
code-traced but not live-validated. See the trace for
the distinction between real captures and synthetic regression fixtures.

Screen reader accessibility mod for **Capcom Fighting Collection 2** (Steam
appid **2400430**). Goal: make the collection playable by a blind player —
NVDA primary, with braille and SAPI fallback via UniversalSpeech.

## ⭐⭐ THE TEXT READERS ARE CONSOLIDATED (2026-09-26)

`src/text_channel.h` / `.c` (+ `src/text_capture.c`, the render-thread half) is
the one speech and read-current-text layer. Full detail in
[docs/shared-text-channel.md](docs/shared-text-channel.md). Four readers --
`cvs2_text`, `cvs2_story`, `cvs2_select`'s win quotes, and the shell's
`menu_speak_line` -- each re-implemented the same five mechanics: narrow->UTF-16
conversion, a render-thread capture buffer, session invalidation, de-duplication,
and a drain that advances only on backend acceptance.

⭐ **The fifth is the one worth centralising on its own.** Every reader had to
learn it separately, and `speech.c`'s comment records the cost: a log full of
tidy "spoke" lines while the user hears nothing. `tc_say()` is now the single
place coupling "said it" to "mark it spoken".

⛔⛔ **WHAT WAS DELIBERATELY NOT UNIFIED: the de-dup POLICY.** The three CVS2
readers key suppression differently and every difference is load-bearing:
text on the TEXT with a 2 s re-arm (a blinking prompt), story on the ENTRY INDEX
with 1.5 s and FIFO (every page, in order), quote ONE SHOT per root needing two
identical reads to settle (the buffer is read mid-animation). A single policy
would be their union, and the union is wrong for all three. So the caller picks
a primitive -- `tc_textdedup` / `tc_keydedup` / `tc_settle` -- and the layer
supplies mechanism only. That is also what makes a port cheap.

⭐ **The ``` ` ``` key (read current text) goes through a REGISTRY, not a branch
per game.** A source registers
a name and a read function; the first that speaks wins, and registration order
IS priority order (games register at install, the shell last). Porting another
title does not touch `mod.c` or the hotkey. Every outcome has its own sentence
-- nothing registered / not active here / nothing readable / no fresh frame --
because collapsing those into silence is what makes a key undiagnosable without
sight.

⚠ The shell source repeats what was **actually spoken and accepted**, never the
prose recorder's history. That history is a history, not a screen; the
text-source audit declined a read-all over it and that still holds.

**Tests:** `.\tools\run_tc_test.ps1` (11 cases) and
`python tools\sabotage_tc.py` (9 sabotages, all verified to fail the case that
tests them). ⚠⚠ **SEVEN runners needed `text_channel.c` on their link lines** --
exactly the "an un-buildable runner looks like a passing one" trap below. Run
all 28 after touching this layer, not a chosen few.

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

## ▶▶ IN-GAME (world 2) STAGE 1: the emulator anchor is FOUND — needs one run

**2026-09-16. Derived OFFLINE from `cfc2_dump/` + a rebuilt `riprefs.pkl`.
Zero game launches.** Full detail in memory `cfc2-emu-state-object`.

    emu_state = *(void**)0x141c14f98      ; the emulator session singleton
    emu_arena = *(void**)emu_state        ; 256 MB, page aligned (state+0x00)

**Exactly ONE instruction writes that slot** (`0x140048270`, the constructor);
**3070 read it.** That asymmetry is what makes it trustworthy. The constructor
is an allocation sequence: `alloc(0x10000000, 0x1000)` through the allocator
singleton at `0x14221ef88`, stored to `state+0x00`.

Known fields, read off the instructions that set them:

    +0x00 arena   +0x30 ROM name ptr   +0x44 ROM index 1..18   +0x4c derived id
    +0x40/+0x48/+0x50  descriptor bytes from the 0x40-stride table 0x1411f5f18

`rom_index_from_name()` is `0x1400b6250` — walks the 18-record table at
`0x141208b00`, returns a **0-BASED** index (`0 cvs1 … 3 cvs2u … 17 a3uu`).

⛔⛔ **I first read it as 1-based, and the live cross-check caught it.** The
loop looks 1-based because entry [0] is loaded BEFORE the loop and compared
with the counter still at zero; the `+8` in the body advances past the entry
already consumed. Two consequences, both fixed: every game was shifted by one,
and **index 0 (cvs1) is a VALID game**, not "nothing matched" — treating it as
NOT_RUNNING would have silenced exactly one game, invisibly.

⚠ That index is over REVISIONS, not the 0..7 game index;
`emu_game_from_rom_index()` maps between them with a TABLE, because the
plausible formula `rom/2` is right for 16 of 18 rows and wrong for
`z3uu`/`a3uu`.

⚠⚠ **THE ARENA IS NOT GUEST RAM.** 256 MB is the whole emulator arena; NAOMI
system RAM is 16 MB and lives somewhere inside it. Which sub-range is the NEXT
thing to prove — `src/emu_ram.h` says so rather than implying the work is done.

### ⛔⛔ THE FIRST GUEST-RAM SCAN WAS A FALSE POSITIVE (2026-09-16)

It reported **16 candidates**. All were one run of repeating `0x0222` words,
matched at 16 consecutive dword offsets — 4-12 bytes apart, every one reading
`hp 546 / 546`.

⭐⭐ **A signature built only from RELATIONSHIPS cannot reject a CONSTANT.**
`mx != 0`, `mx1 == mx2`, `mx <= 0x2000`, `hp <= mx`, "not both zero" are ALL
true of `0x0222` at every alignment, forever. Uniformity is the best-behaved
input a relational test can receive, and memory is full of it. Fixed by adding
a CONTENT test (`looks_uniform()`) — see memory
`relational-filter-cannot-exclude-a-constant`.

Two more faults the same run exposed:
- **The 16-hit cap ended the SWEEP**, leaving 99.8% of the arena unexamined
  and reporting the truncation as the answer. `n` now counts ENCOUNTERS and the
  sweep always completes.
- **"examined 0 MB" beside 16 hits** was integer division, not a measurement.
  Reports KB now, and names the arena size it is a fraction of.

⭐ The tell that would have named it on sight: **all 16 hits fitted in 0x1c
bytes.** The log now says so explicitly when the whole candidate set fits in
4 KB — that is ONE region matching repeatedly, not many structures.

### ⛔⛔⛔ THE SCAN STALLED THE MOD FOR 114 SECONDS (2026-09-16, fixed)

The full-arena sweep called `mem_readable_run()` -- a `VirtualQuery` syscall --
**once per 4 KB page**: ~65,000 syscalls per sweep. Speech was dead for nearly
two minutes.

⚠⚠ **FOURTH time this project has hit that exact wall** (`gmd_find`, the
heapdiff capture, the span scan, now this), and the comment warning about it
was directly above the code that did it.

⚠ **The heartbeat HID it**: it logs `beats * 2`, so a stalled worker
under-reports its own age -- 126 s of wall clock read as "alive 20 s". A
liveness counter derived from the loop it monitors cannot detect that loop
stalling.

**Fix:** one `VirtualQuery` per REGION then 256 KB block reads; a 25 ms
wall-clock budget checked in the INNERMOST loop; and RESUMPTION so coverage
still completes across sweeps. **Measured: 256 MB in 125 ms over 8 sweeps,
worst sweep 32 ms** (was 114,600 ms).

⭐⭐ **The trigger worth keeping, since remembering the rule has failed four
times: before any loop over memory, COUNT THE SYSCALLS it will make.** If that
number scales with items rather than regions, it is already too slow -- no
profiling needed, just multiplication.

### ▶ THE RUN THAT IS NEEDED (cfc2_emu.on is already deployed and on)

Start **any** game and read `emu:` lines in `CFC2Access.log`. What they answer:

- `emu: ✅ cross-check OK` + a plausible arena → **the anchor is live**, and
  the next work is locating guest RAM inside the arena.
  ⭐ **First run (2026-09-16) reached this after the 0-based fix.** It reported
  `rom_index=3 name='cvs2u'` — the user was playing CVS2 (US), read correctly.
  Arena `0x7d81000`, VirtualQuery region **511 MB** (⚠ not the 256 MB the
  constructor requests — use the MEASURED size, and do not assume the arena
  starts at the region base).
- `emu: ⛔⛔ MISMATCH` → the field map is wrong; do not build on the offsets.
- `emu: no game is loaded` while a game IS running → the slot is not what we
  think, or it is populated later than expected.
- `emu: ⚠ COULD NOT read` → **not a negative.** Conclude nothing.

`.\tools\run_emu_test.ps1` — 59 cases, sixteen sabotages each verified to fail the
case that tests it. Case 1e pins the live pairing (index 3 ↔ `cvs2u`) as a
regression, so the mapping is anchored to evidence rather than to my reading of
the disassembly, which was wrong once.

⚠ Two false leads, recorded so they are not re-chased:
- **`mCTREmulation`** (`0x14101acc8`) is NOT a class name — it sits in a
  reflection *property-name* pool between `mBloomColor` and `mSunDir`.
- **Scanning code for size immediates** is useless here: 13007 raw occurrences
  of the 16 MB byte pattern. Chasing common constants produced this project's
  earlier false leads.

⚠ Tooling: `riprefs.pkl` goes stale silently and its guard correctly refuses it
— rebuild first (~55 s). **Capstone's linear sweep over a whole code section
yields NOTHING** here; search byte encodings like `find_callers.py` does.

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

### ⛔⛔ SPEECH LAGGED KEYPRESSES AGAIN (2026-09-19, fixed)

User-reported: "we're back to the state where keypresses cause the speech
announcement to become delayed." Diagnosed by COUNTING, not profiling, per the
rule below — and the count was damning.

**Cause 1 — `cvs2_select_tick()` ran the whole validation at 100 Hz.**
Ten `read_at()` calls, each a `mem_readable()` → **1000 `VirtualQuery` per
second**, plus a **134 KB** guarded copy every tick = 13 MB/s, on the same
worker thread that speaks. ⚠ **FIFTH time this project has hit the
VirtualQuery wall** (`gmd_find`, the heapdiff capture, the span scan, the
arena sweep, now this).

Fixed three ways, none of which weakens a guard:
- the `.text` signature check is **hoisted to once** — `.text` is immutable
  (ASLR off, no relocations), so its answer cannot differ between ticks;
- **one 8-byte read** of the session slot decides whether to go further, and
  it stays on every tick so leaving a match still silences the gauges
  promptly — throttling the INACTIVE path would hold a tone over the next
  screen;
- everything past it, including the 134 KB copy, runs at **33 ms**.
  ⭐ That is FASTER than the game's own 60 fps frame, so no selection can
  appear and vanish between samples; the old 2 s-poll defect needed a gap
  sixty times larger. Health is unaffected either way — `cvs2_frame.c` feeds
  it on the frame hook precisely so this poll's rate cannot make it stale.

Measured: **1000 → 370 VirtualQuery/s, 13.1 → 3.9 MB/s.**

⛔ What was NOT done: dropping `mem_readable()` before the copies. Case C of
`run_span_test.ps1` still passes today — a guarded `memcpy` across a bad page
is NOT caught. The fault guard is a net for the race, never a replacement.

**Cause 2 — ⭐⭐ THE SAME BUG IN THREE PLACES: a change-guard on a counter
that always changes.**

    if (fed == last_fed && rejected == last_rej) return;   // never suppresses

`fed`/`rejected` increment twice per tick during a match, so the guard was
true on every call: **2741 `gauge:` lines in one 5-minute session, one every
~25 ms**, each a file write from the speech worker. This is verbatim the bug
already found and fixed in `cvs2_frame_log_stats()` — whose own comment
explains it — and it was left standing in `cvs2_gauges_log_stats()`.
⚠ **Knowing a trap is not the same as encoding it.**

The third instance: `gt_poll_and_log()`'s "log only DISTINCT strings" filter.
It holds for labels and **fails completely in a match**, because the emulated
game redraws NUMBERS — "188", "191", "198" are all distinct, so every redraw
passed. 612 of 942 `gt:` lines, ~50 ms apart.

⭐⭐ **The rule: a guard asking "is this different from last time?" is no
guard at all against a value that is different every time.** Rate-limit by
TIME and report a RATE a human can act on.

### ⭐⭐ THE gt SWITCH IS SPLIT: SPEECH AND DIAGNOSTICS ARE NOW SEPARATE

`gt_ok` gated **18 call sites**, and they had opposite audiences. Three are
shipping speech a blind player depends on:

- `menu_speak_check_modal()` — the quit / consent dialogs
- `menu_speak_check_hint()` — the hints view
- `gt_poll_and_log()` — feeds `gt_take_caption()`, i.e. the per-row captions

The other fifteen are development reports. Because ONE file switched both,
the only way to stop the diagnostics flooding the log was to turn the hook
off — **which silenced the quit dialogs.** That is a real trade I hit and
had to back out of within the same session.

Now:

- **The hook is always installed** (2026-10-02; it used to need
  `cfc2_guitext.on`, and the v0.2.0 zip shipped without it, silencing these
  for every user). Modal prompts, the hints view and per-row captions all work.
- **`cfc2_guitext_diag.on`** → *additionally* print the development reports
  (focus record, instance dump, layout/movelist probes, stats lines, the
  live-text transcript). Absent = off.

⛔⛔ **SPEECH NEVER READS THE FOCUS RECORD (2026-10-04).** The quit prompts,
hints view and per-row captions used to SEARCH that 48-entry history. Once
the diagnostics report (its only clear) was switched off, it filled ~48
strings into a session and all three went silent ("sometimes the confirm
quit yes/no prompts are not reading"). User: "we shouldn't be relying on a
buffer that can fill up to determine what's currently being spoken." Now the
hook fills one-value SLOTS on each text CHANGE -- newest prose (+ move stamp,
serial) and the modal slot (prompt matched against texts the worker
publishes in the current language, its two options, a serial per opening).
The record is diagnostics only. ⚠ Lesson: a diagnostic's side effect (the
clear) was load-bearing for shipping code, and gt case 19 hid it by calling
the clear itself.

⚠ `gt_poll_and_log()` still runs unconditionally — only the log line *inside*
it is switched. The caption channel needs the poll; it does not need the
transcript.

**Cause 3 — development switches were left on during play.** The game dir had
`cfc2_cursor.on`, `cfc2_emu.on` and `cfc2_snap.on` live: 4716 log lines in
~5 minutes (~16 file writes/second) plus a continuous arena scan. CLAUDE.md
already says of the cursor probe that each arrow press costs a full hash pass
over the arena and it is "not something to impose on ordinary play."
Renamed to `.disabled` (not deleted) — one rename away from returning.

⭐ **Before any loop over memory, COUNT THE SYSCALLS it will make.** And
before shipping a diagnostic, ask whether it needs to run at all.

### ⭐ PER-CUE VOLUME KEYS (2026-09-19)

    F5 health · F6 super meter · F7 beacons · F8 clock tick

The key steps **UP**, shift+key steps **DOWN**, both **wrapping** on
0/25/50/75/100. Default is 100, so **one press mutes** — the user's design:
the reason to mute is usually that the cue is in the way *right now*.

- ⭐ One level per **cue FAMILY**, not per voice. P1 and P2 share a level —
  they are the same cue heard from two sides, and turning one player down
  would make the pan readout lie about which side is which.
- ⭐ Levels multiply the cue's **designed gain**, so 100 is exactly the
  balance tuned by ear and an ini with no `vol_*` keys sounds as before.
- ⭐ Applied as a GAIN, never a skip: a muted cue still runs its voice and
  smoother, so muting **fades** (~7 blocks, measured) instead of clicking.
  Case 19 pins that fade in both directions.
- Persisted to `cfc2_audio.ini` (`vol_health`/`vol_super`/`vol_beacon`/
  `vol_tick`) on each press. Speech is the ONLY feedback — there is no
  on-screen indicator, so an unspoken change is an invisible one.
- ⭐ In-match audio is ALWAYS ON (2026-10-02). It used to need `cfc2_audio.on`,
  and the v0.2.0 zip shipped without that file, so no user heard any cue.
  ⭐⭐ **Shipping features are never behind a switch file.** Switch files are
  for diagnostics only; `cfc2_hook.off` remains the read-only kill switch.

⚠⚠ **THE KEYS WERE CHECKED, not assumed** (the user asked directly, and the
first two proposals were both wrong):
- **F12 is Steam's default screenshot key** — it would snap a screenshot on
  every press. Do not give it to anything used during play.
- CFC2 itself **binds no function keys**; its 16 keyboard-settings rows
  (`g_kbd_settings_rows`) are gameplay actions on letters and arrows.
- The `KB_F10` in `gui_markup.c` and `markup.py` is an **example in a comment**
  about the mechanical `KB_A -> "A"` rule, NOT a binding. ⭐ A grep hit inside
  a comment looks exactly like a real one.

⛔ **THE F7 PAD-DUMP HOTKEY WAS REMOVED** (user: "we don't need that, I don't
think we ever even used it"). It was a manual override for a route CLOSED with
evidence — every sPad record has `Kind = 0`, so the update never runs.
`pad_auto_capture()` still runs under `cfc2_pad.on`, so the diagnostic is
intact; only the unused trigger is gone. That is what freed F5-F8 as one
physical four-pack the user can reach by feel mid-match.

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

## ▶▶ START HERE: items 1-4 DONE and VERIFIED IN-GAME

Run 2026-09-09 22:50 (`docs/run-2026-09-09-2250.log`). Clean: 306 setText
calls, **0 faults**, no crash. The mod speaks the focused row's own
description after its name:

    spoke "Offline Play, 1 of 6. Play offline."
    spoke "Game, 1 of 3. Choose the game you want to play."

### ⭐⭐ Item 4 is ANSWERED: per-row captions DO flow through setText

    [move 0] PROSE "Play offline."
    [move 1] PROSE "Play online using a network connection."
    [move 2] PROSE "Play offline."

A different prose caption under each successive move number, each matching the
row moved to. **0 wrong-row captions in 22 movements.**

⭐ Both guards fired correctly in the wild: `"Choose a game version."` was
refused as AMBIGUOUS (the collision predicted offline), and `"P1"` was refused
as not-in-corpus -- that one is a player VALUE label, and without the corpus
check it would have been announced as the Game row's description.

⚠ **Captions are NOT universal -- only 4 of 18 spoken lines carried one.** The
screen at caller `0x14009BA21` produced none at all (setText froze at 306
across its 7 movements). So the caption channel SUPPLEMENTS cursor+GMD; it does
not replace it. Do not build anything that assumes every row has one.

### ⭐⭐ THE STRIP AFTER CHOOSING A GAME = ARCADE / VERSUS / TRAINING

Caller `0x1400b2729`, 3 items, horizontal -- the screen reached after picking a
game. Now named, from the game's own live text (position 3 "Training Mode",
position 1 "Arcade Mode"). Position 2 ships on being bracketed by two confirmed
endpoints plus the ascending option-id setup, which is weaker and is flagged.

⛔⛔ **This killed `g_game_options_candidate`**, a hypothesis held for days:
"Game Settings / Controller Settings / Display & Sound Settings", reasoned from
the layout name `11_option/option_2`. It fit the count, the category and the
non-contiguity -- and was about a different screen. Its own comment had named
the flaw ("the layout name proves the screen is an options screen, not which
labels it draws") and I still nearly wired it up.

⚠ Deleted rather than commented out: `tools/test_screen_maps.py` scrapes by
regex and does **not** skip comments, so it was reporting `ok` for a disproven
array. Validation confirms a map matches the CORPUS; it says nothing about
whether the map belongs to the SCREEN.

### ⭐⭐ THE OPTIONS SUBMENUS ARE SETTINGS SCREENS (2026-09-10)

System / Music (Sound) / Network / PC Settings are each a **row selector + value
selectors** sharing one screen object -- `MC_SETTINGS_ROW` + `MC_SETTINGS_VALUE`,
the shape Select Game already used. They were unnamed only for want of evidence.

**Shipped:** Network Settings (new), and Sound + PC Settings converted so they
speak VALUES as well as rows ("Display, Windowed, 1 of 3").

⚠⚠ **Every screen's offsets DIFFER** -- `values_off` is 0x338 / 0x358 / 0x35c,
and Network's row cursor is +0x334 where the others use +0x330. Read them from
that screen's own log offsets; never copy another screen's. Case 20b asserts it.

⭐ The standard that made these shippable: **two independent sources per row** --
the caption names it, and the value-slot arithmetic
(`(cursor - screen - values_off)/4`) confirms the ORDER. Both agreed on every
move.

⛔ Rows whose values are GENERATED (volume sliders, Input Delay) get no variant;
the live-caption quote path speaks those. A quote of what the game drew is not a
claim about a label that does not exist.

⭐⭐ `tools/test_screen_maps.py` now validates **value sets** too, which nothing
did before -- a wrong `first` would have spoken the wrong words for a setting
with no test catching it.

### ⭐⭐ OPTIONS > SYSTEM IS NAMED (all 6 rows) -- the slow pass worked

    1  180  Language          2  547  Menu Button    3  549  Start Button
    4  551  Coin Button       5  182  Data Collection 6  168  Default

Values named where a run exists: the 14 display languages, and Decline/Agree.
Button rows are controller glyphs, not label runs -- the live-caption quote
path speaks those.

⚠⚠ **THE PREVIOUS RUN GOT 1 OF 6 WITH IDENTICAL CODE**, because it was walked
at ~300 ms per row. **A caption needs ~1 s of dwell or it never arrives.**
Remember this before concluding a screen "has no captions" -- it is a property
of the channel, not a bug.

⭐ NEW SHAPE: **four** value callers drive this one screen (0x1400a3431,
0x1400a3538, 0x1400a3578, 0x1400a34a8), sharing values_off 0x338. Each needs
its own `g_screens` entry and all four must carry the same `name` so
`menu_speak_sibling_row` finds the row map. Case 20d asserts all four -- a typo
on the FOURTH is caught only by checking every one.

### ⭐⭐ THE MUSIC TRACKS ARE NAMED -- from the game's own live text

`gt_numbered_item()` + `MC_NUMBERED_LIVE`. Museum > Music, caller `0x1400b2ebb`.
**Built and offline-tested; NOT yet heard in-game.**

Track titles are reachable by nothing else: they are **not GMD labels**, so no
base and no map can name them, and **the caption channel is blind here** -- the
list's text is set ONCE on entry (5 sets per object, all one move seq) and moving
the cursor sets **nothing**, so `gt_take_caption` always returns empty. Measured
in `docs/run-2026-09-10-1343.log`, not assumed. That is the same structural
reason the consent dialog is unreachable, and it is why this needed a LOOKUP
rather than a movement-stamped mailbox.

⭐⭐ **The join is the PRINTED NUMBER.** The cursor says "position 2", the game's
text says `"02. Game Select: Overdrive Blaze"`, and a name is spoken only when
those two independent sources agree. No match = position only.

⛔⛔ **The fixture scrambles insertion order deliberately.** In both real runs
insertion order equalled row order, so a lookup returning the Nth entry would
have passed any faithful fixture and shipped. Sabotage: with the number match
replaced by insertion order, asking for track 1 returns **"03. Gallery: Swayin'
Flame"**. ⚠ Half of case 22 still passed under that sabotage (position 4
coincided with entry 4) -- one assertion would not have caught it.

⚠⚠ `count = 0` on that row ON PURPOSE: a 9-item horizontal strip
(`0x14009af21`) picks the game FIRST, and each soundtrack is a different length.
A fixed count would disable the row on every other game.

⭐ `-Wclobbered` (kept from the reverted probe commit) caught a real
setjmp/longjmp hazard in the first function written after it.

### ▶ WHERE SCREEN NAMING STANDS (2026-09-10)

**19 rows shipped.** Every caller ever seen in a log is named EXCEPT the two
halves of Fighter Awards (`0x140079833` 7-item list, `0x140079739` 40-item
grid). Its ITEMS already speak -- the game hands us each achievement's title as
live text -- so the screen is usable; naming the seven list rows is what is
left. ⚠ It has no per-row captions (one static sentence for the whole screen),
so the caption channel cannot do it.

⛔⛔ **A SCREEN's caption must never repeat on every row.** Fighter Awards sets
"View all of your challenges." for all 47 positions. That became a real problem
the moment ACHIEVEMENT_*_NAME entered the prompt table, and is now suppressed:
first sighting on a screen speaks, an identical repeat does not, a screen change
re-arms. ⭐ `gt_take_caption` already refused markup STRIPS on this exact
reasoning -- the principle was right and the TEST WAS A PROXY (it keyed on
markup; this one is plain prose). When a guard tests a proxy, a case with the
property but not the proxy defeats it.

### ⚠ THE ONE REMAINING SCREEN NEEDS A DIFFERENT CHANNEL

Caller `0x140087cda`, 3 items, the data-sharing consent dialog (reached by
confirming System > Data Collection). ⛔ **It sets no item text at all** -- only
its button strip -- so the caption channel can never name it, however slowly it
is walked.

⚠ SYS_WINDOW_YES2/NO2/PP (29-31: Agree / Decline / Privacy Policy) fit the
count and the category exactly. **Not shipped** -- that is the shape of guess
that already mislabelled two screens here. It needs the GUI tree or the
dialog's own construction.

### ⛔⛔ THE ACTUAL FIX: the DISCOVERY sweep is OFF by default

Two rounds went into making that sweep fast. The right answer was that **it is a
development aid and does not need to run during play at all.** It now requires
`cfc2_discover.on` next to the exe.

Why it went off, all measured:
- stalled live sessions **twice** (37 s, then 11.9 s) with speech dead;
- produced **one** candidate list ever -- three wrong answers;
- named **zero** shipped `g_screens` rows (all cite live text, the
  disassembly, or the caption channel);
- its premise is gone: the caption channel names screens now, and named Online
  Play. Use `tools/name_screens.py`.

⚠ Unknown screens are STILL LOGGED with it off, so a run still records which
screens are unnamed. Only the candidate hunt is gated.

⭐ The lesson worth carrying: **a stall in the user's speech is a broken mod; a
diagnostic that does not run is an inconvenience to the developer.** Ask whether
the expensive thing needs to run at all before making it cheap.

### ⚠ THE STALL, ROUND 2 -- and a crash I nearly shipped

The 37 s fix MOVED the stall rather than removing it: the classification pass
ran BEFORE the time budget started, so the 23:52 run stalled **11.9 s** and then
reported "sweep STOPPED after 0 of 4165 bases". The budget worked; it was
guarding the wrong half. Classification is now INSIDE the budget and
**resumable** -- it does what it can in 25 ms and continues on the next
movement. The sweep is bounded by `cls_done`, never the table size (an
unclassified entry is indistinguishable from a real negative).

⛔⛔ **AND THE RULE THAT CAME OUT OF IT.** I also wrote a fast path skipping
`mem_readable_run()` before the guarded `memcpy`, since the range had been
validated at index time. The test written to prove it safe **CRASHED**:

    A. guarded single-byte read of a bad page  -> CAUGHT
    B. guarded byte LOOP across the boundary   -> CAUGHT
    C. guarded memcpy() across the boundary    -> ⛔ NOT CAUGHT

⚠ C is not even consistent -- inline at -O2 it crashed inside memcpy; in a child
process it returned reporting NO fault. **The fault guard is a net for the race
between the check and the copy, never a replacement for the check.** Every bulk
read must call `mem_readable_run()` first. Reverted; pinned by
`.\tools\run_span_test.ps1`.

⚠ **STILL UNEXPLAINED: ~2.9 ms per string read in-game.** Two hypotheses were
measured and REFUTED -- it is not the string index (the whole blob indexes in
80 ms) and it is not VAD-tree depth (6132 regions offline still gives
0.0003 ms/read). The next run's log now prints the rate and the blob's region
shape directly. Do not guess a third time; read those lines.

### ⛔⛔ (round 1) the mod went SILENT for 37 s when leaving the Music screen

User-reported, confirmed in `docs/run-2026-09-09-2313.log`: ONE 37.3-second gap
on the poll thread, bracketing a single `[DISCOVERY]` event for the 9-item
music screen. The mod was alive but stuck, so speech stopped and the user
navigated blind.

⛔⛔ **The benchmark that said "under 1 ms" was measuring code that no longer
existed** -- `bench_discovery.c` still capped at 600 bases, ran ONE pass, and
checked a single trailing sentence, while the shipping sweep had grown to all
4165 bases with a per-item check and a RELAXED SECOND PASS. Corrected, it
measured 70 ms -- and even that understates the live cost, because this blob is
one clean allocation and the game's is on a heap. That understating has now
happened THREE times in that file.

**Two fixes:**
1. A **25 ms wall-clock budget** enforced as the sweep runs, checked every 64
   bases. A budget cannot be wrong about the machine it is on. A truncated
   sweep says so, and the "no candidate" line reports the count ACTUALLY
   examined. It does not back off to the relaxed pass after a timeout.
2. **The algorithm is now cheap: 70.38 ms -> 0.03 ms.** Each string is
   classified ONCE (ITEM-shaped / BLURB-shaped, 1.46 ms for all 4165) and the
   window test is array arithmetic; the old loop re-read string i once per
   window containing it.

⭐ `tools/run_de_test.ps1` proves the rewrite is EQUIVALENT (138 count/mode
combinations, 0 mismatches; fails with 69 when a threshold is nudged) and then
benchmarks it. A speedup that changed which bases survive would silently change
which screens can be named.

### ⭐⭐ ONLINE PLAY IS NAMED (23:13 run) -- and a caller serves TWO screens

`0x14009ba21` shipped as "Select Mode" (3 items) and is **also Online Play**
(6 items). Across two runs it appeared **15 times, always at 6, never at 3**.
The generic cursor helper is shared, so **a call site is NOT a unique screen
identity** -- `menu_speak_lookup_n(caller, count)` now discriminates.

⚠⚠ This is the exact caller a GUESSED row was once shipped for. The reason that
guess failed is now visible: it really is two screens.

Online Play's map is **non-contiguous** -- `{68, 69, 70, 71, 1076, 1078}`, rows
5-6 sitting ~1000 labels from rows 1-4. No base could have named it, and
`name_screens.py` correctly reported ⛔ SIGHTINGS DISAGREE rather than picking
one. Every label came from a caption the game set on that row.

⭐ `tools/test_screen_maps.py` validates every map against the real corpus --
something the C suite CANNOT do (no live GMD in the test process, so it can
only check that the map path is taken). It rejects any entry that is a `HELP_`
description, carries markup, or is blank.

### ▶ NEXT: name the remaining screens as a CLASS
`docs/naming-screens-generally.md` and `tools/name_screens.py`.

⭐⭐ A caption gives the item's GMD INDEX directly (stem pairing:
`HELP_OFFLINE_PLAY` -> `OFFLINE_PLAY` -> 37), and the log gives the position.
Subtract and you have the screen's base -- **no candidate shortlist, no
per-screen offline investigation.** Reproduces the two already-verified bases
(main menu 37, Select Game 51) from log evidence alone. 112 usable captions.

**The procedure:** play the unnamed screens moving the cursor **twice on each**
(two sightings turn a LEAD into a CONSISTENT base), then
`python tools\name_screens.py <log>` and read the per-screen verdict. It
reports CONSISTENT / single-sighting LEAD / ⛔ SIGHTINGS DISAGREE (needs a label
MAP, do not pick one) / no base derived.

⚠ Still ends in a human comparison -- every `g_screens` row needs
`MC_VERIFIED_LIVE_TEXT`. This supplies evidence for a base, not permission to
skip looking.

**9 candidate screens** are reachable this way, notably **68-72 Online Play**
(Casual/Ranked/Custom Match, Leaderboard, Online Options) -- the screen the
22:50 run sat on -- and **250-256** the 7-item pause menu.

⚠ `0x14009BA21` produced NO captions at all, and also fails the "Select Mode"
row check (`expects 3 items but this one has 6`). Caption-less screens need the
old route (`tools/resolve_screens.py`) or a different idea.

⚠ **A wrong description is indistinguishable from a right one to a blind user.**
If a caption is ever spoken for the WRONG row, that is a stop-everything bug --
case 13 in `run_gt_test.ps1` guards it offline.

### ⛔ Two instrumentation defects the run exposed (fixed, worth remembering)
- **Beats 10/30/60 missed the run.** The session ended after beat 15, so ONE
  report fired, covering 2 of 22 movements. Now every 5 beats. ⭐ Pick a report
  cadence the user's ACTUAL session can reach -- this project had already been
  bitten identically by the caller trace's schedule.
- **The recorder went blind at its 48-record cap** and never recovered; the run
  missed it only by ending early. The report now clears its window; case 19
  proves the recorder returns nothing without that.

### The offline suites for all of this
- `.\tools\run_gm_test.ps1` -- markup -> speech (18 cases)
- `.\tools\run_pi_test.ps1` -- captured text -> GMD label (18 cases)
- `.\tools\run_gt_test.ps1` -- includes the focus channel (cases 12-18)
- `python tools\check_markup_parity.py` -- C vs the Python oracle, 239/239
- Sabotage sweeps: `sabotage_gm.ps1`, `sabotage_pi.ps1`, `sabotage_focus.ps1`
- ⚠ Run **all 12** `run_*_test.ps1` after any shared-header change: two runners
  were left un-buildable by this work, and that looks exactly like passing.

### ⭐⭐ What items 1-2 corrected about the plan
- `<FONT>` occurs in the live captures and in **NO** GMD string (0 of 58310,
  all 14 languages). The live tag vocabulary is a **superset** of the file's.
- The help captions are **PER-ROW, not per-screen**. "Choose the game you want
  to play." is the help for the row `GAME_TITLE`, whose siblings are
  `HELP_GAME_VERSION` and `HELP_PLAYER`. So it identifies a ROW and implies the
  screen only by membership -- do not speak it as a screen name.
- `HELP_<LABEL>` as a rule holds for only **8 of 14** known item labels, so the
  join is by exact TEXT, never by name. Details in
  `docs/screen-id-from-prompt-text.md`.

## (older) the GUI text reader WORKS -- now use it

**SOLVED 2026-09-09.** The mod captures the shell's live on-screen text.
Verified in-game: **324 calls, 324 strings, 0 faults.** Detail in memory
`cfc2-gui-text-reader-built`.

**The hook:** `0x1402f0d80` = `setText(cGUIObjText *this, const char *s, ...)`,
**instance vtable `0x141056b40`, SLOT 25**. The text arrives as an ARGUMENT.

⚠ There are FOUR near-identical setText overloads across two classes and only
this one fires. All four are hooked, each with its own counter, so the log
names the winner rather than leaving it to a guess.

**What it captures** -- UTF-8 with inline engine markup:

    "<FONT 2>PRESS <SIZE 80><ICON DECIDE></SIZE> TO START</FONT>"
    "Choose the game you want to play.  <SIZE 58><ICON BTN_START></SIZE>
     Training <SIZE 58><ICON BTN_X></SIZE>Game Settings ... Back"
    "P1"

### ⛔⛔ READ THIS FIRST: `docs/rethink-after-gui-text.md`
Reading the shell's text collapses the chain of inference the speech path was
built on, so it is worth an architectural look. **But the evidence does NOT
support "we can read menu items now, delete the table."**

Measured from the 21:19 run: the captured strings are title prompts, button-hint
strips and captions -- **NOT item labels**. In the same seconds the cursor hook
correctly spoke "Offline Play, 1 of 6" and "Game, Capcom vs. SNK 2..., 2 of 8",
names appearing in NONE of the captures. 324 calls yielded only 4 distinct
objects, and 3 of 6 captures coincided with no cursor movement at all.

⭐ So: TWO COMPLEMENTARY CHANNELS, not a replacement.
- cursor + GMD answers "which ITEM am I on?"  (keep it; it is verified)
- setText answers "what does this SCREEN say?"  (new capability)

The new channel is strongest exactly where the old is weakest -- **screen
identity**. "Choose the game you want to play." names Select Game outright,
with no caller table and no plausible-but-wrong match. That is the real blocker
on the ~14 unnamed screens.

⛔ Do NOT delete `g_screens`. Do NOT join setText strings to cursor indices by
timing (3 of 6 captures had no cursor move). Do NOT speak raw markup.

### ▶ THE NEXT WORK -- ordinary string handling, not reverse engineering
1. **Strip the markup.** `<FONT n>`, `<SIZE n>`, `<ICON NAME>` tags must not
   reach the user as tag soup.
2. **Map the ICON tokens to spoken words** (`DECIDE` -> the confirm button,
   `BTN_START` -> Start, ...). The token vocabulary is already catalogued --
   see memory `cfc2-text-corpus`, 132 ICON tokens.
3. ⚠ **Establish what these strings ARE before wiring them to speech.** The
   captures so far are whole help/prompt STRIPS, not per-item labels. Whether a
   menu row's own caption comes through the same path is an OPEN QUESTION. Do
   not assume it and build a join on top of the assumption -- that is the shape
   of the bug that made a screen speak another screen's labels.

### ⭐⭐ THE LESSON THAT COST THE MOST RUNS
**Hook ALL candidates and let the run name the winner** (memory:
`hook-all-candidates-let-the-run-decide`). I hooked the best-looking of four
overloads and got "setText ran 0 times" -- indistinguishable from "no such
mechanism". Hooking one and guessing would have been wrong 4 ways out of 4.

And underneath four earlier failed runs: **the vtable was off by one DTI
record.** The DTI table is 0x38-byte stride where `+0x00` is a class's vtable
and `+0x08` is a name pointer belonging to its NEIGHBOUR -- so `0x1410563a8`
is cGUIObjRoot's, not cGUIObjText's. ⭐ `tools/dti_table.py` walks it correctly;
use it rather than reading names off adjacency.

⭐⭐ **When a DERIVED value and a MEASURED one disagree, the measurement is the
fact.** Five disassembly readings each looked sound and each was wrong.

### ⚠ And half the wasted runs were my own plumbing, not the target
- a seq-bracket copied from a channel ticking a few times per keypress onto one
  firing 650/second: 616 strings captured, **zero logged**;
- an unbounded spin-lock on the worker thread (a frozen mod, not a missed line);
- a stat collected and never printed, so a run could not say whether the hook
  had fired at all. **A count nobody prints is a count nobody has.**

### The test: `.\tools\run_gt_test.ps1`
un_gt_test.ps1`
18 cases, every mechanism sabotage-verified.
⚠ Run it through PowerShell directly -- via some shells a crashing suite
reported exit 0 with no output.
Always installed (the old `cfc2_guitext.on` is ignored). Respects `cfc2_hook.off`.

## Screens named so far (7 rows in `g_screens`)
Main menu · Options · Museum (992 GALLERY/SOUND) · Select Game (rows +
the 8 game names from the exe's ROM table) · PC Settings' Display row ·
Select Mode (3-entry map against a 6-item screen, so it speaks position only
until items 3..5 are established).

⛔ **Gallery is closed to the GMD**: its confirm indexes resource-id tables, so
the message corpus cannot name its contents.

⚠⚠ **BUT "its items are unnumbered logos" WAS NEVER TESTED** -- that was
extrapolated from the confirm-handler note, which says nothing about whether the
screen sets text. **Museum > Gallery proper has still not been entered.** The
instance route now reads live text for screens the GMD cannot reach (the music
tracks), and the online game-select screen sets game TITLES freely, so the
premise that Gallery needs a selection signal is unsupported in both directions.
⭐ Enter it and read the instance set before designing anything for it.

## Tools built for this (all offline, no game launches)
- `tools/screen_layouts.py` -- names a screen by the GUI layout it loads.
  ⭐ Has CONTROLS built in and exits non-zero if they fail; a vtable-based
  widening was tried, "placed" 13 more screens, and FAILED them.
- `tools/resolve_screens.py` -- intersects a screen's observed positions from a
  run's log to narrow candidate label runs.
- `tools/menu_graph.py` -- the cursor dispatch tables. ⚠ 33 tables, 6
  cursor-indexed, only 2 that actually change screen.
- `tools/bench_discovery.py` -- measures the sweep against the real blob.

## (older) START HERE

**Working end to end:** the mod speaks menu item names in the game's own
language. Verified in a clean run (2026-09-08, 22:33): **zero stalls** -- every
gap in that log is the user not pressing keys.

**Named:** main menu (`0x140097a59`, base 37, 6 items) and the Options *list*
(`0x1400a20fc`, base 170, 5 items). Everything else speaks position only
("3 of 6"), which is correct behaviour, not a bug.

### ⛔⛔ THE NEXT PIECE OF WORK: settings screens need a MAP, not a base

The `base + index` model assumes a screen's items are a CONTIGUOUS run of GMD
labels. That holds for simple menus and **fails for settings screens**.

Measured: the per-game settings screen (caller `0x1400b1bd9`, fn `0x1400b1b10`,
**8 items**) draws from the `OPT_` family, which interleaves setting NAMES with
their VALUE labels:

    643 OPT_DIFFICULTY 'Game Difficulty'   644 OPT_DAMAGE_LEVEL 'Attack Power'
    645..648  its four values              649 OPT_TIMER_SPEED 'Timer Speed'

The names sit at 643, 644, 649, 654, 659, 660, 661, 662, 665, 1108, 1115, 1125
-- non-contiguous -- and WHICH eight appear depends on the game selected.

⭐ So `mc_screen` needs to grow an optional per-screen `index -> label` map
alongside `base_index`. Small extension, not a redesign.

⛔ **Do NOT force a base for such a screen.** Any base that "looks close" will
speak a VALUE label ("Normal", "Fast") where an item name belongs -- confidently
and wrongly. See the long comment in `src/menu_speak.h`.

### Opt-in switches (all absent by default -- a normal session does none of this)
- `cfc2_dump.on` -- write `cfc2_dump/` (60 MB). Needed when the offline tools
  want a fresh dump; ⚠ `.text` is stable across runs but the DATA sections are
  NOT, and `build_riprefs.py` refuses a stale index rather than answering wrongly.
- `cfc2_discover.on` -- log candidate label runs for unknown screens.
  ⚠ Runs on the poll thread and freezes the mod for seconds per new screen;
  three versions of it starved the mod during real play. Turn it on to name a
  screen, then turn it off.
- `cfc2_pad.on` -- the sPad watcher. Inert for the shell (`Kind=0`, update never
  runs); kept only for the in-game work ahead.
- `cfc2_movelist.on` -- the MOVE LIST membership probe. Answers the one question
  no log can: at movement time, does the count of on-screen move-name objects
  equal the vertical cursor's total? Samples every 100 ms (the user moves every
  ~0.6 s), aggregates into a histogram so hundreds of movements cost a handful
  of log lines, reports every 10 s. **Nothing is spoken.**
  ⭐ Membership comes from the game itself: `+0x088` == the cursor helper's
  `screen` pointer, measured in two runs on two different pointers. The name
  column's X is DERIVED (leftmost populated column), never hardcoded -- that
  constant has been written down as 1265, then 1268, and measured as 633.0 for
  the neighbouring condition column.
- `cfc2_hook.off` -- makes the whole mod read-only (kills both hooks).

### To name a SIMPLE screen (contiguous labels)
1. Create `cfc2_discover.on`, walk into the screen, move the cursor once.
2. The log prints `mcspeak: [DISCOVERY]` with candidate bases and the live text
   each would give for the item you are standing on.
3. Whichever matches names the screen. Add a row to `g_screens` in
   `src/menu_speak.c` with `MC_VERIFIED_LIVE_TEXT`.

⛔⛔ **Never add a row from plausibility.** One was added from item count alone
and made a different 6-item screen speak the main menu's labels -- and by
matching, it SUPPRESSED that screen's discovery block, hiding the fix. The suite
now fails unless every row is `MC_VERIFIED_LIVE_TEXT`.

### Known-unnamed callers (from real runs; counts are MEASURED)
- `0x1400b1bd9` **8** and `0x1400b1b69` **3** -- same fn `0x1400b1b10`, i.e. ONE
  screen with two lists. This is the settings screen above; needs the map.
- `0x14009ba21` 6 · `0x1400a1715` 4 · `0x140079833` 7 · `0x140099b23` 2
- `0x140079739` **40**, horizontal -- character select. ⚠ No `HELP_` run, so the
  candidate sweep cannot find it; needs its own approach.

⚠ An earlier note listed `0x1400b1b69` as a plain 3-item screen. That was
before its sibling call site showed it is half of a two-list screen.

## (previous) the node → text join

**Solved:** reading the shell's text (`gmd_find.c`, closed-loop verified).
**Solved:** reading the shell's keyboard (`kbd_state.c`, confirmed live).
**Solved (2026-09-08, offline):** the **selected index and the item count** —
route 7, below. ⚠ Built and self-tested, **not yet run in-game**.
**Not solved, and now the whole remaining problem:** turning an index into the
item's TEXT.

### ⭐⭐ ROUTE 7 ANSWERED: the menu cursor chokepoint
Every shell menu moves its selection through one of two generic helpers:

- **`0x14007cdc0` vertical** (masks `0x10010` up / `0xa00a0` down)
- **`0x14007c9f0` horizontal** (masks `0x80080` left / `0x50050` right)

```
int move(void *screen /*rcx*/, int *cursor /*rdx*/, int min /*r8d*/,
         int max /*r9d*/, int sfx /*[rsp+0x20]*/, int wrap /*[rsp+0x28]*/)
```

Read the index through `rdx`, clamp/wrap, **write it back**, and — only if it
changed — play a cursor SFX and return the delta. **34 vertical + 34 horizontal
call sites**, all inside `0x140079000`–`0x1400b5700`.

⭐ The cursor pointer and the range arrive as ARGUMENTS, so `src/menu_cursor.c`
needs no memory search at all. Many sites pass `max = [screen+0x334]-1` beside
`cursor = [screen+0x330]`, so the **item count** comes free — that is what
"item 3 of 8" needs.

### ⛔⛔ THIS CORRECTS A CLAIM REPEATED ACROSS FOUR CLOSED ROUTES
Those notes say flatly *"there is no flat `0..N-1` selection index in this game
— proven, not assumed"* (1,914,880 fields, nothing over 4 distinct values).
**An index does exist.** It is a field of the **per-screen object**, at a
different address in every menu.

⭐ The old measurement was **sound for what it tested** — there is no single
*global* selection integer — and the error was the generalisation drawn from
it. A span scan for one global could not have found 34 per-screen fields.

⭐⭐ The method that worked is the one that has now worked four times here:
**read the answer out of the instructions.** Memory scanning is 0 for 4 on this
project; disassembly is 4 for 4 (sPad, sKeyboard, the GMD table, this).

### ▶ NEXT: the in-game run, then the join
`src/menu_cursor.c` is built, wired into `mod.c` and deployed. Launch, walk the
menus, and read `mc:` lines in the log. What they answer:

- `mc: ... cursor 2 -> 3 (item 4 of 8 ...)` → the model is right, and the index
  and count are both live.
- `mc: the cursor helpers have not run yet` → the hook is not intercepting;
  nothing can be concluded about menu movement (liveness before findings).
- `mc: N call(s) had an unreadable cursor pointer` → the argument model is
  wrong somewhere; those movements are MISSING, not absent.

⚠⚠ **The join is still the hard half, and the index does not solve it.**
Layouts carry **no GMD label hashes** (`cfc2-gui-layout-finder`), so index →
label is the missing link; label → string is solved (`cfc2-live-gmd-table`).
The promising direction is that menu items are likely a **contiguous label run**
in the GMD, so an index may map to a label by offset from a screen's base label
— but that is a hypothesis, and it needs a positive control before it is
believed.

⚠ **Do not spend the user's launches debugging our own tools** — several rounds
of route 4 did exactly that. Both of this session's steps were resolved offline
with zero game launches.

### The offline caller finder
`tools/find_callers.py` — scans for the `call rel32` **encoding**, not linear
disassembly (capstone's linear sweep stops at the first undecodable byte, and
this project already lost a 16 MB sweep that way). `--jmp` also catches tail
calls. Used here to count the 68 call sites and to prove nothing branches into
either helper's first five bytes.
⚠ Its case 4 is labelled in the file as a **regression guard with no isolating
sabotage** — two earlier versions of that case claimed coverage they did not
have. Cases 2 and 3 are the ones carrying weight.

### ⭐⭐ 2026-09-08: sKeyboard FOUND — the entire input model, offline
The caller trace ran and produced **one** call site inside the exe
(`0x140253860`). Disassembling around it resolved everything, with **no extra
game launches**:

- **`sKeyboard`** — the class name is inline ASCII at **`0x14104e6f8`**, sitting
  inside its own vtable/DTI block. That is a positive identification.
- **vtable `0x14104e718`**; update method at **slot +0x30 → `0x140253220`**.
  Exactly ONE qword in the image holds that address.
- `0x140253220` does `lea rdi,[rcx+0x138]` → **the state block is `this+0x138`**.
- **`0x140253750`** is the state BUILDER, called from `0x1402532f3` with
  **`rcx` = the instance, `rdx` = the state bitmap**.

**THE LAYOUT** (read off `0x140253302` onward), 8 dwords = 256 keys per bank:

    this+0x138:  +0x00 current  +0x20 previous
                 +0x40 Trg      +0x60 Rel      +0x80 Chg

with `Chg = cur^old`, `Trg = Chg&cur`, `Rel = Chg&old`. The same model the sPad
metadata named — but on the object that **actually updates** (sPad has
`Kind = 0` and its update never runs).

### ⚠⚠ CORRECTION: GetAsyncKeyState is the MOUSE only
The keyboard is read by a **virtual call `[rax+0x48]` on `[rbp+0xa40]`** — a
DirectInput `GetDeviceState` filling a 0x100-byte buffer — then a 0x100-iteration
loop translates each pressed DIK through a table at **`manager+0x38`** into an
internal code. Only a **5-iteration mouse loop** uses `GetAsyncKeyState`
(VK table `0x14104e790` = `01 02 04 05 06`, the five mouse buttons).

⭐ That matches the trace's `vks=5, last_vk=0x06` exactly — **the disassembly
and the live measurement agree independently.**

⚠ So the earlier "the shell polls" conclusion was right but INCOMPLETE, and the
"input reader sweeps ~70 KB_* keys" model was **wrong**. Ranking sites by
distinct-VK count would have pointed at a 243-VK startup enumeration in a
foreign module; it only worked because site 0 was **the only caller inside the
exe**. ⭐ The module filter did the real work, not the VK heuristic.

⭐ And **we ARE dinput8.dll**, so the keyboard path is observable for free.

### ▶ NEXT: run it — `src/kbd_state.c` is built and deployed
Hooks the builder at `0x140253750` and captures `rcx`/`rdx`. **The instance
pointer arrives as an ARGUMENT — no memory search.** Then `kbd_poll_and_log()`
reads the five banks on our worker thread and logs **on change only**.

⚠ The instance is NOT statically reachable — `0x140253220` has zero direct
callers (vtable-dispatched) and nothing points at the `sKeyboard` name string.
Same fact that closed routes 3 and 5: this engine WALKS its metadata.

**What the run answers:** navigate a menu and watch `kbd:` lines. Codes in
`pressed=[...]` are the shell's INTERNAL codes (post-DIK-translation), so this
also yields the code for each direction — needed for everything downstream.

**⭐ The falsifiable check:** the log states whether `state - instance == 0x138`
as the disassembly predicted. If it says `⚠ UNEXPECTED`, the offline model is
wrong and the bank offsets cannot be trusted — reported, not assumed.

**Before trusting a null result, run:** `build\test\test_kbd_guard.exe`
(built by the command in `tools/`). It checks that `kbd_install()` REFUSES when
the game is not mapped, and — crucially — **which guard refused**.

⛔⛔ **A test that asserts only "install returned FALSE" is worthless here, and
I shipped one before catching it.** With all three identity guards deleted it
still passed, because **MinHook independently refuses an unmapped address**. The
outcome was right; the assertion was not evidence. The fix is
`kbd_install_why()`: the test now requires `BAD_PROTECT` specifically.
- delete the VirtualQuery block → status becomes `BAD_VTABLE` → **fails**, while
  the "returned FALSE" assertion still passes. That asymmetry is the point.
- delete all three → `MINHOOK_FAILED` → fails. That is the state the old test
  could not tell from success.

⚠ Off switch `cfc2_hook.off` (shared with the caller trace) disables it.

### The caller trace (RETIRED, kept in tree)
`src/caller_trace.c` + `tools/test_ct.c` — did its job in one run and is no
longer installed; `kbd_install()` replaced it in `mod.c`. Six sabotages, each
verified to fail the case testing it. Keep for the next caller question.
⚠ Its report text still advises ranking by distinct VKs — **that heuristic was
wrong here**; see the correction above.

### ⭐⭐ ANSWERED 2026-09-08: the shell polls `GetAsyncKeyState`
The IAT probe ran in-game. **`0x140FDC720` -> `user32.dll!GetAsyncKeyState`.**

    polled_keyboard=YES  raw_input=no  window_messages=YES
    xinput=no  directinput=YES
    examined 289 slots; 289 readable; 272 resolved; 5 matched

⭐ **This is a trustworthy positive AND a trustworthy negative.** 272 of 289
slots resolved into loaded modules, so the probe demonstrably read the real
table — `raw_input=no` is therefore evidence of absence, not absence of
evidence. (Had it been inconclusive, `iat_probe_run()` would have returned
FALSE; see [[inconclusive-must-be-a-return-value]].)

Discount two of those: `window_messages` is noise (every Win32 app imports the
pump), and `directinput` is **us** — we are dinput8.dll.

⚠ Only **53 user32 slots** exist in total and the whole raw-input family is
absent, which bounds where else input could come from.

### ▶ NEXT: the caller trace is BUILT and DEPLOYED — run it
`src/caller_trace.c` + `tools/test_ct.c`. Hooks `user32!GetAsyncKeyState` and
records the **distinct set of return addresses** with a hit count and a
distinct-VK count each. **Not yet run in-game.**

⚠ **This is the first code in this project that WRITES to game memory.**
Off switch ships with it: create **`cfc2_hook.off`** next to the exe and the
mod is read-only again, exactly as every previous version.

**The run:** launch, sit on the title screen a few seconds, then go into the
menus and move around for ~30 s. The trace reports to the log at beats 3, 10,
30 and every 60 after, so successive reports show the set GROWING. Nothing to
press. Then read `CFC2Access.log` for lines starting `ct:`.

**⭐ How to read the result — by DISTINCT VKS, not by hits.** The shell's input
reader sweeps the whole key table (the game's `KB_*` map has ~70 entries); a
site with `vks=1` is a single hotkey test. A tight one-key check in a hot loop
easily out-hits a 70-key sweep, so ranking by hits would name the wrong site.
The self-test asserts that hits and vks genuinely disagree, so this is not a
guess about which to trust.

Feed the winning site's **rva** to `tools/build_riprefs.py` and disassemble
around it: the selection state is written near the input reader. That is the
method that cracked sPad (✅ read the offsets out of the instructions), not the
one that produced three false positives (⛔ search memory for a shape).

**⭐⭐ The trap this design exists to avoid: we are a caller ourselves.**
`mod.c` calls `GetAsyncKeyState` at nine sites every 100 ms. Without a filter
the first "input reader" found would be **us** — not a null result, but a
confident wrong answer that looks exactly like a finding. Callers inside our
module are matched by base address and counted separately.

⭐ And that separate count is the hook's **liveness proof**, not a discard:
we *know* we call it nine times a second, so `self_calls == 0` means the detour
is not intercepting and `ct_report_log()` refuses to print any findings —
"0 game callers" is only meaningful alongside a large `self_calls`. See
[[inconclusive-must-be-a-return-value]].

**Before trusting any in-game result, run the self-test:**
    .\tools\run_ct_test.ps1
It needs TWO real modules — the test exe (standing in for our DLL) and
`test_ct_helper.dll` (standing in for the game exe) — because a single-module
test cannot exercise the self-filter at all; a perfect filter and no filter
would give identical results. **Each case was run with its mechanism sabotaged
and seen to fail:**
1. self-filter deleted → **case 2 fails** (our own calls become "game sites")
   — the exact false positive above.
2. self-range covers all memory → **case 3 fails while case 2 still passes.**
   That asymmetry is why both directions are asserted: a filter rejecting
   everything sails through a self-filter test alone.
3. repeats never collapse → case 4 fails (a call log, not a site set).
4. `distinct_vks` counted unconditionally → **only case 5 fails**, which is
   what backs the ranking advice above.
5. `ct_remove()` returns immediately → case 6 fails.
   ⚠ **Found by sabotage:** deleting only the `MH_DisableHook` call leaves
   case 6 PASSING, because `MH_Uninitialize` also tears the detour down. So
   case 6 proves "removal stops interception" but does not pin which call does
   it. Recorded rather than papered over.
6. hook created but never enabled → **case 1 fails first**, so an inert hook
   cannot masquerade as a clean negative.

⚠⚠ **A caller's return address is untrusted** — it is a value from the live
stack of a thread we do not control. It is `VirtualQuery`-checked (committed
*and* executable) before being recorded, and **never dereferenced**. See
[[hooked-function-pointers-are-untrusted]]: a detour here once dereferenced a
caller's pointer and killed the game in under a second.

⚠ The hook body takes no locks, allocates nothing and does no I/O — it runs on
the render thread every frame. Recording is a bounded table plus atomics; the
log is written later from our own worker thread. Table overflow is **counted
and reported**, never silently dropped: >64 distinct callers would contradict
the model and must be visible.

⚠ `__builtin_return_address(0)` is the caller, not MinHook: MinHook patches the
target with a `jmp` and pushes nothing, so the return address the CPU pushed for
`call [GetAsyncKeyState]` is the game's. (MSVC's `_ReturnAddress()` does not
exist in GCC; the toolchain here is MinGW GCC 16.1.)

⚠ MinHook is linked again in `build.ps1` for this step — it had been dropped
while everything shipping was read-only. Note the `$minhookSrc` array was
already declared there but not passed to gcc.

### The IAT probe (built, tested, deployed — kept for reference)
`src/iat_probe.c` + `tools/test_iat_probe.c`. It runs automatically after
unpack and **speaks the result**, so the user does not need the log for the
headline. Nothing to press; just launch the game and listen.

**What it does:** resolves ~19 input APIs by name from already-loaded modules
(never `LoadLibrary` — that would change the process being measured), then
walks the game's import block and reports which slots point at them, by NAME.

⭐ **The block was located OFFLINE first, so this is not a memory search.**
From the riprefs index: 272 distinct slots are reached by
`call qword ptr [rip+..]`, and they occupy one tight run
**`0x140fdc000`–`0x140fdc900`** (289 qwords; in the last dump 257 held
system-DLL pointers, 15 other, 17 zero). The probe reads that known address
instead of scanning 1.4 GB for something import-shaped — which is exactly what
produced this project's three false positives.

**How to read the spoken result:**
- *"polled keyboard"* → hook `GetAsyncKeyState`/`GetKeyboardState` and record
  the RETURN ADDRESS; the caller is the shell's input reader and the selection
  is written near it.
- *"raw input"* → hook `GetRawInputData` and follow the output pointer into the
  game's own decoded buffer.
- *"window messages"* → **on its own this is NOT evidence.** Every Win32 app
  imports the pump. It only matters if polling and raw input are both absent.
- *"none of them"* → with sPad inert too, the keyboard comes through a DLL not
  yet considered. Check the dinput8 proxy's own calls next: **we ARE that DLL**,
  so every DirectInput call is already observable for free.
- *"INCONCLUSIVE"* → the probe could not read the block. It found nothing AND
  proved nothing; do not treat it as a negative.

⭐⭐ **That last distinction is a RETURN VALUE, not a printed warning.**
`iat_probe_run()` returns FALSE when zero slots resolved into a loaded module,
so a caller cannot mistake "never looked" for "looked and found none". This
project already shipped a tool that printed "coverage is INCOMPLETE" while
every assertion stayed green — *if the code prints a warning, something must
FAIL on it.*

**Before trusting any in-game negative, run the self-test:**
    .\tools\run_iat_test.ps1
Four cases, and **each was verified to FAIL under a deliberate sabotage** —
a passing test that has never been seen to fail is not evidence:
1. planted APIs must be FOUND — sabotage: comparison never matches → 6 failures.
2. absent APIs must be reported ABSENT (the discriminating case) — sabotage:
   comparison always matches → case 2 fails, which is the false positive the
   in-game conclusion depends on not happening.
3. the families must be independent of one another.
4. an unreadable block must return FALSE, not a tidy clean negative — sabotage:
   delete that `return FALSE` → **only that one assertion fails while the other
   three still pass**, which is precisely why it exists.

⚠ The test process must `LoadLibrary("user32.dll")` itself: a plain console exe
does not link user32, and the probe then correctly refuses to conclude
anything. That refusal is the desired in-game behaviour, so the fix belongs in
the harness, never in the probe.

### Why the OFFLINE route to sKeyboard is closed
Worth knowing before repeating it. Four attempts, all negative:

- `0x14002cce0` is a **registration stub**: it registers the class and stores a
  vtable pointer into the DTI record. It constructs nothing.
- The DTI's 7 code references are that stub, its store, three `lea rcx` uses,
  and a one-line `getDTI()` accessor. None reaches an instance.
- VK-table bounds (`0x100`/`0xff`/`0x1f`) appear at 227/1436/1425 sites — far
  too generic, and chasing common constants is what produced this project's
  earlier false leads.
- **The IAT cannot be matched statically.** 295 data slots hold resolved
  system-DLL addresses, but user32 was at a different base when the dump was
  taken, so none match this process's `GetAsyncKeyState` etc.

⭐ Every remaining route needs a **runtime** fact — the live IAT, or the live
instance pointer. Both are trivial from inside the process, where the mod
already runs. That is what the IAT probe above does, and why it replaced more
disassembly.

⚠ Note the fourth point cuts BOTH ways: the same fact that makes the IAT
unmatchable offline makes it trivial to match at runtime, because the process
resolves the names for us.

**Rebuilding the cross-reference index** (~55 s), when offline work needs it:
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
out of alignment** — `0x22c82c8` is the next record's field, not sPad's slot.

⚠⚠ **The DTI size check that originally "proved" this is NOT reproducible.**
It read 0xb8 / 0x50 / 0x3c4 from DTI+0x30 in one dump; the same addresses read
**zero** in a later one, because that region is populated during startup and
the dump's timing decides whether it holds anything. The conclusion does not
depend on it — the sizes come from `.text` instead (`r9d = 0xf10` at the sPad
registration `0x14002a920`) — but **do not cite any DATA-derived offset without
re-deriving it against the dump in front of you.**

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

- **`.text` is still ENCRYPTED when DllMain runs.** ⚠ It decrypts in **~531 ms**
  (measured 2026-09-08), NOT the ~10 s an earlier note claimed — a 20x error.
  Anything timed against "roughly ten seconds" is mistimed; poll, never sleep.
  Historically it decrypted later
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
