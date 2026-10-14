# Alpha 3 in-match fields for match audio

Evidence comes from two sources:
- the labelled snapshots and continuous trace in
  `tools/fixtures/z3u-match-20261014` (ROM 6 `z3u`, Arcade, Ken X-ism
  against R.Mika V-ism);
- the code that uses each field.

Player object = R + 0x1B0 + side*0x550, where R is the root published at
`0x1422FB960`; these are the same objects that character select uses. The
implementation is `src/z3u_frame.c`. Not yet heard in the game.

## Confirmed in capture and code

- **Health, object+0x88, signed 16-bit, maximum 0x90 (144).**
  - Capture: 144 at every round start, falling in steps. -1 on each KO frame.
  - Code: round setup `0x14086CE72` stores 0x90 to +0x88 and its display
    copy +0x8A together. Damage `0x1408B110C` subtracts the hit and floors it
    at 0. The other setup path, `0x14086CE88`, can only lower it.
- **Super gauge, object+0x1EE, signed 16-bit, maximum 0x90 (144).**
  - Capture, P1 (X-ism):
    - 144 at the "level stocked" snapshot;
    - 0 from 119.0 s, just before the "after spending a super" snapshot;
    - then +3 on each hit taken (123.3, 123.7, 124.3 and 142.3 s).
  - Capture, P2 (V-ism): held 144.
  - Code:
    - `0x140888475` allows an X-ism super only when the gauge is exactly
      0x90.
    - The spend sites `0x14088750B` and `0x140889AB9` subtract 0x48, or an
      amount from a table for ism value 1.
    - Match setup `0x14089A21B` fills it to 0x90 unless a setting zeroes it.
      That is why it starts full, and why at the new match it animated
      0 → 144 during the intro.
  - ⚠ A-ism was not captured. Any value outside 0 to 144 leaves the gauge
    silent rather than guessing a scale.
  - ⚠ Correction: an earlier reading of this field as the guard meter was wrong,
    and was abandoned.
- **X, object+0x10, and Y, object+0x14, signed 16.16.**
  - Fighters are walled at X 280 and 1000.
  - Ground is Y = 40: landing `0x1408C51A8` snaps any Y below 0x28 to
    exactly 40.0.
  - Height is Y - 40. It read 79.75 mid-jump.
- **Camera, R+0xC55A.** This is the play layer's scroll X, in the scroll
  object at R+0xC548, one of three at stride 0xA8.
  - `0x140872550` computes screen X = object+0x12 - R+0xC55A, clamped to
    -192..575.
  - Live it ran 256 to 640: the fighters sit at screen X 24 and 360 in the
    corners, and the midpoint is exactly the camera + 192 at round start.
  - Pan = (screen X - 192) / 192, so no camera model is assumed.

## Confirmed in capture only

- **Match gate: words R+0x28, +0x2A, +0x2C = 1, 2, 3.**
  - Every match sample holds this; select reads 1, 1, 2, and the continue screen
    1, 2, 4.
  - The dispatchers for +0x2A and +0x2C are traced (see
    [z3u-selection.md](z3u-selection.md)). The handler for screen 3 is not
    read.
- **Round stage, word R+0x2E.**
  - 2 intro (health refills and the gauge fills), 3 fighting, 4 KO, 5 round
    over, then 0 and 1 while the next round is set up.
  - Health and gauge are fed only in stages 3 to 5.
- **Round clock, R+0xF1, u8.** 99 at each round start, then down one step
  roughly every second. The decrement instruction was not located, the same
  gap as CVS1 and CFJ.
- **Pause.** The whole block froze from 218.3 s until the capture ended. The
  root's frame counter R+0x51, which the update increments, stopped too, so
  the update does not run while paused. The worker turns the beacons off after
  250 ms without frames, as for CFJ.
  - The motion signature uses both fighters' X and Y plus +0x20 and +0x52,
    which change in 40 to 60% of samples during play.

## Frame timing

- The hook is on update `0x14086D160`. Its only caller is the per-frame
  callback `0x14086BE70` (call at `0x14086BE79`), which Alpha 3's initializer
  publishes at callback slot +0x10.
- The entry bytes, 25 up to the first call, are checked before hooking.
- `tools/collection_state_capture.py` lists the site in `OWN_HOOKS`.

## Not decoded, so not sounded

- Crouch and knockdown. The capture has three crouch holds; they were not
  analysed in this increment.
- The guard meter. The "blocking" snapshot went wrong (the player says so);
  it was taken on the continue screen.

## Tests

- `.\tools\run_z3u_frame_test.ps1` replays 13 snapshots plus the intro gauge
  fill, the KO frame and a select sample (`tools/make_z3u_frame_fixture.py`).
- Twelve sabotages each fail it:
  - health on the display copy;
  - the wrong health maximum;
  - the wrong gauge cap;
  - the gauge read from the score;
  - the intro counted as live;
  - no live gate;
  - the KO sentinel accepted;
  - a loosened screen gate;
  - the wrong clock offset;
  - no camera;
  - the wrong ground;
  - the wrong X offset.

## Listening check

Deployed 2026-10-14: `dinput8.dll` SHA-256 `dbc00a56ac52a931fb07e3625a9fdb08cccafea36aa5de2e177cee084cd12d52`.
The select-only build it replaced is kept as `build/deployment-backups/dinput8-bd75c83ad4e5.dll`.

- The health tone falls as each side takes damage, with no sweep when a round
  or match starts.
- The beacons pan with each fighter's place on the screen, not in the stage,
  and rise in pitch during a jump.
- The super gauge tone tracks each side: it is full at a match start, falls to
  empty when a super is spent, and climbs as you hit or are hit.
- Clock ticks sound at 30, 15 and 5 to 1.
- The beacons go silent while paused.
- The log has `z3u-frame: hooked Alpha 3's per-frame update` and, every 10 s
  in a match, a `z3u-frame: N frames` line.
