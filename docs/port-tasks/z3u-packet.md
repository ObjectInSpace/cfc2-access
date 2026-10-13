# Z3U port packet: Street Fighter Alpha 3 Upper

Audited 2026-10-13 against `9ecc31c`. Status: BOUNDED DISCOVERY NEEDED for
every game-specific feature, on TWO runtime branches. No Z3U runtime code or
capture exists.

## Identity and traced acquisition

- Game 3, four ROM identities on two branches, chosen by session `+0x2B4`:
  - zero: 6 `z3u`, 7 `a3u`. Setup `0x14086CFC0`, root slot `0x1422FB960`,
    primary block 0x12F98, update wrapper calling `0x14086D160`.
  - nonzero: 16 `z3uu`, 17 `a3uu`. Initializer `0x140DCB360`, setup
    `0x140DCC280`, root slot `0x142C44050`, primary block 0x1B528, update
    `0x140DCC3F0` via wrapper `0x140DCB970`.
  - The alternate branch places its context at A+0x08B00000 (as STG2 does),
    not A+0x08000000.
- The visible option that sets `+0x2B4` is unresolved. Original CPS-2 or NAOMI
  addresses are not evidence for either branch.
- Nothing below the roots is traced.

## Feature mapping to the shared interfaces

- F01, F13, F19: shared. Gates must name the branch as well as the ROM.
- F02 selection: `selection_channel`, per branch. Names per ROM.
- F03 Ism choice: `selection_channel` kind for the style. Ism labels must come
  from what the game draws.
- F04: no assumed ratio equivalent. Audit extra mode/level choices.
- F05, F11, F14 to F17: per-variant screen audit.
- F06: `transition_channel`. Alpha 3 has no teams in normal play, so a side has
  one member, with the Ism as its style if it is shown.
- F07 to F10: `match_audio`. F08 must reflect Ism-dependent meter behavior,
  and other visible HUD gauges need an audit. Projection and stance are traced
  per branch.
- F12: per branch.
- F18: one `tc_register` source per title, selecting the branch internally.

## First bounded task

Resolve the `+0x2B4` writer and its visible option. Then follow both setups
into phase dispatch and player records. A working ordinary branch does not
establish the alternate one; each needs its own capture.
