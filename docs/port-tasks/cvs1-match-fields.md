# CVS1 in-match fields for match audio

Evidence from two independent sources for each field: the labelled snapshots
in `tools/fixtures/cvs1-match-20261013` (ROM 1 `cvs1u`, Training), and the
code that writes the field (image section SHA-256 `9c340b8c...`).
Actor = R + 0x3FB8 + side*0x700, where R is the root published at `0x1422D2B90`.

## Confirmed and used

- **Health, actor+0x1FC, signed 16-bit.**
  - Snapshots: 14400 at round start, 4320 after damage, refilled by Training.
  - Code: damage at `0x1405B817D` subtracts the hit's damage from it and floors it at 0 (`0x1405B81EE..0x1405B81F8`).
  - `0x1405B4E3B` writes -1 there, so negative values are a sentinel, not health.
  - actor+0x1FE holds a second copy, which is not used.
- **Maximum health, actor+0x200.**
  - Snapshots: 14400, constant, for both players.
  - Code: `0x1405B8224` resets health to three tenths of it, and the recovery path at `0x1405A9E82..0x1405A9ECA` reads it while adding to health.
- **X, actor+0xB4, and height, actor+0xB8, signed 16.16.**
  - Code: motion routine `0x1405EA130` adds velocity +0xA4 to X and +0xA8 to height, each velocity gaining its acceleration (+0xAC, +0xB0). This is CVS2's routine shifted by 0x60.
  - Snapshots: X is exactly -100.0 and +100.0 at round start, and about -352 and +352 in the corners, CVS2's stage geometry. Height read 67.8 mid-jump and 0 in every other snapshot.
- **Match gate: phase R+0x3E58 starting 02 01 02 02.**
  - All ten in-match snapshots match it; character select reads 02 01 01 06 06.
- **Frame timing: the hook is on update `0x1405A65F0`.** Wrapper `0x1405A5C60` calls it once per frame, and setup publishes its root.

## Derived, confirmed by listening 2026-10-13 (pan, airborne pitch, pause)

- **Beacon pan.** CVS2's camera approximation, which tracks the midpoint clamped at the stage edges, applied with CVS2's screen half-width of 192. The world geometry matches CVS2 exactly; the screen width is the assumption a listening check confirms.
- **Airborne pitch** scales with height. Standing otherwise.
- **Pause.** The signature combines both fighters' X, height and animation counters +0x162, +0x164 and +0x168, which change in about half of all 20 Hz trace samples. The trace ended before the pause snapshot, so pause detection is unverified. If the update stops running while paused, the worker turns the beacons off after 250 ms without frames.

## Not decoded, so not sounded

- **Power gauge.** No field cleanly separated snapshot 6 (full) from 7 (spent). It needs code tracing or a capture outside Training.
- **Crouch and knockdown.** actor+0x170 read 0, 1, 20 and 22 for stand, crouch, jump and down, one sample each. Very few instructions touch it and none writes those constants, so it is not used. The beacon stays at standing pitch.
- **Round clock.** It doesn't count down in Training. It needs an Arcade capture.
