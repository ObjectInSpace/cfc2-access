# Shared output invalidation review

Source review at 29c5771, 2026-10-06. This records integration obligations;
it does not install lifecycle hooks or report an observed audible defect.
The user has deferred live staging. The serialized game_session and
game_selection tests establish component behavior, not native thread ordering.

## Existing paths that must participate

- src/cvs2_select.c: the worker validates a copied CVS2 state, then calls
  selection, gauges, announcements, quotes, story and positioned-text readers.
  Its inactive branch resets each. Native pointer brackets can miss an
  in-place restore. The extracted selection copy function is reusable;
  shipping selection still keys its history by root and step.
- src/cvs2_frame.c: the native frame detour directly publishes health,
  beacons and clock cues. Its departure path calls audio_gauge_reset_all
  and audio_beacons_off. A worker-only router therefore cannot own every
  producer. Preserve the frame-timed health path and bounded hook work.
- src/cvs2_story.c: root departure clears tc_queue and increments a local
  epoch. Story capture synchronizes dedup history to that epoch near entry,
  but queue entries carry only text. An already-running producer needs a
  publication-time check so it cannot refill the queue with old-owner text
  after invalidation. Clearing the queue alone is insufficient.
- src/cvs2_text.c: root change resets dedup and calls tc_frame_invalidate.
  That invalidates the published timestamp, not a collecting producer's
  owner/revision. A late old producer must be rejected at publish, and a
  copied frame must be checked again before automatic or manual output.
- src/text_channel.c: tc_queue_drain copies a line, releases its lock,
  calls tc_say, then advances on acceptance. Its tail check protects queue
  bookkeeping; it is not a lifecycle check before speech. tc_frame_take
  checks age and publication sequence, neither an owner generation nor a
  state revision. Preserve backend acceptance coupling while adding labels.

The common layer must distinguish invalidating pending observations from
rearming accepted semantic history. In particular, restore/replay does not
automatically make an accepted quote or selection new. Each content policy
must define that behavior; the selection tests already retain accepted
choices across revision changes.

## Preserve the recovered audio protections

The current audio scene API is stronger than the legacy calls. Once a scene
owns the mixer, audio_gauge_reset rejects legacy resets, audio_beacon_set
rejects legacy feeds, audio_tick rejects legacy ticks, and the mixer reads
scene beacons instead of the legacy backing array. audio_beacons_off still
clears that legacy array but does not directly clear scene beacons.
Do not describe this as an existing scene being silenced by an inactive
CVS2 reset; the recovered ownership checks already prevent that path.

audio_scene_begin requires a strictly increasing epoch. Scene publish checks
epoch and increasing sequence under the mixer lock. Matching-epoch stop
prevents further publication, and the mixer clears scene state when it
consumes the stop/reset. None of those functions accepts a separate state
revision. A newer-sequence observation captured before a same-owner restore
must not become valid merely because its audio epoch still matches.

Two integration choices need a concrete review: add revision-aware acceptance
to the scene handoff, or allocate a fresh monotonic audio output epoch for
each eligible owner/revision lease. If using the latter, retain the native
owner generation separately and test discontinuity seeding, stale stop,
stale publish and exhaustion. Do not reuse a revoked audio epoch. TTL expiry
limits stale sound duration but cannot replace explicit invalidation.

The scene path is not yet a full replacement for all legacy cue families.
Activating it disables legacy clock ticks, while the existing scene snapshot
contains gauges and beacons. Therefore do not turn it on merely to integrate
selection speech: preserve clock behavior through a reviewed common event
path before migrating all CVS2 audio. This is an F11/F12 parity dependency,
not a reason to redesign the mixer or block the copied-selection decoder.

## Required ownership transaction

At an evidenced departure or mutation entry, revoke eligibility first.
Reject old producers at publication and old observations at consumption.
The native callback must not block on worker speech, file I/O or a mixer
reset; the serialized state machine is not safe to invoke concurrently.
Define a bounded cross-thread revocation/handoff mechanism after measuring
actual ordering. Missing native events remain a coverage limit.

The worker then retires old source state and requests the matching audio
lease stop. It must arbitrate already-accepted speech with shell ownership:
speech_stop is global, so delayed cleanup cannot be allowed to cancel a newer
shell or game utterance. Clearing internal buffers does not cancel the screen
reader's accepted queue. A post-backend token check prevents stale bookkeeping
but cannot retract an utterance already submitted.

Only an evidenced completion boundary can authorize the next publication.
Nested load/update returns must not independently reopen an enclosing mutation.
Read-current-text must use the same owner and revision as automatic speech,
while leaving shared shell navigation available when game decoding is invalid.

## Next implementation and acceptance controls

Extend existing frame/FIFO transports with owner/revision labels rather than
adding another speech queue. Test a producer finishing after invalidation,
departure after worker copy but before backend dispatch, backend rejection
across a switch, and a delayed stop after a new owner starts. Keep accepted
history separate from pending transport state. Test automatic and manual text.

For audio, test stale publish/stop across both owner and revision changes,
frame/worker interleaving, clock parity, mixer consumption and queued device
blocks. A passing latest-selection test cannot stand in for FIFO or mixer
acceptance. The existing lifecycle run card remains the next live evidence
step; no new live exercise is requested by this review.
