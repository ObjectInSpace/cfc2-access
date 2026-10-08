# Collection boundary event probe

Status: first switch capture completed; see
[the 2026-10-06 live evidence](collection-boundary-live-20261006.md).
The next exercise is the load-only CVS2 Quick Load capture below.
Snapshot-load/registration remains separate future callback work. The original
preflight record below is historical; the live evidence records the staged build.
This is a diagnostic step toward the shared router, not an installed router.

The next-build extension adds a sixth site for
[Quick Load](collection-quick-save-followup.md). It emits schema 2 and preserves
the operation's signed 32-bit result. The first switch capture used the
original five-site schema 1. The extension is not yet deployed or live-validated;
use the linked load-only exercise rather than repeating the switch.

## Next exercise: load an existing CVS2 quick save

Continuation from 6dc8a5e, 2026-10-08. The session and selection components
remain unwired; CVS1 names and cancellation/re-entry remain unresolved.
This exercise measures Quick Load transaction order, thread and address reuse.
It does not establish the later secondary-to-primary repair completion or
authorize capture at Quick Load return.

Before launch, stage the tested six-site DLL and opt-in marker while the game
is closed. The retained build SHA-256 is
`76596752567f14f636b6eb547c3d0650a76a4f2aa31cc9b6b1e071fb38942896`.
The local DLL hash was rechecked on 2026-10-08. The session runner passed 73
checks, selection passed 209, the probe runner passed, and all 66 collection
Python tests passed. Full build/regression evidence is in the
[Quick Load follow-up](collection-quick-save-followup.md#verification-of-the-six-site-diagnostic-build).
This continuation has not staged the DLL or enabled the probe.

1. Use an existing CVS2 quick save in its relevant offline mode. Record the
   mode and ROM; do not create or overwrite a save for this exercise.
2. Launch normally through Steam and enter that mode within the probe's
   120-second capture window.
3. Open the pause menu, choose the spoken **Load Quick Save** caption, and
   confirm loading. Do not substitute Change Character or a numbered row.
4. Report the success or failure message, the screen reached, and any change
   in speech, audio or controls. The reported Vs. screen checkpoint is the
   comparison to make, not an assumed result.
5. Leave the game open until capture completes. The assistant collects the
   CSV and log and removes the opt-in marker before the next launch.

If no suitable save exists, stop the load-only exercise rather than making
one incidentally. If the action falls outside the capture window, report that
limit; absence of an event is not evidence that loading avoided mutation.

Require a schema-2 `quick_load` entry/exit pair, its signed result, readable
game/ROM/root metadata where available, surrounding outer updates, and the
footer's loss/completion status. Preserve the user's actual action labels.
Missing snapshot-load or cleanup-wrapper events do not establish safety:
Quick Load calls cleanup directly. Do not resume ownership at transaction
return or infer readiness from an unchanged root address.

## Historical first-run question

The first run measures which threads execute the common outer update, game
cleanup and screen release during a CVS2-to-CVS1 switch. It records root
identity at those boundaries. A later run must exercise the shared snapshot
callback path to measure registration/load nesting. No snapshot-load event in
an ordinary offline run means that path was not exercised, not that it is safe.

The current probe records six complete, reviewed bodies:

- Outer update `0x14003a290`, one incoming object pointer, no used return value
  at its reviewed direct caller `0x140039430`.
- Snapshot load `0x1400b6d50`, incoming source pointer and signed length,
  returning the observed byte in AL.
- Root registration `0x1400b6570`, incoming scratch-buffer pointer. Both
  reviewed callers overwrite RAX after return.
- Game cleanup `0x14004adc0`, forwarded incoming object pointer; the routine
  dispatches cleanup and may tail-transfer to another helper.
- Screen release `0x140037140`, incoming screen-object pointer, whose
  conditional branch closes the callback object before game cleanup.
- Quick Load `0x14004ffd0..0x14005007b`, incoming shell-owner pointer,
  signed 32-bit result. Its direct cleanup, initialization and secondary restore
  are distinct from the snapshot-load callback. End address is exclusive.

The observer calls each original exactly once and preserves its incoming
arguments, the snapshot-load result byte, Quick Load's signed result and thread
last-error state. The remaining bodies have no deliberate result construction; their wrappers use
the void method contract inferred from the reviewed code. Live behavior and
all transitive callers are still unverified. Do not add sites using a guessed
calling convention or treat this probe as a general instrumentation framework.

## Bounds and output

Create `cfc2_collection_probe.on` beside the executable to opt in at the next
launch. `cfc2_hook.off` takes precedence. Absent the opt-in file, no probe hook
is installed and the worker tick does no work. Existing audio and speech
settings retain their meanings.

Install checks fingerprints of all six complete bodies before any hook is
created. The fingerprints detect an unexpected build or prior patch; they are
not cryptographic attestation. Failure disables capture and attempts to remove
the created hooks. If disabling a hook is refused, its passive forwarder and
trampoline are retained instead of freeing code a native call could still use.
Any installation failure is logged as a failure.

Callbacks use guarded, shallow metadata reads and a 4096-event buffer with
nonblocking acquisition. They do no file I/O, logging, speech or allocation.
Overflow and contention are counted. They do not copy the game state block,
call game commands or change input. Hooking necessarily patches the six
function entries; this is not the earlier external read-only metadata tool.

Recording stops after 120 seconds. The worker drains up to 128 events per
250 ms into `cfc2_boundary_<process>_<tick>.csv` beside the executable. It
waits for in-flight producers before writing the completion footer. The
detours then remain passive forwarders until process exit; this avoids freeing
a trampoline while an original call could still return through it. Remove the
opt-in file before the following launch when the probe is no longer needed.

Each event contains sequence, performance-counter time, thread, site,
entry/exit, outer-update nesting depth, read status, session/root pointers,
game/ROM IDs, root size, S+0x76c, S+0x770 and S+0x4654, plus the forwarded
argument pointer, length and load result. Argument buffers are not read.
Unreadable records can contain partially filled fields; the report ignores
their identity. Successful metadata reads still do not establish atomicity.

The capture starts after installation. Initial unmatched exits and final
unmatched entries can therefore be window edges. A crash, early exit or write
failure can leave a file without a footer. Missing events must not be filled
in from assumptions. Per-thread order is useful; cross-thread record order
alone does not establish synchronization of native state.

## Completed first user exercise (historical)

After the assistant stages the tested DLL and opt-in marker while the game
is closed:

1. Launch normally through Steam and enter CVS2.
2. Return to the collection menu and enter CVS1.
3. Return to the collection menu. Leave the game open until the capture ends.
4. Report which transitions completed and any audible or control regression.

There is no prescribed tap count and no need to repeat the earlier character
movement capture. The assistant collects the CSV and log, records the user's
actual actions, and removes the marker for the next launch. If these steps
exceed the window, the file shows what was observed; it does not prove the
unrecorded transition. A second run is only justified by a specific missing
observation.

## Offline verification and interpretation

```powershell
.\tools\run_collection_probe_test.ps1
python -m unittest discover -s tools -p 'test_collection*py'
python tools/collection_probe_report.py CAPTURE.csv --out build/boundary-report.json
```

The C tests use fake originals and MinHook stubs; no game is attached or
patched. They cover argument/result/last-error forwarding, nested events,
inactive forwarding, contention/overflow/wraparound, deadline handling,
nested fault guards, refusal of a changed final byte in every reviewed body,
rollback at every create failure and enable failure, worker output, and a
deliberately delayed producer racing capture completion.

The report rejects malformed records and failed-install files. Dropped or
unfinished captures mark call pairing unreliable. Separate parser tests cover
thread separation, partial identities and missing boundaries. Synthetic CSVs
under `build/test` are test artifacts, never live evidence.

Original five-site preflight: all 29 PowerShell runners, the 50 collection Python tests and
the DLL build must pass on the final source. Save the DLL hash with the run.
The existing unrelated DLL warnings remain visible in the build log.

Final offline check: all 29 runners and 50 Python tests passed; DLL build
succeeded. DLL SHA-256:
`876ab4052d21a222d495e1f3aa7e23ddd60a3a2a506b5adf0cbd3e69fc938981`.
The run summaries are `build/test/collection-probe-regression.json` and
`build/collection-probe-build.log`. No probe-source compiler warnings were
reported. These checks did not load this DLL into the game.

This run cannot prove that native memory never changes on another thread,
identify every reset command, validate actual presentation, or settle audio
latency. Those claims require their own evidence. Its intended result is a
labelled call timeline to decide the next capture/publication boundary.
