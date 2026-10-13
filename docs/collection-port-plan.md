# Porting the CVS2 features across CFC2

## Current direction, 2026-10-13: deliver CVS1 selection speech

The [corrected Opus execution plan](port-tasks/opus-shared-selection-execution.md) supersedes conflicting sequencing
and model assignments below. Preserve the existing CVS2 native acquisition
path. Reuse shared presentation and implement only what the scoped CVS1 feature
needs. The optional protected CVS2 reader replacement, global atomic output
dispatcher and complete collection-wide lifetime investigation are deferred;
they are not prerequisites for this pilot or sharing already-copied data.
CVS1 still needs evidence-backed names and an appropriately justified acquisition
path. Resolve concrete feature blockers without claiming unproved safety.
Existing stronger-design contracts remain valid for those designs if pursued,
but do not govern a smaller compatibility-preserving presentation integration.
Historical research and captures remain evidence, not automatic next actions.

Planning baseline: 2026-09-26. This delivers the requested feature evaluation,
initial technical investigation, architecture and staged execution plan.
Runtime ports remain to be implemented; this document does not claim the
other seven games now have CVS2 parity.

Execution now follows the [Astra-to-Opus workflow](collection-workflow.md):
front-load tracing and architecture proof here, then hand bounded implementation
packets to Opus. The [next-session brief](collection-astra-brief.md) gives the
starting point; model responsibilities do not replace the stage acceptance gates.

Start with the [feature inventory](collection-feature-inventory.md), which
assigns F01–F19 and explains applicability for every game. The
[research report](collection-port-research.md) records concrete native entry
points and reproducible evidence. The [shared architecture](collection-port-architecture.md)
defines how system changes reach every title while keeping genuine ROM/game
differences local.

## Scope and priorities

Completion means the applicable CVS2 features work in the other seven games,
with per-feature/per-game validation and preserved CVS2 behavior. Shared-code
completion alone does not meet this goal. The CVS1 pilot below is an initial
proof and delivery step, followed by the remaining applicable feature ports.

User-approved sequencing, 2026-10-05: shared delivery first, then game-specific
problems within each game's feature work. The immediate milestone is the common
session/observation/output path validated through CVS2 and one bounded CVS1
feature. See [the current workflow priority](collection-workflow.md#current-priority--shared-delivery-first-2026-10-05)
for its execution order and exit criteria. PWS2 renderer/capture research is
preserved at 6cc26c8 and is not a prerequisite. Build shared interfaces from
these concrete migration/pilot needs; later game semantics extend them only
when an evidenced requirement arises.

Deliver equivalent access to the information each title actually shows:
selection, transitions, health/resources, position/stance, clock warnings,
quotes/story/credits/prompts, read-current-text and common controls. Preserve
CVS2 and shell behavior. Screen-reader accessibility determines completion;
successful hooks and plausible memory values are intermediate results.

Keep the earlier shell-first dependency for any missing shell path needed to
reach, configure, pause or leave a title. Investigating game adapters can
proceed offline while those gaps are resolved. This is not a mandate to repeat
old shell research or postpone every game behind unrelated museum work.

Shared mechanisms are implemented once. Original arcade hardware, exact ROM
revision and CFC2 runtime access are separate concepts. Different hardware
requires different readers where proven, not different speech/audio systems.

User clarification: every integration path must be traced. Online references
can establish original hardware and suggest semantic candidates; they do not
authorize a CFC2 address, hook or phase mapping. Shared code is reusable only
after each adapter's inputs and lifecycle satisfy its traced contract. Even
apparently identical titles/variants need an explicit path to that contract.
Static traces establish code behavior under their conditions; labelled live
captures verify those conditions and visible semantics. Neither is described
as absolute proof of unexercised paths.

Unknown applicability stays unresolved. Do not turn a difficult ending, raster
caption, untranslated quote or missing memory field into a silent exclusion.
New arena-specific navigation cues are tracked as explicit adaptations, not
represented as features CVS2 already implements.

## Stage 0 — Baseline and evidence ledger

Status: initial audit complete; implementation baseline validation pending.

- Preserve the current working tree, including shared-text work. User permits
  commits along the way. Commit cohesive changes with exact validation notes;
  do not label existing unreviewed work as newly validated.
- Inventory F01–F19 against implementation and retain CVS2's known live gaps:
  alternate selection paths, rare endings/credits, complete prompt coverage
  and non-English readers. Record tests as recorded historical results until
  actually rerun against the chosen implementation baseline.
- Record executable/dump hashes, ROM identity, mode, language, fixture origins
  and build/installed DLL hash separately. Correct stale field labels before
  reusing research tools.
- Establish a per-feature/per-game ledger using the inventory's statuses,
  source locations, capture/replay evidence, exclusions and next action.

Exit: a reproducible baseline and no ambiguity about which code/features are
being ported. Investigation delivered now: all eight dispatch entries, nine
initializer leads, a repeatable offline audit and the feature/design documents.

## Stage 1 — Identity, hardware and runtime mapping

Depends on stage 0. Perform the inexpensive static pass for every title before
repeating individual game launches.

- Use `collection_port_audit.py`, `disasm.py`, the known session constructor
  and RIP references. Follow initializers into state allocation/publication,
  update dispatch and teardown. Record pointer path, bounds, field widths,
  original argument ABI and signature bytes.
- Prove session `+0x1B0/+0x1B8` use per branch before sharing it as a state-block
  interface. Inspect record serializers/save-load paths where useful. An
  analogous initializer is a lead, not a complete gameplay schema.
- Associate each of the 18 ROM records with the actual branch. Resolve Z3U's
  `+0x2B4` discriminator and both initialization paths. Record original
  hardware/reference versions separately; no automatic CPS-2 or NAOMI backend
  assignment from a title abbreviation.
- Identify game-specific phase dispatch, player records and rendered-text
  consumers. Follow health writers, resource clamp/spend logic and projection
  users. Archive text assists decoding; native render paths establish visibility.
- Keep a trace record for every F-number's acquisition path: entry/dispatch,
  owning allocation and lifetime, read/write instructions, width/endianness,
  phase/mode guard, display/update consumer, exact ROM/build and capture evidence.
  Multiple features may reference one traced renderer or state owner; shared
  evidence is linked rather than duplicated. No feature is enabled by analogy.
- Stop each unresolved trace at a named missing call/ownership relationship.
  Avoid broad shape-based arena sweeps and input-count-derived cursor models.

Exit: each game has a bounded acquisition plan and named unresolved fields.
CVS1 needs a verified root/capture recipe before the pilot; other titles can
remain code-traced leads while the shared implementation advances.

## Stage 2 — Shared session and presentation foundation

Depends on baseline and CVS2's existing evidence. Implement the architecture
incrementally, with CVS2 remaining functional at every commit.

- Add the static adapter/capability registry and active session owner. Route
  polling, frame publication, reset and read-current ownership through it.
  Prevent the inactive CVS2 reset path from silencing another game.
- Introduce normalized selections, participants, phases, gauges, spatial
  observations, clocks and text events. Separate four-member teams from four
  simultaneous combatants. Expose validity and freshness explicitly.
- Move selection/transition formatting, audio edge/reset policy and settings
  integration into common systems. Reuse `text_channel`; do not recreate its
  mechanisms. Correct acceptance-before-latch issues as separately tested fixes.
- Keep game offsets, decoding, visibility/completion and projection in adapters.
  Record any exception and its reason. No new per-game direct speech backend.
- Make game/shell overlay arbitration explicit. Keep input responsive and
  capture free of speech, blocking operations and unbounded work.

Exit: CVS2 exercises the shared path, output behavior is regression-checked,
and inactive/stale sources cannot influence output. This stage creates the
mechanism that makes future system changes propagate to every port.

## Stage 3 — CVS1 pilot, one complete vertical implementation

Depends on stages 1–2. CVS1 is the initial candidate because its interface and
initializer structure offer concrete comparisons; this is an efficiency
hypothesis, not a promise that its offsets match CVS2.

- Trace and capture both players' character/groove/team/ratio behavior. Allow
  four-member teams and prove the native-ID/name map. Include confirm, cancel,
  re-entry, alternate characters and mode-specific screens.
- Implement health/resource feeds, frame timing, camera projection, stance,
  clock and lifecycle resets through shared contracts. Validate maxima per
  groove and mode, including damage, gain, spend, KO and character changes.
- Follow native text paths for F14–F18, including endings/credits. Use captured
  selected text and real visibility, not archive lookup predictions.
- Add a bounded capture tool/descriptor and replay fixtures with provenance.
  Adapt the existing snapshot infrastructure only after correcting its stale
  CVS2 labels and validating the new pointer chain.

Exit: CVS1 has a feature-by-feature outcome and at least a validated normal
play path. Rare or language gaps remain explicitly open. Two adapters pass
the shared propagation tests; adjust interfaces here before multiplying ports.

## Stage 4 — CFJ and Alpha 3 variants

Depends on the proven two-adapter foundation. Execute as separate title
increments, prioritizing the one whose stage-1 trace is most complete.

- CFJ: implement team/active-fighter choices and per-character combat-system
  resources, then selection, audio and all applicable text categories.
- Z3U: implement Ism-specific choices/resources and both runtime branches;
  validate all four ROM identities. No inheritance of original CPS-2 addresses
  without a proven matching address space and revision.
- For each, use the same capture/replay/live acceptance package as the pilot.
  Add resource descriptors or reusable policies when needed; avoid title-name
  conditions inside common presentation code.

Exit: both titles have explicit F01–F19 coverage and outstanding variants
listed. A partially validated variant does not mark the whole game complete.

## Stage 5 — Project Justice and Plasma Sword

Depends on stages 1–3, not on finishing every rare path in stage 4.

- Project Justice: three-member teams, active/assist distinction, resources,
  story and any displayed choices. Trace camera data for spatial output.
- Plasma Sword: its different context placement, native player/resources,
  camera/stance and cutscene/text paths. Preserve build and identity gating.
- Extend common observations only for meaningful concepts not yet represented.
  Reuse the existing audio/text systems after decoding each title.

Exit: normal play, transitions and text paths tested with title-specific 3D
projection evidence; no dependence on CVS2's stage constants.

## Stage 6 — Arena spatial and multiplayer audio design

Depends on shared audio extraction; static PWS1/PWS2 discovery can start earlier.

- Trace screen projection, ground-plane position, height, stone ownership,
  transformation, active slots and moving-camera/arena transitions.
- Extend the common audio controller/mixer for four owners, semantic resources
  and an optional depth channel. Preserve the established two-player sound.
- Prototype two- and four-player scenes offline: crossing/co-location, opposing
  corners, jumping at different depths, simultaneous gauge changes and absent
  players. Measure clipping, masking, delay and worker/frame cost.
- Evaluate cues by listening before locking the mapping. Do not overload the
  stance pitch with depth or assign two players the same identity. Keep familiar
  cue-family volume controls. Record the chosen design and perceptual limits.

Exit: reusable arena/four-player audio that conveys the required distinctions.
If the spatial design is not intelligible, do not call arena feature parity done.

## Stage 7 — Power Stone, then Power Stone 2

Depends on stages 1–3 and 6.

- PWS1 proves arena selection, two-player spatial information, stones/power,
  transformation resets, environmental cue scope and all applicable text.
- PWS2 adds up to four players, HUD/owner mapping, join/leave/elimination,
  changing stages and simultaneous effects. Test each local player slot.
- Distinguish visible item/hazard access from hidden information. Keep item
  cues bounded and separate enough that they do not mask fighter beacons.
- Complete title-specific text audits, including results, continue, story,
  credits and any unsupported raster/glyph sources.

Exit: demonstrated arena play access; no two-player-only PWS2 support presented
as complete. Unknown resource/clock/text semantics remain named open items.

## Stage 8 — Revision, language and cross-game completion

Runs incrementally after each port, then once collection-wide.

- Cover all 18 ROM identities, both Z3U branches, applicable Arcade/Versus/
  Training modes, and distinct title-specific options. Distinguish shell
  language from native ROM language and from screen-reader pronunciation.
- Validate native names/encodings, substitutions, font/glyph maps and quotes.
  Japanese ASCII rejection inherited from CVS2 is a known gap, not multilingual
  support. Document each validated language and every missing text category.
- Exercise shell → game → pause → game → result → shell and rapid switches
  across all eight titles. Include load/reset, changed root and reused root,
  queue backlog, backend refusal, disabled audio and unavailable text.
- Audit online local-player ownership, spectator state and rollback behavior
  separately from offline success. No network match is needed for static work;
  controlled sessions are the later evidence requirement.
- Resolve all unresolved applicability entries or retain them as explicit
  release limitations. Record justified “not applicable” outcomes with evidence.

Exit: a truthful collection coverage ledger; a full parity claim requires all
applicable F01–F19 entries to be live-validated, not merely implemented.

## Stage 9 — Release and maintenance

Depends on the chosen release scope passing its gates.

- Run the full existing 28 PowerShell runners when common text/lifecycle code
  changes, plus all new adapter contracts and relevant Python checks. Confirm
  runner exit codes and compile success, not just green assertion text.
- Build the DLL, retain prior DLL/fixture manifests, verify installed hash after
  deployment, and smoke-test startup/signatures. Installation alone does not
  validate hooks executing on rare paths. Update README, controls and per-title
  limitations to match the tested release.
- Maintain a build/signature manifest, explicit capability registry and the
  exception ledger. Future common-system changes run the collection contract
  suite and relevant captured scenarios; hardware-specific changes run the
  relevant backend/variant cases as well.

Exit: reviewable release artifacts, rollback copy and accurate coverage notes.

## Capture and test package for each game

Prepare one compact run card after offline tracing, so a play session answers
several concrete questions. Store only bounded state/text/camera captures with
ROM/build/mode/player/phase labels and hashes, plus observed expected output.

- Selection: idle, move, reverse, confirm, cancel, re-entry, second player,
  every special choice and repeated same-character team slot.
- Combat: full/damaged/zero health; resource empty/gain/spend/full; player
  crossing and corners; standing/crouch/jump/knockdown where present; timer
  warnings; paused and idle-training control; KO, active-fighter and round change.
- Text: partial/completed/skipped quote, consecutive story pages with substituted
  names, credit entry/mid-roll/exit, blinking prompts, changing countdown,
  empty frame, stale frame and explicit read-current-text.
- Reliability: rejected speech retry, queue overflow/contention, failed read,
  signature mismatch, unknown value, wrong ROM, focus/overlay transitions,
  rapid game switches and audio-disabled operation.

Use real fixtures to establish semantics and synthetic fixtures to test edge
cases. Tests of a hook must verify arguments forwarded to the original as well
as captured output. Use wall-clock measurements, bounded worker/capture cost
and audible latency checks; a loop heartbeat cannot reveal its own stall.

## Efficiency and checkpoints

Prefer one discovery pass across the initializers, one common-system extraction,
and one pilot before expanding. Within each title, selection and combat are
early useful increments, but text/ending work remains in that title's ledger.
Batch captures after the questions and tooling are ready. No user session
should be spent debugging an uncompiled probe or a mislabeled capture field.

Progress is measured by evidenced capabilities and closed unknowns, not a
calendar estimate for reverse engineering. Reorder the post-pilot title work
only when evidence shows a cheaper dependency path, preserving the coverage
goal. Commit research, shared-system extraction, individual adapters and
validation evidence separately so each result is reviewable and reversible.
