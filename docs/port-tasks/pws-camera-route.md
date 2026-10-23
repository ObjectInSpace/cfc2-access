# Power Stone camera acquisition, 2026-10-22

First delivery increment for Power Stone 1 and 2: a reproducible stored-view
trace and an offline capture decoder. Neither game has production audio or
selection support yet. No game process was running during this investigation;
there are no new live captures, deployment, or listening results.

## Result

Both games load stored view matrices into the same renderer slot used by
Project Justice and Plasma Sword. Test this route for fighter beacons before
resuming the older PWS2 HUD shader/draw-hook investigation. This finding does
not solve fighter world anchors, HUD ownership, health ranges, or arena audio.

The retained [instruction evidence](../pws-camera-evidence.json) contains nine
initial bounded spans from the hash-checked unpacked image. The selection
continuation adds two position-consumer spans, for eleven total. Reproduce with:

```powershell
python tools/pws_camera_audit.py --out docs/pws-camera-evidence.json --check
python tools/test_pws_camera_decode.py
python tools/test_pws2_actor_audit.py
```

## Exact routes

The selector at `0x140F326E0` accepts slots 0..3 and computes
`context + 0x1F80AC + slot*64`. Slot 2 is therefore `context+0x1F812C`
(view); slot 3 is `context+0x1F816C` (projection).
`0x140F32440` copies 64 bytes into the selected slot. The context comes from
`0x142C79B40`. The shared composition at `0x140F33830` is already used by
`src/render_project.h`; this is instruction evidence, not a shape analogy.

### Power Stone 1

Root: `*0x1423188C0`, size `0x428F0`.

- `0x140996860` tests R+0x280C4. Zero tail-calls `0x1409971D0` with
  R+0x27948. The latter constructs a slot-2 matrix with `0x140F324D0`
  and copies it to record+0x2C and +0x6C. Only the +0x6C copy is then
  passed to `0x140F32190`. The untouched view is R+0x27974.
- The nonzero path repeats construction twice, stride 0x210, storing views
  at R+0x27974 and R+0x27B84. These are camera records, not player records.
- The draw path at `0x1409A8809` checks the same gate. Zero constructs
  projection in slot 3, selects slot 2, loads R+0x27974, then calls
  `0x1409A8A50`. Nonzero uses separate projection setup and two loads,
  R+0x27974 and R+0x27B84. Do not choose the last context matrix for this
  branch or label either pass as a player's camera without live evidence.

### Power Stone 2

Root: `*0x14262F0B0`, size `0x8CD40`.

- Existing transform evidence identifies four records, stride 0x308.
  `0x140A5BBE0` constructs the slot-2 matrix, copies it to record+0x38
  and +0x78, then transforms only the latter copy.
- At `0x140A5D649`, R+0x52FDC == 0 takes the ordinary draw branch.
  Signed byte R+0x52E88 chooses the matrix at R+0x51D90+index*0x308.
  Slot 2 is explicitly selected before the copy at `0x140A5D6B7`.
- The offline decoder accepts only indices 0..3. Nonzero layout gates
  retain all candidates without assigning a selected view. Existing
  alternate-pass and post-draw projection changes still matter: a context
  sampled later is not automatically the projection used for the fight.

## Capture decoder and next live exercise

`tools/pws_camera_decode.py` accepts a full captured root, including gzip
snapshots, and emits raw bytes, finite/affine diagnostics and the ordinary
branch's selected index. It never declares audio eligibility. An all-zero
matrix, invalid selector or alternate layout cannot become a certified camera.
Finite affine data alone is also insufficient to establish a visible scene.

Use the existing read-only collection capture with the new per-game plan.
Example, replacing 12345 with the running game's PID and recording the actual
mode/language (the following labels deliberately say unknown):

```powershell
python tools/collection_state_capture.py 12345 --game pws1 --out build/pws1-camera-live --label "camera investigation; mode unknown" --language unknown --plan tools/capture-plans/pws1-camera.txt --screenshots --minutes 10
python tools/pws_camera_decode.py --game pws1 build/pws1-camera-live/state-01.bin
```

For PWS2 substitute `pws2` in the game, paths and plan. In a second terminal
the existing context sampler can record renderer candidates alongside it:

```powershell
python tools/context_sampler.py 12345 --context 0x142C79B40 --offset 0x1F8100 --size 0x200 --out build/pws-camera-context --minutes 10
```

These independently timed reads can be torn or belong to different passes.
Pairing timestamps is diagnostic evidence, not frame synchronization. Retain
screenshots and context metadata; determine actual screen side from the images,
not by asking the blind player to infer unseen positions. A synchronized hook
is an escalation if this cheaper capture cannot resolve the ordinary path.

Next implementation work: trace each game's fighter position consumer and
selection cursor/name mapping; compare stored-view projection with observed
screen positions during walking, jumping and camera movement. Establish match
phase and player ownership before feeding shared audio. PWS1 is the first
two-player arena increment; PWS2's four-owner audio remains separate work.

## Validation

Eleven spans reproduced from the reviewed image. Eight decoder tests pass,
including every byte value of the signed PWS2 selector, both games' root-size
rejection, alternate layouts, distinct matrices, nonfinite floats and strict
JSON output. Seven existing PWS2 actor-audit tests pass. Runtime sources are
unchanged, so no DLL build or gameplay acceptance is claimed.

## Position candidates from the selection continuation

The decoder now retains separate fixed actor indices, their raw slot bytes
and raw/finite position triples. These indices are not camera indices.

- PWS1: R+0x18428+slot*0x7890; slot byte +3; position +0x30.
  The camera consumer `0x1409974F3..0x140997561` reads both fighters'
  X/Y/Z at R+0x18458/5C/60, with the other index computed as 1-index.
  That establishes camera-follow input; visible body anchoring needs a capture.
- PWS2: R+0x40818+slot*0x42B8; slot byte +1; position +0x3C.
  `0x140A5E0E4` reads the position in the renderer. That path can substitute
  actor+0x1E4 for Y depending on +0x1F8, and has special cases for IDs 12/13.
  Raw position is therefore a comparison candidate, not a universal render anchor.
