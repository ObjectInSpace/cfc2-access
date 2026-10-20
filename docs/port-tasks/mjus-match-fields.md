# Project Justice in-match fields for match audio

Evidence from the continuous trace of `build/captures/mjus-4` (ROM 13 pjus,
Story mode, two fights) and the code that uses each field. Fighter = R +
0x3B48 + side*0x608, where R is the root published at `0x142996830`
(0x140B93055: `imul 0x608`, then `lea rcx,[rbx+0x3B48]` passes the object).
Implemented in `src/mjus_frame.c`.

## Confirmed in capture and code

- **Health, fighter+0x2DE (R+0x3E26 for side 1), signed 16-bit, maximum 200.**
  - Code: 0x140B9304A loads 0xC8 and 0x140B9306C stores it as the refill.
  - Capture: 200 at each fight's start, falling in steps to 0. -6 on the KO
    frame (phase 3): a sentinel, never a value. R+0x2250/0x2252 is the bar's
    copy, not used.
- **Guts gauge, fighter+0x3CC progress and +0x3CD level.** Five levels, as
  the game calls it, so value = level × 256 + progress, maximum 1280.
  - Code: the level is compared with 1, 2 and 5 (0x140B97897..0x140B978FE),
    capped at 5 (0x140C41B10, 0x140BA6BAA), and set to 3 at setup
    (0x140B935F6 for side 1, R+0x451D for side 2).
  - Capture: 768 (three levels) at each fight's start; side 2 reached 1280.
- **Facing, fighter+0x2FF bit 0.** Set means the fighter is on the right and
  faces left.
  - Code: the input handler at 0x140B98A1A swaps left and right (mask 0xC00)
    when it is set. 0x140B97100 writes it from the camera angle against the
    fighter's angle (+0x320 - +0xE4).
  - Capture: side 1 starts at 0, side 2 at 1; they swap together at each
    cross-over (seven in the capture). For a few frames during a cross-over
    the writer has updated one side and not yet the other.
- **Position, fighter+0xEC, +0xF0, +0xF4: X, height and Z, floats, relative
  to the fighters' midpoint.**
  - Code: 0x140BBA7F7 subtracts the two fighters' vectors.
  - Capture: X and Z are always exact mirrors between the sides. Height rose
    to 174 to 198 on every jump and is 0 otherwise.
- **Game phase R+0x21C0 = 2** while fighting (dispatcher 0x140B9E1F2). Health
  refills and the next round is set up in phase 1, so nothing sweeps.

## Beacon pan: the game's own camera (2026-10-19)

The first build estimated the pan from the facing flag and the fighters'
separation. The user asked for the game's own values, so it now projects.

- The shared renderer composes view × projection × viewport (0x140F33830)
  from the context object `*0x142C79B40`: view at +0x1F812C, projection at
  +0x1F816C, both row-vector 4×4. `tools/context_sampler.py` recorded them at
  20 Hz beside a capture of five fights (mjus-5, mjus-5-ctx): the view
  changes every frame and is a rotation plus translation; the projection is
  a constant perspective ([2][3] = -1); the viewport at 0x141C00210 maps
  -1..+1 to 0..640.
- Each fighter's world position, fighter+0xD0 (three floats), projected
  through them: the pan is clip.x / clip.w. The projection now lives in
  `src/render_project.h`, shared with Plasma Sword (stg2-match-fields.md).
- Check against the facing flag, which the game uses to swap the controls:
  both fighters land on screen in 914 of 918 samples, and their order
  agrees with the flag in 892 of 907. The disagreements cluster in the
  0.3 s after each cross-over while the flag catches up; 13 (1.4%) fall
  later, probably the up to 30 ms pairing of two separately sampled files
  during fast camera swings.
- No camera, or a matrix that is not a perspective camera, or a point
  behind it: no beacon. The pan is never estimated.
- **Jump pitch** scales with height, fighter+0xF0, up to 200 (the
  captured jumps peaked at 174 to 198).

## Not sounded

- The round clock, by the user's request: the game already sounds it.
- Crouch and knockdown: not decoded.
- Pause: the update's frame count is the signal, as for CFJ; the beacons
  turn off after 250 ms without frames. Not yet seen in a capture.

## Tests

- `.\tools\run_mjus_frame_test.ps1` replays 2315 frames (2222 fighting).
- `python tools\sabotage_mjus.py`: an inverted facing, a wrong maximum or
  health offset, a removed phase gate, a wrong guts cap, an accepted KO
  sentinel and a removed jump pitch each fail it.

## Listening check

Deployed with the select build (see mjus-selection.md).

- Health tones fall as each side takes damage, with no sweep at a new round.
- The guts tone climbs through five levels.
- The beacons pan by side and distance, swap on a cross-over, rise on a jump.
- The beacons go silent while paused.
- The log has `mjus-frame: hooked Project Justice's per-frame update` and,
  every 10 s, a `mjus-frame: N frames` line.
