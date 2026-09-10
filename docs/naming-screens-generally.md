# Naming screens as a class

Written 2026-09-09 after the caption channel was verified in-game. This
replaces "pick the next screen and investigate it" with a repeatable
procedure, because the caption channel changed what a single sighting is worth.

## The mechanism

The mod logs the focused row's live help caption. The corpus pairs a caption to
its item by stem — `HELP_OFFLINE_PLAY` describes the item `OFFLINE_PLAY` — so
**one sighting yields the item's GMD index**, and the log states the position
the cursor was on. Subtracting gives the screen's base:

    "Play offline."                     -> OFFLINE_PLAY -> idx 37, pos 1 -> 37
    "Choose the game you want to play." -> GAME_TITLE   -> idx 51, pos 1 -> 51

Both are the values already verified in `g_screens`. **112 of the corpus's 187
prose captions are usable this way** (the rest are shared between screens and
are deliberately dropped).

⭐ This is why it scales: no per-screen offline investigation, no candidate
shortlist for a human to eliminate. The caption names the item outright.

## Why the old route was not enough

`resolve_screens.py` intersects candidate bases by plausibility. On the 6-item
Online Play menu it left **six** survivors — and the corpus's own obvious
5-item run (`CASUAL_MATCH`…`ONLINE_OPTION`) was not among them, correctly,
because position 6 would land on a `HELP_` string. It narrows; it does not
name.

## The procedure

1. Play through the unnamed screens, moving the cursor **at least twice on
   each** (two sightings is what turns a lead into a consistent base).
2. `python tools/name_screens.py <log>`
3. Read the verdict per screen:
   - **`⭐ CONSISTENT across N sightings`** — the base is supported by
     independent evidence. Compare the printed item list against what is
     actually on screen, then add a `g_screens` row.
   - **`⚠ single sighting — a LEAD`** — plausible, not established. Get a
     second sighting before shipping it.
   - **`⛔ SIGHTINGS DISAGREE`** — the screen is **not** a contiguous run. It
     needs a label MAP, like Select Game. Do not pick one of the bases.
   - **`⚠ NO base derived`** — no caption paired. Either the screen has no
     captions (some do not — see below) or they are among the shared ones.

⚠ **Step 3 still ends in a human comparison.** Every shipped row needs
`MC_VERIFIED_LIVE_TEXT`, and that has not changed: this route supplies strong
evidence for a base, not permission to skip looking.

## What this cannot reach

⚠ **Captions are not universal.** In the 22:50 run only 4 of 18 spoken lines
carried one, and the screen at caller `0x14009BA21` produced none at all —
`setText` froze across its 7 movements. Those screens still need the old route
or a different idea.

## The 9 candidate screens

Clusters of ≥3 captioned items in the corpus, i.e. what this route could name:

- **37–42** main menu *(already named)*
- **68–72** Online Play: Casual Match, Ranked Match, Custom Match, Ranked
  Leaderboard, Online Options — ⭐ the biggest known gap, and the screen the
  22:50 run sat on
- **78–80** lobby: Create Lobby, Join Lobby, Search Lobby ID
- **250–256** pause menu (7 items): Resume, Restart, How to Play, Move List,
  Load Quick Save…
- **718–720** training gauges · **727–729** training display
- **926–928** standby settings
- **4110–4114** versus menu · **4142–4146** training assists

⚠ These are corpus clusters, **not** confirmed screens. A cluster is a
hypothesis about which labels sit together; the run is what ties one to a
caller address. Do not ship a row from this list alone — that is exactly the
plausibility that made one screen speak another's labels.
