# CFJ character select: evidence for `src/cfj_select.c`

Written 2026-10-14. Built from one live capture and confirmed by the user in
the game the same day: "Character select works."

## The capture

- `tools/fixtures/cfj-capture-20261014/`, taken with
  `tools/collection_state_capture.py --game cfj` on ROM 5 `cfe`, Arcade mode,
  side 0 human.
- ⚠ The capture's keyboard log caught only five arrow presses during select.
  The player's real moves are in the game's own input word at R+0x26 (copy at
  R+0x76): bits 1, 2, 4, 8 are directions and R+0x27 bit 2 is the pick button.
  Use that word for timing, not the tool's press log.
- ⚠ The first F9 snapshot was taken before the first pick, so the plan labels
  are off by one. The trace carries the evidence; the labels do not.
- Seven moves and two picks: Ryu, then Alex. The CPU drew Urien and Leo.

## Fields, all relative to the root R = `*0x1422F3A80`

- R+0x21960 is the phase. The dispatcher `0x140809D2D` indexes the function
  table `0x1412BBBB0` with it. Entry 2 is `0x14080C140`, the select screen.
- R+0x21961 is the select sub-state. `0x14080C184` bounds it at 6 and jumps
  through a table. Entry 0 calls `0x14080BB40`, the picking routine. Live:
  0 while picking, then 1 to 3 after both picks, and back to 0 on leaving.
- R+0x2197C is the human-side mask, bit (1 << side). The commit at
  `0x14080BE61` tests it against each fighter's side bit. Live: 1 during a
  one-player select.
- Per-side select records at R+0x26E00, stride 0x10:
  - +0x06 is the active slot: 0 for the first pick, 1 for the second.
  - +0x08 is a word holding the number of picks made. The cursor driver
    `0x140808E70` stops at 2.
  - +0x0E and +0x0F hold the select number for each slot. The hover writer
    `0x14080AABE` stores it at record+0x0E+slot as the cursor moves. 0xFF
    means nothing yet.
- Cursor x and y are at R+0x26E02+2*side and R+0x26E03+2*side. The grid is 6
  by 5, looked up through `0x141084B70`, row-major.

## Select number to native ID

- The commit at `0x14080BE8B` reads byte table `0x1412BB8F0` with the select
  number and stores the result at the fighter's +0x1EC18 (and +0x1EC19 for
  the second pick). That is the move-list ID space the name table uses.
- The table: 0, 2, 3, 4, 10, 11, 16, 17, 18, 19, 20, 21, 22, 31, 24, 25, 26, 27, 28, 29, 44, 45, 34.
- ⭐ Checked live against the game's own commit: the player's picks 0 and 16
  became 0 Ryu and 26 Alex, and the CPU's 17 and 18 became 27 Urien and
  28 Leo.
- The grid then reads as series clusters, a second, weaker check:
  - Street Fighter II, top left: Ryu, Guile, Zangief, M.Bison.
  - Street Fighter III, top centre: Alex and Chun-Li above Yun and Urien.
  - Darkstalkers, right: Demitri, Felicia, Anakaris, Jedah.
  - Alpha, lower left: Guy, Karin, Sakura, Rose.
  - Red Earth, lower right: Hauzer, Hydron, Leo, Kenji.
  - Ingrid fills the two centre cells, and Shin Akuma and Pyron sit in the
    two side cells of row 4.

## What the adapter does

- Admits game 2 with ROM 4 or 5 only. The published root and size 0x26E80
  must match `*0x1422F3A80`, and the shell's game must be 2.
- Speaks a side only when the phase is 2, the sub-state is 0, the side is
  human, and fewer than two picks are made.
- Names come from `character_name(2, id, version)`, per shell game version.
- The visit is the slot, so the second pick's first highlight is spoken even
  when it matches the last one heard.
- Checks eleven code signatures once, plus the 23 table bytes and phase table
  entry 2, before trusting anything.

## Tests

- `.\tools\run_cfj_select_test.ps1` replays the 19 distinct decoder inputs
  from the capture.
- ⭐ The expected sequence is Ryu, Demitri, Ryu, then Guile, Demitri, Guile,
  Sakura, Rose, Alex. The last highlight of each slot must equal the game's
  committed IDs.
- Sabotages that fail it: swapping two table entries, removing the human
  gate, the sub-state gate or the done gate, and ignoring the slot.
- ⚠ Loosening the select-number range check is NOT caught: it makes the
  decoder read past its own table, which is undefined behaviour no assertion
  can pin.

## Listening check

Deployed 2026-10-14: `dinput8.dll` SHA-256 `9c5bfa9d80e119d159617a387cdbd17ceb5e4ed88bf2023e37d6fe121755e016`.
The previous DLL is kept as `build/deployment-backups/dinput8-cc86ed9a....dll`.

- On CFJ character select, each cursor move speaks the highlighted name.
- After the first pick, the second cursor's starting character is spoken.
- Nothing is spoken for the CPU side, and nothing after both picks.
- The log has `cfj-select: admitted ROM 5 (cfe), shell game version N` and
  one `cfj-select: accepted` line per spoken name.
- If the log has `cfj-select: ⛔`, the code or data check refused; nothing is
  spoken by design.

## Not covered yet

- A second human player (versus). Side 1 reads its own record by the same
  code path, but no capture has shown it.
- The alternate start-cell table `0x141084B90`, used when the second pick's
  cursor is placed. It only chooses where that cursor starts; the hover writer
  still stores a select number.
- An online path at context+0x840 = 2 can override the highlighted cell. It is
  not traced, and the range check refuses anything outside 0 to 22.
