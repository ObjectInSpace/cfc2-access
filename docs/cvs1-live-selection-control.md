# First live CVS1 selection-state control

Captured with `ddefd10` from PID 12508 after the user reported reaching
CVS1 selection. These are real ReadProcessMemory observations, not synthetic
fixtures. No deployment, hooks, asset reads or speech changes occurred.

The game identity, all four callbacks, primary publication and phase-pointer
checks passed. Both manifests' 106 code-span checks and the capture tool's
dispatch/descriptor checks passed before output was created. This is bounded
signature validation, not a full live executable hash.

## Retained evidence

The original JSONL files remain under `build/cvs1-selection/`. Their filenames
contain 20260927, but the recorded UTC timestamps are 2026-09-28 (the prior
evening in America/New_York). Use the timestamps for chronology.
Lossless gzip copies are committed for reproduction:

- `tools/fixtures/cvs1-control-20260928.jsonl.gz`: eight samples from
  01:51:54.760792 to 01:51:56.517397 UTC. Uncompressed size 75,508 bytes;
  SHA-256 `cd05ea053f08eb6ffdb3daea22613a89a2925e7e58d5d5297619ddd8bca613c2`.
- `tools/fixtures/cvs1-movement-20260928.jsonl.gz`: 120 samples from
  01:52:11.928201 to 01:52:41.776761 UTC. Uncompressed size 1,085,883 bytes;
  SHA-256 `0fa05de5ba0c020b5891cbf010c75ad15047aa5098174c34a6e5c82e605ce034`.

The second label describes the requested action, not confirmed input: the
assistant asked for one move, a pause, then a move back without confirmation.
The user's actual action timing/control and visible character were not yet
reported when this note was written. Do not call it a verified movement test.

## Observations and limits

All 128 samples have equal before/after observations, no read errors and
`outside_scope` status because the reviewed branch-B phase gate did not pass.
All have zero asset rows. `outside_scope` is expected for this different
numeric mode; it is not a signature or identity failure.

Observed identity is game 0, region 1, ROM index 1, name `cvs1u`.
R=`0x148c1000`, primary size 0x32ab0; secondary=`0x148f3ab0`, size 0x32ab8.
Common context=`0xfac1000`; phase pointer=`0x148c4e58` = R+0x3e58.
The control records context buffer=`0xfec1000` and resource base=`0x106f6000`.
These are run-specific observations, not addresses to hard-code.

The movement window retained phase bytes 2,1,1,6 and native mode 0.
Side 0 retained owner 0, slot 0, selected byte 16, actor byte 16, state 4,
activity 0. Side 1 retained owner 1, slot 0, selected byte −1, actor byte 1,
state/activity 0. These selected/actor/phase values did not change across
the 120 samples. This does not show that movement was attempted or processed,
nor that value 16 names any particular character.

The offline mode dispatch sends mode 0 to branch A. Its P+3=6 route is
the existing lead `0x1405DBC30`, which dispatches further on P+4. Do not
apply branch B's state=6 or asset contract to this live control. Further
branch-A tracing and user action provenance are required.

On resumption, all 120 movement rows were present, but the previous shell
process handle was unavailable and no CFC2 process was running. Thus the
completed file is verified; the original capture process exit code was not
recovered. The user was asked to clarify the earlier action and return to
selection for a new window when ready.

Next: obtain the actual mode/action label, then record a coordinated movement
window with enough time to return focus to the game. Keep the asset gate
unchanged until the active branch's consumer is traced. This live control
does not validate character names, visibility, audio or lifecycle epochs.
