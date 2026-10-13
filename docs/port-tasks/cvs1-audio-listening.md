# CVS1 listening check: match audio

Installed 2026-10-13 from `c11fc9f`. DLL SHA256
`29245559aed04730de02cf34fbd8c34dee27ef464475b20c382b3cd4e8220dcf`.
The previous DLL, accepted for CVS2 and CVS1 character select, is kept as
`dinput8.dll.pre-cvs1-audio-20261013.bak` (SHA256 `9a3d1220...`). To roll
back, close the game and copy the backup over `dinput8.dll`.

Status, 2026-10-13: the user confirmed the health tone and the position
beacons work. Crouch gives no pitch change, as expected because it is not
decoded yet. Jump pitch and pause are not yet reported. Field evidence: [cvs1-match-fields.md](cvs1-match-fields.md).

## Listen for

1. The health tone when either fighter is hit, from that player's side: P1
   left, P2 right. Silence at full health and at the start of a round.
2. The two beacons during the fight, following each fighter left and right,
   with Player 1's faster and higher. Tell Claude if the position feels off,
   for example a fighter near the edge not sounding near the edge. That tests
   the screen-width assumption.
3. Beacon pitch rising and falling through a jump.
4. Pause: the beacons stop within about half a second and return on resume.
5. Exit to the menus: every tone stops.
6. F5 and F7 still change the health and beacon volumes.

## Not expected yet

No power-gauge tone, no clock ticks, and no pitch change when crouching or
knocked down. Those fields are not decoded yet.

The CVS1 log lines to look for in `CFC2Access.log`: `cvs1-frame: hooked`
at start-up, and periodic `cvs1-frame: N frames (60/s), ... health-change(s)`
while in CVS1.
