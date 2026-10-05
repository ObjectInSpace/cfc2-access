# PWS2 draw-stage capture research

- Task ID: PWS2-R02. Kind: ABI and acquisition research.
- Status: research with tested, unwired entry ABI component; not ready for
  runtime installation or deployment.
- Baseline: e308e64 plus the publisher/resource continuation containing this
  packet. Preserve the audio checkpoint b01dc6f and ownership checkpoint
  cee7511 and subsequent work.
- Evidence: [HUD trace](../pws2-hud-ownership-trace.md),
  [HUD manifest](../pws2-hud-manifest.json),
  [actor trace](../pws2-actor-trace.md), and
  [session boundary review](../collection-session-boundary-review.md).
- Deliverable: a reviewed, bounded diagnostic capture contract and tested
  implementation that joins submitted HUD resources to fixed actor owners.
  No production audio eligibility follows from a diagnostic capture.

## Established candidate sites

List-8 draw entry is 0x140a5de70. Its retained body uses no incoming
argument and constructs no deliberate return value. The reviewed caller at
0x140a5d85c does not use its result. The body selects slot 1 and reads the
list head from R+0x40790. Its entry is not a per-resource submission event.

At 0x140a5df7c, RCX=resource+0x34 is passed to 0x140f32440,
after the object and resource submission gates described in the HUD trace.
The return address is 0x140a5df81. The helper copies four 16-byte blocks
to the currently selected slot using context+0x1f80a4; it does not select
slot 1 itself. Its null branch returns without copying and without defining
a result. On the nonnull path RAX is the selected slot offset, not an
intentional returned resource. Preserve the original's behavior; do not
invent a return-value contract from that incidental RAX value.

At the call site RDI holds the display object and RBX the resource. These
are native register facts, not extra C parameters. A C detour at the helper
entry cannot simply declare two additional arguments to obtain them. The
helper is shared with actor and matrix-building paths, so an unfiltered hook
would mix incompatible stages.

The implemented offline candidate is the helper-entry observer in
src/pws2_draw_entry.{h,c,S}. It is excluded from build.ps1 and has no
installer, capture reader or production sink. Its exact return-address filter
passes RDI/RBX as object/resource identities without treating them as C
arguments. The assembly saves flags, volatile integer registers and baseline
SSE/x87 state, invokes the last-error-preserving C observer, restores state
and tail-jumps to the original. It makes no upper-AVX-lane preservation claim.
Configure its original/sink globals before any future installation; concurrent
replacement is not supported.

tools/run_pws2_draw_entry_test.ps1 compiled the production C filter with
warnings as errors and passed synthetic active, inactive, nested and unrelated
caller controls. The checks include incoming registers, native return address,
original return value, flags, floating-point state and last-error. This proves
the isolated ABI component, not the missing installer or live object validity.

The installation choice still needs a concrete review:

- A call-site capture stub preserving the native register/stack state and
  the original matrix copy. Review instruction relocation, all live registers,
  flags, stack alignment and reentrancy before choosing this approach.
- A helper-entry observer filtered by the exact return address and an active
  list-8 draw context. Establish how a resource is associated with its current
  display object without stale caches, ambiguous resource pointers, unchecked
  linked-list traversal or assuming thread identity. Reuse must be tested.

Do not choose a site solely because it is easy to hook. In particular,
capturing the current matrix at list entry or on the speech worker cannot
prove which resource was submitted.

## Copied research record

The eventual event needs copied values, with addresses retained only as
opaque diagnostic identities. The worker must never dereference them:

- Session/build/ROM identity, router owner generation and state revision,
  capture sequence, time, thread, explicit draw stage, draw invocation,
  pass globals and selected matrix slot.
- Display-object identity, wrapping +0x38 diagnostic word, callback, raw
  +1 layout byte, signed +2 actor index, +0x28 state, +0xc0 enable byte,
  +0xc4 flags, resource count and resource ordinal.
- Resource identity/type, enable byte, flags, packet identity/header,
  parent override identity, matrix +0x34, and raw local components
  +0x80..+0xa3. Preserve ordinary and special callbacks separately.
- Selected slot and the other matrices/viewport needed by the traced final
  renderer. Capture them at the same stage. Do not apply the actor two-point
  projection formula to HUD resources without tracing that renderer.
- Explicit read, bounds, ambiguity, nonfinite, overflow, contention,
  discontinuity and incomplete-invocation status. No missing event is filled
  with a prior frame's values.

The native resource pool has 640 records of 0xa8 bytes. That is a storage
fact, not an acceptable per-frame memory-read budget. Choose smaller fixed
diagnostic limits and report truncation. Count guarded reads and bytes before
implementation; avoid per-item VirtualQuery scans of entire pools. Native
object/resource counts and asset indices are not already validated limits.

## Remaining evidence before runtime implementation

1. Resolve the selected hook's complete ABI and the object/resource join.
   Test repeated resources, address reuse and an unrelated caller with the
   same helper. Keep the special layout byte 0xff independent of owner.
2. Trace the submission helpers reached by the indexed and special gauge
   flags through final projection/clipping. Scalar rejection/clamping and the
   types-0/1 versus types-2/3 queue matrix copies are now retained in the HUD
   trace, along with capacity rejection. A matrix load proves neither queue
   acceptance nor visible pixels or normalized HUD pan. The renderer's
   intervening vertex pipeline and depth helper remain open.
3. Complete actor-value damage/range and publication-mode writer review.
   An initializer of 1000.0 does not certify the maximum in every mode.
4. Connect diagnostic revision labels to the existing lifecycle probe without
   promoting its unmeasured thread/order assumptions to production authority.
   Same-address restore and replay must invalidate or label observations.

## Required offline controls for the implementation

- Signature changes at every installed site; partial installation rollback;
  inactive and failed-install forwarding; original called exactly once.
- Full argument/result/last-error preservation for the chosen ABI; nested
  calls, unrelated callers, repeated draw passes and wrong selected slots.
- Missing or unreadable object/resource/packet/matrix, invalid owner, negative
  count, oversized count, list cycle, duplicate association, nonfinite data,
  allocation failure and address reuse.
- Ordinary versus compacted ownership: fixed slots 1 and 3 mapped to layouts
  0 and 1; compaction must never reassign participant slot/incarnation.
- Special callback with layout 0xff; invalid fallback owner rejected.
- State-2 appearance, disabled object, disabled resource, null packet and
  negative packet header. Submission is separate from final visibility.
- Bounded overflow/contention, explicit losses, timeout, completion racing a
  producer, and stale epoch/revision rejection at consumption.

Synthetic fixtures must say synthetic. Extend the existing shared capture
transport where appropriate; do not copy a speech queue or audio mixer into
the diagnostic. No audio_scene_begin calls from native HUD callbacks.

## Live sequence after the offline work

The existing [boundary run card](../collection-boundary-run-card.md) measures
the still-missing lifecycle thread/order evidence. Complete its final-source
regressions and DLL build before staging. It is a separate exercise from the
future draw capture; the raw ownership sampler cannot substitute for either.

Once the draw diagnostic is implemented and its controls pass, prepare a
short run covering ordinary and compacted layouts, a missing early actor,
the special-object branch, damage to distinct owners, camera movement and
departure/restore. Record actual actions and observed HUD outcomes. A failed
or unexercised branch remains unknown. Listening acceptance follows shared
session/audio integration, never an inferred pan from local resource offsets.
