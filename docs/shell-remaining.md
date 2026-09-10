# What is left in the SHELL

## ⚠⚠ UPDATE 2026-09-10, after the 19:13 run — THE COUNT IS NOT CONVERGING

**15 unnamed of 29 callers ever seen.** The count has gone **8 → 10 → 15** across
three runs *while screens were being shipped in each*. Seven of the 19:13 run's
callers had never been seen before.

⭐ **This is the single most important planning fact in this file.** Deeper
exploration keeps finding screens faster than naming retires them, so "name the
remaining screens" is not a task with a near horizon. It is not a setback — the
mod degrades correctly on every unknown screen ("3 of 6" is true and useful) —
but it should stop being described as almost done.

⭐ The 15 callers are only ~8 SCREENS: call sites pair up on one screen object
(a row list plus a value axis), the shape already known from settings screens.

### What the 19:13 run added
- `0x14008a8ef` (36, vertical) + `0x14008a91b` (6, horizontal) on
  `0x2C440110` — reached immediately after **"Search Lobby ID"**, so this is the
  lobby-ID **entry keypad**: 36 = 10 digits + 26 letters, with a value of "0".
  ⚠ No single-character objects appear in the instance set, so its glyphs are
  probably layout data — the consent-dialog problem, not the caption problem.
- `0x14008e799` (6, vertical) + `0x14008e813` (3, horizontal) on `0x2C5002F0`.
- `0x1400ac059` (8, horizontal), standalone.

⚠ None produced usable captions. They are listed so the next run knows they
exist, NOT as evidence about what they contain.

---

## ⭐ UPDATE 2026-09-10, after the 17:32 run — THREE SHIPPED, TEN REMAIN
Shipped since this file was written: **Music tracks** (confirmed in-game),
**Lobby** (`0x140083e09`), **Gallery** (`0x14009a482`). Section B below is
therefore ANSWERED — Gallery was never closed; nothing had entered it.

⚠ The run also revealed **three callers never seen before**, so the remaining
count went 8 → 10. Naming screens uncovers screens.

### ▶ THE NEW ONE: lobby settings (`0x140082c3c` 10-row + `0x140082d16` 9-value)
One screen object (`0x2C370E20`), reached from Create Lobby. A ROW+VALUE screen,
the shape Select Game and the Options submenus already use.

⭐ Identity is CERTAIN: the corpus has a **60-label `LOBBY_*` family**, and the
live instance set on that screen holds its values verbatim — "On (Quick Match)"
(`LOBBY_EVENT_00`), "First to 1" (`LOBBY_ROUND_00`), "On", "2".

⛔⛔ **AND IT IS NOT SHIPPABLE, FOR THE MOST DANGEROUS REASON.** The family
contains exactly the plausible row labels — Event Mode, Victory Condition,
Number of Players, Passcode, Cross-region Matchmaking, Game Start Privilege,
One-button Special Moves — and ten of them could be assembled into a map that
"fits" a 10-row screen. **That is the reasoning that produced
`g_game_options_candidate` and the mislabelled "Controls" screen.**

What the evidence ACTUALLY supports, from the single visit:
- row 1 → `HELP_GAME_TITLE` → `GAME_TITLE`, unique. ⭐ Usable.
- row 2 → `HELP_GAME_VERSION` / `HELP_GAME_VERSION_ON`, **AMBIGUOUS** — and the
  mod correctly refused it in the run.
- rows 3-10 → **no caption, no evidence of any kind.**

▶ It needs one slow pass (~1 s dwell per row) down all ten rows. Then the
caption channel names them and `test_screen_maps.py` validates the map. Until
then it speaks position, which is correct.

⚠ A `LOBBY_*` label existing is NOT evidence that it is row N of this screen.
The corpus says which labels EXIST; only a run says which the screen DRAWS.

---


Written 2026-09-10, after the music tracks shipped. Derived by cross-referencing
every `[DISCOVERY] unknown screen` line in **all seven run logs** against the
current `g_screens`, so it is a measurement rather than an impression.

⚠ "Appears in menu_speak.c" is NOT the same as "named". Two callers appear only
inside ⛔ do-not-name comments (`0x140087cda`, `0x14009af21`) and are counted
here as unnamed, which is what they are.

## Shipped: 20 rows across 11 screens
Main menu · Options · Museum · Select Game · Select Mode (x2 callers) ·
Online Play · Network / Sound / PC / System Settings · Music (tracks).

## THE EIGHT THAT REMAIN

Grouped by what actually blocks them, because that decides whether the next step
is a run or a build.

### A. Blocked on a RUN, not on code (4 screens, one cluster)
`0x2BD9C3E0`, reached from **Online Play > Casual Match**. Four call sites share
this one screen object:

- `0x14009885e` — 3 items, vertical
- `0x1400988bf` — 3 items, vertical
- `0x14009890d` — 4 items, vertical
- `0x140098986` — 8 items, horizontal

⭐ **The 8-item strip is the online GAME CHOOSER and it sets titles LIVE.**
Unlike the music strip, the titles are re-set *during its own movements*
("Plasma Sword", "Capcom Fighting Evolution", "Capcom Vs. SNK Pro" arriving
between movements at 13:42:41-45). That is a changing signal, not resident data.

⛔ The three vertical lists set **no item text of their own**, and their cursor
is a **stack local** (`0x06D7FC50`), so no `values_off` arithmetic can address
them and no label map can be built by that route.

▶ What a run must supply: walk each list with **~1 s dwell per row** so captions
arrive, then `python tools\name_screens.py <log>`. The dwell requirement is the
single most repeated lesson in this project -- a fast walk produces
position-only lines and looks exactly like a broken mod.

### B. Blocked on a RUN, and never once entered (1 screen)
**Museum > Gallery** — `0x14009a482`, 9 items, the presumed sibling of the Music
chooser. ⚠ **No run has ever entered it.** The old claim that its items are
"unnumbered logos needing a selection signal" was extrapolated from a note about
its CONFIRM handler and is untested in both directions.

▶ Enter it and read the instance set. If it sets live text like the online
chooser does, it may need nothing new at all.

### C. Blocked on EVIDENCE that may not exist (2 screens)
**Fighter Awards** — `0x140079833` (7-item list) and `0x140079739` (40-item
grid), two halves of one screen.

⭐ Its ITEMS already speak: the game hands each achievement's title as live text
and the `ACHIEVEMENT_*` branch in `menu_speak()` leads with it. So the screen is
**usable today**; what is missing is naming the seven list rows.

⛔ It has **no per-row captions** -- one static sentence for the whole screen
("View all of your challenges."), which is why the repeat-suppression rule
exists. So the caption channel cannot name these rows however slowly it is
walked. Needs a different idea, and is the lowest value of the eight because the
content is already reaching the user.

### D. Blocked on a CHANNEL THAT DOES NOT REACH IT (1 screen)
**The data-sharing consent dialog** — `0x140087cda`, 3 items.

⛔⛔ It sets **no item text at all**; its buttons come from layout data. No
pacing fixes this. `SYS_WINDOW_YES2/NO2/PP` fit the count and the category
exactly -- and were shipped once, then reverted when the user said they were
"not confident about the data collection options you chose". They were right:
nothing has ever shown those three words on that screen, and the ORDER was pure
assumption.

▶ The only honest routes are the GUI tree or the dialog's own construction.

## ⭐ THE HONEST SUMMARY
**Five of the eight are blocked on ONE play session**, not on code: the four
Casual Match lists and Gallery. Two more (Fighter Awards) are already usable and
need an idea nobody has. One (consent) needs a channel that does not exist.

⛔ So there is **no offline shell work that names a screen**. Anything built
without that run would be reasoning about screens nobody has observed, which is
the specific error that produced `g_game_options_candidate`, the mislabelled
"Controls" screen, and the reverted consent dialog.

## What IS buildable offline (does not name anything)
- ⚠ The instance table is **64 slots and was 100% full with 534 evictions** in
  the 13:43 run, 30 of 64 entries stale. It evicts oldest-first so it tracks the
  present, but a screen with many objects could still push out its own siblings.
  Raising the cap and/or reclaiming stale slots is real, testable, offline work
  -- and the music lookup now depends on that table holding the current screen.
- The `-Wclobbered` sweep over the rest of the codebase: it was added recently
  and immediately found a real setjmp/longjmp hazard in the first new function.
  Nothing has checked the OLDER guarded reads against it.
