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

## ▶ START HERE (next session) — WIDEN THE SCREEN TABLE

**The join is CLOSED for the main menu and deployed, but NOT yet heard.**
`src/menu_speak.c` speaks "Offline Play, 1 of 6". One screen is in the table:
caller `0x14009ba21`, base 37, count 6.

**The next run does two things at once:**
1. Confirms the main menu speaks item NAMES (not just position).
2. **Discovery**: walk into other menus — options, museum, game select, pause,
   character select. Each unknown screen logs, once, a
   `mc: [DISCOVERY] caller=0x... items=N axis=...` line. That line is the table
   row; find the label run in msg.arc and add it to `g_screens`.

⚠ Speaking position only ("3 of 6") is the CORRECT behaviour for a screen not
yet in the table — it is not a bug to chase.

⛔⛔ Never key a screen on item count. Menus share counts, and the failure mode
is confident WRONG names, which a blind user cannot catch. Key on the caller.

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

