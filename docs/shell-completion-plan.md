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

### 1. Online Play > Casual Match — `0x2BD9C3E0` (4 callers)
`0x14009885e` 3v · `0x1400988bf` 3v · `0x14009890d` 4v · `0x140098986` 8h

⛔⛔ **The 8-item strip has NEVER BEEN MOVED ON.** Zero movements in any run — it
was discovered and left. The game titles seen near it in the 13:43 log were set
by the ADJACENT 3-item list, not by the strip. There is currently **no evidence
whatsoever** about its contents.

▶ **Walk the 8-item strip end to end.** If it captions like Gallery, it ships
immediately via `MC_GAME_STRIP` (already built). Then the three vertical lists at
~1 s dwell each.

### 2. Lobby settings — `0x2C370E20` (`0x140082c3c` 10v + `0x140082d16` 9h)
⭐ **The highest-value target, and pacing alone decides it.** Identity is
certain: the corpus holds a 60-label `LOBBY_*` family and the live values match
verbatim ("On (Quick Match)", "First to 1").
⛔ Only row 1 has ever resolved; rows 3-10 have no evidence.
▶ **Ten rows, ~1 s dwell each, in one pass.** Then `name_screens.py`.

### 3. Fighter Awards — `0x2BE0B650` (`0x140079833` 7v + `0x140079739` 40h)
⭐ Already USABLE — its items speak via the achievement path. Only the 7 list
rows are unnamed, and it has ONE static caption for the whole screen, so the
caption channel cannot name them. Needs a different idea; lowest priority.

### 4. Music chooser — `0x14009af21` 9h
⚠ Gave NO captions in any run, unlike Gallery's identical-looking strip.
▶ Walk it slowly once. If it stays silent, it needs the instance route.

### 5. Lobby-ID keypad — `0x2C440110` (`0x14008a8ef` 36v + `0x14008a91b` 6h)
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

### 6. ⭐ IDENTIFIED: Join Lobby's search filters — `0x2C5002F0`
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

### 7. Unknown — `0x1400ac059` 8h, standalone.
Identify by ENTRY POINT on the next run, never by shape.

### 8. Consent dialog — `0x140087cda` 3h
⛔ Sets no item text at all. No pacing fixes it; needs the GUI tree.

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
