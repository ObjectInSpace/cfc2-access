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

## Screen update and conditional release continuation

The lifecycle audit now pins 70 spans. Constructor `0x140037000` allocates
a 0xb8-byte object and installs table `0x140fde6f0`. Its +0x38 slot holds
the complete update body `0x140038f90`; +0x40 holds `0x140037140`.
These slots belong to this screen object, not the large shell owner whose
deleting destructor was traced earlier.

Update calls the shared shell update at `0x140039430`, then
`0x140039ed0` at `0x140039438`. The latter updates shell/session fields
after the native game processing; its full bounded body is now retained.

When screen-object byte +0x38 is nonzero, the +0x40 method first disables
the callback path through `0x1400520b0` with DL=0 at `0x140037179`, then
calls game cleanup through `0x14004adc0` at `0x14003719d`. This establishes
close-before-cleanup on that branch. The conditional bypass and other entry
paths remain distinct. Vtable membership alone does not prove when the
engine invokes either method or on which thread.

The collection group now passes 40 tests, including screen-slot mutation
rejection and the close-before-cleanup instruction check. A bounded event
probe is the next implementation step for live ordering; no production
router or new live validation is claimed.

## Preparation target continuation — 2026-10-01

The previously open target `0x140047270..0x1400472db` is now retained,
along with its three direct callees (74 lifecycle spans total). It uses
the object from `0x1422c82c8`, distinct from S and the shell owner slots.
First it passes token `0x12345678` to `0x1401a0300`. That function compares
object+0x32528 with the token and conditionally calls embedded object
+0x32500's virtual +0x10. The imported entry/exit calls use object+0x32fb0;
their API identities are not established by this trace.

Preparation clears byte globals `0x141c14e9d/e9e`, dwords
`0x141c14ea8/e98`, and qword `0x141c14ea0`, then sets byte
`0x141c14e9c` to 1. It calls `0x1401998c0` with the same object in RCX/RDX
and R8D=0x10. Subject to object+0x32510=0 and embedded virtual +0x28
returning AL=1, that callee records the token/owner at +0x32528 and the
mode at +0x32fa8. Finally preparation tail-calls `0x14019aec0` with the
same object in RCX/RDX and R8D=2. If +0x32528 matches, it writes
+0x325b4 and conditionally forwards the option through object+0x32f90's
virtual +0x128.

These direct bodies do not establish a game-root replacement. Their indirect
callees remain unresolved; absence of a direct root write is not proof that
they cannot change state. This path is therefore not approved as a universal
epoch or publication hook. Its subsystem/UI purpose and transitive lifetime
effects remain open. The existing boundary probe is still the next live
thread/order measurement; this continuation adds no hooks or deployment.

## Screen manager: deferred replacement after update

The lifecycle manifest is now schema 3 with 83 spans. The `aGame` type
registration at `0x140026f70..0x140026fc0` registers descriptor
`0x141c049d0`, size 0xb8, name `0x140fde690`, then installs factory table
`0x140fde698`. Table +8 is the previously traced constructor `0x140037000`.
The screen's own virtual +0x20 getter `0x1400372b0` returns that descriptor.
The base constructor clears byte +0x38; its meaning is readiness in the
manager route below, not an independently proven visible-screen flag.

Manager constructor `0x1401a5900..0x1401a5a50` allocates 0x7088 bytes,
installs table `0x141018b60`, and publishes the object at `0x1422c8358`.
Its virtual +0x50 is `0x1401a61f0`, which only writes the incoming descriptor
to manager+0x7040. One complete request path `0x14003c800..0x14003c97a`
passes the `aGame` descriptor to this virtual method, after enabling the
callback path. This is a pending request, not synchronous game construction.
The UI action invoking this request path has not been identified.

Manager virtual +0x30, `0x1401a6200..0x1401a62a6`, traverses up to eight
screen pointers starting at +0x7048. A null pointer stops the traversal.
For a screen whose byte +0x38 is zero, it calls virtual +0x28; AL=0 stops
the traversal. Otherwise it calls +0x30 and sets byte +0x38=1. It then
calls virtual +0x38 at `0x1401a624e`, which is `0x140038f90` when this
object has the retained game-screen table. This supplies the previously
missing indirect update consumer, conditional on actual object membership.

After traversal, a nonzero pending descriptor is processed only if the
screen count is zero or the last counted screen's byte +0x38 is nonzero.
Virtual +0x58 at `0x1401a6288` invokes `0x1401a6420..0x1401a65e7`;
the pending descriptor is cleared after it returns. Replacement compares
existing descriptor ancestry with the requested chain, retains a matching
prefix, and disposes the remaining screens in reverse order. Even an exact
nonempty match reduces the retained prefix by one, so requesting the same
descriptor can replace a screen. Descriptor equality is not a session epoch.
The native hierarchy construction and all callers are not exhaustively traced.

For each disposed screen the replacement body calls virtual +0x40 at
`0x1401a6566`, then virtual +0 with EDX=1 at `0x1401a6579`, then clears
its array slot. New objects are constructed through descriptor virtual +8
at `0x1401a65bc`, stored into the screen array and assigned their index at
+0x3c. Thus the game-screen release method can be invoked after its update
returns, within this same manager update invocation. The already-traced
release branch closes the callback object before calling game cleanup.

A second disposal route is manager virtual +0x28,
`0x1401a6390..0x1401a6403`: it traverses all eight slots in reverse,
calling +0x40 then the deleting destructor on each nonnull object, clearing
slots, pending descriptor and count. Its engine caller remains open.

This ordering is static code evidence, not measured thread identity. It
strengthens the requirement to revoke output on teardown entry and to reject
queued observations at consumption time. Neither the inner native update nor
the game-screen update return alone establishes a final manager state.
Manager return is a candidate boundary, not an approved new hook: its engine
callers, other mutation paths and live ordering still need review. The existing
five-site probe does not enclose the manager call; preserve that capture limit.
