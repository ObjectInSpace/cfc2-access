# Training Options + Move List — Training Options SHIPPED, move list open

⭐ Training Options shipped 2026-09-11: `g_training_option_rows` (6 rows) +
`g_training_option_values` (5 count-keyed variants) in `src/menu_speak.c`.
30 map checks green, 16 runners pass, deployed and hash-verified.

Settled 2026-09-11 in `docs/run-2026-09-11-1626.log`, the first run with
`cfc2_layout.on` enabled. Both screens were blocked on the same missing join —
**position → live object** — and the coordinate dump answered both.

## ⭐⭐ THE CONTROL FIRST — the method is validated on this run, not assumed

Sorting the training PAUSE MENU by `+0x044` reproduces its independently
established order, 8 of 8, zero contradictions:

    Y=225 Resume · 305 Marquee Card · 385 Move List · 465 Controller Settings
    533 Display & Sound · 625 Training Menu · 705 Change Character · 785 Quit

That order was already in `g_screens` from captions, so this is a free control:
the answer was known before the coordinates were read.

⚠ **A MENU IS A COLUMN — group by X first, then sort by Y.** Established
earlier ([[cfc2-gui-object-coordinates]]) and it is load-bearing again here.

---

# 1. TRAINING OPTIONS — all six rows established

Screen `0x2C387...`, two call sites:

- `0x1400a41ef` — **6 rows, vertical**, cursor `screen+0x350`
- `0x1400a4241` — **value axis**, `values_off = 0x3F0`

Reached from the TRAINING pause menu row 6, labelled **"Training Menu"** live
(arcade has "Load Quick Save" there).

## ⭐⭐⭐ THE X GROUPING RESOLVED THE AMBIGUITY I REFUSED TO GUESS

The instance set held **seven** labels for a **six**-row screen, and insertion
order could not say which was not a row. Coordinates say it outright:

    X=960   "Dummy Behavior"   Y=265      <- ALONE, and ABOVE everything
    X=320   the six real rows, 80.0 px apart:

        row 0  Y=326  Action         →  8 values → DUMMY_ACTION   683
        row 1  Y=406  Block          → 13 values → CVS2_GUARD    2927
        row 2  Y=486  Throw Escape   →  3 values → DUMMY_NNUKE    700
        row 3  Y=566  Recovery       →  6 values → CVS2_OKI      2940
        row 4  Y=646  Counter Hit    →  3 values → COUNTER_HIT   2960
        row 5  Y=726  Default        → no value axis → MENU_OP_DEFAULT 168

⛔⛔ **THE INDICES ABOVE WERE WRONG IN v3 OF THIS FILE.** I derived them from
`enumerate()` over `data/text_english.json` — a DICT INSERTION POSITION, not
the GMD index the mod looks up. The map resolved to fighting-move names
("Psychic Throw", "Crystal Shoot") and would have spoken those where training
settings belong. `tools/test_screen_maps.py` caught it before it shipped.

⭐ The index must come from the parsed `menu_eng` table
(`tools/name_screens.load_corpus`), and the control confirms that order:
250 → `CONTINUE` → "Resume", matching the already-shipped pause map.

**"Dummy Behavior" is the screen HEADING, not a row.** Different column, higher
on screen, and it is the only member of the family with no `HELP_` caption.
That was a lead in v2 of this file; it is now measured. The X=960 column is the
same one that carries "Ken Moves" and "P1 Pause Menu" — a heading column shared
across screens, which corroborates the reading.

## ⭐ ROWS 2 AND 4 ARE SEPARATED BY POSITION, AND ONLY BY POSITION

Both have **3 values**, and `DUMMY_UKEMI` and `COUNTER_HIT` both read
*Off / On / Random* — identical strings. Count cannot separate them; text
cannot separate them. v2 of this file recorded them as unresolvable and that
was correct on the evidence then available. Y-order resolves them.

⭐ So every row now has **two independent sources**: the coordinate for its
ORDER, the live count for its VALUE LIST. That is the standard every shipped
settings screen here meets.

## ⚠ THE SCREEN IS PER-GAME — still true, still unconfirmed elsewhere

Rows 1 and 3 resolved to **`CVS2_GUARD`** (13) and **`CVS2_OKI`** (6), not the
generic `DUMMY_GUARD` (4) / `DUMMY_OKI` (4). The corpus carries `CVS1_OKI` (3),
`MJUS_OKI` (6), `Z3U_UKEMI`, `Z3U_GUARD_04/05` — one family per game.

⛔ **Both coordinate runs were CVS2.** A map keyed on caller+count would speak
CVS2's thirteen blocking options on a game that has four. `mc_value_set` keys
on the live count already, which handles it — but the ROW LABELS may also swap
(does Alpha 3 even have a "Block" row?), and nothing has measured that.

▶ **Before shipping: one training session on CVS1 or Alpha 3**, same walk. If
the six row labels are identical and only the value counts differ, one row map
plus per-count value sets covers every game.

---

# 2. THE MOVE LIST — same structure, and the names are readable

`0x14008028e` (horizontal) + `0x140080250` (vertical), screen `0x2C3F7B60` /
`0x2C3874D0`. User-reported as "not reading"; confirmed.

## ⛔⛔ CORRECTION: THE TWO AXES WERE LABELLED BACKWARDS

Earlier versions of this file called `0x14008028e` "the 48 moves" and
`0x140080250` a "per-character category selector". **Both are wrong.** The
interleaved movement log proves it: every step of the horizontal axis is
followed by a vertical axis reporting a *different* total — 3, 7, 4, 4, 6, 2, 4
in sequence.

    0x14008028e  HORIZONTAL, "of 48"  = the CHARACTER ROSTER (CVS2's cast)
    0x140080250  VERTICAL, 2..14      = that character's MOVE LIST

⭐ Everything that looked odd now follows: the vertical count changes right
after each horizontal step because a different character is selected; 154
distinct move names appear across the run because that is many characters at
6-10 moves each; and the live move-name window is 6-10 rows, not a scrolling
view of 48.

⚠ 35 also appears as a horizontal total (16:35 run), so the roster size differs
per GAME rather than being a fixed 48.

## ⭐⭐ THREE COLUMNS, and one page is a clean 73 px grid

From a single dump block (Ken):

    X=  84   the move NAMES          73.0 px apart, uniform
    X= 960   the heading             "Ken Moves", Y=142
    X=1565   the INPUT notation      <RGHT><SIZE 58><ICON C_…>

    Y=234 Hadoken · 307 Shoryuken · 380 Tatsumaki Senpu-kyaku
    453 Ryusen Kyaku · 526 Nataotoshigeri · 599 Kamabaraigeri
    672 Osoto Mawashigeri · 745 Zenpou Tenshin · 818 Shoryureppa · 891 Shinryuken

Ten names, ten evenly spaced rows, in screen order. `gui_markup.c` already
speaks the input column (`<ICON C_236>` → "quarter circle forward").

## ⛔⛔ THE CATEGORY COUNT IS NOT PER-CHARACTER — v2 of this file was wrong

v2 said "per-character category list (Balrog: 7)". Across **17 characters**
walked in this run the category axis reports:

    2, 3, 4, 5, 6, 7, 8, 9, 12, 14

and Balrog alone showed 7/4/6/2 **within one visit**. So the count varies per
PAGE, not per character. A fixed count is wrong by construction — which is
exactly why nothing names it today.

⭐ That is fine for the coordinate route, which never needs a fixed count: it
reads whatever objects are live in the column and orders them.

## ⚠ TWO PRACTICAL WRINKLES BEFORE ANY MATCHING

1. **Every move name carries a LEADING SPACE** (`" Hadoken"`), and some carry
   inline markup (`" Tatsumaki Senpu-kyaku <SIZE 20>(…)</SIZE>"`). Trim and
   strip before comparing.
2. **The layout dump truncates at 24 chars** — a dump-only limit, not a limit
   of the live text. Do not build a matcher against truncated strings.

⛔ The GMD route stays closed for ORDER: `CMD_NAME_CVS2_20` holds Balrog's 13
moves, but the screen pages them 7/4/6/2, so GMD order ≠ screen order. The
corpus says which names exist; the coordinates say where they sit.

---

# ▶ WHAT IS LEFT

1. **One run on a second game** (CVS1 or Alpha 3) → Training Options.
   Confirms whether the per-game swap touches row LABELS or only value counts.
2. Then ship Training Options: a 6-entry row map + `mc_value_set` entries keyed
   on the measured counts, validated by `tools/test_screen_maps.py`.
3. The move list needs the coordinate join wired as a reader (the `MC_COORD_LIVE`
   machinery already exists for the Lobby ID field) rather than a label table.

⚠ Every shipped row still needs `MC_VERIFIED_LIVE_TEXT`.

---

# ⚠ THREE CORRECTIONS THIS FILE HAS ABSORBED

Kept because each was a confident wrong reading that measurement overturned.

1. **v1's row map was invented.** It paired the instance-set labels to the
   `DUMMY_*` family by plausibility and predicted 4 values for rows 1 and 3.
   Measured: **13 and 6**. My map contained no such list. That is the
   `g_game_options_candidate` error — fits the count, fits the category, wrong.

2. **v1 blamed the user's pacing for missing captions.** The 16:13 run walked
   at ~600 ms-1 s and still produced **zero** captions for this screen while 30
   prose captions flowed for other objects in the same window. The channel is
   structurally blind here, like the Music chooser. Not a pacing problem, and
   it should not have been attributed to how the user played.

3. **The `KB_*` "unmapped tokens" report was wrong.** I said `gui_markup.c`
   failed to map 12 of 15 keyboard tokens and that this was why binding values
   are silent. `gm_icon()` has a mechanical `KB_` rule at
   `src/gui_markup.c:157-166` — any unlisted `KB_*` token has its prefix
   stripped and the remainder spoken, so `KB_F1` → "F1". Lines 24-25 say so and
   `tools/test_gui_markup.c:156` covers it. **I grepped the TABLE and never read
   the LOOKUP.** Same error as calling the game titles images because they were
   not in the GMD: *"not in the file" is not "not in the program."* Adding those
   rows would have been dead code shadowing a working rule and would have broken
   the 239/239 `check_markup_parity.py` parity.

⚠ So the KEYBOARD BINDING values remain unexplained, and the markup layer is
not the cause. Known obstacles, from `src/menu_speak.c`: the 2-value axis
`0x14009ed68` has a **stack-local cursor** (no arithmetic join), and
`0x14009ed20` has no value set because its live count of 4 disagrees with the
corpus's Type A / Type B pair. ⭐ The coordinate join now proven on two screens
is the obvious candidate for it — the key glyphs are live text objects with
coordinates, exactly like these rows.
