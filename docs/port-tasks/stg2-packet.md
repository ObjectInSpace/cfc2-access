# STG2 port packet: Plasma Sword

Audited 2026-10-13 against `9ecc31c`. Status: BOUNDED DISCOVERY NEEDED for
every game-specific feature. No STG2 runtime code or capture exists.

## Identity and traced acquisition

- Game 7. ROM indices 14 `stg2` and 15 `pswd`.
- Root slot `0x142BE6370`, primary block 0x511B8, setup `0x140CCC940`, update
  wrapper `0x140CCB440` calling `0x140CCCB60`. Context slot `0x142C79B40`.
- ⚠ The context sits at A+0x08B00000, with context `+8` = A+0x08F00000 and
  `+0x18` = A+0x08D00000, unlike CVS2. This placement is a good check that no
  CVS2 constant leaked into shared code.
- Nothing below the root is traced.

## Feature mapping to the shared interfaces

- F01, F13, F19: shared. The adapter adds its own gates.
- F02: `selection_channel` for the character choice. No team, ratio or groove
  screen is presumed. Audit any extra native choices rather than naming them.
- F06: `transition_channel` with one member per side.
- F07 to F10: `match_audio`. F08 is the visible plasma resource and special
  states. F09 and F10 need the actual camera projection and movement/stance
  semantics.
- F05, F11: depend on the actual stage and clock screens.
- F14 to F18: the cutscene/story path, plus each discovered quote, ending,
  credit and prompt source.
- F12: phase and motion-signature fields.

## First bounded task

Follow setup and update into phase dispatch and player records. Then take one
labelled capture session using a read-only tool with STG2's own gates.
