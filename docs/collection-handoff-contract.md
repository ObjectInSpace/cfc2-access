# Shared handoff after the first live switch

2026-10-07. Design requirements, not installed runtime behavior. Sources are
the [live switch capture](collection-boundary-live-20261006.md),
[snapshot trace](collection-snapshot-trace.md), and
[output review](shared-output-invalidation-review.md).

## Departure and acquisition

The two observed exits nest cleanup inside screen release. Both exit records
still contain the departing game's root. Revoke output at screen-release
entry, before calling the original. Cleanup entry must independently revoke
for paths that do not enter through screen release. A nested cleanup return
must not reopen its enclosing release. Release return leaves the owner departed.
Reuse of the screen, cleanup, session or root address must not preserve ownership.

Use a monotonically changing revocation label visible to consumers immediately;
a worker notification alone leaves a window for stale output. The serialized
game_session component must remain worker-owned. Native hooks must not invoke
it concurrently or wait on speech, file I/O or the mixer. Any lost handoff record
must leave decoding ineligible until a fresh acquisition is established.

Acquire a new generation only after a validated initialization/completion path.
An ordinary update with matching adapter/ROM/root metadata is insufficient:
the switch capture does not locate publication, and the screen manager can
dispose a screen after its update returns. An acquisition request must carry
the revocation label it observed; a later departure invalidates that request.
No return from the old owner may resume the new owner.

Offline implementation, 2026-10-08: game_session_begin_if_current now checks
the acquisition request's expected generation and revision before beginning
an owner. A later invalidation or acquisition rejects the request without
changing state. Replaying a successful request is also rejected. Success
still leaves eligibility false. This is a serialized component guard, not a
native callback synchronization mechanism or proof of initialization readiness.
The session runner passes 73 checks, including stale Quick Load acquisition,
same-identity replacement and rejection of an old published observation; the
selection runner passes its existing 209 checks.

## Capture and mutation

The [Quick Load command trace](collection-quick-save-followup.md) establishes
cleanup and initialization inside 0x14004ffd0, followed by secondary-buffer
restoration. This transaction needs departure/new-generation semantics even
for identical game/ROM/root addresses. Its direct cleanup call bypasses the
old monitored wrapper. Return is not yet secondary-to-primary restore completion.

Native memory must be copied at an evidenced lifetime-safe point. Checking
labels before and after an arbitrary worker read can reject stale data but
does not protect a freed allocation during that read. Keep native pointers
inside the adapter; send bounded copied observations to the worker.

A snapshot load invalidates the state revision before original execution.
Its return does not imply the enclosing replay batch is finished: callback
finish may poll and replay again. Resume only after all enclosing mutations
finish under the same generation/revocation labels. The current five-site
probe does not enclose every manager or mutation path; unobserved paths remain
ineligible rather than being authorized by an empty nesting counter.

Selection settling counts distinct eligible publications. Pending observations
are discarded on revocation. Accepted semantic history follows its content
policy: a restore alone must not reannounce an already accepted choice or quote.

## Output transaction

Frame/FIFO publication and automatic/manual consumption must both check labels.
Carry labels on the existing transports rather than constructing another queue.
A worker-copied message remains revocable until backend dispatch. A before-call
token check alone leaves a check-to-call race with native revocation; a concrete
dispatch protocol must define its ordering and treatment of an in-flight call.
Do not promise that a post-call check can retract accepted NVDA speech.

Shell and game output need shared arbitration. A delayed old-owner speech_stop
must not cancel a newer shell utterance. Retirement must distinguish pending
transport state, backend-accepted output, and accepted semantic history.

Use revision-aware audio acceptance or a fresh monotonic audio lease for every
eligible generation/revision. Stale publish and stop must fail for the newer
lease. Preserve the recovered scene ownership guards. Activating scene ownership
currently rejects legacy clock ticks; migrate that cue explicitly before enabling
the scene path for CVS2. Preserve frame-timed health sampling.

## Required controls before wiring

- Replay the captured release/cleanup nesting; neither return restores eligibility.
- Exercise direct cleanup, same-address reacquisition and stale acquisition completion.
- Finish an old producer after revocation and dispatch a copied message after departure.
- Exercise nested restore/update/finish without intermediate publication.
- Test rejected speech, shell takeover, delayed stop and dispatch/revocation interleaving.
- Test old audio publish/stop across revision changes and clock/health parity.
- Treat unknown native ordering, lost handoff records and counter exhaustion as ineligible.

These controls describe needed integration tests. Existing serial session and
presenter tests do not establish native concurrency or lifetime protection.

## Next live evidence

Priority correction after the user's Vs. screen checkpoint observation:
follow [user-facing quick save/load](collection-quick-save-followup.md) for
the offline pilot. The join-match candidate below remains a separate future
callback investigation, not the next requested live exercise or a prerequisite
for every supported offline mode.

The callback-mode trace now connects uUiJoinMatch state 12 to aGame2Match,
then to mode enable and a deferred aGame request. A controlled match-entry
capture is the next candidate for observing registration. The visible menu route
and primary/secondary callback choice still need confirmation. Registration
alone is not restore coverage; require actual snapshot-load entry/exit events
and their enclosing updates before claiming that path exercised.

Do not substitute Quick Load, training reset or another ordinary switch based
on their names. Do not force native callbacks, corrupt state, or manipulate
network traffic to manufacture a load event. No new live run is staged by this
document; the previous probe remains disabled for future launches.
