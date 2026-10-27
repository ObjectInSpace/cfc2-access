# Power Stone 2 runtime adapter, 2026-10-27

PWS2 selection speech, health/stone/clock tones and pickup pings are built,
offline-tested and deployed: `src/pws2_frame.c` (frame hook and decoders)
and `src/pws2_select.c` (worker gates, selection channel, audio ownership).
**Not yet heard in the game.** Same design as PWS1
([pws1-match-fields.md](pws1-match-fields.md)); none of PWS1's offsets
carry over.

## Reproduce

```powershell
python tools/make_pws2_fixtures.py --check
.\tools\run_pws2_frame_test.ps1
.\tools\run_pws2_select_test.ps1
python tools/sabotage_pws2.py
```

## The capture

One session, `build/pws2-live-20261027` (local), ROM 11 pws2u, Arcade:
the user on fighter 0 (Falcon) against three CPU fighters (IDs 5, 3, 7).
Eight F9 snapshots plus the continuous trace (4,519 samples at 20 a second):
character select, the screens before the fight, one fight of about three
minutes with stones picked up and lost, a CPU transformation, a 3.5 s pause,
and the user's KO with 19 s on the clock, then the continue screen. Every
result below is classified by the traced fields, not the snapshot labels.

The renderer sampler started only after the game had closed (it waited on a
permission prompt), so no live projection was recorded. See "Camera".

Fixture: `tools/fixtures/pws2-frames-20261027.bin`, every sample where an
input of either decoder changes (4,448 records). The raw trace stays local.

## Hook site

- `0x140A47290` is the PWS2 update. Its only caller is the published
  per-frame callback `0x140A46420` (call at `0x140A46429`). Entry bytes
  `48 83 EC 28 E8 rel32`, the same shape as the PWS1 hook.
- 31 code signatures and the item table's 204 category bytes are checked
  before hooking; the worker checks 10 more. All were generated from the
  dump and re-verified against it.
- `tools/collection_state_capture.py` lists the site in `OWN_HOOKS`.

## Phases, read off the dispatch and seen live

- Phase dispatch `0x140A48CC0`: R+0x52BD8 = 1, then R+0x52BD9 selects the
  nested phase. Select and fights both run under nested phase 2
  (`0x140A4CA30`), with R+0x52BDC = 1. Nested phase 1 rebuilds the fighters
  too; it is not entered in this capture (likely the attract demo) and is
  excluded.
- R+0x52BDD is the step (table `0x140A4CDB8`). Live: 1 select (4.2 to 19.5 s),
  2 and 3 the screens before the fight, 4 the intro, **5 the fight**
  (49.8 s on; entry 5 calls `0x140A4C6D0`).
- R+0x52EE5 is the fight's mode: 0 through the intro, 1 at "ready" (63.9 s),
  2 while fighting (66.4 s), 0 again from 232.16 s once the KO decided the
  round, through the continue screen (still step 5).
- R+0x52D2B / R+0x52D2E bypass the dispatch when set. Both stayed 0
  throughout, pause included.

The adapter: in a match while step 5 and mode 1 or 2; the clock and pings
only in mode 2, ordinary draw (R+0x52FDC = 0) and view index 0..3.

## Health

- Float at A+0x1DC, A = R+0x40818 + index*0x42B8 (index 0..3). The fight's
  setup writes 1000.0 (`0x140A66A99`); food adds the item table's word
  +0x1A and caps at 1000.0 (`0x140A5586D`..`0x140A5588E`). So the maximum
  is 1000 from two writers.
- Live: 1000 at the start, damage and one heal on the way down, 0.0 on the
  KO frame (232.10 s), one sample before the mode left 2. The frame hook sees
  every frame, so the 0 is heard before the cues stop.
- The decoder rounds to an integer, keeps any living value at least 1, and
  refuses non-finite or out-of-range values.

## Stones

- A+0x4055, 0..3. The stone giver `0x140ADC780` and the pickup handler
  `0x140A55430` both refuse a fourth and increment it, and store the stone's
  index at A+0x4056+count. Setup gives every fighter one (`0x140A66AA8`).
- R+0x51B32 is a stack of the stones not yet in play (7 in this mode, filled
  at `0x140AF92F8`); it is not the count lying in the arena.
- Live, the user: 1, lost to a hit at 100.2 s, then 1, 0, 1, 2 before the KO.
  Fighter 1 reached 3 at 104.1 s and fell to 0 at 112.7 s when its
  transformation ended, as in PWS1.

## Clock

- Signed word R+0x52EE0. `0x140A6FF4A` decrements its 60-frame sub-counter
  R+0x52EE2 only while the mode is 2, and the clock when it runs out.
- Live: 180 from 67.5 s, one step per 60 frames, 19 at the KO, frozen after.

## Pause

- R+0x52D34 increments in the input copy (`0x140A4EA25`), past the point
  where a paused game returns. Live it stood still for the whole pause
  (94.0 s, 3.5 to 3.8 s); the other counters kept moving. Pings run only on
  frames where it moved.

## People and cue owners

- A+0x4028 is 0 for a fighter a person plays and 1 for the CPU: treasure
  scores only for people (`0x140A59B11`), and the input copy feeds only
  fighters with it 0. Live: 0, 1, 1, 1.
- A+1 is the slot byte (0..3); a record whose slot byte does not match its
  index is not used.
- **Every fighter, CPU too** (user, 2026-10-27: "The CPU's health will need
  to be tracked as well"), owner = fighter index 0..3. With more than two
  fighters a fixed HUD side says nothing, so the user chose **a sound per
  fighter plus pan**: each fighter's health and stone tones are panned to
  where that fighter is on screen at the moment of the change (the same
  projection as pickups; centre with no camera), and each fighter has its own articulation: fighter 0 steady (the
  plain tone every other game uses), 1 flutter (24 Hz tremolo), 2 warble
  (16 Hz, 7% vibrato), 3 growl (55 Hz modulation). The rates fit about three
  cycles into the 140 ms tone. Health keeps its square wave and low register,
  stones their triangle and high register.
- Shared code: `match_audio` now has four owners and a per-sample
  `fighter_gauges` flag with per-owner `gauge_pan`; the mixer has
  `audio_gauge_set_fighter_owned`. HUD games are unchanged: owners 2 and 3
  never sound for them, P1/P2 keep fixed sides and the steady tone. Beacons
  stay two-owner. Tests: `run_audio_fighter_test.ps1` and case 13 of
  `run_match_audio_test.ps1`.
- Untested live: how the four articulations read in a real fight, and two
  people playing.

## Pickups

- The pool: 200 records of 0x4C8 at R+0x4B10 (`0x140A4BF69`). +0 is the list
  an object is on. The pickup handler walks **list 9** (head R+0x40798) and
  takes an object whose state +0x4B1 is 2 (`0x140A55522`); position +0x3C
  (X, Y up, Z), relative to the matrix at +0xB8 when that is set.
- The item code +0x4B0 indexes the table at `0x1417319E0` (0x34 bytes an
  entry); its first byte is the category the handler switches on
  (`0x140A5565E`):
  - 4: power stone (code 0xC1), the branch that increments A+0x4055.
  - 3: food, healing by the entry's word +0x1A (45 to 125). Codes 0x4A,
    0x75, 0x78 branch elsewhere first and are not food.
  - 6: treasure (0xC6), adds to the side's score.
  - 9: collectibles (0x81..0x8F), recorded in a list at R+0x5436A.
  - 0: the rest; codes below 0xC0 are what fighters pick up and use.
- A+0x4080 is the object a fighter carries (written by the lift at
  `0x140A5619D`). Live it pointed at a state-5 object 876 times. Carried
  codes: 0xC3 most often, then items 0x3B, 0x33, 0x39, 0x36, 0x2B and others,
  and 0xC8.
- 0xC3 is the liftable prop (8 to 18 lying at once); 0xC8 is also carried.
  0xC2 and 0xCA sit at fixed places, never carried: stage fixtures, not
  pinged. Category 1, 2 and the other codes are not pinged.
- A stone in the arena sits in state 2 at floor height until picked up. A
  dropped stone first travels in another form for a few seconds, so the
  count of lying stones is not exactly 7 minus held minus stacked.

The cue, as for PWS1:

- Families: stone (0xC1), food (category 3 healers, one semitone per kind
  in code order, ten kinds), prop (0xC3, and 0xC8 a semitone up), item
  (everything else that is pinged). Items have dozens of kinds and the voices
  take two octaves, so an item's semitone is its code modulo 25: kinds 25
  codes apart share a pitch.
- Distance to the nearest fighter a person plays; pan through the drawn view
  and the renderer's projection.
- **Nothing off screen is sounded** (user, 2026-10-27: nothing there can be
  picked up, hit or hit you, and fewer tones at once help focus on what
  matters now). A pickup is placed only while drawn within the screen,
  horizontally and vertically; past an edge or behind the camera it stays
  present but silent, and pings once it comes into view. A fighter off screen
  has no health or stone tones; a change there is not heard, and its first
  value back on screen seeds silently. Shared test: `render_project_screen`
  in `render_project.h`, used by PWS1 too.
- Live replay: every family turned up; at most 5 stones, 1 food, 13 items
  and 12 props lying at once. Before the rule, about 1,800 pings over the
  fight (roughly 11 a second); with it, about 1,150, as about half of what
  lay in the arena at a time was off screen (mostly props). Fighters were off
  screen in 157 of 11,928 checks (1.3%).
- Not exercised by the capture: an object with a parent matrix (none
  occurred; it would be present but unplaced, so silent).

## Pickup announcements

User, 2026-10-27: say who picked up what, for every fighter, since any
player may be the blind one. The game holds **no item names as text**: none
in the collection's message tables, none as plain or offset-encoded text in
the executable or the ROM container (only PWS1's debug fighter and stage
labels turned up). The Item Book draws its names as pictures. So the user
chose **kind only**: "Falcon, food". Fighter names come from the shell's
roster table in the game's language; the kind words are the mod's own, in
all 14 languages.

- Kinds: power stone, food (category 3 healers), treasure (0xC6), something
  to throw (0xC3, 0xC8), item (everything else, including unknown codes).
- Touched pickups: the handler `0x140A55430` is hooked. Its only caller
  (`0x140B14A68`) passes the fighter in RCX and tests EAX; every success path
  returns 1 (`0x140A55B4D`), otherwise 0. The object taken is the one lying
  in list 9 before the call and gone after it.
- Lifted: the carried pointer A+0x4080 naming a new pool object.
- Stones: the fighter's stone count rising. Not the handler: in the capture 5
  of the 20 stones gained were caught as they fell, never a lying object.
  The handler path skips stones so none is said twice.
- Replay: 20 stones gained, each announced for the right fighter; 34 lifts
  (14 things to throw, 20 items). The hook itself only runs in the game.

## Settings (F9)

- **F9** cycles pickup announcements: every fighter, players only, off.
- **Shift+F9** toggles off-screen sounds: off (as above) or on, when a pickup
  or fighter tone off screen sounds at the edge it lies beyond (behind the
  camera: the side the view's right axis puts it). Applies to PWS1 too.
- Spoken and saved in `cfc2_audio.ini` (`pickup_speech` 2/1/0,
  `offscreen_sounds` 0/1). Keys checked: the mod uses F5 to F8 and `; CFC2
  binds no function keys; Steam takes F12; F10 is Windows' menu key; the
  capture tools moved their snapshot key from F9 to F11.

## Camera

- The ordinary draw (R+0x52FDC = 0) copies R+0x51D90 + 0x308 * i into
  renderer slot 2 (`0x140A5D6A0`), i = signed R+0x52E88 (0 throughout live).
  Projection: slot 3, ctx+0x1F816C, read live at runtime as for PWS1.
- Without a live projection sample, the tests use the one the traced
  constructor builds from the game's constants (angle 0x1D28, aspect 4/3).
  Through it, 1,176 of 1,192 fighter positions sampled during the fight land
  on screen, as a following camera should. The x scale it gives (2.0) is the
  same as PWS1's measured 2.0059. Left/right follows the shared renderer's
  viewport, which maps +x to the right without a flip.

## Selection

- The decoder path in `pws-selection.md` held on every live sample: step 1,
  sub-phase 1, sub-state 2, participation mask 1, side state 1 while
  choosing and 2 once confirmed (19.42 s).
- Live: Falcon, then three steps out and back (Ayame, Pete, Wang-Tang),
  confirmed on Falcon; spoken once each, in order; nothing after.
- Not covered: other selecting sides, shuffle (decoded, not seen live in
  PWS2). User, 2026-10-27: Versus, Training and the Japanese version work.
## Stage select (walk to a stage)

The screen between select and fight is step 3, and with the collection's EX
option "Stage Selection: Manual" (the user's setting) it is the stage
select. Capture `build/pws2-versus-stage-20261027` (local); fixture
`tools/fixtures/pws2-stage-20261027.bin`. (A first reading here called it a
non-interactive scene: the input record's newly-pressed field showed no
directions. The held-buttons field and the fighter's position showed it
moving; corrected the same day.)

- The fighter drops in (height 4,450 to 40, about 13 s), then walks freely
  alone, other fighters not in play. Mode R+0x52EE5 reads 1 from the landing.
  Held bits: 0x2000 forward (z falls), 0x1000 back, 0x800 left, 0x400 right.
- The walk `0x140A6A090`: for each fighter in play and each of five spots
  (a table of object pointers at `0x14298C940`), the flat distance from
  A+0x3C/+0x44 to the spot; the first within 300.0 (`0x140FE478C`) is the
  choice. A countdown R+0x52BE8 starts at 1199 (20 s) and picks one of the
  five at random at 0 (`0x140A46770` mod 5). The choice R+0x52BEA becomes
  the fight's stage R+0x53010 (`0x140A69971`).
- The spots are list-5 objects with callback `0x140A68A00`, +2 the stage
  0..4, at (-740,700) (-530,177) (0,-40) (530,177) (740,700): an arc about
  725 from where the fighter lands (-10,680). From there, as the camera
  faces: left, ahead left, ahead, ahead right, right.
- Live: Arcade's countdown ran out and picked index 4; in Versus the user
  walked into index 2, 300.3 from its centre, with 76 frames left.
- No stage names exist as text (the title banner is a picture), so the user
  chose numbers: stages 1 to 5, left to right as the game numbers them.

What is said and heard:

- On landing: "Stage select. Stage: 1, left. Stage: 2, ahead left. ..."
  (the collection's own "Stage:"; directions from the person's fighter,
  computed from the drawn view, eight ways). Once chosen: "Stage: 3".
  Not under the F9 pickup setting.
- While walking, each spot pings: a plain sine tone, its own pitch per stage
  (C, E, G, B, D: user, 2026-10-27, after the bell with whole-tone steps
  sounded too alike), faster and louder as the fighter nears, the distance
  counted to the 300-unit reach. A spot off screen still pings at its edge: unlike a
  pickup, it can be walked to unseen.

## Tests

- `run_pws2_frame_test.ps1`: every fixture record through the frame and
  pickup decoders (gates, health, the stone sequence, clock, pause, pickups,
  carried exclusion, families, ping scheduling with no pings in the pause,
  fail-closed cases).
- `run_pws2_select_test.ps1`: every record through the decoder and the shared
  channel, plus retry, re-announcement on a new root and shuffle.
- `sabotage_pws2.py`: each mechanism removed in turn must fail its test.
