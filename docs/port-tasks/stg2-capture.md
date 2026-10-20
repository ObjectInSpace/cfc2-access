# Plasma Sword capture and implementation status

Prepared 2026-10-20. The collection shell already handles this game's menus,
move lists and EX Settings.

Update 2026-10-20: the Arcade capture (`tools/fixtures/stg2-arcade-20261020`)
and a Training capture with the renderer's context sampled beside it
(`tools/fixtures/stg2-training-20261020`, plan `stg2-camera.txt`) are traced
and implemented, built and deployed, awaiting a listening check:
- character select speech, [stg2-selection.md](stg2-selection.md);
- match audio (health, plasma gauge, beacons, clock),
  [stg2-match-fields.md](stg2-match-fields.md).
Still open: a Versus capture, Training's select screens, the stage menu,
crouch and side step, and all arcade text (story, quotes, endings,
credits).

The lifecycle manifest identifies both ROMs (14 `stg2`, 15 `pswd`) with
initializer `0x140CCB1C0`, root slot `0x142BE6370`, primary size `0x511B8`,
frame callback `0x140CCB440` and update `0x140CCCB60`. These are acquisition
anchors, not a validated player or screen schema.

## Capture procedure

The user plays without a monitor. Do not request or capture screenshots.
Use `tools/collection_state_capture.py` with `--game stg2` and one of the
plans in `tools/capture-plans/stg2-*.txt`. Run on the interactive desktop
(outside the agent sandbox) so F9 and NVDA work. Use a fresh output directory
for each run. For example, from the repository root:

```powershell
python tools/collection_state_capture.py GAME_PID --game stg2 --out build/captures/stg2-arcade-3 --label stg2-arcade-3 --language unknown --minutes 15 --plan tools/capture-plans/stg2-arcade.txt
```

Replace `GAME_PID` with the running game's process ID. The tool verifies the
retained build signatures before recording and records only the identified
game's published block. It uses read-only process access. F9 saves a block,
then announces the next label through NVDA. Continuous state deltas and
direction inputs are recorded at approximately 20 Hz. Ctrl+C stops and
closes the trace.

Do Arcade, Versus and Training as separate captures. If a described screen
is skipped or an action is unavailable, report that fact: plan labels are
instructions, not proof of what was captured. Do not infer screen identity
from a label without tracing the code and checking the user's actions. For an unexpected screen,
take an extra snapshot and record which step diverged.

## Required follow-up

- Trace screen gates, both players' highlight writers and ID mappings.
- Trace health, plasma resource, clock and camera projection; distinguish
  side stepping, jumping, camera movement and character transformations.
- Inventory title, prompts, selection, results, continue, quotes, story,
  endings and credits in both ROM versions; distinguish sprite text from
  strings and shared shell screens.
- Implement readers through the existing selection, text and match-audio
  channels, with pause, mode-switch and stale-state invalidation.
- Retain compact real fixtures and code evidence with regression tests;
  verify audible behavior in play before claiming full support.
