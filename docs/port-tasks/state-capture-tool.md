# Task: collection state capture and cursor-candidate analyzer

Decided with the user 2026-10-13, inside step 6 of the
[Opus execution plan](opus-shared-selection-execution.md). Not a new phase.

## Scope

- `tools/collection_state_capture.py`: one read-only capture tool for every
  traced game path (CVS1, CVS2, CFJ, both Alpha 3 paths, PWS1, PWS2, MJUS,
  STG2), selected with `--game`. Gates come from
  `docs/collection-lifecycle-manifest.json`: reviewed code spans, game and ROM
  identity, the ROM's initializer naming the path, callbacks, published root
  and size. It records:
  - a compressed trace of the published primary block (a full keyframe, then
    changed bytes only, at about 20 samples per second);
  - direction presses from the keyboard arrows and XInput pads, with times;
  - a game-window screenshot shortly after the cursor stops moving;
  - F9 labelled whole-block snapshots with screenshots, spoken via NVDA;
  - automatic character pictures only where a game has a traced cursor
    decoder. Today that is CVS1 only.
  It replaces `tools/cvs1_name_capture.py`, whose behaviour it keeps.
- `tools/cursor_candidates.py`: ranks the bytes of a trace that change right
  after direction presses and stay still otherwise.

## Rules

- The analyzer produces LEADS. A candidate becomes a game's cursor decoder
  only after the instruction that writes it is found in the code and the
  screenshots agree with what it decodes. This keeps the port plan's warning
  against cursor models built from keypress counts.
- A press at a grid edge may legitimately move nothing, so recall below 1 is
  expected. Quiet stretches with no input are the control that excludes
  timers and animation counters.
- Offsets are only comparable within one root. A root change starts a new
  segment.
- No hooks, no game-memory writes, no DLL changes.

## Field result, 2026-10-13

The first live run (CVS1, `tools/fixtures/cvs1-capture-20261013`) confirmed
the trace and the CVS1 selection decoder. It also exposed two gaps:
- The user's controller presses were never seen. Polling XInput from another process missed them, so no press overlapped the 51 s of CVS1 state.
- Screenshots were black in fullscreen.

Screenshots are now opt-in (`--screenshots`). Names no longer need them:
see [character-names.md](character-names.md). The cursor analyzer still
needs a press source the game actually uses. The candidate is each game's
own input record (CVS1 keeps one at actor+0x5BA, which the replay test
already uses). It stays unvalidated live until then.

## Acceptance

- Synthetic tests: a cursor byte ranks first. A timer, an animation counter
  and a byte that changes only during quiet time are excluded. Root changes
  split segments. The CVS1 capture behaviour is carried over.
- Replay of the retained CVS1 movement fixture: its real phase bytes, with
  presses taken from the game's own input record, must rank the traced
  selection byte P+0x78 first.
- Live control, in the user's CVS1 session: the analyzer must rank the traced
  fields at the top: selection byte root+0x3ED0 and group/position bytes
  around root+0x3F5C. If it does not, stop and report before using it on
  another game.
