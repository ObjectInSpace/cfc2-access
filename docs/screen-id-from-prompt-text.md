# Item 2: identifying a screen from its prompt text — what the corpus says

Measured 2026-09-09, offline, before writing any matcher. The plan called this
"the highest-value use" of the live text channel, and the measurements support
that — but they change the design in two ways worth settling first.

## 1. There are TWO text populations, not one

The live `setText` captures mix two shapes, and they answer different questions:

- **Button hint strips** (`HELP_BUTTON_*`, `HB_*`, 93 labels). The row of
  affordances along the bottom: "Confirm / Back", "Training / Game Settings /
  Back". These belong to the SCREEN.
- **Prose captions** (`HELP_*` without markup, 187 labels). One-line
  descriptions: "Choose the game you want to play.", "Play offline." These
  belong to the **focused ROW**.

## 2. ⭐⭐ The captions are PER-ROW, and they track the cursor

This is the correction to the plan's premise. `docs/rethink-after-gui-text.md`
reasoned that `"Choose the game you want to play."` "names the Select Game
screen outright". It does — but not because it is a screen title. It is the
help text for the row **`GAME_TITLE`**, which happens to be row 0.

The Select Game screen's three rows are GMD 51/52/56 —
`GAME_TITLE` · `GAME_VERSION` · `PLAYER` — and the corpus carries exactly:

    HELP_GAME_TITLE     "Choose the game you want to play."
    HELP_GAME_VERSION   "Choose a game version."
    HELP_PLAYER         "Choose a player number."

The main menu behaves the same way — a different caption per row:

    OFFLINE_PLAY   -> "Play offline."
    ONLINE_PLAY    -> "Play online using a network connection."
    MUSEUM         -> "Browse various art galleries or listen to..."
    FIGHTER_AWARD  -> "View your in-game challenges and play status."
    EXIT           -> "Exit the application."

⭐ **This is better than what the plan expected, not worse.** A per-row caption
is a description of the item the user is standing on — the "verbose
description" a screen reader would normally offer — and the corpus already
authored one for essentially every menu item. That is a feature the mod could
not otherwise have.

⚠ But it means a caption identifies a **row**, and only implies the screen by
membership. Speaking it as a screen NAME would be a claim the evidence does not
support — the same shape of error as the plausible `g_screens` row that made
one screen speak another's labels.

## 3. ⚠ `HELP_<LABEL>` is a HEURISTIC, not a rule — 8 of 14

The obvious mechanical rule is "the caption for item label `X` is `HELP_X`".
Tested against the three screens whose labels are known:

- Main menu: 5 of 6 (`HELP_OPTIONS` does not exist)
- Select Game: 3 of 3
- Options: **0 of 5** — the labels are `MENU_OP_SYSTEM` etc. but the captions
  are `HELP_OP_SYSTEM`, dropping the `MENU_` prefix. And `MENU_OP_NET`'s
  caption is `HELP_OP_NETWORK`, which no string transformation derives.

⛔ So a name-derived join must NOT ship on its own. It is right often enough to
look correct in testing and wrong often enough to lie in use — precisely the
profile of the bug this project keeps re-learning. (Compare
`cfc2-menu-label-runs`: "the name-based check lies".)

## 4. ⭐ Identification from the strips IS sound — measured

The question that decides the feature: does one strip text map to one screen?

- 93 strips, 74 distinct texts. 31 of 93 are not unique by raw text.
- **But the duplicates are almost entirely PLATFORM VARIANTS of one screen** —
  `_XB`, `_PS`, `_NX_EU`, `_NX_US` suffixes. They differ by which controller
  glyph is drawn, not by screen.
- Collapsing those suffixes: **only 1 of 74 texts maps to more than one screen**
  (`"Cancel Ready"` → `HB_LOBBY_CANCEL` / `HELP_BUTTON_LOBBY_READY`, two
  related lobby screens).

Prose captions are similarly clean: **164 distinct texts, 4 collisions**, all
near-synonyms (`HELP_OPTION`/`HELP_OP_SYSTEM`,
`HELP_GAME_VERSION`/`_ON`, pad variants, dummy variants).

⭐ So a strip or caption is a strong identifier — provided the platform-variant
collapse is applied, and provided the 1 known collision returns "ambiguous"
rather than picking one.

## 5. What this licenses building

1. A lookup from a captured string to the **label** that produced it, built
   from the GMD — exact text match, platform variants collapsed.
2. That label answers "which ROW am I on?" directly and "which SCREEN?" only
   where a label is unique to one screen.
3. ⛔ It must return **ambiguous** as a value, not a guess, for the collisions
   above. See `inconclusive-must-be-a-return-value`.
4. ⛔ It must NOT be joined to the cursor index by timing — three of six
   captures in the 21:19 run coincided with no cursor movement.

## 6. What is still open

⚠ Whether the game actually sets a caption on every row change is **not
established** — the corpus proves the strings EXIST and are distinct, not that
the shell pushes each one through `setText` as the cursor moves. The 21:19 run
saw 6 distinct strings from 324 calls, which is far fewer than one per row.

That is item 4's measurement, and it should be taken before any caption is
spoken as if it tracked the cursor.
