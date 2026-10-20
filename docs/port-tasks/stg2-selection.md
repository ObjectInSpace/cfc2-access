# Plasma Sword character select: evidence for `src/stg2_select.c`

Written 2026-10-20 from one live capture plus the reviewed image dump. Built,
offline-tested and deployed; not yet heard in the game.

## The capture

- `tools/fixtures/stg2-arcade-20261020/`, taken with
  `tools/collection_state_capture.py --game stg2` on ROM 15 `pswd`, Arcade,
  side 0 human. The raw trace (37 MB) stays out of git; the snapshots,
  session log and derived fixtures are kept.
- ⚠ The plan labels do not line up with the screens. The game re-initialised
  at about 342 s (new root), so snapshot 1 is an earlier session's fight.
  Character select ran from 349.05 s to 362.24 s; snapshots 2 and 3 are on it
  and 4 is the screen after it. The game's own state names the screens; the
  labels do not.
- ⚠ The keyboard log caught four presses at 348.2 to 348.6 s, before select
  opened. The cursor moves are read from the game's own select number.
- The player's path: Hayato, left to Kaede, right to Hayato, right to June,
  down to Ele, picked. The CPU drew native 5 on the second row (Gantetsu).

## Screen gates, all relative to the root R = `*0x142BE6370`

- R+0 is the top phase. The update `0x140CCCB60` dispatches 1, 2 or 3 on it
  at `0x140CCCB92`. 3 is in game.
- Phase 3 (`0x140CDE150`) sets W = R+0x4E5A0 and dispatches on the mode word
  W+0x4C = R+0x4E5EC through jump table `0x140CDE384` (mode - 1). Mode 1 tail
  calls `0x140D1DA20`, character select. Live modes: 1 select, 2 the screen
  after it, 3 round intro, 4 fighting, 11 round over, 6 and 7 continue.
- Select's sub-state is W+0x54 = R+0x4E5F4: 1 is picking (`0x140D1E2E0`),
  3 is the commit (`0x140D1DB30`), 2 is a stage menu (not covered).
- R+0x4E470+side is each side's state: 0 choosing, 1 chosen, 3 done. The side
  driver `0x140D1EA80` runs the cursor `0x140D1E5A0` only at 0.
- R+0x4E4B9 is the human mask, bit (1 << side), tested at `0x140D1E41A`.
  Live: 1.

## The cursor and the shuffle

- R+0x4E472+side is the select number. The cursor writes it from grid tables
  (left `0x141983C17`, `0x141983C47`, `0x141983C5F`, `0x141983C77`; right
  `0x141983C2F`, `0x141983C8F`, `0x141983CA7`, `0x141983CBF`, chosen by the
  unlock flags R+0x8E and R+0x8F, both 1 live) and by plus or minus 10
  between the rows.
- The grid: top row 1 to 10, bottom row 11 to 20 plus 21 and 22, and with
  both unlocks 23 and 24 above 21 and 22. Select number 0 is never written.
- Holding left or right for 60 frames sets R + side*0xA40 + 0xAB4, and the
  cursor then jumps to a random cell every fourth frame (`0x140D1E680`). The
  adapter reports the side as unavailable until it stops, so the shuffle is
  not read out cell by cell and a stop on the same character is not repeated.

## Select number to name

- At the pick, `0x140D1EA15` reads the native ID table `0x141983BD7` and the
  row flag table `0x141983BFF` with the select number, and writes
  ID | 0x10 (when the flag is set) to `*0x141C14F98 + 0x578 + 4*side`.
- The pause move list reads that same slot (`0x14007FFCB`) and names it with
  `0x14004B2F0`, so the ID is in the shell's name space. The adapter speaks
  `character_name(7, id, 0)`.
- The two rows reuse native IDs 1 to 12; the row flag tells them apart.
  ⭐ The 24 cells give 24 distinct IDs, exactly the shell's STG2 move-list
  roster (1 to 12 and 17 to 28).
- The cells, in select-number order:
  - Top row: Hayato, June, Saturn, Gamof, Gerelt, Vector, Zelkin, Gore,
    Blood, Ghost Bilstein.
  - Bottom row: Black Hayato, Ele, Prince, Gantetsu, Claire, Omega, Eagle,
    Luca, Shaker, Bilstein, then Rain (21) and Byakko (22).
  - Unlocked: Kaede (23) and Rai-on (24).
- ⭐ Checked against the game's own commit: the last highlight was select 12,
  and the game committed native 2 at R+0x4DF30 with row flag 1 at R+0x4E476,
  i.e. 18, Ele.

## What the adapter does

- Admits game 7 with ROM 14 or 15 only. The published root and size 0x511B8
  must match `*0x142BE6370`, and the shell's game must be 7.
- Speaks a side only in phase 3, mode 1, sub-state 1, when the side is human
  and still choosing.
- Only while the frame hook's counter advances (`frame_liveness.h`), so a
  quit game's frozen block is silent.
- Checks twelve code signatures and both tables once before trusting anything.

## Tests

- `.\tools\run_stg2_select_test.ps1` replays the 20 distinct decoder inputs.
- `python tools\sabotage_stg2.py` (select half): swapped table entries, the
  row flag ignored, and each gate removed or loosened all fail it, as does a
  wrong select-number offset or speaking the shuffle.
- ⚠ Dropping the select number 0 check is not caught. It is redundant: number
  0 maps to native 0, whose shell name is empty, and the name check refuses it.

## Listening check

Deployed 2026-10-20: `dinput8.dll` SHA-256 `7bbcb0520e8d9f7ef3e4a3e6dfc963e371d275bcfbab7b8dc810ab0949e764d8`.
The build before Plasma Sword is kept as `build/deployment-backups/dinput8-5d172caf.dll`.

- On Arcade character select, each cursor move speaks the highlighted name.
- Holding left or right shuffles silently, then speaks where it stops.
- Nothing is spoken for the CPU side, and nothing after the pick.
- The log has `stg2-select: admitted ROM 15 (pswd)` and one
  `stg2-select: accepted` line per spoken name.
- If the log has `stg2-select: ⛔`, a code or data check refused; nothing is
  spoken by design.

## Not covered yet

- A second human player (Versus). Side 1 reads its own fields by the same
  code; no capture has shown it.
- The stage menu, sub-state 2: R+0x4E47C steps 0 to 7 with left and right.
  No capture and no names.
- Training mode: not captured.
