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
