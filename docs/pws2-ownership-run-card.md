# PWS2 ownership investigation capture

Prepared 2026-10-02. The capture tool is implemented and tested with synthetic
memory. No live capture, HUD mapping, camera selection, listening check or
production audio integration has been performed. This is a preliminary
ownership investigation, not the stage-synchronized projection probe.

Validation: audit/manifest generation exited 0 with 64 spans; seven actor-audit
tests, six ownership-capture tests and eleven existing metadata-capture tests
passed. Capture CLI help and `git diff --check` exited 0. Runtime sources were
unchanged, so no DLL build or runtime regression result is claimed.

## Capture later

Launch the reviewed game build through Steam and enter PWS2. Record the mode,
ROM language, selected character and actual controller assignment. Obtain the
game process ID. From the repository root, substitute that ID for 12345:

```powershell
python tools/pws2_ownership_capture.py 12345 --out build/pws2-idle.jsonl --label "P1 idle; character and HUD location recorded separately" --mode "actual mode" --language "actual language" --count 40 --interval 0.25
```

Use a new output path for each exercise. The tool refuses overwrites and opens
the process with read/query rights only. It does not launch, inject, hook,
write game memory, deploy DLLs or generate audio. Each sample is bounded to
35 reads and 72,232 bytes when all metadata is readable and identity matches.
The CLI limits the nominal sampling schedule to 30 seconds and 120 samples;
read and disk overhead add to that duration.

Both lifecycle and actor code spans must match their manifests before sampling.
The identity gate checks session, game/ROM/region, published root and size,
and callback addresses. Null, unknown or inconsistent identity never authorizes
root-record reads. Neither identity checks nor matching metadata establish an
active match. A stale session can still describe the prior game.

The file retains raw fixed actor records, transform records and current/copied
input records, bracketed by collection metadata. Labels are supplied by the
operator; they are not inferred from records. Each file records tool and
manifest hashes, checked spans and UTC timestamps. A `capture_complete` footer
means the requested sampling loop ended; it does not mean valid gameplay
evidence. Missing footer means interrupted/incomplete output. Any identity
change or read error disqualifies that sample from ownership comparisons.

Matching metadata cannot rule out within-frame changes, same-address rebuilds
or temporary identity changes between reads. Raw records may be torn. No
current-matrix snapshot is taken: sampling it off-thread would not establish
which draw pass produced it. Every sample has `audio_eligible: false`.

## Labelled exercises

- Idle baseline, then move only the known human participant. Keep input device,
  selected character and visible HUD position in the notes. CPU movement is
  a confounder; it must not be mistaken for human ownership.
- Repeat with another controller slot where the selected mode supports it.
  Actor fixed index, input slot and HUD location must be compared independently.
- Take damage to one identified participant, then another. Record the visible
  health changes. Numeric correlation is a lead; trace the HUD consumer and
  value writers before assigning a health field or range.
- Exercise elimination/rejoin, character change and round restart where
  available. Check whether record addresses are reused for new participants.
- Capture pause/menu and game-switch controls. A passing identity gate in a
  paused or inactive screen must not be interpreted as audio eligibility.

Visual HUD descriptions must come from an actual screen observation, not a
guess from memory values. A screenshot/video observation can be paired with
the labels later. Do not ask the blind player to infer unseen HUD state.

## Remaining production gates

- Trace actor callback publication, HUD consumers, value/range writers and
  the complete transform-vector producers. The mode-zero selector-2 link is
  a transform target relationship, not a player/HUD mapping.
- Implement and test a draw-stage probe with a reviewed hook ABI. Retain
  actor index, transform index, pass ID, current slot, all relevant matrices,
  input vectors and lifecycle information together at the actual consumer.
  The raw tool above cannot supply those samples or certify frame coherence.
- Resolve visible pass/body anchor with labelled captures and camera movement,
  including repeated passes and stage transitions. Do not choose first/last
  draw or copy a post-draw matrix to an earlier draw.
- Implement shared session invalidation and presentation ownership before
  connecting PWS2. The current production mixer supports two players and fixed
  left/right gauge pan; increasing an array to four is not an ownership design.
- Validate four-participant cue identity and HUD pan through listening. Exercise
  stale samples, pause, unload, same-address rebuild and dropped frames; these
  must silence/invalidate appropriate cues without changing CVS2 behavior.

Production integration remains open until those gates have evidence. Offline
projection arithmetic and synthetic captures cannot satisfy live acceptance.
