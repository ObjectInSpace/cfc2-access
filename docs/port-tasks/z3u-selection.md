# Alpha 3 character, ism and speed select: evidence for `src/z3u_select.c`

Written 2026-10-14. Built from one live capture and the code that writes each
field. Not yet heard in the game.

## Which branch

- The live session ran ROM 7 `a3u` (US) with session+0x2B4 = 0, the ordinary
  branch: root slot `0x1422FB960`, block 0x12F98, callbacks as in the
  lifecycle manifest.
- The user confirmed the shell's game-version setting picks US (`a3u`) or
  Japanese (`z3u`). Both are on this branch, so one adapter covers both.
- The alternate branch (`z3uu`/`a3uu`, root `0x142C44050`) has its own code and
  layout. 2026-10-15: it is the Additional Characters setting, and the
  adapter now reads it too; see [z3u-alternate.md](z3u-alternate.md).

## The capture

- `tools/fixtures/z3u-capture-20261014/`, taken with
  `tools/collection_state_capture.py --game z3u`, Arcade mode, side 0 human.
- ⚠ The keyboard log caught no moves before the first pick, so the player was on a
  controller. Timing comes from the game's own input word: root+0xF748 for
  side 0 (+0xF790 for side 1). Its bits are 1 right, 2 left, 4 down, 8 up, and
  0x3F0 the buttons.
- Path: 18 highlights ending on Birdie, then the ism menu Z → V → Z → X
  (picked), then the speed menu Turbo → Normal → Turbo (picked).
- ⚠ No screenshots, so the ism and speed values are confirmed from the code.
  Only the user has seen the screen.

## Fields, relative to the root R = `*0x1422FB960`

- **Screen chain.** The game task block is R+0x28: setup `0x140895C68`
  (`lea rbx,[rax+0x28]`).
  - It dispatches on the word at R+0x2A through table `0x1412D7A48`
    (`0x140895CF1`).
  - Entry 1 jumps to `0x1408A7F50`, which dispatches on the word at R+0x2C
    through table `0x1412DBC60`.
  - Entry 2 there is the select screen, `0x1408AA700`.
  - Live, these three words (+0x28, +0x2A, +0x2C) read 1, 1, 2 on all three menus, 1, 1, 3 while leaving, and 1, 2, 1 in the match.
- **Player objects** are at R+0x1B0 and R+0x700, with stride 0x550.
  - `0x1408955EC` and `0x1408A9D07` load these two addresses, along with
    R+0xC50.
  - Object+0x1D1 is the side.
- **Human gate.** `0x1408A9D33` runs a side's select state only if bit
  (object+0x1D1) is set in R+0x59 | R+0x69. Live, R+0x59 = 1 during select
  and R+0x69 = 1 once the player is through.
- **State** object+4 dispatches through `0x1412DBCE0`. Each state's object+5
  sub-state 1 is its choosing routine:
  - state 0 is the character (`0x1408AA120`);
  - state 2 is the ism (`0x1408AA650`);
  - state 3 is the speed (`0x1408AAAE0`);
  - state 4 commits.
  - Live, P1's state ran 0, 2, 3, 4 and then 1 in the match.

### Character, state 0

- The grid is 7 by 7 at `0x1412DBB90`, indexed y*7+x with x = object+0x12 and
  y = object+0x16. Cursor movement wraps at 0 and 6, as the code shows.
- Hover writer `0x1408AA2FD`: object+0x1D2 = the cell's value, a native ID.
- Cells with bit 7 set are empty and skipped. Cells with bit 6 set are the four
  random cells: `0x1408AA2DB` sets object+0x59 = 0x26 + (cell & 15), and
  `0x1408A9470` rolls a new ID from the pool `0x1412DBD20` on every frame. The
  adapter says "Random" while object+0x59 is nonzero, never the rolling name.
- Every grid ID and every pool ID is 0 to 37. In this range the native ID is
  the shell's name-table index directly. The shell's +1 remap for IDs from 0x26
  never applies, and the adapter refuses anything above 37.
- The grid, with US names:
  - row 0: random, M.Bison, random
  - row 1: Guile, Charlie, Chun-Li, Sagat, Fei-Long
  - row 2: Dee Jay, Dan, Guy, R.Mika, Birdie, Akuma, E.Ryu
  - row 3: Ryu, E.Honda, Cody, Karin, Vega, Blanka, Ken
  - row 4: Juli, Dhalsim, Sodom, Cammy, Adon, Zangief, Juni
  - row 5: T.Hawk, Rolento, Sakura, Gen, Balrog
  - row 6: random, Rose, random
- ⭐ Checked against the trace: every cursor position the game recorded maps
  through this grid to the ID it stored at object+0x1D2.

### Ism, state 2

- `0x1408AA650` steps object+0x202 within -1..1, using up (bit 8) and down
  (bit 4) from the input word. The value is stored at `0x1408AA6EF`.
- ⭐ Its meaning comes from `0x140891B35`. When the fighter is Chun-Li (4) or
  Sodom (6) and object+0x202 is negative, the match swaps in their X-ism
  variants (IDs 0x12 and 0x14). So -1 is X-ism.
- The shell's style-name function `0x14004AE30` names game 3's styles
  0 X-ISM, 1 Z-ISM, 2 V-ISM, from table `0x1411F7C28`. That is object+0x202 + 1.
- ⚠ The US release names the middle style A-ISM. The adapter says "A-ism" for the
  US game version and "Z-ism" for the Japanese one. The shell text has only
  Z-ISM, so the US wording is the release's own, not something read from
  memory.
- The character state resets object+0x202 on each move, so the ism is read
  only in state 2.

### Speed, state 3

- `0x1408AAAE0` toggles bit 0 of R+0x3C on up or down (`0x1408AAB3E`). There is
  one value for the whole game, not one per side.
- The commit at `0x1408AAB5B` reads the speed table `0x1412DBDC8` at
  R+0xB6 + value - 1. In this capture R+0xB6 = 2, so value 0 gives speed 6 and
  value 1 gives speed 8. The match ran at R+0xFE = 8.
- ⚠ The labels Normal (0) and Turbo (1) are inferred from 8 being the faster
  setting. The user should confirm the menu's words and that it opens on Turbo.

## What the adapter does

- Admits game 3 with ROM 6 or 7 only. The published root and size 0x12F98 must
  match `*0x1422FB960`, and the shell's game must be 3.
- Speaks a side only on the select screen, when the side is human and its
  object is in a choosing sub-state. Kinds: 0 character, 1 ism, 2 speed. A new
  kind is a new utterance even when its value equals the last one.
- Checks 19 code signatures, 8 dispatch-table entries, the speed table and the
  three style-name strings once, before trusting anything.

## Tests

- `.\tools\run_z3u_select_test.ps1` replays the capture's 102 distinct decoder inputs (`tools/make_z3u_select_fixture.py`).
- The expected path is the 18 highlights, the four ism steps and the three
  speed steps above. Birdie with X-ism must be what the match then runs.
- Ten sabotages each fail it:
  - removing the human, owner, sub-state or screen gate;
  - ignoring the random marker;
  - shifting the ism value;
  - using the Japanese ism name for the US version;
  - swapping the speed labels;
  - giving the speed menu the ism's kind;
  - loosening the ID range check.
- ⚠ The swapped-speed-labels sabotage is caught only against the inferred labels.

## Not covered yet

- A second human (versus). Side 1 reads its own object by the same code path,
  but no capture has shown it.
- The four random cells' own meaning: (cell & 2) feeds the random ism table
  `0x1412DBD50`. They are all spoken as "Random".
- The online override at context+0x840 = 2 (`0x1408AA324`), which can set
  object+0x202 from the network. Not traced.
- Match audio, in-game text and the alternate branch.

## Listening check

Deployed 2026-10-14: `dinput8.dll` SHA-256 `bd75c83ad4e5438ad5f7a2785d885f802f2feef371ab25ad047ba530c64640a5`.
The previous DLL is kept as `build/deployment-backups/dinput8-ccc54d30b694.dll`.

- On Alpha 3 character select, each cursor move speaks the highlighted name.
  The four corner cells say "Random".
- After the pick, the ism menu speaks its highlighted style. It is "A-ism" on
  the US version and "Z-ism" on the Japanese one.
- Then the speed menu speaks "Turbo" or "Normal". Confirm that these words
  match what the menu shows.
- Nothing is spoken for the CPU side, and nothing in the match.
- The log has `z3u-select: admitted ROM 7 (a3u), shell game version 1` and one
  `z3u-select: accepted` line per spoken choice.
- If the log has `z3u-select: ⛔`, the code or data check refused; nothing is
  spoken by design.
