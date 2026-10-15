# Alpha 3's alternate copy: Additional Characters on

Written 2026-10-15, after player reports that Alpha 3 was silent for them in
v0.5.0, in both the Japanese and the US version, while it worked here.
Traced offline from the image dump.

✅ Confirmed by the user in the game, 2026-10-15, with Additional Characters
on: "This works." A general acceptance, not a pass of each item in the
listening check below.

## What selects the copy

- The game holds two separately compiled copies of Alpha 3. Session
  `+0x2B4` picks one when a game starts (`0x14004C5DF`, `0x140049DB1`):
  - zero runs ROMs 6 `z3u` / 7 `a3u`, the ordinary copy;
  - nonzero runs ROMs 16 `z3uu` / 17 `a3uu`, the alternate copy.
- `+0x2B4` is the **Additional Characters** row of Alpha 3's game settings
  (`EXOPT_Z3U_01`, Off by default).
  - The settings table at `0x1411E7CD8` lists Alpha 3's rows. The last three
    are Crouch Cancel, Additional Characters and Mode Select.
  - At `0x14006EF17`, the per-character resource loader skips `pl26` to
    `pl29` while `+0x2B4` is zero. Those are the four extra fighters below.
  - ⚠ The instruction that copies that one settings row into `+0x2B4` was not
    found. The settings are copied as a block (`0x140094CCC`, `0x14006970B`).
    The link to the row rests on the four skipped fighters.
- The shell's Japanese/US version setting only picks between the two ROMs of
  whichever copy runs. That is why testing both versions here never reached
  the alternate copy.

## How the copies were matched

- `tools/branch_map.py` aligns each ordinary function with its alternate
  counterpart instruction by instruction. It walks both call graphs from the
  four pairs the lifecycle trace already had, and pairs dispatch tables entry
  by entry. It mapped 5496 functions.
- `tools/branch_align.py ORD ALT --show` prints one aligned pair.
  `tools/branch_bytes.py` finds a small leaf routine by its bytes.
- Every address below is the instruction aligned with the ordinary one, read
  in the aligned listing. None is inferred from the numbers alone.

## Layout differences

- **Player objects:** R+0x1F8 and R+0x778, stride 0x580 (`0x140E0AB97`).
  Ordinary: R+0x1B0 and R+0x700, stride 0x550.
  - Every object field up to +0x50 is unchanged. From +0x51 on, every field
    moves 8 bytes later. A tally over all aligned functions shows this
    without exception for +0x51 to +0x80.
  - Health is at +0x90: round setup `0x140DCC132` and damage `0x140E11FEC`.
  - The super gauge is at +0x1F6: spend `0x140DEA599`.
  - Side is at +0x1D9, the hover ID at +0x1DA and the ism at +0x20A: screen X
    `0x140DD2960`, hover `0x140E0B161`, ism `0x140E0B59F`.
  - The random marker is at +0x61 (`0x140E0B13F`). The motion byte moves from
    +0x52 to +0x5A.
  - X (+0x10), Y (+0x14) and ground Y = 40 are unchanged: landing
    `0x140E5462B`.
- **Root block:** 0x1B528 bytes, published at slot `0x142C44050`.
  - These are unchanged: mode words +0x28/+0x2A/+0x2C, the round stage +0x2E,
    the speed +0x3C, and the side masks +0x59/+0x69.
  - The camera is at R+0xE4EA (`0x140DD2973`).
  - The clock is at R+0x129. `0x140E03ECF` reads it against 80, 40 and 60, as
    `0x1408A337F` reads +0xF1. ⚠ The round setup still writes +0xF0 to
    +0xF2 on this copy (`0x140DFE1E7`). The live check must confirm +0x129.
  - The text queue is at R+0xBC00 (print `0x140DD925A`), with the same 0x60
    records of 0x18 bytes.
  - The typewriter messages are at R+0xBAF8, with the same stride 0x58 and
    pointer +0x38.
- **Code:**
  - The update is `0x140DCC3F0`. Its only caller is wrapper `0x140DCB970`.
  - The attract walker is `0x140DD92B0`. Its entry bytes match the
    ordinary walker's.
  - The in-game drawer `0x140DDECB0` tests the same byte classes. It adds
    fonts 5 and 7, which the decoder refuses.
- **Select:**
  - Every dispatch entry the adapter checks maps one to one: mode 1, screen
    2, states 0/2/3 and their sub-state 1.
  - The speed table `0x1419E8668` and the random-ism table are identical.
  - The grid is `0x1419E83F0`, read through r12 (`0x140E0B076`). It is the
    ordinary grid plus four cells:
    - native IDs 40 and 41 at the ends of row 5;
    - 38 and 39 in place of two of the random cells in the bottom row.

## Names of the extra fighters

- The shell adds 1 to Alpha 3 IDs from 0x26 before naming them
  (`0x14008221C`), and its move-list roster lists 38 to 41.
- So the four are **Eagle (38), Maki (39), Yun (40) and Ingrid (41)**.
  `z3u_name_index()` applies the same +1.
- ⚠ An earlier reading without the +1 said Final M. Bison, Eagle, Maki and
  Yun. The listening check should confirm Ingrid.

## Story text on this copy

- Each message entry is `[0x1419E2D80] + line*0x80` (`0x140E09F9A`).
  - In the dump, that pointer names a static buffer at `0x142C61BA0`, which
    is still empty.
  - No code was found that fills it, so its layout is unseen.
- The same join and printable-ASCII decode are used. A different layout is
  refused and stays silent; it is never misread.
- ⚠ The listening check must confirm that pre-fight story text is spoken on
  this copy.

## Code

- `src/z3u_branch.h` describes both copies, and `z3u_branch_for_rom()` picks
  one by ROM.
- `src/z3u_frame.c` hooks both updates and both walkers.
- `src/z3u_text.c` and `src/z3u_select.c` read the copy's own offsets.
- The select adapter checks 17 alternate code signatures, 8 dispatch
  entries, the speed table, the style names and the grid. It checks them
  once per copy and disables only the copy that differs.
- A ROM that neither copy covers is logged once as silent, not dropped
  without a trace.

## Tests

- `run_z3u_frame_test.ps1` case 8 relocates every labelled snapshot into an
  alternate-shaped block.
  - It uses the traced numbers as literals.
  - The alternate decode must equal the ordinary decode, and the ordinary
    layout must misread the block.
- `run_z3u_select_test.ps1` case 6 covers:
  - IDs 38 to 41 named Eagle, Maki, Yun and Ingrid;
  - the ordinary limit refusing them, and 42 refused;
  - ROM mapping, and speech on ROM 17.
- `python tools\sabotage_z3u_branch.py` runs eight sabotages of
  `z3u_branch.h`. Each fails the suite that covers it.
- ⚠ These test the plumbing against the traced map. Only the game can test
  the map itself.

## Listening check

Turn on Alpha 3's Additional Characters setting, then start Arcade in either
version.

- The log has these lines:
  - `z3u-frame: hooked Alpha 3's alternate per-frame update`;
  - `z3u-select: admitted ROM 16 (z3uu, alternate copy)`, or 17 (a3uu).
- Character select speaks each highlight. Eagle, Maki, Yun and Ingrid sit at
  the edges of the bottom two rows.
- The ism and speed menus speak.
- In a match:
  - health and gauge tones;
  - beacons that pan with the fighters;
  - clock ticks at 30, 15 and 5 to 1;
  - silence on pause.
- The on-screen text and pre-fight story are spoken.
