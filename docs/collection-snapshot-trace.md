# Shared snapshot replacement and repeated updates

Offline evidence for the pinned executable in the lifecycle audit. Reproduce:

```powershell
python tools/collection_snapshot_audit.py --manifest-out docs/collection-snapshot-manifest.json
python -m unittest discover -s tools -p 'test_collection*py'
```

The manifest pins code spans; the full instruction report goes to
`build/collection-snapshot-audit.json`. No runtime hook or live observation is
claimed. S means the pointer loaded from `0x141c14f98`.

## An additional state replacement path

`0x1400b6570..0x1400b65de` clears a scratch snapshot and registers S+0x1b0
as a state pointer with length S+0x1b8, provided both are nonzero. All nine
reviewed game setups publish those fields. Registration writes pointer table
`0x14211ca90`, size table `0x14211cb90`, count `0x14211cc10` and total size
`0x14211cc14`. This invocation resets count and registers at most one block;
the consumers themselves loop over the count. Registration retains the raw
pointer. Its lifetime across reinitialization still needs proof.

- Load scatter, `0x1400b65e0..0x1400b6663`, copies snapshot+4+running offset
  into each registered live pointer. The copy call is `0x1400b6632`.
- Save gather, `0x1400b6670..0x1400b66f3`, copies those live blocks into
  snapshot+4+running offset. The copy call is `0x1400b66c2`.
- Load callback, `0x1400b6d50..0x1400b6dba`, copies the incoming source into
  scratch `0x14211ce50`, invokes scatter, then increments S+0x76c. It returns
  AL=1. The incoming signed length is extended from EDX; this body supplies
  no independent bounds validation and is not an externally callable API.
- Save callback, `0x1400b7130..0x1400b718e`, allocates 0x100004 bytes, gathers
  state and copies scratch into the allocation. The four-byte prefix is
  outside the registered game block.

These loads bypass the nine game-specific restore routines. Root-address
equality cannot establish that a prior accessibility snapshot remains current.
S+0x76c counts this callback's completion; it is not a universal session epoch.

## Callback ownership and replay

Registration prefixes at `0x1400b6890` and `0x1400b6aa0` construct the same
seven-pointer callback layout. Relative to the callback array, save is +8,
load +0x10 and advance +0x28. They pass it to `0x1400b7700` and
`0x1400b7780`, respectively. The associated constructors copy the array into
their objects at +0x18. The first constructor additionally passes a copy to
`0x1400ba690`, which places the callbacks in a replay object at +8. Thus that
object's save, load and advance slots are +0x10, +0x18 and +0x30.

Two reviewed routines actually consume those slots:

- `0x1400ba2c0..0x1400ba3e0` takes an earlier frame number, sets S+0x770=1,
  selects a snapshot from ten records of stride 0x18, invokes load at
  `0x1400ba33e`, adjusts input histories, and invokes advance in a loop at
  `0x1400ba3aa`. The loop count comes from the previous frame minus the
  requested frame. S+0x770 is cleared at `0x1400ba3d0` after the loop.
- `0x1400ba3e0..0x1400ba548` selects a frame through input-history helpers
  and performs the corresponding load at `0x1400ba4a2` and advance loop at
  `0x1400ba512`, bracketed by S+0x770 writes at `0x1400ba459` and
  `0x1400ba52e`.

This establishes restore-and-replay behavior locally. It does not establish
every caller's UI label, networking mode, or exception/thread behavior.

The advance callback `0x1400b6cb0..0x1400b6d3d` calls the shared input bridge
`0x1400b6700..0x1400b682e`. That bridge increments the snapshot prefix,
maps four input words through S+0x258..0x264, masks them to 24 bits, and
tail-jumps through S+0x10. This is the same per-game update slot used by the
shell. Consequently, the existing CVS2 native-update detour can run during
these replay iterations.

## The flag is broader than replay

The complete shell update routine `0x14003a290..0x14003a79d` also writes
S+0x770. Before each S+0x10 call it sets that flag from the signed comparison
S+0x798 > 1, then decrements S+0x798 after the call and clears the flag.
The loop can repeat. Hence +0x770 marks intermediate work in more than one
path; it must not be labelled exclusively as a network or rollback flag.

Polling this flag from the speech worker is insufficient synchronization:
the worker can miss an entire restore/replay sequence, and the shell clears
the flag between iterations. A zero value also does not prove that state is
stable while another thread copies it. The native update boundary and the
outer batch boundary must participate in publication.

## Outer callback completion and close

The expanded audit pins 31 spans and six exact virtual slots. The common
shell update calls `0x14003a7a0` at `0x14003a30e` on its callback-driven
branch, then joins the ordinary path at `0x14003a767`. This branch is
selected through the shell owner, which is distinct from S.

The callback-driven routine registers one of the two callback objects on
initialization. On later invocations it polls at `0x14003abcf`, then calls
`0x1400b6b80` at `0x14003ac2a`. That routine obtains inputs, calls the
native-update bridge at `0x1400b6bf1`, and calls the object's finish method
through `0x1400b7680` at `0x1400b6c37`.

The primary object's table is `0x140ff02d0`; poll, input and finish occupy
+8, +0x20 and +0x28. Its poll routine `0x1400b9020` calls the selected
restore/replay routine at `0x1400b915a`. Crucially, finish `0x1400b9520`
calls poll again at `0x1400b9541`. Therefore the normal native-update
return can precede another replay in the same shell update. Capturing there
and publishing immediately can publish state that is about to be replaced.

The secondary object's table is `0x140ff0390` with different implementations
in the same slots. Its finish `0x1400b9c40` also calls its poll slot, then
performs more processing. The primary object's replay schema must not be
assumed for the secondary object merely because the callback layout matches.

The shell path also invokes a secondary callback and finally tail-transfers
to `0x14006d530`, which drains a native work queue. The outer return is a
candidate observation boundary, not yet a proven presentation or thread
boundary. The inspected direct caller at `0x140039430` continues into
`0x140039ed0` afterward; that caller continuation is a remaining lead.

Close is explicit: `0x1400520b0` with DL=0 calls `0x1400b6830`, which
invokes virtual destruction on the callback object at `0x14221ce58` and
then zeros that slot. It clears scratch but does not clear the registered
root pointer table in its reviewed body. The shell-owner mode byte +0x1cd
is cleared after close returns. This does not establish that every possible
callback reference has ceased or that teardown cannot race a worker.

## Consequences for the shared architecture

Keep three independent concepts:

- Owner generation: changes when ownership/lifetime changes, invalidating
  all work from the departed game even if an address is reused.
- State revision: invalidates observations across an in-place restoration
  within the same owner. It does not automatically re-arm every speech event.
- Publication sequence and eligibility: describe a completed observation
  permitted to reach output. Intermediate replay/batch updates are ineligible.

Speech acceptance and event/visit deduplication remain separate. Replaying a
frame must not make an already accepted line new merely because the state
revision changed. Continuous audio should receive the resulting eligible
state while preserving the native frame timing required by CVS2. Moving
health sampling to the worker would not solve the ownership problem.

Pending proof: all callback consumers, root-registration lifetime, outer
completion versus actual presentation, teardown ordering, thread identity and
hook ABI. These are integration prerequisites, not reasons to treat polling
or a copy-hook return as sufficient. No deployment is justified by this audit.

## Callback-mode entry continuation, 2026-10-07

The audit now retains 39 spans. The shell mode byte is owner+0x1cd,
where the owner comes from 0x141c14f90, distinct from S at 0x141c14f98.
The outer update tests this byte at 0x14003a305. Setter 0x1400520b0
with DL=1 writes it; DL=0 takes the already-traced close path first.

Type registration 0x140027010 binds literal aGame2Match at 0x140fde918
to descriptor 0x141c14be8 and factory 0x140fde938. Factory+8 creates
an object with table 0x140fde958. Its +0x20 getter returns that descriptor;
its +0x30 initializer is 0x14003c800. The initializer passes DL=1 to
the setter at 0x14003c93c, then requests aGame descriptor 0x141c049d0
through screen-manager virtual +0x50. That request is deferred.

The upstream type is uUiJoinMatch, literal 0x140fe9fb8. Registration
0x1400282b0 binds descriptor 0x141d1c098 and factory 0x140fe9f68.
Constructor 0x14008f220 installs table 0x140fe9fc8; its +0x40 method
is 0x14008f390. This body dispatches on object+8 through the RVA table
0x14009027c. Entry 12 targets 0x140090079. After a timing/preparation
path and a zero result from 0x14004c810, it requests descriptor
0x141c14be8 at 0x140090137 through manager+0x50 at 0x14009018b.
The intervening owner+0xd0380 branch affects copied settings; both branches
reach the same request. These names are literal native type identities,
not reconstructed visible menu labels.

This establishes a join-match route into callback mode. It does not prove
which visible menu action reaches state 12, that every match uses this route,
which callback implementation is selected, or that a match necessarily loads
a snapshot. The bounded next capture candidate is controlled match entry to
observe registration; load/replay needs its own positive callback evidence.
Quick Load and training reset are not established substitutes.

The [handoff contract](collection-handoff-contract.md) combines these limits
with the observed release/cleanup order. No runtime integration is enabled.

## Current verification

The expanded extraction includes negative controls for callback enable,
constructor identity, the native type literal, join-state dispatch and the
requested descriptor. The full collection group passes 58 tests. No DLL source
changed in this continuation, so no new build or deployment was performed.

## Historical verification

Nine tests reproduce the manifest and reject mutations to copy direction,
payload offset, registered callback address, replay flag, load callback slot
and repeated advance slot, plus final polling and the secondary virtual slot.
The collection group passes 38 tests. These test
the retained evidence contract, not live execution or thread safety.
