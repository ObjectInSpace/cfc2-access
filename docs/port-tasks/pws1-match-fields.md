# Power Stone 1 health and resource trace, 2026-10-23

The existing October 22 memory capture supports further work without another
visual capture, as requested by the user. This increment identifies health
through damage, recovery and gauge consumers, and retains collectible/form
resource candidates. It does not install hooks or enable match audio; the
runtime adapter that does is described at the end of this file.

## Reproduce

```powershell
python tools/pws1_health_audit.py --check
python tools/test_pws1_health.py
python tools/pws1_health_decode.py tools/fixtures/pws1-observations-20261022/state-12.bin.gz
```

Fourteen instruction-aligned spans are hash-pinned in
`docs/pws1-health-manifest.json`. Full instructions regenerate into
`build/pws1-health-audit.json`. Five tests cover manifest reproduction,
captured health loss/reset and resource changes, signed floor/range handling,
separate current/recovery/display values, and root-size refusal.

## Health is a signed word, not a float

For actor `R+0x18428+index*0x7890`, current health is signed int16 at
`+0x7790` (absolute root offsets +0x1FBB8 and +0x27448). Adjacent words
`+0x7792` and `+0x7794` are recovery target and trailing display state.
They must not be reported as separate resources or substituted for health.

The damage routine `0x1409BE300..0x1409BE82E` reads an attack descriptor,
applies conditional multipliers, truncates the result to an integer, subtracts
from the current word, and writes it back. Negative branches choose 0 or -1
according to descriptor flags and context options. The later signed-negative
test routes to a native event. A reader must preserve the signed value; -1
must never become a large unsigned health value. The descriptor and mode
branches have not all been given gameplay labels.

`0x140991360..0x1409913CC` increments current health toward +0x7792 on a
counter and separately decreases +0x7794 toward +0x7792 on alternating updates.
The fixed-actor caller at `0x140993804..0x14099384C` supplies each actor.
Thus even within a round, health need not be monotonically decreasing.

Recovery at `0x140986317..0x14098639A` adds 24/48/96 depending on the
selected item type, caps current health at 240, and raises the two adjacent
values as needed. A conditional both-actor refill at
`0x140993775..0x1409937D0` writes 240 to all three words. Another
context-option path at `0x1409B2412..0x1409B2460` adds four, caps at 240
and copies to the adjacent words. These establish the traced refill cap;
they do not prove exhaustive writer coverage or every mode's rules.

The gauge code at `0x1409AAE1C..0x1409AB003` starts with fixed actor zero,
reads health and +0x7794, emits segments with a maximum segment width of
48, subtracts 48 from the remaining values, and repeats. Its outer loop
advances by 0x7890 and visits two actors. This joins gameplay health to a
gauge consumer independently of numeric correlation or a screenshot.
Its layout branch and animation do not establish final screen positions.

Retained snapshots 7 through 13 show (actor 0, then actor 1):

- Snapshot 7: 240, 240
- Snapshot 8: 156, 240
- Snapshot 9: 156, 240
- Snapshot 10: 32, 240
- Snapshot 11: 32, 240
- Snapshot 12: 12, 240
- Snapshot 13: 240, 240

Snapshot 13 contains a reset/refill; the plan's round-end label does not
independently prove its cause. The inspection decoder preserves raw signed
values and produces a display candidate only for -1..240, flooring -1 at zero.
It does not infer a match or permission to speak from valid numeric values.

## Collectible and form-resource candidates

`0x140986150..0x140986260` checks actor gates, searches object type 5,
checks collection eligibility, increments byte actor+0x77EC, and sets a bit
in actor+0x77ED using the collected object's +0xE value. Collection is
refused at count three or higher in this path. These are the strong stone
count/identity candidates; object-type construction and HUD icons remain
to be traced before assigning stone colors or complete semantics.

`0x1409ECB50..0x1409ECBBF` invokes that collector and checks the count
against three. The corresponding branch at `0x1409ECD34..0x1409ECD50`
requests action 0x17 through `0x140987220`. Do not infer that reaching
three means the transformation animation has completed.

`0x140986470..0x140986497` sets actor+0x77AD to one, calls model setup,
and initializes the signed word actor+0x77B0 to 2400. The model setup
`0x1409E3410..0x1409E34D0` uses +0x77AD together with fighter ID to choose
scale and select between two model-record sets. A periodic path at
`0x1409ECD50..0x1409ECD76` decrements the resource under a global gate.
Other move-specific writers also modify it; it must not be presented as an
unconditional wall-clock timer or given a universal maximum from one writer.

For fixed actor 1, snapshots 9, 11 and 12 contain respectively:
`(count=3, bits=7, form=1, resource=2307/2292/1349)`. Snapshots 7 and 8
have `(1,2,0,0)`, snapshot 10 `(2,3,0,0)`, and snapshot 13 `(1,2,0,0)`.
The decoder retains these raw observations without declaring transformation
completion, visible gauge eligibility, controller ownership or audio range.

## Runtime adapter, 2026-10-23

PWS1 selection speech and match audio are now built, offline-tested and
deployed: `src/pws1_frame.c` (frame hook and decoder) and
`src/pws1_select.c` (worker gates, selection channel, audio ownership).
**Not yet heard in the game.** The evidence below came from the retained
October 22 trace `build/pws1-live-20261022-c/trace.bin.gz` (local, about
3,200 samples at 20 per second) and from the code; no new capture was taken.

### Hook site

- `0x140974100` is the PWS1 update. Its only caller is the published
  per-frame callback `0x140973090` (call at `0x140973099`); nothing else
  calls it, jumps to it or stores its address. It runs the shared dispatch
  thunk and tail-calls the game logic `0x140973300`.
- Entry bytes `48 83 EC 28 E8 rel32`, the same shape as the CVS1 and CFJ
  hooks. Eighteen code signatures are checked before hooking.
- `tools/collection_state_capture.py` lists the site in `OWN_HOOKS`.

### Match gate, read off the dispatch

- R+0x27E58 = 1 (running branch) and R+0x27E5D = 4. Phase-table entry 4
  (`0x140977082`) calls the match routine `0x140990B20`.
- That routine calls through table `0x141523970` with signed byte
  R+0x27FAC. R+0x27FAD is the step inside each sub-state.
- Observed live in one round, in order: 0 then 1 (health set to 240, stones
  to 1), 2 (round intro), 3 (fighting), 4 and 5 (round over), then 2 again
  for round two, where health is refilled to 240.
- Sub-state 3, step 0, is the round being fought. `0x140993804` leaves step
  0 when either fighter's health is below 0; the clock running out does the
  same. Live: step 0 to 2 on the frame actor 0 read -1.
- Phase 0 also runs fights with sub-state 3: the attract demo, seen in trace
  b. The phase test keeps it silent.
- The adapter: in a match for sub-states 2 to 5; health and stones fed from
  3 to 5 (the intro refill is never heard as a sweep); beacons only for
  sub-state 3, step 0, and only with the ordinary single view
  (R+0x280C4 = 0, the gate the draw tests at `0x1409A8809`).
- Not identified: sub-states 0, 1 and 6 to 9. They are treated as not a
  match.

### Health

- Damage leaves 0 or -1. Live, the round lost by KO went 240, 215, 208, 201,
  202 (the game's own recovery), ... 3, 0, then -1 on the KO frame. The
  adapter accepts -1 to 240 and floors -1 at 0.
- The opponent (CPU) stayed at 240 throughout.

### Stones (the resource tone)

- A+0x77EC, 0 to 3. Every writer is accounted for: the collector increments
  and refuses a fourth (`0x1409861AF`), being hit decrements above 0
  (`0x1409BDDEE`), round setup sets 1 (`0x1409E38FF`), and three sites
  zero it.
- Live: P1 lost its one stone on the first hit. The CPU went 1, 2, 3,
  transformed, dropped to 0 when the transformation ended, then 1, 2, 3
  again. So the tone climbs with stones, holds full while transformed and
  falls when it ends.
- The transformation timer A+0x77B0 is not sounded: its maximum is not
  established for every writer.

### Clock

- R+0x27FCD counts down from 99. Its sub-counter R+0x27FCE is reloaded from
  table `0x14109BB38` indexed by the timer option (60 frames a step here).
  It is not counted while R+0x27E7D is set or R+0x27E92 = 4
  (`0x140993873`); the adapter then leaves the clock silent.
- Live: 99 at the round start, one step a second, 77 at the KO, frozen
  through the round end.

### Fighter beacons: removed

User, 2026-10-24: the game's own footsteps already place the fighters, so
PWS1 has no fighter beacons. Jumps are a separate button in this game, so
the jump-pitch question no longer applies.

### Pickups: positional pings

What the user asked for instead: cues for the stones and pickups lying in
the arena, so they can be found. Different kinds sound different.

The arena's objects are one pool of 100 records, stride 0x3E0, at R+0
(built at `0x140995CC0`; the fighter records follow at R+0x18428). Read off
the code that uses each byte:

- +0 in use, +1 shown, +2 class, +4 type, +0x30 position (X, Y up, Z),
  +0xE a stone's index, +0x3C8 an item's code.
- The collector `0x14098A9F0` takes class 2 or 3 and matches the type.
- Type 5 is a power stone (`0x1409861BC`). The stone index at +0xE is the
  bit the collector sets in A+0x77ED.
- Type 6 is an item. Its code indexes an 18-entry table at `0x14098AB35`, so
  there are 18 item types, 0 to 17. Codes 15, 16 and 17 are food, healing
  96, 48 and 24 (`0x140986317`). Codes 0 to 14 are not yet named.
- Type 1 is a liftable prop: the pick-up `0x140986EF1` lifts type 1 or 6.
- A+0x7780 points at the object a fighter carries (`0x14098AB00`); a carried
  object does not ping. A+0x7778 is the object within reach, seen live
  pointing at props, poles (type 2) and the item as the fighters passed them.
- R+0x27FF2 has a bit per side played by a person; `0x140976ECB` derives the
  CPU sides as its complement. It read 1 throughout the one-player capture.

Checked against all 581 fought samples of trace c
(`tools/fixtures/pws1-pickup-frames-20261022.bin`):

- A stone is shown exactly while it lies in the arena. Shown plus held never
  passes 3, except for 19 samples, never more than 0.35 s at a time, at the
  moment of a pick-up or a drop.
- Two items appeared: code 8 (pool 29) and code 1 (pool 46). Each popped up,
  bounced and settled. Item 46 blinked (its shown flag toggling) for its last
  half-second, so items are judged by "in use", not "shown".
- Between 1 and 6 props were shown at a time.

The cue, as the user chose it:

- One timbre per family: stone a bright bell, food a soft falling "bloop",
  item a clear blip, prop a low quieter thud. Inside a family each type is a
  semitone higher (item code 0 to 14, food 15 to 17).
- A repeating ping, panned to where the game draws the thing (stored view
  and live projection, as before).
- Louder and faster the nearer it is to the human fighter (the nearer one
  if both sides are human): every 0.2 s when touching, about 1.2 s across
  the arena; props half again as slow.
- Pickups arriving together (a round start) are staggered over 0.35 s.
- Nothing off screen pings (user, 2026-10-27, for consistency with PWS2;
  previously a pickup beside or behind the camera panned hard to its side).
  A pickup past an edge or behind the camera is present but silent and pings
  once it comes into view. In the capture: 1,961 sightings on screen, 825
  past an edge, and 36 for the stone that lay beside and behind the camera
  for 1.7 s (34 samples behind it, 2 past the right edge either side).
- Pings only while a round is fought (sub-state 3, step 0, single view), and
  only on frames the game simulates: the frame counter R+0x27F94, which
  stopped for the whole 3.7 s pause, must have moved.
- Volume follows F7, the beacon level.
- Shared code: `src/pickup_cues.c` (scheduling) and the ping voices in
  `src/audio.c`, so Power Stone 2 can reuse them.

### Ownership

- Owner 0 is the fighter record R+0x18428, owner 1 R+0x1FCB8. The selection
  driver uses the record's slot byte A+3 as the side index; a record whose
  slot byte does not match is not fed.
- In the capture the human was side 0 (owner mask 1) and its record took the
  damage. A second human side, Versus and Training were not captured.

### Selection, now wired

- `pws1_select_tick()` admits game 4 with ROM 8 (pws1) or 9 (pws1u), the
  published root and size at session+0x1B0, the root slot `0x1423188C0` and
  the shell game field 4. Ten code signatures and the cursor ID table
  `0x14109C510` are checked once.
- One 0x230-byte read covers the side states, cursors, phase, gates and owner
  mask; four small reads take the slot bytes and shuffle flags.
- Speech only while the update runs (`frame_liveness`), as for STG2.
- Live replay: three select screens; the two entered on camera opened on
  Falcon; screen 2 settled on Falcon, Kraken, Falcon, Ayame, Falcon, Valgas,
  Kraken between shuffles; both confirmations committed the last highlight;
  143 shuffle samples stayed silent.
- The Japanese ROM is admitted but untested; names for it come from the same
  shell table.

### Versus stage select

Captured 2026-10-24 (`build/pws1-stage-20261024`, US ROM, Versus; fixture
`tools/fixtures/pws1-stage-live-20261024.bin`). The log of the session before
it showed no collection-menu activity on that screen, so it is the emulated
game's own.

- On screen while R+0x27E5D = 3, R+0x27E5E = 1 and R+0x27E5F = 1 (2 once
  chosen). R+0x27E61 drops to 0 for a quarter-second on each move, so it is
  not used as a gate.
- Cursor: the word R+0x27FC2, 0 to 9 with wrap (`0x1409A710F` up,
  `0x1409A717B` down). `0x1409A82AB` maps it through the table at
  `0x14109C540` = 3, 0, 8, 7, 2, 6, 1, 9, 5, 4 to the stage ID at R+0x27FD1.
  Live: cursor 6 gave ID 1, then cursor 7 gave ID 9, on the same samples.
- Stage ID = the home stage of the fighter with that native ID. Evidence:
  the movement skips cursors 2 and 7 when stages are locked (global
  0x14261E3F9), and those are IDs 8 and 9, Kraken's and Valgas's, the two
  boss stages; and the collection's soundtrack lists the stage themes in
  fighter-ID order. This is an inference from those two facts, not a table
  read off the code.
- Names: each stage theme's title in the game's own text (labels 3816 to
  3828, SOUND_PWS1_07 to 19), without its track number: Londo, Mutsu,
  Tong-An, Manches, Dawnvolta, Dullstown, Oedo, Mahdad, Skullhaven, Avalon
  Is. Stage 1. They follow the game's language. If the text is not ready,
  nothing is spoken.
- To check a name by ear: Museum > Music plays each stage's theme by that
  title; the stage's music in the fight should be the same piece.
- Code signatures for the cursor, its bounds and the table read, and the
  table's bytes, are checked once before anything is spoken.

### Pickup announcements

User, 2026-10-27, consistent with PWS2: who picked up what kind of thing,
for both fighters ("Kraken, power stone"), through the shared
`src/pickup_speech.c` and the F9 setting (every fighter, players only, off).
The fighter's name is the shell roster's for its ID at A+4 (live: 8 Kraken
for the user, 3 then 6 for the CPU during the fights).

- A stone: the count A+0x77EC rising. Live replay: 5 stones gained, each for
  the right side (round two's setup stone is not fought, so not announced).
- A lifted thing: the carried pointer A+0x7780 naming a new pool object:
  type 1 something to throw, type 6 an item, or food for codes 15..17. No
  object was carried in the capture; tested on constructed samples only.
- Food eaten on touch: health rising 20 or more in one frame (food heals 24,
  48 or 96 at once; the game's own recovery steps by one), not after a lifted
  food within 3 s. No food appeared in the capture: the replay shows the rule
  does not fire on recovery, but eating itself is untested live.

### Tests

- `.\tools\run_pws1_frame_test.ps1`: 2,350 distinct trace frames, 581 fought
  pickup samples, the snapshots and every gate.
- `.\tools\run_pws1_select_test.ps1`: 190 selection samples through the
  shared channel, including retry and re-announcement on a new root.
- `.\tools\run_pickup_cues_test.ps1`: the ping scheduler (timing, stagger,
  distance, restarts) and the mixer's ping voices (owner, pan, decay, a
  semitone per type, families, volume).
- `python tools\sabotage_pws1.py`: 43 sabotages, each verified to fail.
- `python tools\make_pws1_fixtures.py --check` regenerates the fixtures from
  the local trace.

### Listening check

User, 2026-10-24: the Japanese version, Versus and Training work. One extra
screen was not spoken: the Versus stage select, now traced and spoken
(above).

Still to hear, after the pickup build:

- Each stone, item and prop pings from where it is, faster and louder as
  your fighter approaches; stones, food, items and props are told apart by
  sound; nothing pings while paused or once the round is decided.
- No fighter beacons.

PWS2 remains a separate four-actor integration: its health is a float at
actor+0x1DC, and its display layout ordinal can differ from fixed actor index.
None of the PWS1 offsets, caps or two-actor assumptions transfer to it.
