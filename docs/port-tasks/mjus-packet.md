# MJUS port packet: Project Justice

Audited 2026-10-13 against `9ecc31c`. Status: BOUNDED DISCOVERY NEEDED for
every game-specific feature. No MJUS runtime code or capture exists.

## Identity and traced acquisition

- Game 6. ROM indices 12 `mjus` and 13 `pjus`.
- Root slot `0x142996830`, primary block 0x9E8A8, setup `0x140B930D0`, update
  wrapper `0x140B92930` calling `0x140B93500`. Context slot `0x142C79B40`.
- ⚠ The context `+0x10` assignment uses a multiply/shift/remainder sequence
  where CVS2 uses `and 0x3F`. Reusing CVS2's formula is unjustified.
- Nothing below the root is traced.

## Feature mapping to the shared interfaces

- F01, F13, F19: shared. The adapter adds its own gates.
- F02: `selection_channel`. Three-member teams. Distinguish persistent player
  slots from assist actors.
- F03, F04: CVS2 grooves and ratios do not transfer. Audit native team-related
  choices and level controls.
- F06: `transition_channel` with three members and the active actor. Assists
  are not active-fighter changes unless the game presents them that way.
- F07 to F10: `match_audio`. Visible power/resource rules are needed for F08.
  F09 and F10 need the 3D camera's projection and actor states, not CVS2's
  fixed stage width.
- F05, F11: conditional on the actual stage and clock UI.
- F14 to F18: story progression and any visible choices, plus quotes,
  endings, credits and prompts the screen audit finds.
- F12: phase and motion-signature fields.

## First bounded task

Follow setup and update into phase dispatch and the team/actor records. Then
take one labelled capture session (selection, team order, in-match), using a
read-only tool with MJUS's own gates.
