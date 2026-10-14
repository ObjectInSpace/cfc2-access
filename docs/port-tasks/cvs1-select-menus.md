# CVS1 select menus: groove, speed and order (2026-10-14)

User report the same day: the groove menu, a second menu after choosing a
character in Arcade, and the "choose the order of the bout" screen all read
nothing. All three are now spoken by `src/cvs1_select.c` through the shared
selection channel.

✅ Confirmed by the user in the game, 2026-10-14, after v0.5.0: "Everything's
tested in game and works fine." A general acceptance covering this release,
not a pass of each item in the listening check below.

## Evidence

- Capture: `tools/fixtures/cvs1-select-menus-20261014/`, ROM 1 `cvs1u`,
  Arcade, side 0, Speed Select setting Free Select (P+0x3C = 0). The user
  pressed F9 on each new screen and moved on every menu.
- ⚠ The capture tool's arrow-key log recorded only the character-grid
  presses. The game's own input record has every press: actor+0x5BE (new
  presses), with up 0x2000, down 0x1000, left 0x0400, right 0x0800 and
  confirm 0x0300. Use it, not the tool's input rows, for menu timing.
- Replay: `tools/make_cvs1_menu_fixture.py` writes 14 moments whose expected
  values were read off the trace by hand. Case 5 of `run_cvs1_select_test`
  replays them; eight sabotages each fail it.

## Groove (spoken "Capcom Groove" / "SNK Groove")

- ⛔ The first groove code gated on actor state 3 (`0x1405D9D40`) inside the
  character-grid phase. That state never ran in this Arcade session: the
  select timer R+0x32814 was 0 before the grid, so it would pass in one
  frame, and the actor went from state 0 straight to 4.
- The groove screen is the select phase's step `02 01 01 02 05`. The handler
  is `0x1405D8FF0` (dispatch table `0x141237D80` entry 2), and step 5 is at
  `0x1405D9340`.
- Up sets P+0x76+side to 0, down sets it to 1. Confirm copies it to
  actor+0x62C (R+0x45E4 / R+0x4CE4), which feeds the gauge's groove flag.
- A side is choosing while its bit is set in both P+0x113 and P+0x65, the
  handler's own test. Live, P+0x113 was 1 from step 4 until the confirm.
- The state 3 path writes the same byte and is still accepted.

## Speed (spoken "Normal" / "Turbo")

- This is the menu after the character grid: actor state 5, `0x1405DAF10`.
  It appears when the Speed Select setting is Free Select
  (`OPT_CVS1_GS_00`).
- Up sets actor+0x58E to 1, down to 0, and state 2 defaults it to 1. Live:
  down at 25369.59 (0), up at 25369.85 (1), then confirm.
- The value sets P+0xC7 to 3 (0) or 5 (1). The Speed Select setting fills
  the same field (`0x1405B3E2D`): Normal leaves it 0, Turbo 1 to 4 give 3,
  5, 6, 7. P+0xC7 schedules a special frame every 9 − C7 frames
  (`0x1405B1D46`), so 1 is the faster option.
- ⚠ The words "Normal" and "Turbo" are inferred from the rate, as for Alpha
  3. The listening check should confirm the menu's own words and that it
  opens on Turbo.

## Order (spoken as the fighter and the fight, e.g. "Ken first")

- Phase `02 01 02 01 02 01`, handler `0x1405E74A0`, human input at
  `0x1405E7790`.
- Left increments the cursor P+0x10C+side and right decrements it, wrapping
  at P+0x104+4×side (members left − 1) plus 1.
- The highlighted member is R+0x31AD0[side×4 + cursor]. Its native ID is
  actor+0x618 + member. P+0x106+4×side counts the members already placed.
- ⚠ The human mask here is P+0x64, not the groove screen's P+0x65 (0 on
  this screen). The dispatcher at `0x1405E750B` sends sides with their P+0x64
  bit to the input handler and the rest to the CPU picker `0x1405E67F0`.
- Live: the cursor flipped Ryu and Ken on every press, and confirm placed Ken
  first. With one member left there is no choice, and nothing is spoken.

## Listening check

Deployed `dinput8.dll` SHA-256
`8d0c1dac3c5fdc563b22af4ebd017f1c67e6909c95a523a29e1606f7d997236f`
(build of `e67f357`); the previous DLL is kept as
`dinput8.dll.20261014-menus.bak`.

1. In CVS1 Arcade, the groove screen says "Capcom Groove" on arrival, and
   up and down switch it.
2. After the characters, the speed menu says "Turbo" on arrival, and down
   says "Normal". Confirm that those are the words on screen.
3. With two or more fighters, the order screen names the highlighted fighter
   and the fight it is being placed for, e.g. "Ryu first". Left and right
   move through the team.
4. "Free Play" is no longer spoken between screens (`986f987`). The ` key
   still reads it.
