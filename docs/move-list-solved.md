# The MOVE LIST: solved from the disassembly

Read out of the instructions 2026-09-11, offline, no play session. **Not yet
implemented** — this is the mechanism, verified against the shipped data.

## Why the coordinate route failed, and why it does not matter now

The probe (`cfc2_movelist.on`, since removed) measured `names != total` on
every sample, and the disassembly says exactly why:

    0x1400803e5  mov    r8d, [rcx+0x3d8]     ; the REAL item count
    0x14008040f  mov    ecx, 0xa
    0x140080418  cmp    r8d, ecx
    0x140080423  cmovge edx, ecx              ; clamp to TEN visible rows

⭐ **The screen draws at most 10 rows.** So a character with 14 moves shows 10,
and `position N` is not the Nth visible object. The probe's negative was right;
the cause was this cap, not the instance table saturating as I first guessed.

⛔ Two of my own readings to not repeat: I reported `names=10` as "constant
while total varies" (it was the row cap, and the layout dumps do show 5..10),
and I blamed table saturation (97 dropped). Saturation can only undercount, so
it could never explain `names=8` against `total=12` — that row is what settles
it.

## ⭐⭐⭐ THE DATA PATH

From the vertical redraw `0x1400803d0`, draw loop at `0x140080445`-`0x14008047b`:

    add    ecx, r9d                                  ; cursor + loop counter i
    movsxd rdx, [r14 + rcx*4 + 0x354]                ; per-row MOVE ID
    movsxd rcx, [r14 + 0x330]                        ; which GAME is loaded
    mov    rcx, [rsi + rcx*8 + 0x1205d30]            ; that game's move table
    lea    r8, [rdx + rdx*2]                         ; id*3
    lea    r13, [rcx + r8*4]                         ; 12-byte records

So, per row:

    move_id  = [screen + row*4 + 0x354]
    record   = game_table[ [screen+0x330] ] + 12 * move_id
    NAME     = gmd_text_by_index( record[+4] )

### Screen fields (the vertical axis)

    +0x330   game index (0..7)
    +0x354   array of per-row move IDs
    +0x3d4   the cursor (matches the +0x3D4 measured live)
    +0x3d8   the REAL item total (the cursor's reported count)

⚠ The horizontal (roster) axis uses `+0x340` cursor with its bound at `+0x344`
— measured live and confirmed at `0x14008026f`-`0x14008028e`.

### The per-game table array: `0x141205d30`

**Exactly EIGHT valid pointers, then the array ends** — which is the
collection's eight games, and a strong confirmation rather than a coincidence:

    0 CVS1  0x1411FC590     4 PWS1  0x1412025A0
    1 CVS2  0x1411FDCA0     5 PWS2  0x1412027A0
    2 CFJ   0x1411FFB50     6 MJUS  0x141202A10
    3 Z3U   0x141200CA0     7 STG2  0x141203FA0

Identified by the `CMD_NAME_<TAG>_*` family of the labels their records point
at: tables 2-7 are unambiguous (CFJ 251, Z3U 156, PWS1 40, PWS2 25, MJUS 398,
STG2 187). ⚠ Tables 0 and 1 both read "mostly CVS2" only because a fixed
400-record walk runs past each table's end into its neighbour — the record
COUNT per table is not yet derived, so do not trust those two tallies.

### ⭐ Record `+4` IS a GMD index — verified against the corpus

    +4=898  -> CMD_NAME_DASH        'Dash'
    +4=1286 -> CMD_NAME_CVS1_FF_03  'High Jump'
    +4=1288 -> CMD_NAME_CVS1_FF_05  'Throw'
    +4=1310 -> CMD_NAME_CVS2_00_00  'Shoulder Throw'
    +4=1311 -> CMD_NAME_CVS2_00_01  'Somersault Throw'
    +4=1312 -> CMD_NAME_CVS2_00_02  'Collarbone Breaker'

Coherent move names in sequence, which a wrong field would not produce.

⭐⭐ **This is language-safe by construction**: the record yields an INDEX and
`gmd_text_by_index()` reads whichever language table is live. No per-game and
no per-region name tables, so the JP/US question does not arise — and it covers
all 1658 names across 183 characters in all 9 game tags by the same path.

## ⭐ THE "SCROLL BASE" IS JUST THE LOOP COUNTER — traced, and simpler

`r9d` comes from `r12d` (`mov r9d, r12d` at `0x140080420`), and `r12d` was
zeroed at `0x1400803fe` (`xor r12d, r12d`). So it is **the draw loop's own
counter `i`**, starting at 0 — not a scroll offset.

⚠ I wrote "scroll base" in the first version of this file. It is the index of
the row being drawn within the visible window.

So the array index is `[screen+0x3d4] + i`, and `+0x3d4` is the **SELECTED
row**, confirmed by the live run: the cursor runs 0->6 on a 7-item list with
`min_index = 0`, so the helper's reported total (7) is the real list length.

⭐ **Consequence: for a list of 10 or fewer the selected row indexes
`screen+0x354` directly**, and the observed totals are almost all in range
(2,3,4,5,6,7,8,9 seen; only 12 and 14 exceed the 10-row cap). A reader can name
the focused row today and only needs care for the two long lists.

## ▶ WHAT IS STILL OPEN

1. **Records per table** — needed only to bound reads defensively, not to name
   a row.
2. **`+0` and `+8`** of each record are unidentified (`+0` is often 12415 /
   12288; `+8` varies wildly and may be packed input notation — worth checking
   against `CMD_TYPE_*`, which holds 28 button-suffix strings like "(LP)").
3. **How lists longer than 10 scroll** — whether `+0x3d4` stays the selection
   and a separate field holds the window top. Only affects the 12- and 14-item
   characters.

## ⭐ Why this route is better than the one it replaces

It reads the game's own indirection instead of inferring from what happens to
be on screen, so the 10-row cap, the scrolling and the per-character counts all
stop mattering. It is also the method with the record here: **disassembly is
4 for 4 on this project** (sPad, sKeyboard, the GMD table, the cursor
chokepoint) where memory-shape scanning went 0 for 4.

---

# THE NOTATION: what is established, and the one thing that is not

Investigated 2026-09-12 after the user reported the notation still unread.

## ⭐⭐ THERE ARE TWO COLUMNS, AND THE SHIPPED ONE IS THE WEAKER

    x=1565   a DIRECTION + move-type glyph only, prefixed "<RGHT>"
             -> a row reads "Shoryuken, back, special move": true, and useless
    x=633    the FULL INPUT, e.g. "<ICON C_236>+<ICON C_P>"

`x=1565` is what ships today. It is not the right column; it is the one that
demonstrably does not break the screen.

## ⭐ x=633 IS THE INPUT COLUMN, NOT A "CONDITION" COLUMN

⛔ An earlier note here called it condition text and excluded it as a decoy.
Then a later note called it "condition AND input". Both were partial. Measured
across every run: **153 plain-input objects against 23 condition-prefixed
ones**. The condition is an OPTIONAL PREFIX on the same column:

    "<SIZE 58><ICON C_236></SIZE>+<SIZE 58><ICON C_P></SIZE>"      (plain)
    " During Jump<ICON C_236>+<ICON C_P>"                           (prefixed)
    " At Close Range<ICON C_63214>+<ICON C_K>"
    " After Iai-Geri Hits<ICON C_28>+<ICON C_K>"

⭐ And it ALIGNS: every x=633 object ever captured shares a Y with a move-name
row. The pairing gt_column_item() already does would work.

## ⛔⛔ SO WHY DID SWITCHING TO x=633 SILENCE THE SCREEN? UNKNOWN.

The 00:06 run had 20 name objects, 0 at x=633, and spoke NOTHING for 21
move-list movements -- yet `gt_column_item()` falls through to name-only when
the pair column is empty, so the names alone should still have been read.

Four diagnoses were produced and all four were refuted by the log:
  1. instance-table eviction  -> 40 and 41 objects were live at the movements
  2. parent pointer mismatch  -> dump and cursor both read 2c40ca30
  3. the column swap itself   -> the fall-through path makes it harmless
  4. a missing g_screens row  -> both rows were present in that build

⚠ The cause could not be determined because the refusal path logged NOTHING.
That is fixed: it now prints the name count on both the success and failure
paths. ⭐ **The next run on an x=633 build will identify the cause in one go** --
`names==0` means the column query found nothing; `names>0` with an empty line
means every row was refused after translation.

## ▶ THE NEXT STEP, precisely

1. Switch the pair column back to 633.0f in menu_speak.c's MC_MOVE_LIST branch.
2. One run: open the move list, move a few times.
3. Read the `mcspeak: move list` lines. They now name the cause directly.

⚠ Do NOT theorise before reading those lines. Five consecutive diagnoses of this
screen were wrong, every one of them plausible, because an instrument's limit
kept being read as a fact about the game -- a 24-char log field, a 64-slot
table ceiling, a 6.6s sampling window.
