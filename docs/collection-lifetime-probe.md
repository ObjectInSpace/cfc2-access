# Bounded Training and wait-outcome diagnostic

Prepared 2026-10-12 after the independent static audit in `6ca5e51`.
This is diagnostic instrumentation, not a native selection producer or a
lifetime guard. The 2026-10-13 capture below records Change Character and actual
batch wait outcomes. Training reset and complete lifetime coverage remain open.

## Instrumentation and limits

`cfc2_lifetime_probe.on`, together with the existing unpacked-build and
`cfc2_hook.off` checks, selects schema 3. It takes precedence over the legacy
`cfc2_collection_probe.on` marker. Without the new marker, the six-site
schema-2 diagnostic still works as before; without either marker neither
diagnostic installs. The capture window remains 120 seconds after hook enable.

The existing bounded ring/worker file writer is reused. Ten native entry hooks
cover the original six sites plus application update, Training callback
dispatch, initializer dispatch and the complete pause-task update. Their whole
bodies are fingerprinted before installation. Four additional worker/join bodies
are fingerprinted without hooking them. Both wait IAT slots must match the
loaded KERNEL32 export addresses. Partial installation rolls back created hooks.
Trampolines are not freed if disabling fails.

The two API hooks forward every call and preserve all input arguments, the raw
DWORD result and last-error state. Only these return addresses emit wait records:

| Return address | Purpose |
| --- | --- |
| `0x1401f7da2` | Generic worker wake |
| `0x1401f847f` | Batch worker wake |
| `0x1401f80ad` | Batch completion join |
| `0x1401f81b9` | Generic worker retirement join |
| `0x1401f8208` | Batch worker retirement join |

Each new call pair has a per-thread monotonic invocation ID, caller return
address and current same-thread application invocation ID. A worker's zero
application ID is **not** joined to an application call by timing alone.
Legacy site records retain their stack-pairing fields and also carry the
application ID. Copied session metadata includes S+0x80c, +0x828, +0x830,
+0x838 and +0x840. These external observations are non-atomic; they do not
replace user-supplied mode/action provenance or validate initialization success.

Wait records retain count, timeout, wait-all, at most 64 handles copied before
the original call, the raw result and last error immediately after it. An
unreadable/oversized list is explicitly unavailable; the original still receives
the unchanged pointer/count. No handle is duplicated, waited on separately or
closed by the diagnostic. The owned handle values are identifiers, not lifetime
leases. The original pointer appears only numerically in the diagnostic CSV.

The 4096-record ring drops on contention/fullness and records the drop count.
The lifetime writer drains at most 128 records per worker tick with a 25-ms
minimum interval. The callback never writes the file or speaks. There is no
guarantee that every diagnostic event fits: inspect the footer and unmatched
records. Calls crossing the window may be unmatched, including sleeping
workers. Process shutdown can leave the capture without a footer; retirement
needs its own exercise and cannot be certified from this Training run.

The parser accepts schemas 1/2/3 and rejects malformed columns, invalid wait
callers/counts, partial readable handle lists and reused invocation IDs. It
retains per-pair outcomes even when the whole trace has loss/window-edge gaps,
but marks schema-3 pairing unreliable if any pair is unmatched. Classifications
follow Microsoft's [single-object wait documentation](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject)
and [multiple-object wait documentation](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects).
Last error is meaningful for WAIT_FAILED only. No join record is not a successful
join; caller-only batches execute no wait. A successful outer join still does
not prove generic descendant completion or native allocation lifetime.

## Validation and guard correction

The optimized unreadable-handle control initially crashed. Disassembly showed
the compiler had eliminated the guard's armed=1 store around the pure load/store
loop. A volatile TLS declaration alone did not retain it in this GCC build.
The guard now uses a volatile TLS flag **and compiler memory barriers** around
the guarded statement. The same failing control then passed. This is an
observed synthetic harness failure and repair, not an observed game failure or
a solution to concurrent native frees. The existing multithread guard harness
also checks 24,000 faults across eight threads.

The probe harness checks exact original forwarding, arbitrary 32-bit results,
last error, caller filtering, pre-call handle copies, unreadable memory,
nested guards, invocation/application correlation, every body guard, partial
installation, overflow/contention and footer ordering. It serializes a real
schema-3 CSV and parses it through the Python reporter. Its WAIT_FAILED/error-6
record is explicitly **synthetic**, not captured from the game. The runner links
its compiler runtime statically to avoid a toolchain DLL dependency.

121 collection Python tests and all 35 native test runners pass (each runner's
exit code checked). The DLL build and import checks pass; existing unrelated
compiler warnings remain. The span harness now expects guarded memcpy to
recover, with a readable-copy control, replacing its historical failure
expectation. The native manifest now contains 87
spans, adding the contiguous pause body ending before its dispatch tables.
The historical three-mode captures remain unchanged with 73 verified spans.

Retained offline validation: [`collection-lifetime-offline-validation-20261012.json.gz`](../tools/fixtures/collection-lifetime-offline-validation-20261012.json.gz),
SHA256 `08f959c0d2656909cbf14a63c7352b7095656d0a5178e754eed34934665109ff`.
It contains the native runner logs, build log and synthetic CSV/report; it is
explicitly not live-game evidence.

Staged 2026-10-13 while the game was closed, after the user confirmed they would
close it and wait. Installed DLL SHA256:
`f5fa279ec1c47fb9c251c64beb7f62ce77e52be3f101aed01c28997596a135aa`.
The prior DLL (`cc259d9d107d2ced154322bd09bbb62286b1423fae4f26980342f88dda3ef63f`)
was backed up under `build/deployment-backups/dinput8-<hash>.dll` and verified
before replacement. The new marker is enabled, the legacy marker is absent,
and the user has been notified to launch. Staging is not a completed capture.

## First live exercise

Stage the tested DLL and the new opt-in marker with the game closed, retaining
the installed DLL backup/hash. Keep the legacy probe marker absent. Record the
build hash, PID, ROM, user-confirmed Training mode and actual action order.

1. Launch normally and enter CVS2 Training within the capture window.
2. Reach a match and use the configured Coin Button reset. If time permits,
   exercise its directional modifiers and report which were used.
3. Open the pause menu, select **Change Character**, then reach selection again.
   Report if the action was unavailable, did nothing or did not reach selection.
4. Leave the game running until capture completion has been checked. Report any
   speech, control or performance change. Do not repeat Quick Load or recreate
   the already-retained selection inventories for this exercise.

Collect and retain the CSV, log, hashes, user action labels and reporter output;
remove `cfc2_lifetime_probe.on` before the next launch. Use:

```powershell
python tools/collection_probe_report.py CAPTURE.csv --out build/lifetime-report.json
```

Require actual matched wait results and the Training/pause/cleanup/init pairs
inside the relevant application invocation. Missing events remain missing
evidence. Do not manufacture the S+0x840==0 branch by modifying game memory.
Arcade, Versus, shutdown, signal outcomes, complete callback descendants and
overhead measurements remain separate work. The conditional 4096 counterexample
is not promoted to an active game failure by any of these diagnostics.

## Live Free Play Change Character capture, 2026-10-13

The user confirmed CVS2 Training in Free Play, using Pause > Change Character,
and returning to stage selection. Reported selection order: stage, groove,
character. Coin/reset was skipped; no Training reset was exercised. Speech or
control issues were not explicitly reported. PID 25264 used the staged DLL.
The log records capture start 11:35:13.825, Change Character speech 11:36:32.585,
stage speech 11:36:34.211, and capture end 11:37:13.819.

Retained [live evidence](../tools/fixtures/collection-lifetime-training-change-character-20261013.json.gz)
contains the original CSV, copied log, user provenance, hashes and full report.
Artifact SHA256: `ec4efb4e3632d12e6bc846921c23c00734cf565c63bc78159f4216481ec72175`.
CSV SHA256: `02b09966484d12b8a8f8c4ca1456fc307b09b04097897b31e0e2d9983c53319c`.
The footer reports **11,225 dropped records**. There are 311,693 retained events,
150,585 candidate pairs, 4,109 unmatched entries and 6,414 unmatched exits.
All records are readable, but `pairing_reliable=false` and `lifetime_proven=false`.
There is no global loss-free ordering claim.

Thread 14208 application invocation 24189 contains these matched boundaries:

| Boundary | Entry sequence | Exit sequence |
| --- | --- | --- |
| Application update | 181132 | 181190 |
| Pause update, invocation 24190 | 181135 | 181140 |
| Game cleanup | 181136 | 181137 |
| Initialization, invocation 24191, caller `0x1400a6c45` | 181138 | 181139 |

Initialization changes root `0x176c1000` to `0x13ac1000`, with game=1, ROM=3,
Training=1 and bytes=136880. Cleanup return still reports the former root;
that pointer value does not establish continued allocation validity. Reset-state
changes 0 to 2 across initialization; character gate remains 1. These are
reinitialization observations, not a reset-button exercise or atomic snapshot.

After pause return, five matched batch joins in that application invocation
(24192 through 24196) return raw 0: sequence pairs 181142/181149,
181158/181159, 181168/181169, 181172/181179 and 181180/181189. Each waits for all
four copied handles [3436, 3440, 3468, 3472] with infinite timeout at reviewed
return site `0x1401f80ad`, before application return. These are actual successful
API returns, not proof of every task descendant's completion or allocation lifetime.

Across the window, 105,772 matched batch wakes and 27,408 matched batch joins
all return raw 0 (signaled). No matched failed, timeout, abandoned, generic-wake
or retirement outcomes occur in retained pairs. Loss and unobserved paths
prohibit extrapolating to all waits. Worker application IDs remain zero and are
not assigned to application calls by timing. There are no Training-reset pairs.
Arena-mutating callback descendants and signal outcomes remain open. The
conditional 4096 bypass was not observed. The lifetime marker was removed after
capture completion; no restart is needed for this retained evidence.
