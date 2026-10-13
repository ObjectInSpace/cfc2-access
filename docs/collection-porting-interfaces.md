# Implemented porting interfaces and remaining shared work

Execution owner: Opus can implement the remaining shared work and then the
ports. Follow the [execution plan](port-tasks/opus-shared-selection-execution.md).
Astra is optional escalation for a concrete blocker; none of the unfinished
items below is inherently Astra-only work.

Updated 2026-10-13. The purpose of upfront work is to give the implementation
owner reusable code with explicit inputs, not to require a second-game
"demonstration" before deciding whether code is useful. Build missing shared
mechanisms when an actual feature needs them; keep game-specific discovery in
bounded adapter tasks. This document distinguishes implemented interfaces from
proposed collection-wide guarantees.

## Selection: implemented worker path

`src/selection_channel.{h,c}` owns `game_session` and `game_selection` and binds
output to the existing `tc_say`. It is linked into the DLL and used by
`cvs2_select_observe`. The former private CVS2 settling/spoken-history loop is
removed. Other adapters reuse this channel rather than copying that loop.

Adapter responsibilities:

1. Admit the actual build/ROM/phase and obtain an owned state copy using its
   justified acquisition path. None of the presentation APIs authorizes reading
   native memory. Game offsets, name maps and phase/visit decoding stay local.
2. Keep a zero-initialized `selection_channel` on the existing speech worker.
   Call `selection_channel_begin(channel, adapter, rom, source)` on a new
   presentation owner. Source is a bounded copied log label. Begin creates a
   new presentation generation, even for the same game/ROM. It is not evidence
   of native initialization, synchronization or allocation lifetime.
3. Decode each NEW sample into four `game_selection_choice` slots. A valid
   choice has kind/value, visit and a bounded copied label. Mark unused owners
   INACTIVE. A busy/unknown/unreadable observation is UNAVAILABLE; reserve
   INACTIVE for an observed exit so temporary missing data does not rearm names.
4. Call `selection_channel_offer` with capture time and current monotonic time
   in milliseconds. NULL/incomplete observations break settling. They do not
   manufacture selection exit. A retry must not resubmit the same sample as
   another observation.
5. Call `selection_channel_drain` on the worker to emit or retry. It returns the
   accepted-owner mask. There is one grouped utterance, two distinct samples
   to settle, 500-ms freshness, and no queue of obsolete highlights. Backend
   refusal does not mark a choice spoken. Changing visits rearms the choice,
   including a brief visit followed by return before any speech is accepted.
6. Depart the old channel before activating another owner. Call
   `selection_channel_depart` on departure or unsupported context. Pending
   retries cannot emit afterward. Keep exactly one active selection output
   path; begin/depart routing belongs to the worker, not concurrent callbacks.

CVS2's existing acquisition, native decoders, 33-ms polling and sibling
quote/story/text/gauge/announcement work are retained. Its adapter still uses
observed root changes as its compatibility reset policy, now including ROM
changes. That policy does not detect all same-address replacements. This work
makes no new native safety claim and installs no new hooks. No global atomic
revocation protocol is implemented. Actual accepted speech cannot be retracted
by merely changing a token.

The existing CVS2 suite now exercises the shipping shared path, including
Arcade/alternate character, P2/team, groove, stage/ratio, re-entry and retries.
`run_selection_channel_test.ps1` exercises synthetic four-owner grouping,
adapter switches, departure during backend execution, copied source ownership,
visit re-entry, unavailable data, stale/future input and sequence exhaustion.
Synthetic labels do not certify another game's name map or four-player audio.

## Text: reuse existing mechanisms

Use `text_channel` and the existing text capture transports. Choose the actual
policy: settled quote, deduplicated prompt, ordered story, or positioned frame.
Register read-current-text with `tc_register`. Use the current readers as
examples of these distinct policies; do not copy their native offsets or assume
all titles expose strings in the same format. Keep backend acceptance and
encoding in the common code. A new game's text reader still needs visibility,
completion and name/encoding evidence.

## Gameplay audio: implemented shared controller

`src/match_audio.{h,c}` owns the in-match cue policy formerly private to
`cvs2_frame.c` and `cvs2_gauges_observe`. CVS2 uses it in the DLL build. The
mixer, voices, F5-F8 volume controls and `cfc2_audio.ini` are unchanged. The
scene interface is still not enabled; this controller drives the two-player
legacy setters through owner epochs instead.

Adapter responsibilities:

1. Decode an owned `match_audio_sample` from the adapter's justified
   acquisition: `in_match` (an OBSERVED match or non-match state, never a read
   failure), per owner health and resource as value plus current maximum with
   explicit validity, beacon screen pan already projected (-1..+1) and stance
   already mapped onto the `AUDIO_STANCE_*` ladder, the visible clock with
   validity, and a motion signature built from fields only the simulation
   advances. Native offsets, sentinels, resource caps, camera projection and
   action-ID meanings stay in the adapter. Unreadable state is not a sample:
   do not feed it.
2. On the speech worker, call `match_audio_begin()` for each new presentation
   owner (CVS2 uses its root + ROM compatibility identity) and
   `match_audio_depart(epoch)` on departure or unsupported context. Begin
   departs any previous owner. A stale depart is ignored.
3. Keep one zero-initialised `match_audio_producer` per producer thread, with
   `feeds` naming the cue families that thread owns. CVS2's frame hook feeds
   health, beacons and clock on the frame they change; its worker feeds the
   resource meter. Each producer reads `match_audio_current()` and passes that
   epoch to `match_audio_feed`.

Shared policy: gauges write the mixer only on a change; an invalid value
resets once so the next real value seeds silently; a new maximum reseeds
silently; beacons are fed every frame unless paused; pause is a motion
signature unchanged for 30 frames; clock ticks only on a decrease landing on
30, 15 or 1-5; an observed exit silences that producer's families once; a new
epoch discards the producer's edge state.

Ownership: `audio_owner_begin/stop` in `audio.c` reject every write from any
epoch but the current running one. Gauges check under the mixer lock.
Beacons and ticks stay lock-free for the frame thread and are stamped with
their epoch; the mixer drops a stamp that is not current. A frame-thread
write that passes its check just before a switch is therefore silent after
it. Once an epoch is claimed, the unowned legacy setters are rejected.

Deliberate corrections relative to the previous CVS2 code: the meter is now
gated on the in-match phase as health always was; a changed maximum reseeds
silently instead of sounding a rescale; beacons are switched off once at the
pause edge rather than on every paused frame; the worker writes the meter
only on change. Tests: `run_match_audio_test.ps1` (the real mixer, sample
assertions, including the switch race through an `AUDIO_OWNER_TEST` seam) and
`python tools\sabotage_match_audio.py` (5 sabotages, each caught). The CVS2
replay suite now exercises `cvs2_frame_decode` against real captures (KO
sentinel, stance ladder, pan, clock, win-quote exit). Live listening is
pending.

## Transitions: implemented shared announcer

`src/transition_channel.{h,c}` owns match introductions and active-fighter
change announcements, formerly private to `cvs2_announce_observe`. CVS2 uses
it in the DLL build. Worker only; output through `tc_say`.

Adapter responsibilities:

1. Keep one zero-initialised `transition_channel`. Call
   `transition_channel_begin` on a new presentation owner and
   `transition_channel_depart` on departure. Offer only in the title's
   in-match phase; latches survive between matches of one owner.
2. Fill a `transition_observation` with copied labels: up to 4 sides, each
   with its 1-based player number, ownership, optional style label, team of up
   to `TRANSITION_TEAM_MAX` (4) members with ids, names and displayed
   ratio/level (0 = none), and the current active fighter. Set `team_ready`
   only when every present side's team and style are complete.
3. Supply ownership only with evidence. `UNKNOWN` speaks "Player N";
   `LOCAL`/`OPPONENT` speak "You"/"Opponent". Team rules (sizes, ratio
   budgets, style tables) and native IDs stay in the adapter.

Policy: one grouped introduction per team composition, then grouped changes
of the active fighter with their ratio. Fighters on screen at the
introduction are not repeated. No queue: each offer recomputes from the
latest observation, so a newer transition supersedes a refused one.

Deliberate corrections relative to the previous CVS2 code, pinned by tests:
nothing is latched until `tc_say` accepts it (the fighter-change path used to
latch first, losing a refused line); the introduction is keyed on the team
composition, so the next Arcade opponent's team is introduced; CVS2 says
"You/Opponent" only when its joined-player mask `0x1FFEC` has exactly one bit
(every Arcade capture: 0x1) and "Player 1/Player 2" otherwise (every Training
capture: 0x0, or two joined humans). Tests: `run_transition_test.ps1` and
`python tools\sabotage_transition.py` (4 sabotages, each caught), plus CVS2
replay cases for refusal retry and ownership. Live listening is pending.

## Remaining shared work

- Four-player/arena spatial audio and perceptual acceptance for Power Stone 2.
- A collection-wide runtime adapter router and ownership policy for asynchronous
  text/audio sources. The selection channel is a worker-side component, not
  that router or the proposed global output dispatcher.

Before handing an affected feature family to Opus as "ready to port," finish
its required shared mechanism or provide a bounded implementation task naming
its actual dependency. These gaps do not require a protected CVS2 native-reader
replacement. Implement in feature order; avoid a speculative framework or
collection-wide lifetime campaign. The overall upfront work is not declared
complete merely because selection now has a reusable implementation.

## Evidence and next acquisition task

CVS1's name packet remains unresolved; the shared code cannot turn unlabeled
IDs into correct names. Use `port-tasks/cvs1-branch-a-names.md` and existing
selection/visit evidence. Ask for new captures only for a named unresolved
association or concrete access issue. Preserve research for other titles.

Native-copy lifetime redesign and the global atomic dispatcher remain separate
optional designs unless a concrete feature or observed defect requires them.
The 4096 bypass remains conditional. Existing successful wait captures do not
prove universal safety, and record loss is not hidden.

## Validation and deployment: shared audio and transitions

At `cd3e09a`: all 38 native runners passed with individually checked exit
codes, 121 collection Python tests passed, both sabotage sweeps caught every
sabotage, and the DLL build and import checks passed.
[Retained offline validation](../tools/fixtures/shared-audio-transition-validation-20261013.json.gz).
Artifact SHA256:
`837c3a9e393969180115af02b3ebe6503e2207b68d1ca46f0f9ce569e70e0327`.

The build is deployed to the game with the previous DLL backed up. Live
acceptance is PENDING; see the
[listening checklist](port-tasks/cvs2-shared-audio-listening.md) for hashes,
rollback and what to listen for. This deployment also carries the selection
integration below, so that check accepts both.

## Validation of the selection integration (historical, a800114)

All 36 native runners passed with individually checked process exit codes;
121 collection Python tests passed. The CVS2 suite was rerun after adding the
same-root ROM-switch case and passed. DLL build/import checks passed with
existing unrelated compiler warnings. No deployment or new live listening
acceptance was performed. The installed diagnostic DLL remains unchanged.

Built DLL SHA256: `89eb5b5ad9b1a31fc58ec4b1ad589603f75a7b1a0d0c4258e0340dee1be6666d`.
[Retained offline validation](../tools/fixtures/shared-selection-integration-validation-20261013.json.gz)
contains runner logs, build log, result counts and source hashes. Artifact
SHA256: `cfe486baac76f53c6de209e5084e0f1cba119164366b76a8b997318c8eda8149`.
