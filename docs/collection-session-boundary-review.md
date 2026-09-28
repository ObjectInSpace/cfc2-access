# Shared session boundary review

Offline continuation from b60d3bf and the pending CVS1 catalog work.
This is Astra architecture investigation, not an installed lifecycle hook.
Same pinned code SHA-256 as `collection-lifecycle-manifest.json`.

## Initializer entry coverage

The direct call/jump encoding scan for `0x14004c540` found seven candidate
calls. Each is now decoded from a preceding function-entry lead to the end
of the call instruction and pinned in the lifecycle manifest:

- `0x140038b1c`, prefix starts `0x140038af0`: calls `0x14004c6f0` first.
- `0x140038fd1`, prefix starts `0x140038f90`: a conditional path increments
  S+0x7dc, calls cleanup `0x14004adc0`, then `0x140038280`.
- `0x14005005b`, in the existing `0x14004ffd0` span: guarded S+0x20
  cleanup followed by `0x14006fc20` before initialization.
- `0x14008c9e1`, prefix starts `0x14008c8a0`: calls `0x14006fa90` and
  cleanup `0x14004adc0` on the reviewed path.
- `0x1400a6c40`, prefix starts `0x1400a65e0`: cleanup, clears S+0x4f4
  and S+0x830, increments S+0x7dc, then initializes.
- `0x1400b0fa3`, prefix starts `0x1400b0d80`: calls `0x14006fa90` and
  cleanup on the reviewed path.
- `0x1400b49dd`, prefix starts `0x1400b4900`: likewise calls
  `0x14006fa90` and cleanup on the reviewed path.

These are bounded prefixes, not complete caller control-flow graphs or
identified UI commands. Conditional branches can leave the prefixes. The
encoding scan does not enumerate indirect callers. No live event labels
or coverage claims follow from the seven direct sites.

The complete dispatcher `0x14004c540..0x14004c674` clears fields on its
incoming object, dispatches by S+8 with unsigned <=7 guard, and invokes
the nine already-traced initializers. Most branches tail-jump; alternate
Alpha 3 calls its initializer and then sets incoming-object+0xded58=1.
The invalid-game path sets S+0x280=-1 and returns. A dispatcher return
alone therefore does not establish a valid, ready adapter.

S+0x7dc is incremented at two reviewed sites, but the dispatcher contains
no direct write to that displacement. Other helpers and writers remain
untraced. This field is not approved as a universal session epoch.

The `0x14004c6f0` preparation helper tests S+0x48: values 1 or 2
tail-jump to `0x140047270`; other values return. Its target remains open.
The cleanup helper is now pinned from its actual entry `0x14004adc0`,
including stack setup, through the existing exclusive end `0x14004ade9`.

A scan also found a candidate caller of `0x14004ffd0` at `0x1400a717a`.
The heuristic enclosing-entry decode did not reach an aligned endpoint;
that site remains an unverified lead, excluded from the pinned entry list.

## Consequences for the common router

Initialization entry is a candidate invalidation boundary across all nine
branches, but it is not sufficient: resource cleanup begins before it on
several paths, and the CVS1 in-place restore bypasses initialization.
Teardown entry, in-place restoration and activation must be separate events.
Hook arguments, callback thread ordering and safe return boundaries still
require proof before detours can be installed.

The intended router must revoke the old output generation before teardown
or restore, clear its pending output, and activate only after identity,
publication and adapter-specific readiness pass. Root address equality
cannot preserve the old generation across a traced replacement event.
Repeated polling without an event must not manufacture new generations.

Current code confirms two migration obligations:

- `mod.c` calls `cvs2_select_tick` unconditionally. Its inactive path calls
  `cvs2_gauges_observe(NULL,0,0)`, which resets all audio gauges. Once another
  adapter exists, inactive CVS2 must have no authority to reset its output.
- `cvs2_frame.c` feeds and resets audio directly from the frame detour,
  while worker-side CVS2 code also feeds/resets gauges. Routing only the
  worker therefore cannot enforce output ownership. Both paths need the
  same generation/owner contract, including checks at consumption time.

These are integration constraints, not reports of a newly observed audible
bug. Preserve the frame-timed health behavior; moving reads to asynchronous
polling would regress its original purpose. Review the mixer handoff and
latency before choosing where normalized observations are consumed.

## Remaining Astra work in this boundary

1. Trace `0x140047270`, destructor entry and remaining cleanup paths.
2. Establish shared restore/load consumers and event ownership. The subsequent
   [collection restore trace](collection-restore-trace.md) now pins in-place
   restore bodies and preparation edges for all nine branches, including
   selective restoration; UI events and earliest suspension points remain open.
3. Prove lifecycle/frame hook ABI and ordering, then prepare targeted live
   event captures with the already-tested metadata tool.
4. Implement the router and generation-aware output handoff, migrate CVS2,
   and prove stale/inactive rejection with CVS1 before multiplying adapters.

The full upfront campaign also retains feature acquisition, spatial/audio
prototype and implementation-packet deliverables. Character-ID/name mapping
is a separate bounded Opus research task; it is not the next Astra task.

The restore continuation also identifies the destructor vtable's owning shell
object and pins its constructor publication. The actual virtual invocation
and lifetime trigger remain open. Lifecycle manifest now contains 66 spans.
