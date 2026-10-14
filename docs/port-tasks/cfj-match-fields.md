# CFJ in-match fields for match audio

Evidence from the labelled snapshots and continuous trace in
`tools/fixtures/cfj-match-20261014` (ROM 5 `cfe`, Arcade, three rounds), and
from the code that uses each field. Fighter = R + 0x1E678 + side*0x660, where
R is the root published at `0x1422F3A80`. Implemented in `src/cfj_frame.c`.

## Confirmed in capture and code

- **Health, fighter+0x16C, signed 16-bit.**
  - Capture: 14800 at round start for Ryu, falling in steps; 13800 for Guile in round 2.
  - Code: damage `0x1407E6367` subtracts the hit (`sub word [r10+0x16C], ax`) and floors it at 0. `0x1407E640D` writes -1 at a KO, so negative values are a sentinel. Live, -1 appeared on the round 2 KO frame.
  - +0x16E is the bar's animated copy and is not used.
- **Maximum health, fighter+0x170.**
  - Capture: constant per fighter per round, and equal to health at round start.
  - Code: `0x1407D5961` computes health × 100 ÷ +0x170.
- **X, fighter+0x54, and height, fighter+0x58, signed 16.16.**
  - Code: `0x14081EEA1` adds velocity +0xA8 to X and +0xAC to height.
  - Capture: X is exactly -100.0 and +100.0 at round start, and -351.8 and +352.1 in the corners: CVS1 and CVS2's stage geometry. Height read 41.4 mid-jump and 0 in every other P1 snapshot.
- **Power gauge, fighter+0x294, signed 16-bit, capped by gauge type fighter+0x221.**
  - Code: gain `0x1407E6033` reads the type, bounds it with `cmp eax,5` and jumps through `0x1407E625C`. Each branch clamps at its own cap and marks levels:
    - Type 0: cap 115.
    - Type 1: cap 170, a level at 85.
    - Type 2: cap 200, a level at 100.
    - Type 3: cap 240, levels at 80 and 160.
    - Type 4: cap 180, a level at 90.
    - Type 5: cap 210, levels at 70 and 140.
  - Capture: P1 (Ryu, then Guile) is type 0. Their gauge climbed to 100 by the end of round 1, carried over, stopped at 115, and a super took it to 0. P2's Felicia is type 3 and reached exactly 240.
  - ⚠ An unknown type leaves the gauge silent; no cap is guessed.

## Confirmed in capture only

- **Match gate: phase chain R+0xC8 = 02 01 02 02.**
  - Every match sample in both captures holds it; character select reads 02 01 01 01. CVS1 and CVS2 use the same prefix.
  - ⚠ The code that dispatches on it was not located. The low root area is laid out as a game object, and a plain displacement search mostly finds other objects.
- **Round stage, R+0xCC.**
  - Three rounds: 3 intro, 4 fighting, 5 to 7 KO and round over, 8 transition, then 0 to 3 while the next round is set up.
  - Fighting starts exactly when R+0x206 drops to 0, which is a second witness.
  - Health and gauge are fed only in stages 4 to 7. Outside those, the fighter records are cleared, the next fighter comes in and the gauge blips, and none of that may sound.
- **Round clock, R+0x1D4, u8.** 99 at each round start, then down one step about every second. Round 1 ended on time, with the clock at 0 and stage 6. The decrement instruction was not located, the same gap as CVS1's clock.
- **Pause.** The whole block froze while paused: no byte changed from about 9082.5 s until the capture ended. The motion signature uses both fighters' X and height plus +0xCE, +0xD0, +0xD8 and +0x246, which change in 20 to 80% of samples during play. If the update stops while paused, the worker turns the beacons off after 250 ms without frames.

## Frame timing

- The hook is on update `0x1407CB8B0`. Its only caller is the per-frame callback `0x1407CAEE0` (call at `0x1407CAEEE`), which CFJ setup publishes.
- The entry bytes `48 83 EC 28 E8 67 57 00 00` are checked before hooking.
- `tools/collection_state_capture.py` lists the site in `OWN_HOOKS`.

## Derived, accepted by listening

- **Beacon pan** uses CVS2's camera approximation and screen half-width of 192, as CVS1 does. The world geometry matches exactly; the screen width is the assumption.
- **Airborne pitch** scales with height. Standing otherwise.

## Not decoded, so not sounded

- Crouch and knockdown. The capture has three crouch holds; they were not analysed in this increment. CVS1's ruled-out leads are a caution against reading a stance from one byte.

## Tests

- `.\tools\run_cfj_frame_test.ps1` replays 12 snapshots plus the round-setup, KO and select frames.
- Twelve sabotages each fail it: health on the display copy, the wrong maximum, a wrong type-0 or type-3 cap, no live gate, the intro counted as live, the KO sentinel accepted, a loosened phase gate, and wrong clock, height or X offsets.
- ⚠ Loosening the gauge-type bound also failed, but only because it reads past the cap table. That is undefined behaviour, not a reliable check.

## Listening check

✅ Confirmed by the user in the game, 2026-10-14: "Works well." This is a
general acceptance, not a pass of each item below.

Deployed 2026-10-14: `dinput8.dll` SHA-256 `6fb768a08eb5623658d9c4bdda7610a45c7b61a44d7d9f25f704df0eac88dc0d`.
The select-only build it replaced is kept as `build/deployment-backups/dinput8-9c5bfa9d....dll`.

- The health tone falls as each side takes damage, with no sweep when the next round or fighter starts.
- The beacons pan with each fighter's position and rise in pitch during a jump.
- The power gauge tone tracks each side. A Darkstalkers fighter's gauge fills over a longer range than a Street Fighter II fighter's.
- Clock ticks sound at 30, 15 and 5 to 1.
- The beacons go silent while paused.
- The log has `cfj-frame: hooked CFJ's per-frame update` and, every 10 s in a match, a `cfj-frame: N frames` line.
