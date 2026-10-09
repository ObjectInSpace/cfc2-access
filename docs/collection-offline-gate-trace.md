# Offline entry, flag provenance and native copy boundary

2026-10-09. Continuation of the [Quick Load completion trace](collection-quickload-completion-trace.md).
Static evidence from the same unpacked image; code SHA-256 remains
`9c340b8ce40a0c15a3eccd3c47207b02a59e38365bf99ae7605afc26bb84f95b`.
Reproduce with `python tools/collection_offline_gate_audit.py --manifest-out docs/collection-offline-gate-manifest.json`.
The audit retains fourteen instruction-aligned spans, exact dispatch entries,
and the relevant local `msg.arc/menu_eng` label records. Full instructions
are in `build/collection-offline-gate-audit.json`.

This establishes a positive **Offline Play / Arcade Mode** entry contract for
a future CVS2 pilot. It does not implement support or certify a protected
native copy. The best evidenced completion candidate remains the synchronous
return from screen-manager update, after deferred replacement. Its thread and
allocation-lifetime protection remain unresolved.

User scope clarification, 2026-10-09: **Arcade, Versus and Training are all
required supported modes.** The Arcade-only gate below is an intermediate
research contract, not the product scope or completion criterion. Versus and
Training remain required implementation and validation work. Their checkpoint
consumer bypass flags must not become blanket exclusions from accessibility
support; each mode needs its own evidenced readiness, mutation and copy path.

Use O for `*0x141c14f90` (shell owner), S for `*0x141c14f98` (session),
and M for the screen manager. O and S are different allocations; equal numeric
offsets on them do not identify the same field.

## Positive entry route

The existing live-text mapping in `src/menu_speak.c` identifies the menu at
callsite `0x140097a59`: base index 37, six items. The local English GMD records
identify index 37 as `OFFLINE_PLAY`, text `Offline Play`. This uses established
label provenance, not item-count inference.

At that callsite the selection helper receives menu-object+0x330. On a later
confirmed-selection state, `0x140097b38` reads the same field, guards unsigned
index <=5, and dispatches through RVA table `0x140098098`. Exact entry zero
targets `0x140097b54`. That branch clears EBX, writes O+0xd0320=0 at
`0x140097b5d`, and O+0xd0240=4 at `0x140097b6a`. Other menu entries write
different modes; this trace does not assign all their downstream semantics.

The parent update is **0x14003dbe0**, not 0x14003db00. Padding-based entry
searches can conflate those routines because the earlier routine ends in a
jump table. The retained span starts at the actual later prologue.
At `0x14003deea..0x14003df08`, the parent dispatches O+0xd0240 minus four.
Exact request-4 slot `0x14003e81c` targets `0x14003dfd7`, which sets parent
state 4. State-4 slot `0x14003e7f4` targets `0x14003e010`, constructing the
game selector through `0x1400b13f0`. Its table `0x140feeda0`, virtual +0x40,
connects to update `0x1400b1510`, which calls `0x1400b1b10` to handle selection.

Ordinary game confirmation at `0x1400b1d29..0x1400b1d78` clears
O+0xd0330 and O+0xd0338. It chooses state 3 when O+0xd0320=0, state 2
otherwise. State 3 creates a separate mode selector through `0x1400b2530`
at `0x1400b175b`. That object's table `0x140feefd8`, virtual +0x40,
connects to `0x1400b26a0`.

The mode selector reads the highlighted row from object+0x338, then obtains
its numeric **mode code** from object+0x340+4*row. The row number and mode code
are distinct. Its label renderer `0x1400b2950` performs the same lookup and
indexes the dword label table at `0x1412084d8` with the resulting code.
The exact four table entries and local GMD labels are:

| Mode code | GMD index / label | Writes on mode confirmation |
| --- | --- | --- |
| 0 | 4105 / MODE_ARCADE / Arcade Mode | O+0xd0330=0, O+0xd0338=0 |
| 1 | 4106 / MODE_VERSUS / Versus Mode | O+0xd0338=1; O+0xd0330 stays 0 |
| 2 | 4107 / MODE_TRAINING / Training Mode | O+0xd0330=1, O+0xd0334=0 |
| 3 | 4108 / MODE_TRAINING2 / Training Mode (Boss) | O+0xd0330=1, O+0xd0334=1 |

The confirmation block at `0x1400b2789` first requires substate zero and
clears both option bytes. Subtract/branch operations at `0x1400b27b9`
and `0x1400b27be` distinguish codes 1, 2 and 3; code 0 retains the clears.
An unknown code can also fall through with both bytes clear. Therefore the
positive record must retain **code == 0**, not infer Arcade from those clears.
The reviewed constructor builds codes 0, conditionally 1, and 2. The code-3
handler and label do not prove that CVS2 exposes that choice on this route.

Following successful child completion, the game selector sets its +0x2a1
completion byte at `0x1400b1a7e`. The parent observes child completion and
sets state 13 at `0x14003e11d`. Exact state-13 slot `0x14003e818` targets
`0x14003e7b4`, which passes the aGame descriptor `0x141c049d0` to manager
virtual +0x50 at `0x14003e7d1`. This is a deferred request, not readiness.
Entry records need correlation through these objects and the resulting game
identity; reaching any one shared helper is insufficient.

## Session flag writers

`0x140037a60` prepares screen/session state and calls `0x140038280` at
`0x140037c3d`. These give the following precise provenance:

| Field | Writer and derivation | Consequence |
| --- | --- | --- |
| S+0x7b4, dword | `0x140037ae9`: O+0xd0320 == 1 | Not a universal online flag |
| S+0x7b4, dword | `0x140037b8a`: clear when mode==1 and O+0xd0328==4, set S+0x7f8=1 | A special mode-1 path passes this exclusion |
| S+0x7b4, dword | `0x140037bc2`: clear when initially nonzero and signed O+0xd037c>=2, set S+0x7c8=1 | Another mode-1 path passes this exclusion |
| S+0x7bc, dword | `0x140037b1e`: zero-extended O+0x1cd | Session copy of the callback byte; check the current owner too |
| S+0x828, dword | `0x140038501` clears; if O+0xd0330!=0, `0x140038533` writes O+0xd0334+1 | The traced Training and Training (Boss) codes produce 1 and 2 |
| S+0x80c, dword | `0x140038631` clears; if O+0xd0338!=0, `0x140038654` writes 1 | The traced Versus selection sets this bypass |
| S+0x1a8, qword | CVS2 initializer `0x1406a9900` writes zero, with EDI cleared at `0x1406a9839` | Initialization/import check, not an offline mode label |

These are reviewed writers and their control flow, not an exhaustive alias,
bulk-copy or transitive writer inventory. The owner byte setter/exclusion
from the previous trace remains necessary. Unknown field values are rejected.
Do not assign UI meaning to unrelated objects with the same displacement.

The zero shell mode is also written in the parent's reset path at
`0x14003ddf7`; constructor defaults and other entry paths cannot be treated
as a recorded Offline Play selection. Similarly, game confirmation clears
option bytes before the mode dialog completes. A tuple sampled in either
window would be premature even if every exclusion passed.

The user's Vs. checkpoint-screen observation is distinct from the GMD
**Versus Mode** selection. The prior load fixture lacks these fields and
cannot retrospectively establish either Arcade or Versus mode. No new load
is required to re-prove that already-captured Quick Load transaction.

## Narrow positive gate contract

For a future CVS2 Quick Load pilot, admission requires all of the following:

1. A correlated Offline Play row-zero entry and explicit Arcade **code-zero**
   confirmation, followed by the traced aGame request and validated CVS2
   initialization. The record carries the expected revocation label and game/
   ROM identity. Missing records, unrecognized entry paths and counter loss
   leave admission false. An all-zero snapshot cannot create this record.
2. At the protected copy point, O+0xd0320==0, O+0xd0330==0,
   O+0xd0338==0, O+0x1cd==0 and S+0x7bc==0. Require the three restore-bypass
   dwords to be zero and S+0x1a8==0 for the secondary-import path. These are
   consistency checks on the positive route record, not substitutes for it.
3. Validated build, game/ROM, current screen/session/root identity and bounded
   readable observation fields, checked under the same native lifetime
   protection as the copy. A worker must not dereference the retained pointers.
4. For a pending Quick Load, a new owner generation and a positively observed
   restore branch through local repairs and all enclosing work. Save, bypass,
   initializer return, unchanged addresses, or flag clears cannot satisfy it.

This describes only the intermediate Arcade Quick Load pilot, not completed
support for the required Arcade, Versus and Training modes or all CVS2 ROMs.
It leaves the established CVS1 milestone scope unchanged. Quick Load must
carry forward the validated route provenance under a fresh transaction label,
then revalidate it; it must not reuse the old owner generation. Departure to
the shell or a different/unknown mode discards that provenance. A future
implementation must observe intervening mode changes, not trust a historical
entry forever.

## Copy boundary after enclosing updates

The ordered completion chain is:

`confirmed 0x1406aabe6 restore branch -> local repairs/return -> native update
-> CVS2 post-update wrapper -> 0x14003a290 including queue drain -> screen
update and 0x140039ed0 -> screen-manager traversal and deferred replacement`.

The manager at `0x1401a6200` calls each screen's update (+0x38) at
`0x1401a624e`. After traversal it can invoke replacement (+0x58) at
`0x1401a6288`. Replacement can call game-screen release and deletion at
`0x1401a6566/0x1401a6579`. The manager then clears its pending descriptor
at `0x1401a628b` and returns at `0x1401a62a5`. An earlier screen-update
return cannot account for that departure. A cleared pending descriptor also
does not prove that no replacement happened: it is cleared after replacement.

**Candidate placement:** a synchronous post-original observer around the
manager-update entry, before returning to its caller, and only on the
outermost admitted invocation. At that point re-resolve the current ready
aGame screen from manager membership. Reject a departed/replaced owner,
including same-address replacement; do not read the root remembered on entry.
Newly constructed screens are not automatically initialized/eligible. Carry
the restore/initialization completion record, positive mode record, native
invocation identity and revocation label into one bounded copied observation.
The worker receives copied fields and labels only, using existing transports.

**Protection is not yet established.** The retained manager body contains
no explicit lifetime lock or root pin. Its virtual engine caller is unresolved;
a direct-call encoding search found no direct call/jump to this entry. This
does not establish a single native thread or exclude indirect/reentrant calls.
The previous live trace observed some operations on one thread but did not
enclose this manager. There is no defensible basis yet to call this a protected
native copy boundary or install it as a production observation hook.

To turn this placement into a protected boundary, establish either native
exclusive thread ownership covering every admitted mutation and destruction
path, or an actual lifetime guard shared by the copier and **all** such paths.
An observer-only lock, an event counter, thread-ID check, readable-page query,
or before/after revision comparison does not prevent a concurrent free.
Immediate revocation prevents stale publication/output but cannot replace
lifetime protection during the read. Do not suppress native cleanup merely
because a nonblocking observer lock is contended.

The next concrete research target is the engine consumer of manager table
`0x141018b60` virtual +0x30 (update), +0x28 (release-all at `0x1401a6390`)
and +0 (deleting destructor at `0x1401a5c10`). Relate those callers to direct
Quick Load cleanup at `0x14004ffd0` and the shell-owner arena release edge at
`0x1400485f6`, to establish common thread ownership or a lifetime guard.
A subsequent bounded
completion probe needs manager nesting, route/mode records, initialization,
positive restore/repair completion and revocation events, with a reviewed ABI.
Until then the proposed copy path stays disabled. No ordinary worker read or
arbitrary post-load update is promoted to eligible by this trace.

## Validation

`python -m unittest discover -s tools -p 'test_collection_*.py'` passes all
90 tests (79 existing plus 11 new). New controls reproduce the manifest and
reject mutations to menu dispatch, label indices/text, Versus branch polarity,
Training arithmetic, session-mode derivation/special clear, manager dispatch
and deferred replacement. These validate evidence extraction, not native
execution, hook ABI or concurrency. No DLL source changes, staging, activation,
game attachment or save-file operations were made.
