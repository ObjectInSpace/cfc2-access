# Shared output dispatch contract

Implementation proposal, 2026-10-10. This specifies the race semantics required
by the [invalidation review](shared-output-invalidation-review.md). It is not
implemented synchronization or a native lifetime guarantee.

## Backend ownership

Keep all speech backend calls on the existing worker. Shell speech already
runs there; no drawing callback is required. Route game, shell, manual read,
hotkeys and fallback announcements through one dispatcher. Inventory every
`speech_say` and `speech_stop` caller, including `menu_speak_line`, `mod.c`
and `text_channel.c`. No global stop or backend submission may bypass it.
Preserve existing FIFO/latest-frame transports and distinct source policies;
do not add a speech queue.

Separate source identity, native generation/revision and output lease. Shell
takeover retires game output eligibility without necessarily departing the
native game. Returning game output requires a fresh lease and current eligible
source state. Manual reads carry source labels; fallback diagnostics use the
worker's current control lease. Shell remains available during game suspension.

## Reservation versus revocation

Use an atomic per-source gate containing a monotonically unique version/lease
and CLOSED, OPEN or RESERVED state. Choose a demonstrably lock-free encoding
for the target before native wiring; never wrap identifiers. Only the worker
opens a fresh lease after validated completion, using compare/exchange against
the exact closed version associated with the completion record. A newer
invalidation defeats that operation.

Native callbacks atomically advance the version and close admission before
original mutation/release. They never wait on speech or acquire the dispatcher
lock. Concurrent invalidations require a bounded algorithm: after bounded CAS
failure, set a sticky atomic fault that disables the producer. Every open,
reserve and acceptance path checks that fault. Recovery requires controlled
reinitialization with producers quiescent; do not clear it amid active work.

The worker copies bounded candidate text/identity and CASes matching OPEN to
RESERVED. That CAS linearizes reservation; the successful native gate change
linearizes revocation. Start with sequentially consistent gate/fault atomics.
Payload records require their own synchronization; labels cannot repair a
data race in payload storage. Loss, overflow or unsupported atomics close
admission.

- Revocation wins reservation: no backend call.
- Reservation wins: an in-flight attempt exists. Check the gate/fault again
  immediately before backend entry and cancel when invalidation is visible.
- Revocation can still occur just after that last check. The reserved call
  may reach the backend after revocation. Native callbacks must not wait for it.

Strictly forbidding all audible output after revocation requires a
cancellation-aware backend or synchronization that can block native work.
This proposal instead permits the bounded number of already-reserved attempts
(one synchronous worker attempt), rejects stale acceptance and retires them
before newer output. It does not bound backend call duration.

## Acceptance and retirement

Maintain a worker-only ledger: source, lease, monotonic dispatch ID and backend
attempt status. Set it before calling the backend, including before an interrupt
stop/say pair. No newer submission runs while that synchronous call executes.

After return, semantic acceptance requires backend acceptance and a successful
matching RESERVED-to-OPEN CAS with no fault. That CAS linearizes acceptance.
If invalidation wins, do not advance accepted history or retry the old lease.
If acceptance wins, a later revision invalidation applies normal source policy:
selection retains accepted history across revision changes and clears it across
generation changes. Keep the commit labelled with its original identity; the
worker processes later invalidation against that labelled history. A current
backend rejection reopens for retry within the original pending/age policy.

Retire revoked backend attempts before any newer speech, including shell
speech. Each stop request carries the exact lease and dispatch ID. If the
ledger now names a newer attempt, the old stop is a no-op. Treat a rejected
backend attempt as potentially dirty too: an interrupt may already have stopped
earlier speech. Use the available backend stop and record failure; backend
acceptance is not audible completion, and unavailable cancellation cannot be
reported as successful audible cancellation.

Before each dispatch reconcile the ledger with current gates/faults, even if
the descriptive invalidation record was lost. All stops and interrupt stop/say
pairs belong to this worker. Thus delayed cleanup cannot cancel newer output
through this dispatcher. External speech software behavior is outside that
ordering claim. An atomic version check followed by an untracked `tc_say` is
not an implementation of this contract.

## Transport and audio

Label text at capture, reject stale enqueue/frame publication, and repeat checks
at consumption and manual read. Never relabel old text on arrival. Lost records
prevent reopening until fresh validated acquisition. Pointer lifetime remains
an independent precondition for creating copied observations.

Use a fresh monotonic audio scene epoch per eligible generation/revision lease,
retaining native identity separately. Reject stale publish and stop, preserve
scene ownership guards and frame-timed health. Implement the clock-event path
before enabling scene ownership, which rejects legacy clock ticks. Reuse the
existing mixer and transports. Test exhaustion and discontinuity seeding.

## Implementation acceptance

Implement behind a fake backend before hooks. Couple the existing `tc_say`
result to labelled semantic acceptance; keep backend access private to the
worker dispatcher. Use a C harness with actual atomics and barrier-controlled
interleavings, not timing sleeps. Required cases:

- Revoke before reservation, after reservation, after final check and inside
  backend: verify the distinct submission/acceptance outcomes above.
- Acceptance wins before revision invalidation: retain selection history;
  generation replacement clears it. Current rejection retries; stale rejection
  does not.
- Shell takeover during game backend call: retirement precedes shell submit.
  Old delayed stop after newer output must do nothing.
- Concurrent invalidations, loss, overflow, CAS contention exhaustion, version
  exhaustion and fault during open/reserve/commit keep admission closed.
- Late story enqueue, frame publication and manual read cannot acquire a new
  identity; stale audio publish/stop are rejected; clock/health parity survives.

Existing serialized session tests do not certify this concurrency protocol.
This document supplies implementation decisions and test obligations; native
activation and live listening acceptance remain unfinished.
