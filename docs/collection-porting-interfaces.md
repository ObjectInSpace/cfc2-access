# Implemented porting interfaces and remaining shared work

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

## Audio and transitions: existing code plus named remaining work

The mixer, cue volume controls/persistence, gauge feeds and beacon feeds already
exist in `audio`. Reuse them; do not add per-game mixers or settings files.
The scene interface exists, but activating its ownership policy requires the
compatible clock-event path described in the audio contract. Do not turn it on
incidentally while porting selection.

The following shared work is NOT yet claimed complete:

- Extracting CVS2's match/team/active-fighter presentation into reusable
  transition formatting and acceptance handling, with explicit player ownership.
- A common gameplay controller for pause/exit/reset, gauge/clock edges and
  title-supplied resource maxima/projection/stance. The current CVS2 behavior is
  the implementation input, not a universal per-game schema.
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

## Validation of this implementation

All 36 native runners passed with individually checked process exit codes;
121 collection Python tests passed. The CVS2 suite was rerun after adding the
same-root ROM-switch case and passed. DLL build/import checks passed with
existing unrelated compiler warnings. No deployment or new live listening
acceptance was performed. The installed diagnostic DLL remains unchanged.

Built DLL SHA256: `89eb5b5ad9b1a31fc58ec4b1ad589603f75a7b1a0d0c4258e0340dee1be6666d`.
[Retained offline validation](../tools/fixtures/shared-selection-integration-validation-20261013.json.gz)
contains runner logs, build log, result counts and source hashes. Artifact
SHA256: `cfe486baac76f53c6de209e5084e0f1cba119164366b76a8b997318c8eda8149`.
