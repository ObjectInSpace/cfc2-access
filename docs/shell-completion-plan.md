# Completing the shell — the run plan

User's decision 2026-09-10: **finish the shell, then the games.** The shell is
the first thing a blind player meets, and a half-read menu is a wall in front of
everything else.

⚠ Written because "name the remaining screens" has not converged (8 → 10 → 15
across three runs). Treating it as a campaign with a checklist is the fix; doing
one screen per run is what let the count drift.

## The target: 15 callers = 8 SCREENS

Call sites pair up on one screen object (row list + value axis), the shape
settings screens already use. Grouped by screen, with what each NEEDS:

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

### Fighter Awards — `0x2BE0B650` (`0x140079833` 7v + `0x140079739` 40h)
⭐ Already USABLE — its items speak via the achievement path. Only the 7 list
rows are unnamed, and it has ONE static caption for the whole screen, so the
caption channel cannot name them. Needs a different idea; lowest priority.

### Music chooser — `0x14009af21` 9h
⚠ Gave NO captions in any run, unlike Gallery's identical-looking strip.
▶ Walk it slowly once. If it stays silent, it needs the instance route.

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

### Consent dialog — `0x140087cda` 3h
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
