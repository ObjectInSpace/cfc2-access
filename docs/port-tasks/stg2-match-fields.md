# Plasma Sword in-match fields for match audio

Evidence from the trace and snapshots in `tools/fixtures/stg2-arcade-20261020`
(ROM 15 `pswd`, Arcade, two rounds, one pause) and from the code that uses
each field. R is the root published at `0x142BE6370`; each side's record is
R + side*0xA40. Implemented in `src/stg2_frame.c`. Built, offline-tested and
deployed; not yet heard in the game.

## Match gate

- R+0 = 3 and the mode word R+0x4E5EC (see stg2-selection.md) in 3, 4 or 11.
- Mode 3 is the round intro. Health refills from 80 (round 1) or 20 (round 2)
  to 200 there (`0x140CD5DD8`, `0x140CD5E34`), so no gauge is fed in mode 3.
- Mode 4 is fighting. Health, the plasma gauge and the beacons are fed.
- Mode 11 is round over. Health and the plasma gauge are still fed; the
  beacons are not, because the KO replay cuts the camera around (fighters
  read up to 3.6 times the screen width away from its axis).
- Modes 6 and 7 (continue) are not a match.

## Health

- **side+0x57C, signed 16-bit.** Damage `0x140D19DA9` subtracts the hit
  (`sub word [..+0x57C], bx`). +0x57E is a display copy.
- **Maximum 200, a code constant.** The intro fills to 0xC8
  (`0x140CD5E6E`) and the training refill caps at 0xC8 (`0x140D16C89`).
- Damage is floored at 0 only when context +0x828 is set (`0x140D19DDE`;
  that flag is not traced, and was 0 here), so the KO frame read below 0 by
  the overkill: -39 and -7 live. The adapter treats that as 0, and accepts
  down to -200.
- Live, P1: 200, 187, 165, 146, 76, 54, 35, 23, 8, KO; then 200, 114, ...,
  7, KO. The player lost both rounds.

## Plasma gauge

- **R+0x4DFF0 + 2*side fill, R+0x4DFF8 + side level.** The gain routine
  `0x140CD9750` adds to the fill, and every 0x90 (144) becomes a level
  (`0x140CD97E5`). At the cap it stores fill 0x90.
- **The cap comes from table `0x141954058`, indexed by native ID** (the gain's
  fighter object is R + side*0xA40 + 0xB8, so its +0x370 is R+0x428; its
  side index at R+0x4DC reads 0 and 1 live). Every entry is 3.
- The tone is level × 144 + fill, out of 432. A level-up never sounds as a
  drop; spending a level does drop it.
- Live: P1 spent a level at 373.68 s and ended with all three; P2 climbed to
  3 and spent down to 0.
- R+0x4DFF4/6 and R+0x4DFFA/B are animated display copies, not used.

## Round clock

- **R+0x4DF33, u8.** Set at the intro from the timer option R+0x79
  (`0x140CD5CAB`); 100 there means no timer, which sets R+0x4E598 and the
  clock to 90. Decremented every 60 frames (`0x140CD7301`) unless R+0x4E598
  is set.
- Live: 90 at the intro, already 89 when fighting starts, one step a second.
- The adapter leaves the clock silent while R+0x4E598 is set.

## Positions and the camera

- **side+0x220 X, +0x224 height, +0x228 Z, floats.** Up is negative: the
  labelled jump read -680, the highest -2344; standing is exactly 0.
- **The pan is the game's own projection, read, not estimated** (corrected
  2026-10-20; the first build estimated a screen width from the camera's eye
  and target, and that estimate also had the sides mirrored).
  - `0x140CEAA70` builds the game's camera matrix, row-vector 4x4, at
    R+0x4D4B4: the rotation from the camera angles (`0x140CCB510`) and a
    translation row from the camera's eye (`0x140CEABC7`). It pushes it onto
    the shared renderer's matrix stack (`0x140CEABEF` -> `0x140F331C0`).
  - The shared renderer (`0x140F39830`) builds a gluPerspective from field of
    view 39.5, aspect 1.3, near 3, far 100000 (`0x140F338F0`) and keeps its
    view and projection at ctx+0x1F812C and ctx+0x1F816C, ctx =
    `*0x142C79B40`: the same pair MJUS projects through
    (`src/render_project.h`, shared now).
  - Sampled live beside the Training capture
    (`tools/fixtures/stg2-training-20261020`, `tools/context_sampler.py`):
    in all 2725 samples of the fight the view was diag(1, -1, -1) and the
    projection 2.142, 2.785 (cot(39.5/2)/1.3 and cot(39.5/2)), w = -z. The
    viewport at 0x141C00210 maps -1..+1 to 0..640 without a flip.
  - Pan = clip.x / clip.w of the fighter's stored position through the
    game's matrix, then the renderer's view and projection. The adapter reads
    the renderer's matrices live each frame; no camera, a non-perspective
    projection, a non-affine game matrix or a point behind the camera gives
    no beacon, never an estimate.
- **Which convention, settled by the game's own data, not by choice:** the
  stored positions as row vectors put both fighters on screen in all 2639
  fighting frames of Training and drew all 266 airborne samples above their
  ground point. Negated positions drew none of them above.
- **P1 is drawn on the RIGHT at round start**, in both captures: the camera
  maps P1's X = -308 to the right. In Arcade, P1 was drawn left of P2 in
  only 50 of 1642 fighting frames.
- Arcade (renderer not sampled there; its matrices are the constant setup
  above): both fighters on screen in 1501 of 1642 fighting frames. The rest
  are short clusters, 140 frames of them from 450.96 to 461.4 s after P2's
  plasma level fell from 1 to 0, where the game's camera closes in and a
  fighter is really off the drawn screen (up to 3.4 half-widths); the pan
  clamps to that side. For 0.4 s at 456.4 s both fighters were behind the
  camera, so there is no beacon there.
- **Airborne pitch** scales with height up to 1000. Standing otherwise.

## Pause

- The whole block froze while paused (no byte changed from 407.9 to 411.0 s).
  The worker turns the beacons off after 250 ms without frames, as for CFJ.
- The motion signature also uses R+0x94, the game's random word, which
  changes every frame of play, plus both fighters' positions.

## Frame timing

- The hook is on update `0x140CCCB60`. Its only caller is the per-frame
  callback `0x140CCB440` (call at `0x140CCB449`).
- The entry bytes `40 53 48 83 EC 20 B9 45 00 00 00` and fourteen code
  signatures (including the game matrix's build and push and the renderer's
  view and projection offsets) plus the cap table are checked before
  hooking.
- `tools/collection_state_capture.py` lists the site in `OWN_HOOKS`.

## Not decoded, so not sounded

- Crouch, knockdown and side steps. Side steps move the fighters in depth
  and the camera turns to follow, so they show as nothing in the pan.
- Snapshot 1 is a fight in an earlier session, before the player entered
  Arcade. If that was the attract demo, cues will sound over the demo; the
  listening check should say.

## Tests

- `.\tools\run_stg2_frame_test.ps1` replays 1994 distinct Arcade frames
  (1642 fighting) and every distinct Training frame (2639 fighting) through
  the renderer matrices sampled live.
- `python tools\sabotage_stg2.py` (frame half): a wrong health offset or
  maximum, an unclamped KO, the intro fed, beacons through the KO replay, a
  loosened match gate, a wrong plasma level size, fill added at the cap,
  negated positions, a transposed game matrix, the affine check removed, a
  beacon without a camera, a mirrored projection, a point behind the camera,
  an inverted height, the clock on its frame counter and an infinite timer
  ticking each fail it.

## Listening check

✅ Confirmed by the user in the game, 2026-10-20, after v0.8.0: "I tested
versus and training modes and they are okay." A general acceptance of
Versus and Training, not a pass of each item below. Arcade was not named.

Released as v0.8.0 (commit `a6f479f`): `dinput8.dll` SHA-256
`aa16a06fbdc015e9864cec064dafeaf749a0293f616ca7bfc0bda8c13d2aacaf`.

- The health tone falls as each side takes damage, with no sweep at a round
  start.
- The beacons pan with each fighter where the game draws it: P1 starts on
  the right. They rise in pitch during a jump.
- The plasma tone climbs as each side builds levels and drops when one is
  spent.
- Clock ticks sound at 30, 15 and 5 to 1.
- The beacons go silent while paused.
- The log has `stg2-frame: hooked Plasma Sword's per-frame update` and,
  every 10 s in a match, a `stg2-frame: N frames` line.
