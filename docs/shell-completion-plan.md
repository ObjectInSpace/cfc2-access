# Completing the shell — the run plan

User's decision 2026-09-10: **finish the shell, then the games.** The shell is
the first thing a blind player meets, and a half-read menu is a wall in front of
everything else.

⚠ Written because "name the remaining screens" has not converged (8 → 10 → 15
across three runs). Treating it as a campaign with a checklist is the fix; doing
one screen per run is what let the count drift.

## ⭐⭐ ROW LABELS: 57 of 58 CONFIRMED — and my audit was wrong three times

Extending the sightings question from values to ROW LABELS, which is what the
user hears most. The first three answers I got were all wrong, in instructive
ways, and the correct answer is nearly the opposite of the first:

    attempt 1:  22 of 58 confirmed   -- counted only TEXT SET sightings
    attempt 2:  49 of 58             -- added "Row, N of M. Caption." lines
    attempt 3:  39 of 58             -- added captions with no row name, but
                                        used a WRONG stem rule
    correct:    57 of 58             -- used name_screens.py's own resolver

⛔ **Attempt 1's error: ignoring the caption channel.** A row named by the
game's own per-row help caption is BETTER evidence than a text sighting, and it
is how most of the Options screens were established. Counting only `TEXT SET`
measures which screens happen to redraw their labels, not which labels are
verified.

⛔⛔ **Attempt 3's error: inventing a stem rule.** I assumed `HELP_X` pairs with
`X`. The real pairing is `HELP_OP_BGM_VOL` → `MENU_OP_BGM_VOLUME` — a different
prefix AND a different suffix. `docs/screen-id-from-prompt-text.md` already
records that the join is by exact TEXT and never by name, and I rebuilt the
broken version of it anyway.

⭐ **The fix was to stop writing my own pairing and call the tool that already
does it.** `name_screens.py` resolves captions to labels correctly; feeding
every log through it confirms 57 of 58 shipped row labels, including all six
Sound Settings rows (20 sightings, every position) exactly as their commit
claimed.

⚠ The 58th, "Input Delay", IS caption-confirmed — `"3 of 5. Set the multiplayer
input delay…"` — but the resolver does not pair it, plausibly because that
caption is unusually long. A tooling gap, not missing evidence.

⭐ **The lesson, and it is the same one four times over this session: my ad-hoc
pairing is the least reliable component in this project.** It has now produced a
wrong reading about captions across an axis boundary, about stale values, about
"contaminated" counts, and about this. When a pairing already exists as a tested
tool, use it.

## ⚠⚠ HOW MUCH OF THE VALUE DATA IS SIGHTED? 45 of 104 (2026-09-10)

Prompted by the user asking whether "Either" was data or assumption. Auditing
EVERY shipped value string the same way: **59 of 104 have never been seen drawn
in any run.** That is far more inference than my commit messages implied, and
the honest scope of the question.

⛔ A never-sighted string is not automatically wrong. Most are positions nobody
has visited — the 14 display languages, eight lobby comment presets, P3/P4 on
four-player games. They come from the renderer's own table plus the corpus, and
that is real evidence.

### ⭐ The sharp end: SPOKEN but never confirmed
`audit_values.py` now reports the subset that matters — strings the mod has
already said to the user while no run has ever shown the game drawing them:

    Japanese Version / English Version   (on all FOUR screens that have the row)
    English [en] / French [fr]           (System > Language)

⛔⛔ **And the version row cannot be checked from the logs at all.** Six
Game-Version value movements on Select Game draw exactly one thing: `"P1"`, the
Player row's object. The row draws nothing capturable, so no amount of log
analysis will confirm those two strings.

⭐ What DOES check out by sighting: the Player row. Value 1 draws `"P1"` and
value 2 draws `"P2"`, consistently across three runs — so `{2, 2, 58}` is
confirmed by measurement, not inference.

▶ The version strings can only be settled BY EAR: change Game Version on Select
Game and listen. If it says "Japanese Version" while the screen shows something
else, that is a real defect and the audit cannot find it.

## ⭐⭐ "IF WE KNOW WHAT THE TEXT DRAWS, WHY NOT USE THAT INSTEAD OF THE TABLE?"

User's question, 2026-09-10. It is the right question and the answer is
measured, not assumed: **the drawn text is not reliable enough to REPLACE the
tables, but it is the ideal CROSS-CHECK.**

### Measurement 1 — coverage: 38%
Of **580 value-axis movements** across every log, only **223 (38%)** have any
text drawn at the same instant. Live text alone would leave nearly two thirds of
value movements silent — worse than today.

### Measurement 2 — accuracy: 25% WRONG, tested against a known-good set
The decisive test. Lobby Settings row 8 is "Number of Players", whose values are
`"2".."9"` — shipped and corpus-validated, so the right answer is known
independently. If live text could replace the table, value position N must draw
`str(N+1)`:

    matched 12x · MISMATCHED 4x
      pos 5 expected '6', drew 'Challenger'
      pos 2 expected '3', drew 'Game Version'
      pos 1 expected '2', drew 'Victory Condition'

A **25% error rate** against data known to be correct. The wrong draws are other
rows' objects on the same screen re-setting themselves.

### Measurement 3 — object identity does NOT rescue it
The obvious fix is to key on the object rather than the timestamp. It fails:
object `2C507A70` holds `"3"`, `"4"`, `"5"` **and** `"Challenger"` within one
screen. Addresses are per-run and one object serves different purposes across
visits, so identity alone cannot say which row a string belongs to.

### ⭐ So the right use is an AUDIT, not a replacement
The table gives coverage (100% of movements, from the code the game itself
indexes); the live text gives an independent opinion 38% of the time. Comparing
them is exactly how the two wrong-value classes the user reported would have
been caught by measurement instead of by ear — and it is the next piece of work.

⚠ This also explains why my earlier pairing misreads kept happening: on these
screens the "text near a movement" is very often a NEIGHBOURING row's object.
Four times this session that produced a confident wrong reading.

## ⛔⛔ OPTIONS VALUES MUST READ THEIR ACTUAL VALUES — NOT YET SOLVED

User, 2026-09-11: suppressing the wrong value is "not good enough, they will need
to read their actual values." Correct — position-only is a regression dressed as
a fix, and a sighted player sees the binding and the volume.

### What is established
- ⭐ The bad behaviour is FIXED: the mod no longer appends another row's value
  ("Coin Button, 1 of 6, English [en]"). That was worse than silence.
- ⭐ The row is already derived correctly for every Options row, via the value
  cursor's address. "Coin Button, 1 of 6" names the right row.
- ⭐ Language and Data Collection DO read their values, from shipped tables.

### ⛔ Why the remaining rows do not, and what was ruled out
1. **No GMD run.** After `MENU_OP_MENU_BTN` / `START_BTN` / `COIN_BTN` the next
   label is each one's `HELP_`, not a value list. There is nothing to index.
2. **Not in the instance set either.** The System Settings instance report holds
   one object per ROW LABEL and values for Language ("English [en]") and Data
   Collection ("Decline") — but the only `<ICON BTN_*>` objects anywhere in any
   log are `BTN_LB` / `BTN_RB`, which are the PAGE-FLIPPER hints, not the
   bindings. Checked across every log: 13 objects, all LB or RB.
3. **Insertion order cannot join them.** The row labels do arrive in row order,
   but the value objects are interleaved with unrelated ones.
4. **The renderer has no label table.** Unlike the lobby screens
   (`0x141205e30` / `0x141206e30`), `0x1400a37b0` carries no data pointer; it
   branches on the row index and drives per-row ANIMATION state.

### ▶ What would actually solve it
The binding is a controller glyph, so the value is a **button id**, not a
string. The remaining routes, in order of promise:
- ⛔ **NOT the `+0x378` array.** That was the obvious candidate and it is
  checked: `0x1400a33c4` compares it against zero, i.e. it is the
  enabled/disabled flag, the same shape every other settings screen has. Ruled
  out before proposing it.
- **Hook the glyph draw.** The ICON vocabulary already exists in
  `gui_markup.c` (`BTN_START`→"Start", `BTN_LB`→"LB", 132 tokens), so if the
  per-row glyph passes through any text or draw call with an id, the naming is
  already written. Nothing in the current hooks sees it, so this needs a new
  observation point rather than more log analysis.
- ⛔ **NOT the saved config either — tried and closed.** The bindings persist, so
  they must be stored; the file is
  `Steam/userdata/215494178/2400430/remote/savedata.bin`, 1.2 MB, touched during
  the user's run. It is **encrypted or compressed**: 244 distinct byte values in
  the first 64 KB and no meaningful strings. Reading it means breaking that
  first, which is a far bigger job than the feature warrants.

▶ **So the honest position: this needs a new observation point in the running
game, not more offline analysis.** The most likely is a hook on whatever sets a
GUI object's ICON — the same class of hook as `setText`, which is how every
other text channel here was opened. That is real work, not a lookup.

⚠ Until then these rows say "Menu Button, 3 of 7". That is a true statement and
an incomplete one, and it should be recorded as UNFINISHED rather than shipped
as correct.

### ⭐⭐ CORRECTION: THE RESOLUTION *IS* IN THE TEXT CHANNEL

⛔ I concluded "no resolution-shaped string has EVER been drawn". **Wrong** — I
had only checked same-timestamp `TEXT SET` lines on the VALUE axis. Searching
the instance set and the caption channel finds them:

    run-2026-09-10-1913: mcspeak: caption not in the corpus, not spoken: "1280x720"
    run-2026-09-10-1732: instance set: LIVE [move 185, 6 sets] "1280x768"
    run-2026-09-10-0053: focus record: [move 53] PROSE "1280x720"

⭐ And the 19:13 sighting lands on the **ROW** movement to Resolution — i.e. the
row's CURRENT VALUE is drawn when you arrive on the row. It was refused only
because it is not a corpus label, which is exactly what the value-quote path
existed to handle.

### ⛔ BUT THE ROW AXIS HAS THE SAME ATTRIBUTION PROBLEM
Pairing every Options ROW movement to the caption refused at that instant:

    PC Settings   row 1 -> '1280x720'      ✅ correct (Resolution)
    Sound         row 1 -> 'Original'      ✅ plausible (Challenger Jingle)
    System        row 3 -> 'Agree'         ⛔ wrong row (Data Collection is 5)
    System        row 5 -> 'English [en]'  ⛔ wrong row (Language is 1)
    System        row 6 -> 'English [en]'  ⛔ wrong row

⚠ So "speak the caption on the row axis" would be right sometimes and wrong
sometimes, on the same screens the user already reported as wrong. That is the
bug they complained about, relocated rather than fixed.

⭐ **What this changes:** the value is REACHABLE — it is in the channel, not
hidden behind a glyph or an encrypted file. What is missing is still the join
from a string to the row it belongs to.

## ⭐⭐⭐ THE GUI TREE ANSWERS IT: rows, then their values, in order

The user asked about the GUI tree. The INSTANCE SET (which is the live object
list, not route 3's static layout template) has the structure:

    PC Settings   Resolution · Display · VSync · Default
                  then:  '1280x768' · 'Borderless Windowed' · 'Off'
    System        Language · Menu Button · Start Button · Coin Button ·
                  Data Collection · Default
                  then:  'English [en]' · <ICON BTN_RB> · <ICON BTN_START> ·
                         <ICON BTN_LB> · 'Decline'

⭐⭐ **This gives the button glyphs** — `BTN_RB`, `BTN_START`, `BTN_LB` for
Menu/Start/Coin — which four earlier routes said were unreachable. They are
plain `<ICON …>` text after all, and `gui_markup.c` already maps 132 such
tokens to words. It also gives the resolution and the display mode.

⭐ Verified across FOUR runs and both screens: the row labels appear
contiguously in row order, immediately followed by their values in the same
order.

### ⛔ AND THE OBVIOUS IMPLEMENTATION IS ALREADY DISPROVEN
"Take the next N objects after the last row label" fails: in the 01:30 run the
sequence is interleaved with help text and a hint strip —

    'English [en]', <ICON BTN_RB>, 'Change the coin button for each game…',
    '<ICON CANCEL>Back', <ICON BTN_START>, <ICON BTN_LB>

so positions 3 and 4 are prose, not values. A naive offset would speak a help
sentence as the Start Button binding.

⚠ Also: one 19:13 report shows `BTN_SELECT`/`RSB` where the others show
`BTN_RB`/`BTN_START`. That is consistent with the bindings actually being
changed during the session — which is the feature working, not noise — but it
means the values are genuinely dynamic and cannot be baked into a table.

## ▶▶ NEXT TARGET: THE PAUSE MENU (user's choice, 2026-09-11)

⚠ **No run has ever entered it.** Both 7-item callers seen so far are Fighter
Awards (`0x140079833`) and System Settings (`0x1400a3578`) — neither is the
pause menu, so its caller address is unknown and that is the one thing only a
run supplies.

⭐ The labels are already predicted and they are a clean contiguous run of
exactly seven, which is unusual and promising:

    250 CONTINUE      'Resume'          253 COMMAND_LIST 'Move List'
    251 RESTART       'Restart'         254 LOAD         'Load Quick Save'
    252 HOW_TO_PLAY   'How to Play'     255 SAVE         'Quick Save'
                                        256 QUIT         'Quit'
    257 HELP_CONTINUE 'Return to the game.'   <- the caption that confirms it

⭐ So a single visit should settle it: `HELP_CONTINUE` at position 1 gives
base 250 by subtraction, and `name_screens.py` resolves that automatically from
the log without any new code.

⚠⚠ **But this is an IN-GAME screen, not a shell screen.** It is reached by
starting a match and pausing, so it sits behind the emulated ROM — the first
screen in this project that does. Whether the shell's cursor hooks even fire
there is itself unknown and is the first thing the log will answer:
- `mc:` lines on pause  → the shell still drives it, and this is ordinary work;
- no `mc:` lines at all  → the pause menu belongs to the emulated game and needs
  the in-game route, which is a different project entirely.

▶ **The run:** start any game, pause, move through all seven rows with ~1 s
dwell, then quit back out. Small and specific.

## ⭐⭐ WHERE ELSE DOES "THE POSITION IS THE VALUE" APPLY? — swept, answer: nowhere else

Asked after the volume fix. Every value row in every log, classified by its
measured count:

    Sound   rows 0-3  10 values   VOLUME LEVELS      ✅ now marked
    Network rows 1-2  11 values   Voice Chat, Delay  ✅ now marked
    Network row 0/3   2/5         Off-On, Any/2+/..  named, has a table
    Sound   row 4     2           Original/Remix     named, has a table
    PC      row 0     21          Resolution         NAMES (generated strings)
    PC      rows 1-2  3/2         Display, VSync     named, has a table
    Lobby   rows 6-7  10/8        First to N, 2..9   named, HAS a table
    System  rows 1-3  7/8/7       button bindings    GLYPHS

⭐ So the marking is COMPLETE — there is no other row where the cursor index is
itself the setting. Checked rather than assumed, including the tempting
10-value and 8-value rows on Lobby Settings, which are named lists with shipped
tables and would have been WRONG as levels ("Victory Condition, 4, of 10"
instead of "First to 4").

⚠ The ID display (`0x14008a91b`, 6 slots, the largest remaining bare count) was
also checked: its cursor is WHICH SLOT you are editing, so "3 of 6" is already
the correct reading. The level idea does not apply.

### ⭐ THE STRICT JOIN: 3 correct blocks, 0 wrong, but it only fires 33% of the time
Requiring the row labels to be TRULY CONSECUTIVE (not merely in order) fixes the
Sound misalignment by refusing that screen instead of corrupting it:

    PC Settings   Resolution -> '1280x768' / '1280x720'
                  Display    -> 'Borderless Windowed'   VSync -> 'Off'
    System        Language -> 'English [en]' · Menu -> <ICON BTN_RB>
                  Start -> <ICON BTN_START> · Coin -> <ICON BTN_LB>
                  Data Collection -> 'Decline'

⚠ Of 9 reports where all row labels are present, the strict rule accepts **3
(33%)**. So these values would read on some visits and not others. That is
honest but uneven, and it is the cost of refusing to guess when the block is
broken up.

### ⚠⚠ RETRACTION: "SOUND IS THE DISPROOF" WAS MY BUG, NOT THE CHANNEL'S

The user pushed back — "I would expect a number for a volume setting" — and was
right. Two separate errors of mine:

**1. The join was misaligned on that screen.** I asserted "row labels appear
contiguously". On Sound Settings they do NOT: the positions are
`[0, 4, 5, 6, 7, 8]` — "Music Volume" is separated from the other five by a
caption, a hint strip and a menu item. My check only tested that the indices
were SORTED, which they are, so a broken case passed. Every "wrong" value I
reported was the join reading from the wrong offset.

**2. I claimed the sliders "draw no text at all" without testing it.** The block
I examined came from a run where the volumes were never adjusted. In the one run
that has both the instance set AND volume movements, the cursor only ever hit
positions 1 and 10 — the endpoints — so the level was jumped, not stepped, and
nothing was captured either way. **I have no data on what a volume row shows
while being adjusted.** That is an untested assumption, not a disproof.

⛔ So the honest status of the Sound result is UNKNOWN, not "wrong on every
row". The bare numbers that DO appear in the instance set ('0' x87, '2' x17,
'5' x4) may well be volume levels; I have not shown otherwise.

▶ What would settle it: step a volume row one notch at a time with the instance
set on, and look at what appears. That is a small, specific ask.

### (superseded) CAN IT REPLACE THE TABLES? — the original test

User's question, 2026-09-11. Answered by running the join against every screen
rather than only the two it was found on:

    System        ✅ correct (Language, 3 button glyphs, Data Collection)
    PC Settings   ✅ correct (Resolution, Display, VSync)
    Network       ⚠ partial  (Microphone -> 'On', Voice Chat Volume -> '5')
    Sound         ⛔ WRONG ON EVERY ROW
    Lobby Settings ⛔ never captured as a block at all

⛔⛔ **Sound Settings is the disproof.** The join produces:

    Music Volume         -> 'Remix'        (that is Challenger Jingle's value)
    Sound Effects Volume -> 'Music'        (a MENU ITEM)
    Announcer Volume     -> 'Options'      (a MENU ITEM)
    Challenger Jingle    -> 'Network'      (a MENU ITEM)

Every row wrong, and wrong in the most dangerous way: plausible short strings a
blind user cannot distinguish from a real value.

⭐ **The cause is structural, not a tuning problem.** The volume sliders draw NO
text at all, so the "skip prose, take the Nth remaining" rule falls straight
through them and lands on whatever object comes next — which on that screen is
the Options menu itself. A filter cannot fix a missing element; there is nothing
to skip TO.

⚠ And the bare numbers that do appear ('0' x87, '2' x17, '5' x4) carry no
identity — nothing distinguishes a volume level from a player count or a lobby
round setting.

### ⭐ SO THE ANSWER IS: ADD IT WHERE IT IS PROVEN, KEEP THE TABLES
- the tables cover **every** row and are corpus-validated; this channel covers
  four screens and silently corrupts a fifth;
- but it reaches THREE things no table can: the button glyphs, the resolution,
  and the display mode — all previously written off as unreachable.

▶ So it ships as an ADDITIONAL source for rows that have no value set, gated to
the screens where it is verified, and never as a replacement for a shipped
table. ⛔ The Sound result is exactly why it must be gated per screen rather
than enabled generally.

### ⭐⭐ THE FILTERED JOIN IS VERIFIED OFFLINE — 6 of 6 reports (System, PC)

Skipping prose (ends in punctuation, or > 40 chars) and hint strips, then taking
the Nth remaining object after the row-label block:

    PC Settings   Resolution -> '1280x768' / '1280x720'
                  Display    -> 'Borderless Windowed'
                  VSync      -> 'Off'
    System        Language        -> 'English [en]'
                  Menu Button     -> <ICON BTN_RB>
                  Start Button    -> <ICON BTN_START>
                  Coin Button     -> <ICON BTN_LB>
                  Data Collection -> 'Decline'

Correct on **every** report in every run, including the 19:13 one where the
bindings differ (`BTN_SELECT`/`RSB`) — the join tracks the live setting rather
than a remembered one.

⚠ **"Default" overruns** — it has no value, so it picks up whatever follows
("Options"). The row count is known, so the fix is to stop at rows that have a
value; it must be handled, not discovered later.

⛔ Still to prove before shipping: this is verified against LOGGED instance
reports, which are taken every 5 beats. The mod would have to do the same walk
LIVE, at movement time, and the instance table's contents at that instant are
not guaranteed to match what a later report printed.

### ⭐ RESOLUTION AND VOLUME vs THE GLYPHS

⚠ The generated rows — Resolution (21 values), the volume sliders (10-11),
Input Delay (11) — are NOT the same case as the button bindings, and should not
be filed together:

- a **glyph** has no string anywhere: not in the GMD, not in the instance set,
  not in any log. It needs a new capture channel.
- a **generated number** is a STRING the game composes, and the only reason it
  is unavailable is that nothing captures it — plus, for resolution, the list is
  ENUMERABLE from the display itself.

Established for Resolution specifically:
- `MENU_OP_PC_RESO` (218) is followed immediately by `MENU_OP_PC_DISP` (219),
  so there is **no value run in the corpus** — confirming these are composed at
  runtime, exactly as the shipped note said.
- No resolution-shaped string (`1920x1080` etc.) has EVER been drawn in any
  log; 32 movements on that row produced only VSync's "On"/"Off" bleeding in.
- ⭐ But the live per-row COUNT is readable: `0x1400a172e` reads
  `[screen + row*4 + 0x358]` as the max index and `+0x378` beside it. So the
  mod already knows the row has 21 values.

⚠ Windows reports **24** display modes on this machine against the game's 21,
so enumerating modes and indexing by position would be a "fits the shape" guess
— the three-mode difference is exactly where it would go wrong silently.

▶ The honest route for these is the same new capture channel the glyphs need:
whatever composes "1920x1080" writes it somewhere. That single channel would
close resolution, volumes, Input Delay AND the button glyphs together, which is
why it is worth building properly rather than special-casing one row.

## ⭐⭐ THE 01:30 RUN: Options confirmed, and Fighter Awards' shape solved

⭐ **The Language values finally spoke** — the one class no log could confirm:

    "Language, English [en], 2 of 14"   "Language, French [fr], 3 of 14"

⭐⭐ **And ALL EIGHT game titles now speak the US names**, including the two that
were worst before: "Project Justice" (was "Moero! Justice Gakuen") and "Plasma
Sword: Nightmare of Bilstein" (was "Star Gladiator 2"). The JP/US fix is
confirmed across the whole set, not just the three positions seen earlier.

    Display, Borderless Windowed / Fullscreen · Data Collection, Agree
    Game Version, Japanese Version / Either

### ⭐ FIGHTER AWARDS IS A 6-COLUMN GRID, and the arithmetic is exact
The 7-item list is a ROW selector over the 40 achievements:

    achievement = 6 x (position - 1) + 1

Verified on all five observed pairs: pos 2→7, 3→13, 5→25, 6→31, 7→37. This
matches the disassembly's `0x2aaaaaab` divide-by-3 and multiply-by-6 arithmetic
found earlier, and it explains the screen rather than changing it — both halves
already speak through the ACHIEVEMENT_* path (the grid names 12 of 20).

⛔ **It also killed a join I was one sample away from shipping.** The 40-grid
drew `ACHIEVEMENT_02_DESC` at position 2, so "position == achievement number"
looked exact — 1 for 1. The 7-item list disproves it outright: position 2 is
achievement 7. A single agreeing sample is a coincidence, not a rule, and this
is the third time that standard has paid for itself today.

## ⭐⭐ THE 00:10 RUN CONFIRMS YESTERDAY'S VALUE WORK — with one new gap

Everything shipped for the online screens was exercised and is correct:

    "Comment, Everyone is welcome!, 1 of 8" ... "Let's chat!, 8 of 8"   (all 8)
    "Comment, Any, 1 of 9"            (the 9-value FILTER variant, correctly
                                       starting with the filter-only "Any")
    "Event Mode, On (Quick Match), 1 of 2" · "Off (Arcade Style), 2 of 2"
    "Game Start Privilege, Challenger, 1 of 2" · "Host, 2 of 2"
    "Number of Players, 3, 2 of 8" · "Passcode, Off/On"
    "Cross-region Matchmaking, Off/On" · "One-button Special Moves, On/Off"

⭐⭐ **AND THE JP/US FIX IS CONFIRMED BY DIRECT COMPARISON.** Before and after,
same screens:

    2130 run:  "Game, Capcom Fighting Jam, ..."
               "Game, Capcom vs. SNK 2: Millionaire Fighting 2001, ..."
    0010 run:  "Game, Capcom Fighting Evolution, ..."
               "Game, Capcom vs. SNK 2: Mark of the Millennium 2001, ..."

⭐ "Either" also spoke, at position 3 of 3 on both online screens. Still not
confirmed against the display (the row draws nothing), but it is now exercised.

### ⛔ THE NEW GAP: the lobby screens' game list has NINE entries
Row 0 of both Lobby Settings and Lobby Filters is a 9-item game list, and it
names nothing: `"Game, 1 of 9"` through `"Game, 9 of 9"`.

⚠ The tempting inference is "9 = the collection + 8 games", because that is
exactly what Gallery's and Music's 9-item choosers are. **That is a borrowed
assumption, not a measurement on THIS screen** — the same shape of reasoning
that produced `g_game_options_candidate`. Pairing every 9-item row-0 movement
across all logs (via `logpair`) yields only neighbouring values: `'On'`, `'2'`,
`'4'`. No position has ever been named here.

▶ It needs either a renderer table for row 0 (Lobby Settings' table gives first
= 14 = `GAME_NAME_ANY`, which suggests the list DOES start with an "Any"
filter entry rather than the collection) or a run where the titles are drawn.

## ⭐⭐ THE PICKER'S CHARACTERS ARE EXACT — my "off by one" was a pairing error

Pooling every picker sample across all logs, keyed on SAME-TIMESTAMP text:

    32 samples, ring delta +0 on ALL of them
    pos 36='Z' · 35='Y' · 34='X' · ... · 2='1' · 1='0'

⛔⛔ **This overturns the "20 of 20 off by one" finding I recorded earlier.**
That measurement paired captions that arrived AFTER the movement, which lands on
the next position's character. Keyed on the same timestamp, the character always
matches the position exactly, and `char_picker_matches()`'s mapping
(`K[position-1]`) is correct as shipped.

⭐ So there is no direction question and nothing to fix in the matching. The
`[PICKERPROBE]` instrumentation can be retired once a run confirms speech.

### ⛔ THE REMAINING PUZZLE: the screen usually draws NOTHING
Per run, picker movements versus movements with a character drawn:

    19:13   41 moves, median gap 0.21s, 37 with a char
    19:45   37 moves, median gap 3.56s,  0 with a char
    22:45   15 moves, median gap 0.31s,  0 with a char
    00:10    5 moves, median gap 0.61s,  0 with a char

⚠ **Pacing is NOT the cause** — 19:13 was the FASTEST run and the only one that
drew. I had twice offered pacing as the likely explanation; the data refutes it.

⚠ The 19:13 run also predates the keypad shipping (21:01), so the one session
with characters is the one session where nothing could speak them. Every session
since has had the code and no characters.

▶ Unexplained. What distinguishes that session is not in any log line I can
find, and I am not going to invent a third theory. The next run that draws
characters will speak them; until then the screen says position only, which is
true.

## ⚠ (superseded) THE 22:45 RUN DID NOT SETTLE THE PICKER

The run delivered what was asked: **7 upward movements** on the picker (the
data every previous run lacked). But it cannot answer the direction question,
because **the picker set NO character text at all** — zero `TEXT SET` lines
across its entire 15-movement window.

The three `[PICKERPROBE]` lines that did fire quote `'0'` against positions 35
and 36, i.e. a stale caption left over from before the screen was entered, not a
neighbouring character. That is why their delta reads -34/-35 rather than the
consistent -1 seen earlier.

⚠ **I cannot explain why it was silent, and checked the two obvious causes:**
- pacing is comparable (median gap 0.31 s vs 0.21 s in the 19:13 run);
- position coverage is not the issue (both runs span positions 1..36).

⛔ So this is an honest "not established", not a finding. The 19:13 run's picker
cycled all 36 characters; this one cycled none, under similar conditions.
Something about the screen's state differs and no log line so far names it.

⚠ The ID display (`0x14008a91b`) also produced nothing across its 8 movements —
consistent with the earlier 105-movement measurement, and expected, because no
characters were ENTERED this visit. Entering is the action that makes its slots
write.

▶ What a run still needs: on the picker, move up and down and **watch for the
`[PICKERPROBE]` lines to appear at all**. If they do not, the screen is not
drawing characters and the direction question is moot for that visit. Then
**confirm a character** two or three times so the ID slots write.

## ⛔⛔ THE TARGET IS 100% (user, 2026-09-10) — what that actually costs

The user will not move to the games until the shell is complete. So "no route"
is no longer an acceptable end state for any screen, and each one needs either a
route or a stated reason it is impossible. Current honest position:

### The two Casual Match 3-item lists — MEASURED NEGATIVE, no text exists
`0x14009885e` and `0x1400988bf`, 19 of the 38 remaining bare lines.

Exhaustive check: across **70 movements** on those two callers, every piece of
text set within the same SECOND is either a game title from the 8-item strip
(which re-sets its eight objects 150-380 times per visit and swamps any window)
or the Confirm/Back hint strip. **They set no text of their own, ever.**

⛔ And a lead that looked promising died on inspection: the instance set shows
"Either", "Off", "On" as a group, which is 3 values for a 3-item list. They
belong to **Lobby Filters' value axis**, which already speaks ("Game, 2 of 9,
On") — three separate objects each holding one setting's current value, not a
list to index.

### ⭐⭐ DISASSEMBLED — and they are NOT ITEM LISTS AT ALL

The route that has worked 4 for 4 on this project answered it offline, with no
run. Reading `0x14009885e` and `0x1400988bf` in context:

- Both pass a **stack-local** cursor (`[rsp+0x50]`) to the generic helper and,
  on a change, adjust `[rbx+0x344]` (a scroll/page offset) and `[rcx+0x334]`,
  then call `0x140098bd0` to redraw.
- `0x140098bd0` loops: `movsxd rax, [rdi+0x334]` · `mov edx, [rsi+rax*4]` ·
  `call 0x14007d0f0`, walking a table at `0x141207230` between bounds held at
  `0x1412072a0`/`0x1412072a8`.
- Those three targets are **integer tables, not strings** (`09 00 00 00 0a 00
  00 00 …`) — resource/layout ids, the same shape as Gallery's confirm handler.
- `0x14007d0f0` resolves a GUI object by id and calls a method taking a float in
  `xmm1` — an animation/visibility setter.

⛔ **So these two callers are a PAGING control over image resources.** They have
no item names because they name nothing: they move a highlight and re-trigger
animations. "3 of 3" is not a gap in the mod — it is a complete description of
what the control does.

⭐ That is 19 of the 38 remaining bare lines accounted for as **correct
behaviour**, established from the instructions rather than from another run.
⚠ It also explains six runs of silence: there was never any text to find.

### What 100% requires, screen by screen
1. **The two 3-item lists** — disassembly. Untried, and the method with the best
   record here.
2. **Lobby Settings rows 1-4** — captions have not appeared in four visits;
   disassembly is the fallback.
3. **The ID display** — needs a run that ENTERS characters, then likely reads
   the six slot objects directly (they are in the instance set).
4. **The picker direction** — one run with upward movements. Already
   instrumented.
5. **Fighter Awards' 7 list rows** — one static caption for the screen;
   disassembly.
6. **The consent dialog** — sets no item text; disassembly or the GUI tree.

⚠ Items 1, 2, 5 and 6 all reduce to the same technique. That is the next real
piece of work, and it is offline.

## ▶▶ WHAT IS ACTUALLY SILENT — measured on the NEWEST build (21:17 run)

**77% of spoken lines are named: 134 named, 38 bare.** ⚠ Measure this on the
newest run only. Historical totals across all logs are dominated by builds that
have since been fixed, and they made two solved screens look broken:

⭐ **The music list is FINE.** It showed 25 bare lines in the 19:13 run and
**zero** in the 21:17 run — the instance-table reclaim fixed it. Those failures
were the table saturating (8 LIVE / 56 stale) so `gt_numbered_item` found
nothing. Post-reclaim the same screen is clean.

⭐ **Network Settings' value axis is FINE too.** Its bare lines are the
generated rows — Voice Chat Volume and Input Delay, 11 values each. There is no
11-label family in the corpus because those values are computed, so they get no
variant by design and the live-quote path handles them.

### The 38 that remain, in order
- `0x14009885e` **11** and `0x1400988bf` **8** — Casual Match's two 3-item
  lists. Stack-local cursors, no captions in six runs. **No route.**
- `0x140082c3c` **5** — Lobby Settings' four still-unknown rows (1-4).
- `0x14008a91b` **5** — the ID display. Sets no text on movement (105 movements,
  zero same-timestamp text); its slots write only when a character is ENTERED.
- `0x1400ac059` **4** — Ranked Leaderboard positions whose caption did not
  arrive that pass; the channel works there, this is just coverage.
- `0x14008a8ef` **3** — the picker, correctly refusing off-by-one captions.
- `0x14008e799` **2** — Lobby Filters' one unknown row.

⭐ **So the remaining silence is ~10 rows on three screens, not a broad gap.**
Two of those screens have no known route; the rest is coverage or a resolved
direction question.

Shipped and CONFIRMED BY LOG since this plan was written: Lobby · Lobby Filters
(5 of 6 rows + its value axis) · Casual Match (4 rows) · Lobby Settings (5 of 10
rows + values_off 0x33c) · Gallery · Ranked Leaderboard · Music tracks.

⚠ **ONE THING IS DEPLOYED BUT NOT YET HEARD:** Casual Match's game list speaking
the eight TITLES instead of "Game, N of 8". Corroborated by two independent
sources but never played. If any position names the wrong game, revert it — a
confident wrong title is worse than "Game, 3 of 8".

### The eight that remain, and why each is stuck
- `0x14009885e` 3v · `0x1400988bf` 3v — Casual Match's other two lists. **No
  route**: stack-local cursors (no values_off arithmetic possible) and no
  per-row captions in any run. Every caption near them is a stale leftover.
- `0x140079833` 7v · `0x140079739` 40h — Fighter Awards. Its ITEMS already
  speak; only the 7 list rows are unnamed, and one static caption covers the
  whole screen.
- `0x14008a8ef` 36v · `0x14008a91b` 6h — the lobby-ID keypad. It DOES set text
  ("Enter Lobby ID", six character slots), so it is reachable; nobody has walked
  the 36-item picker slowly enough to see whether it sets text per position.
- `0x14009af21` 9h — the Music chooser. Gave no captions in any run, unlike
  Gallery's identical-looking strip.
- `0x140087cda` 3h — the consent dialog. Sets no item text at all.

⭐ **The keypad is the only one with an untried, evidence-backed route.** The
other seven need either a channel nobody has (selection state — see the focus
differ's negative below) or captions the game has never produced in five runs.

## The target: 15 callers = 8 SCREENS

Call sites pair up on one screen object (row list + value axis), the shape
settings screens already use. Grouped by screen, with what each NEEDS:

### ⭐⭐ THE 8-ITEM STRIP WAS NEVER A NAMING PROBLEM — RE-FRAMED (21:30 run)

⛔ I had this filed as "an unnamed screen needing a selection signal" for days.
It is neither. Since Casual Match shipped, the strip **is that screen's VALUE
axis** and it already speaks:

    spoke "Game, 2 of 8"    spoke "Game, 6 of 8"

So the row is named and the position is right. What is missing is only the eight
**value labels** — the game names — which is an ordinary `mc_value_set` gap, not
a selection problem. The focus differ's negative below does not block it at all.

▶ **What it needs: the strip's cursor index → ROM index mapping, confirmed.**
`rom_table_index_for(game, 0)` already predicts it and `[ROMPROBE]` already logs
the prediction on that screen (30 times in this run, unspoken by design).

⭐ **AND THE ORDERING NOW HAS INDEPENDENT SUPPORT.** The Ranked Leaderboard's
strip captions properly, so it names itself from the game's own text — and
across the runs it confirms **five positions**, every one matching the ROM
table's canonical order:

    pos 1 CVS: Millennium Fight 2000 Pro · pos 2 CVS2: Mark of the Millennium
    pos 3 Capcom Fighting Evolution · pos 7 Project Justice
    pos 8 Plasma Sword: Nightmare of Bilstein

⚠ That is the COLLECTION's ordering confirmed on a DIFFERENT screen, not on
Casual Match's strip. It makes the transfer highly likely and does not prove it.
⛔ Casual Match's own strip yields nothing usable: its titles arrive on the
transcript channel unsynchronised to movements, and captions paired to its
positions CONTRADICT each other ("Capcom Fighting Evolution" lands on positions
1, 4 AND 5 across runs — stale leftovers, not evidence).

▶ **One cheap run settles it:** walk that strip slowly end to end and read the
`[ROMPROBE]` lines against what the screen actually shows. Eight predictions,
eight positions, already instrumented — nothing to build.

### ⛔⛔ THE FOCUS DIFFER HAS ANSWERED, AND THE ANSWER IS NO (21:30 run)

The differ ran (167 diff lines over 53 movements). **Selection is not a field of
`cGUIObjText`.** Three measurements, the third decisive:

1. **The eight game-title objects NEVER change. Not once, in any diff line.**
   Those are precisely the objects the 8-item strip would need a focus signal
   on. A marker that never moves cannot mark anything.
2. What *does* change is caption and hint-strip objects — the ones whose TEXT is
   being re-set every movement ("Play a friendly match against…",
   `<ICON DECIDE>`). They change because their content changed, which is the
   thing we already knew.
3. A genuine menu-row object ("Online Play", 36 changes) touches only **three**
   offsets — `+0x1b8`, `+0x188`, `+0x170` — and **every one holds churning
   POINTERS** (`0x2c376be0`, `0x2beb6900`, …), never a stable small value. No
   offset anywhere in the 528-byte object toggles between two values the way a
   selection flag must.

⚠ **My first reading of this data was wrong and optimistic.** "2, 3 or 5 objects
change per movement, never all — a focus candidate, not a ticker" was true as
far as it went and measured the wrong thing: the objects changing were the ones
being *rewritten*, not the ones being *selected*. The same-screen control
distinguishes a ticker from a signal; it does not distinguish a signal from a
side-effect of text assignment. That distinction needed the third check.

⭐ **This agrees with route 3 rather than contradicting it.** Route 3 found the
layout TEMPLATE static; this finds the live TEXT objects carry no selection
state either. Selection lives on a parent node or in the animation system — the
two places neither route has reached.

▶ So the strip and the keypad stay unnamed, and **the differ should be left off**
(it is opt-in and costs a 528-byte read per object per movement). It did its job:
it converted an open question into a measured negative for the price of one run.

### ⛔⛔ THE 8-ITEM STRIP: MEASURED AND REFUSED (19:45 run)

It WAS finally walked — **20 movements** — and the result is a clean negative
worth recording so nobody rebuilds it from the same false lead:

- **Zero captions.** One caption appears near a strip movement
  ("Capcom Fighting Evolution"), and pairing it properly shows it belongs to the
  **4-item vertical list** (`0x14009890d`, "3 of 4"), not the strip. ⚠ I first
  read it as the strip's own — an awk pairing that attributed a caption across
  an axis boundary. `MC_GAME_STRIP` therefore cannot see this screen at all.
- **The instance set DOES hold all eight titles**, live, 504 sets each, in the
  collection's canonical order matching the ROM table exactly.

⛔ And that is precisely the trap already refused twice here. The order is
*insertion* order, not position evidence, and the one caption that exists
**contradicts** it: it landed on position 2, where the canonical order puts
"Capcom Vs. SNK 2", not "Capcom Fighting Evolution".

⭐ So the single apparent data point argues AGAINST the mapping it appears to
support. Shipping insertion order here would have been the `g_game_options_
candidate` error with better-looking evidence.

▶ What would settle it: a run that moves the strip **one position at a time with
~1 s dwell**, so the game's own title text lands per position. If the titles
never caption, this screen needs a selection signal — the genuinely open
question the instance route has never had to answer (the music list dodged it by
being numbered; Gallery dodged it by captioning).

### ⭐ CASUAL MATCH: the labels are visible, the JOIN is not (analysed 2026-09-10)

The instance set on that screen holds the row labels outright, at `move 3`:
**Game · Game Version · Cross-region Matchmaking · One-button Special Moves** —
GMD 51, 52, 84, 88. Four labels, and the 4-item list (`0x14009890d`) needs
exactly four.

⛔⛔ **AND THAT IS NOT ENOUGH TO SHIP.** It is "fits the count and the category",
the precise reasoning behind `g_game_options_candidate` and the mislabelled
"Controls" screen. Nothing ties those four labels to THAT caller rather than to
either 3-item list on the same screen object.

The second source this project requires is **unavailable here**, and for a
measurable reason:

- `0x14009885e` and `0x1400988bf` (both 3-item) pass a **STACK-LOCAL** cursor
  (`0x06EBFC50`, far from the screen at `0x2BF16AC0`), so no `values_off`
  arithmetic can address them at all — the Fighter Awards / music-list shape.
- `0x14009890d` (4-item) passes `screen+0x334`, and the 8-item strip passes
  `screen+0x344`. So the row cursor and the strip are 0x10 apart and the value
  cursors must lie between — but **only one value cursor has ever been seen**,
  so which row it belongs to cannot be derived.
- No per-row captions exist on any of the three: every caption near them is a
  stale leftover ("Arcade Mode", "P1") appearing against unrelated positions.

▶ What would settle it: walk the **4-item list** slowly, nudging each row's
value once. That yields per-row value cursors, and `(cursor − screen −
values_off)/4` then confirms the ORDER independently of the label set — the same
two-source standard every shipped settings screen meets.

The cluster's four call sites, for reference:
`0x14009885e` 3v · `0x1400988bf` 3v · `0x14009890d` 4v · `0x140098986` 8h

### ⭐⭐ LOBBY SETTINGS: the LABELS are in hand, the ORDER is not (19:45 run)

The instance set on that screen holds **both halves of the screen**, and they
separate themselves by a property of the data rather than by position:

- **8 row labels, all with `7 set(s)`:** Game · Game Version · Cross-region
  Matchmaking · Comment · Event Mode · One-button Special Moves · Victory
  Condition · Number of Players
- **8 values, all with `6 set(s)`:** Japanese Version · Off · "Everyone is
  welcome!" · On (Quick Match) · On · First to 1 · 2 · CVS2: Millionaire
  Fighting 2001

⭐ So the caption channel's silence on this screen is NOT the end of the road:
the row names are being drawn, just not as per-row help captions.

⛔⛔ **BUT INSERTION ORDER CANNOT BE USED, AND THIS RUN PROVES IT.** The screen
has **10 rows**; only 8 labels were captured. Captions independently pin
**position 7 = Victory Condition** and **position 10 = Passcode**:

- insertion order puts Victory Condition 7th — ✅ agrees;
- **"Passcode" is absent from the instance set entirely** — so the 8 captured
  labels cannot simply be positions 1-8, and the one apparent confirmation is a
  single hit that a wrong ordering would also produce ~1 time in 8.

⚠ AND THE TABLE WAS SATURATED for that report: `64 objects, 31 LIVE, 33 stale`.
So "Passcode is absent" may mean *evicted*, not *not drawn* — two explanations
that cannot be told apart offline. **That ambiguity is itself the finding.**

▶ What would settle it: revisit with the table healthier (fewer screens visited
first), or walk rows 8-10 slowly so their captions arrive. Either gives the two
missing labels AND a second independent order check.

### Lobby settings — `0x2C370E20` (`0x140082c3c` 10v + `0x140082d16` 9h)
Identity is certain: the corpus holds a 60-label `LOBBY_*` family and the live
values match verbatim ("On (Quick Match)", "First to 1").

⚠ **Superseded by the LOBBY SETTINGS section above** — an earlier note here said
"pacing alone decides it" and "only row 1 has ever resolved". The 19:45 run was
paced WELL (2-3 s per row) and still produced only 2 captions, so pacing is NOT
the blocker; and 8 of the 10 labels are in hand from the instance set. What is
missing is the ORDER, not the labels.

### ✅ Fighter Awards — ALREADY DONE, and the "7 unnamed rows" was my error

⛔ I listed "the 7 list rows are unnamed" as outstanding work for days. They are
not. Per run:

    00:53   0 named / 8 bare     (before the achievement path shipped)
    17:32   3 named / 0 bare
    19:13  19 named / 2 bare     "The Year We Made Contact, 2 of 7"

The list's rows ARE achievements -- a scrolling window over the 40-item grid,
which the code confirms (the handler's `0x2aaaaaab` multiplier is a divide-by-3
for the 3-column grid). They speak through the `ACHIEVEMENT_*` branch.

⚠ The historical bare lines are all pre-fix, exactly like the music list's. ⭐
**Ranking silence across ALL logs keeps resurrecting solved screens** -- that is
now twice. Always measure on the newest run that visited the screen.

⚠ And the disassembly confirms there is no row-label table to find: the only
per-index table in that path (`0x1411fb070`) holds packed bitfields
(0, 0, 65536, 0, 131072, …) indexed by `[rcx+0x354]`, a layout/mode field, and
is stored to `+0x340`. Layout parameters, not labels -- correctly so, because
the rows are achievement titles supplied as live text.

### (superseded) Fighter Awards — `0x2BE0B650` (`0x140079833` 7v + `0x140079739` 40h)
⭐ Already USABLE — its items speak via the achievement path. Only the 7 list
rows are unnamed, and it has ONE static caption for the whole screen, so the
caption channel cannot name them. Needs a different idea; lowest priority.

### Music chooser — `0x14009af21` 9h — RE-TESTED AND STILL CLOSED

⚠⚠ **A LOOSE PAIRING WINDOW NEARLY REOPENED THIS WRONGLY.** Scanning for "the
next TEXT SET after a movement" appeared to show the Music chooser naming its
positions just like Gallery — pos 1 "Capcom Fighting Collection 2", pos 2 "CVS:
Millennium Fight 2000 Pro", pos 9 "Plasma Sword" — **four positions, zero
conflicts, and matching Gallery's confirmed mapping exactly.** It looked
conclusive enough to ship.

⛔ It was an artefact of the window. Constrained to the SAME TIMESTAMP as the
movement — the standard Gallery and the Ranked Leaderboard actually meet, where
the title is set in the same millisecond — the Music chooser yields only:

    pos 2: ""            pos 3: "0:00 / 0:00"     (24 movements)

i.e. an empty string and the playback timer. The titles that appeared to pair
were landing one or two log lines further out, belonging to a later event.

⭐ **THE RULE: match the evidence window to the standard the SHIPPED screens
meet.** Gallery sets its title on the same millisecond as the movement. A window
loose enough to admit "a line or two later" admits the whole screen's unrelated
traffic, and on a screen with a playback timer that traffic is constant. This is
the third time in one session that a loose pairing produced a confident wrong
reading (the others: a caption attributed across an axis boundary, and stale
captions read as per-row evidence).

▶ Still needs a channel nobody has. Not reopened.

### Lobby-ID keypad — `0x2C440110` (`0x14008a8ef` 36v + `0x14008a91b` 6h)
36 = 10 digits + 26 letters, reached from "Search Lobby ID".

⭐⭐ **CORRECTED: it DOES set text, and this screen is well within reach.** I
first said its glyphs were probably layout data (the consent-dialog problem)
after looking in the wrong part of the log. The instance report at 19:16:26
shows otherwise:

    "Enter Lobby ID"                                  <- the screen's own name
    "0" "0" "0" "0" "0" "Y"   [move 253, 31 sets ea]  <- SIX character slots
    "Input the lobby ID you'd like to search for."    <- the help caption

So the **6-item horizontal axis is the six ID slots**, each its own live object,
and the 36-item vertical axis is the character picker. The entered ID is
readable directly from those six objects.

▶ What it needs: speak the **character under the cursor** as it moves through
the 36, and the **current ID** as it changes. ⚠ The 36 picker glyphs themselves
were not captured — only the slots — so a run must establish whether the picker
sets text per position or whether the slot contents are the only signal.
⚠ A single character ("Y", "0") is exactly the case `is_value_shaped` admits and
the corpus cannot confirm, so quoting rules apply, not naming rules.

### ✅ SHIPPED: Join Lobby's search filters — `0x2C5002F0`
(`0x14008e799` 6v + `0x14008e813` 3h)

Entered from **"Join Lobby"** (the trail is in the 19:13 log), and row 6's own
caption is *"Turn one-button special moves on or off."* → `LOBBY_SPMOVE`
("One-button Special Moves", idx 88). So it is the lobby SEARCH FILTER screen,
6 rows + a 3-value axis — another `LOBBY_*` row+value screen.

⛔ **`name_screens.py` correctly REJECTED base 83**: it would put
`HELP_SEARCH_LOBBY_ID` at position 1 and `HELP_LOBBY_AREA` at position 5 — i.e.
descriptions where item names belong. The rows are interleaved with their own
help text, exactly like Select Game, so this needs a **MAP**, not a base.

▶ One sighting at one position is not a map. Walk all 6 rows at ~1 s dwell.

### ✅ SHIPPED: `0x1400ac059` 8h — the Ranked Leaderboard's game strip.
Identified by ENTRY POINT (reached from "Ranked Leaderboard") and it captions
one title per movement, like Gallery. `MC_GAME_STRIP`, no map needed.

### ⛔ Consent dialog — DISASSEMBLED, and there is no label table

`0x140087cda`: cursor at `[rbx+0x340]`, count from `[rbx+0x344]`. The confirm
path indexes a table at **`0x141206b78`** by that cursor — `(13, 15, 14, 0, …)`,
exactly three entries for a 3-item dialog, which looks promising.

⛔ They are NOT GMD indices. 13/15/14 name "Arabic [ar]", "Confirm", "Any" —
incoherent as consent buttons. And the table feeds `0x14007ea30`, which
**150 call sites across the shell use**, including Fighter Awards' renderer,
already established as a layout/animation setter rather than a text path.

⭐ So this agrees with the original finding by a second route: the dialog sets no
item text, and its buttons come from layout data. `SYS_WINDOW_YES2/NO2/PP` fit
the count and the category — and fitting is exactly what got that guess reverted
once already.

▶ The remaining route is the GUI tree (the dialog's constructed button objects),
not the message corpus.

### (superseded) Consent dialog — `0x140087cda` 3h
⛔ Sets no item text at all. No pacing fixes it; needs the GUI tree.

## ⭐⭐ ROWS OR VALUES? — what the run actually has to cover

**Rows: yes, every one, slowly. Values: ONE movement per row is enough — you do
NOT need to step through every value.**

Why one movement suffices. A value list ships as `mc_value_set {row, count,
first}`, keyed on the **live count**, and a single movement on the value axis
reports that count outright:

    mcspeak: spoke "Game Version, 2 of 3"
    mc: horizontal cursor 0 -> 1 (item 2 of 3, range 0..2, ...)

One nudge left-or-right gives `count` (and the row it belongs to). `first` is
then found in the corpus offline. Stepping through all of them adds nothing the
first movement did not already establish.

⭐ **But nudge EVERY row that has values**, because `values_off` and the counts
differ per row and per screen, and a row never nudged contributes nothing.

⚠ **A value list whose values are GENERATED gets no variant at all** — volume
sliders, Input Delay, resolutions. Those are quoted live from the game's own
text instead, so extra movements on them are wasted effort.

⭐ **And the count-keying is load-bearing, visibly so.** The 19:13 run shows
"Game Version, 2 of 3" while the shipped variant is for a count of **2** — so no
label was spoken, correctly, rather than a wrong one. A value set is only ever
added from a MEASURED count for exactly this reason.

### ⭐ A CONCRETE EXAMPLE ALREADY WAITING: Select Game's "Game Version"
The 19:13 run shows it with **3 values**; the shipped variant covers a count of
**2** (Japanese / English), so nothing is spoken for the third. The header
predicted exactly this — "Either" is an online-only option — but the run never
captured the value TEXT, only the position, so the third value cannot be
confirmed offline and no variant may be added.

▶ On Select Game, nudge Game Version through its values **with ~1 s between
each** so the value text lands in a caption. That one row is the shortest path
from "predicted" to "measured".

⚠ This is the ONE place stepping through values earns its keep: when a row's
live count DISAGREES with the shipped variant, the values themselves are the
evidence needed. Everywhere else, one nudge.

### So the per-screen recipe is:
1. Move DOWN through every row, ~1 s dwell on each (this is what captures the
   row NAMES — the slow part, and the part that cannot be shortened).
2. On each row that has a value, nudge RIGHT once (and back if you like).
3. Move on. Do not walk the value lists.

## ⭐ THE PACING RULE, restated because it decides most of this
**~1 second of dwell per row.** A screen walked at ~300 ms produces
position-only lines and looks exactly like a broken mod. This has been
mis-diagnosed twice in this project.

## The order to run them
1. Lobby settings (10 rows, slow) — biggest single win, pacing-limited.
2. Casual Match's 8-item strip, then its three lists.
3. The two unknown screens (6+3, 8) — identify by entry point.
4. Music chooser.
5. Keypad and consent dialog — expect these to need a different channel.

## What "complete" will mean
⚠ Not "no unknown screens ever again" — new callers appeared in every run so
far. It means **every screen a player passes through in normal use speaks its
items**, with the remainder degrading correctly to position, which they already
do.

---

# ▶ WHERE IT ACTUALLY STANDS (2026-09-11, measured by tools/coverage.py)

## ⛔⛔ FIRST, THE MEASUREMENT THAT WAS WRONG

I reported "16% of movements speak a name" from the newest log. **That number
described a build four commits old.** The run started 02:10:34; the commits
naming the pause menu, keyboard settings, the in-game AV screen and the move
list landed 02:18-02:39. A log cannot show the effect of code written after it.

⭐ This is structural, not a slip: every log in `docs/` ages the moment the
table grows, and the screens named most recently are exactly the ones the
newest log predates. So the naive count **drifts downward as the project
improves** -- the same defect as [[benchmark-drifts-from-the-code]], in a
measurement rather than a benchmark.

**`tools/coverage.py`** replays a log's movements (caller + count + position --
facts about the GAME, which do not age) against the table as it stands NOW.

- newest run, current table: **55%** (not 16%)
- **all runs, current table: 2045 of 2642 movements = 77% named**

⚠ Sabotage-verified: with `parse_maps()` returning nothing, 77% collapses to
1%. The number comes from the table, not from the parser being generous.

⚠ It reports whether a row is COVERED, never whether the label is RIGHT. A
screen can read 100% here and speak the wrong words. Coverage is necessary,
never sufficient; every row still needs `MC_VERIFIED_LIVE_TEXT`.

⭐ Its first version reported a false gap on the MAIN MENU, because it only
understood label maps and the main menu uses the original `base + index`
contiguous-run model. Fixed. A coverage number is only as good as its model of
what counts as covered.

## What the remaining 597 movements actually are

**Not silent -- already correct, and only "gaps" because a bare position is
the complete reading:**
- `0x140079739` (40) + `0x140079833` (7) -- Fighter Awards; items speak via the
  ACHIEVEMENT path, the game hands us each title as live text.
- `0x14009885e`, `0x1400988bf`, `0x14009890d` (3 each) -- Casual Match paging
  controls over image resources. "3 of 3" IS the reading.
- `0x140080250` (3) + `0x14008028e` (48) -- the move list's page selector and
  its 48 moves, which now speak motion notation.

**Two more AXES of screens already named** (same shape as the 0795b7f commit):
- `0x14009ed20` (4) and `0x14009ed68` (2) both carry screen `0x2C498050` --
  the SAME screen object as the in-game AV value axis already shipped.

**Genuinely unnamed, and both need the same missing piece:**

### ⭐⭐ `0x14008a91b` IS THE LOBBY ID FIELD -- identified by ENTRY POINT
6 items, horizontal, 121 movements -- the most-visited unnamed screen. It
appears in EVERY run that also contains the 36-item character picker
(`0x14008a8ef`), always interleaved with it.

The evidence is decisive: the instance set holds **exactly six
single-character objects, all LIVE at once**, and the one being edited walks
the picker's alphabet (`Z` -> `W` -> `U` -> `Q`) while the other five hold
`"0"`. Six slots = a six-character lobby ID. The refused captions on this
screen are `"0"` and `"Y"` -- `Y` is the last letter of the picker's `0-9 A-Y`
range, i.e. the field reading back what the picker committed.

⛔⛔ **BUT INSERTION ORDER IS NOT SLOT ORDER.** In the 1913 run the changing
slot is LAST in the instance list; in the 1945 run it is FIRST. Whichever slot
was edited decides. A lookup by list position would have passed both runs
individually and spoken the wrong slot in general -- the exact sabotage the
music-track fixture exists to catch.

▶ So the join must be cursor-address arithmetic
(`(cursor - screen - values_off)/4`), the only method never wrong here. That is
a LIVE-MEMORY question and cannot be settled from these logs.

### ⚠ `0x14009af21` (Music chooser's 9-item strip) is NOT a game strip
The titles ARE live text on that screen (`"Capcom Fighting Evolution"`,
`"Capcom Vs. SNK 2: Mark of the Millennium 2001"` appear in its instance dump),
so they are reachable. But **24 movements across all runs produced ZERO
captions** -- confirmed across every log, not from the single run the earlier
note cited. `MC_GAME_STRIP` names from the CAPTION, so shipping it as one would
ship a screen that speaks nothing.

⭐ Same structural problem as the lobby field: the text exists, the caption
channel is blind, so it needs a position join. This is what `MC_NUMBERED_LIVE`
did for the music tracks -- but the tracks self-verify via a PRINTED NUMBER and
game titles carry none, so the ROM table is the corroborator instead.

## ⭐ THE ONE PIECE BOTH NEED
A **position -> live object** join for screens where the caption channel is
blind. Two screens now want it, both already have the text on screen, and both
are blocked on the same thing. That, not more label tables, is the next real
piece of work.
