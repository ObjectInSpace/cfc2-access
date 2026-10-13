# Astra investigation and Opus implementation workflow

## Current execution owner and sequence, 2026-10-13

The [complete Opus execution plan](port-tasks/opus-shared-selection-execution.md) governs conflicting sequencing and
model assignments below. Opus owns the remaining shared gameplay-audio and
transition implementation, integration/validation, game-packet audit and ports.
Reuse the selection channel integrated at `a800114`; its live acceptance is
pending. Complete required shared mechanisms before multiplying adapters.
Astra is optional escalation for a concrete blocker, not required groundwork.
Power Stone adaptations are a separate later phase. The optional protected
CVS2 reader and broad lifetime campaign remain deferred. Historical research
and stronger-design contracts are retained, not automatic next actions.

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

## Current priority — shared delivery first, 2026-10-05

Delivery objective: port the applicable CVS2 accessibility features to the
other seven collection games, using F01–F19 in the feature inventory as the
acceptance scope. Shared infrastructure is a means to deliver those ports,
not a separate substitute for them. Validate reuse by delivering the same
feature behavior in other games, with their actual rules and content, and
preserving CVS2. Isolated common-system tests are necessary but insufficient.
Track progress per feature and game through implementation, offline checks
and labelled live acceptance. A CVS1 pilot is the first delivery milestone,
not the end of the porting objective. Unsupported or unresolved features must
remain explicit; only evidenced inapplicability removes a feature from scope.

User-approved direction after 6cc26c8: do the shared work first, then resolve
game-specific problems within each game's delivery work. This supersedes the
PWS2-first next actions in the historical continuations below. Preserve the
PWS2 reader, batches and renderer evidence at 6cc26c8; do not resume that
research by default or make its completion a collection-wide dependency.

The next milestone is a working shared session/observation/output path,
validated through the existing CVS2 behavior and one bounded CVS1 feature.
Start by reviewing the implemented common systems and remaining dependencies;
reuse text_channel, audio and existing diagnostics. Implement only the shared
interfaces needed to deliver that milestone, not a speculative framework for
every future game feature.

Execution order:

1. Close the shared lifecycle/invalidation contract: identity, departure,
   replacement and same-address restore, with revocation at capture and output.
   Use the prepared boundary probe/run card for missing thread/order evidence;
   do not repeat completed static investigation or substitute pointer equality.
2. Implement the session/adapter and copied-observation boundaries, using the
   existing shared text, presentation and audio mechanisms as applicable.
   Test stale queued output, switching, resets and backend acceptance.
3. Migrate a bounded existing CVS2 path and deliver one explicitly scoped CVS1
   feature through the same interfaces. Resolve only the game-specific facts
   needed to supply and validate that feature. Run required shared regressions
   and targeted live checks before declaring this milestone complete.
4. Continue per-game feature packets through the proven common path. Keep
   native fields, names, phase rules, meter semantics and camera/shader problems
   in their adapters and evidence packets. Shared fixes apply across adapters.

Exit evidence is working shared code, preserved CVS2 behavior, one usable CVS1
feature, passing invalidation/regression checks and labelled live acceptance.
Choose and record the CVS1 feature and its acceptance criteria at the start of
implementation; do not claim that the existing names research is already ready.
Report delivered behavior and remaining acceptance gates, not just increasing
test/span counts. Research must answer a named delivery dependency.

PWS2's draw enclosure, diagnostic installer, full matrix/shader interpretation,
health normalization and four-player spatial policy remain tracked work for
PWS2. They do not block this shared milestone. If a game reveals a genuine
shared-contract gap, describe the cross-game effect and address that gap in the
common system. Shared-first does not mean solving every game's semantics before
shipping the first adapter feature.

Continuation, 2026-10-05, from clean 904b955: added unwired native bounded
reader and fixed batch storage; 234 native reader and 70 batch checks pass.
The HUD manifest now retains 53 spans and 27 operands, including the complete
rectangle helper, complete vertex routine and a derived-position consumer.
All 48 PWS2 Python tests pass. See the HUD trace for rectangle halving,
derived constant destinations and alternating strip expansion. Command 7's
shader consumer remains unresolved. Enclosure/admission, worker/footer,
installer and live acceptance remain open; nothing was installed or deployed.
Next: implement the list enclosure and arm lifecycle against the v1 contract,
then worker integration/rollback tests; continue generic binding-array/shader
tracing and remaining matrix algebra independently.

Continuation, 2026-10-05, from clean 5f9e3fc: traced command-buffer reset and
count-based consumption, command 4's matrix argument order/setup helper, and
command 7's three-row transpose upload. The HUD manifest retains 50 spans
and 24 data ranges; all 45 PWS2 Python tests pass. The
[v1 draw-capture contract](pws2-draw-capture-contract.md) now specifies stage,
association, ten-read/904-byte event bounds, invocation/batch limits and
lifetime/loss rules, with fifteen synthetic reader/parser tests. Native
enclosure/reader/transport/installer acceptance is still open. No runtime
source, deployment, audio wiring or live state changed. Next: implement that
bounded offline contract and decode the retained matrix-setup helper's
derived constants, rectangle helper, shader bindings and remaining vertices.

Continuation, 2026-10-05, from 92f805f: the vertex submission wrapper and
0x14004ca10 copy into another command buffer after the session gate. The
native payload capacity comparison excludes the subsequently added header;
it is not a safe-reader bound or proof of final pixels. The HUD manifest now
has 43 spans and 24 data ranges; all 27 PWS2 tests pass. Next: command-buffer
consumption and matrix-command handling, alongside the bounded capture
contract. No runtime source, audio/ownership recovery or live state changed.

Continuation from c801b79: the types-2/3 queue helper returns a sorting scalar,
with fixed, sign-flipped and transformed-W branches. It does not calculate a
screen position. The HUD manifest now retains 41 spans and 24 data ranges;
all 26 PWS2 tests pass. No native source, deployment or live result changed.
The [remaining Astra work](collection-astra-remaining.md) separates unresolved
renderer/capture/session/audio decisions from bounded implementation work and
records the wider cross-game pilot and feature-packet obligations. Next trace:
the vertex submission and matrix-command consumers. Also make the raw
diagnostic's bounded read and lifetime contract concrete; it need not wait
for a universal health maximum.

Continuation, 2026-10-04, from clean e308e64: the
[HUD trace](pws2-hud-ownership-trace.md#publisher-and-resource-continuation-from-e308e64)
corrects the publisher's enclosing entry to 0x140a6e250, establishes outer
state 2 / inner state 2 publication with the +0x52bd9 exclusion, and follows
type-zero resources through allocation, binding, release, matrix construction
and list-8 submission gates. The HUD manifest now retains 40 spans and 21
data ranges. Object and resource transforms are built after callbacks; local
layout offsets remain unsuitable for direct audio pan. A further actor-value
writer and descriptor-driven subtraction establish conditional zero/one
floors, not a complete health range. Renderer types 0/1 and 2/3 copy different
second matrices; segment and queue rejection mean a matrix load is not an
accepted draw command.

All 25 PWS2 Python tests and eleven metadata tests passed. Recovered audio
passed 35 scene and 44 mixer checks, with compiler access on retry. Audio and
ownership recovery are preserved. An unwired assembly entry prototype now
passes synthetic active, inactive, nested and unrelated-caller ABI checks;
the production filter also compiles. It is excluded from the DLL build and
has no installer, capture reader or audio calls. No deployment or live result
is claimed. [PWS2-R02](port-tasks/pws2-draw-stage-capture.md) records this
component and the remaining bounded capture, final renderer and gauge writer
work. Shared production session/audio integration still needs lifecycle
thread/order evidence and accepted gameplay semantics. Continue useful offline
work before the targeted live exercises described in the packet.

Continuation, 2026-10-04, from b01dc6f: the
[HUD ownership trace](pws2-hud-ownership-trace.md) establishes separate fixed
actor and compacted display-layout indices, general-pool callback publication,
and actor+0x1dc feeding gauge animation arrays. Its independent manifest retains
nine spans and nine pinned operands. Ownership capture schema 2 adds bounded
raw display-state reads. Four HUD tests, eight capture tests, seven actor tests
and eleven metadata tests passed; preserved audio passed 35 scene and 44 mixer
checks. No live or runtime integration result is claimed. Continue with publisher
entry conditions, list-8 resource/matrix construction and gauge writers before
the draw-stage probe and shared session integration.

Actual recovery state at this continuation: the working tree began clean at
b01dc6f, with audio and ownership recovery committed. `git stash list` returned
no entries. This session did not apply, drop or modify stashes. Historical notes
below about both stashes remaining are not a verified description of this tree.
Preserve the recovered committed work regardless of stash availability.

Committed recovery checkpoint, 2026-10-04: ownership preparation is saved in
cee7511; the accompanying audio checkpoint preserves the optional, unwired
four-participant scene API and its 35 passing offline checks. The existing
mixer suite and full DLL build/import check also passed (compiler warnings
remain in unrelated sources). No deployment, listening test or gameplay
integration occurred. The recovery notes below are historical; continue from
the committed files. Redundant recovery stashes can be removed after verifying
their contents are preserved. Next: actor/HUD ownership, stage-specific capture
and shared session integration, with offline work preceding targeted live work.

Ownership recovery, 2026-10-04: recovered the seven actor-audit, trace and
capture files from `PWS2 ownership capture increment (uncommitted)` without
dropping that stash. Its workflow entry is reconciled here with newer notes:
the actor audit retains 64 spans and the [ownership capture tool](pws2-ownership-run-card.md)
is available for preliminary read-only samples. It is not draw-stage
synchronized and does not certify camera/HUD ownership or authorize audio.
Both saved prototypes are now restored in the working tree. The committed
transform-target continuation remains intact. Offline preparation comes first;
live gameplay checks are authorized afterward.

Recovery validation: seven actor-audit tests, six ownership-capture tests and
eleven metadata-capture tests passed. Capture CLI help and `git diff --check`
passed. These are offline checks; no process attachment or deployment occurred.

Audio recovery, 2026-10-04: the earlier four-participant scene prototype and
its two test files were located in the stash named `four-player audio scene
layer (uncommitted, unwired)` and applied without dropping the stash. The
current audio files had no intervening changes relative to its base. All 35
offline scene checks and the existing audio mixer suite passed after recovery.
The scene API remains unwired to gameplay. The separate `PWS2 ownership
capture increment (uncommitted)` stash is also present; absence from the
working tree did not mean those prototypes were lost. Continue from the
recovered work rather than reimplementing it. No live validation is claimed.

Latest continuation, 2026-10-04: the [transform-target trace](pws2-transform-target-trace.md)
retains the multi-point producer and closes the resource-vector translation
read, including the conditional matrix-stack failure path. Its separate
manifest now contains 13 spans and 13 data ranges. The current clean starting
baseline was 9fbe2fb; earlier uncommitted audio/capture prototypes are absent.
The user wants offline preparation completed before targeted live sessions,
and permits live work afterward. Camera/HUD ownership, shared integration and
listening acceptance remain open; the campaign is not complete.

Latest continuation, 2026-10-02: [PWS2-T01](port-tasks/pws2-projection-reference.md)
is ready for bounded Opus offline-tooling implementation, with a 58-span
evidence base and 24 synthetic acceptance cases. It evaluates supplied
matrices for one traced primitive; live capture, camera/HUD ownership and
production audio integration remain outside its acceptance. The reference
tool has not been implemented. The wider Astra campaign remains incomplete.

The bounded Opus research packet
[CVS1-R01](port-tasks/cvs1-branch-a-names.md) is ready for research, not runtime
implementation. No name mapping was performed while preparing it. The current
[boundary review](collection-session-boundary-review.md) traces the screen
manager's deferred replacement after screen update; lifecycle manifest schema 3
retains 83 spans. All 52 collection Python tests pass. The tested five-site
probe from `21da6ff` remains staged only, with no deployment/live result.
Shared runtime integration and arena/four-player proof remain incomplete.

The [initial PWS2 record/input trace](pws2-actor-trace.md) starts the independent
four-player investigation. It retains fifty-eight spans and six passing offline
tests. Four fixed records, a filtered list, conditional indexed input routing
and variable processing order are established; controller/HUD
ownership, gameplay field semantics and live observations remain open.
The draw continuation adds indexed transform blocks and repeated draw passes,
without assigning a visible camera or pass meaning.
The next continuation establishes four mutable matrix slots, affine vector
math and temporary resource matrix push/pop. A further continuation traces
homogeneous projection, perspective divide, mutable viewport composition and
rejection for one actor-linked two-point primitive. HUD ownership and the
audible camera pass remain unproved; the current-matrix pointer cannot be
sampled independently of its draw stage. Setup helpers also distinguish
context direction/flag writes from actual matrix mutation. Constructor
premultiplication, the fixed-angle math path and initial/post-draw slot-3
setup are now traced. Their depth parameters differ; ordinary-draw entry
contents still need stage-specific evidence. Template writer coverage remains open.

User-approved direction, 2026-09-26. Use Astra here for the upfront discovery,
architecture proof and difficult reviews; hand bounded implementation work to
Opus. This is a work allocation, not a claim that either model is incapable of
the other's tasks. No automatic model switching or subagent execution is set
up by this document.

The [port plan](collection-port-plan.md) remains the feature/delivery plan.
The [architecture](collection-port-architecture.md) remains the common-system
contract. This document defines how evidence and implementation cross sessions.

## Current baseline

- `87daa25`: feature inventory, initial native investigation, architecture,
  staged plan and `tools/collection_port_audit.py`.
- `6849ccc`: CVS2 narrative/rendered-text readers and shared text channel.
  All 28 PowerShell regression runners and the DLL build passed at that commit;
  existing build warnings remain. No new deployment/live validation was done.
- Eight game dispatch entries and nine initializer targets are recorded.
  These are initial leads, not completed per-feature traces for seven ports.
- The common adapter/session/presentation architecture is proposed, not yet
  implemented. The CVS1 pilot and four-player audio prototype are not done.
- No complete runtime implementation packet is ready for Opus yet. Opus can
  receive a bounded research/tooling task earlier if it has its own contract.
- Session-end commit messages and packet status supersede this dated baseline
  as work progresses; always inspect the actual Git state before editing.

## Upfront Astra campaign

Work through these deliverables, committing coherent results. Continue useful
offline work when a live capture is unavailable; name the missing capture
rather than treating the whole campaign as blocked.

1. Trace identity and lifecycle for every game and variant. Extend the existing
   initializer leads through allocation, root publication, update and teardown.
   Resolve Alpha 3's branch discriminator and 18-ROM routing. Separate original
   hardware references from CFC2's actual runtime mechanism.
2. Trace applicable F01–F19 acquisition paths. Record phase gates, owners,
   field widths/endianness, readers/writers, hook arguments and rendered-text
   visibility/completion. Trace camera/resource semantics independently.
   Link shared trace evidence instead of repeating it for each feature.
3. Obtain targeted captures and falsifying controls. Keep static evidence,
   real captures, synthetic tests and audible live observations distinct.
   Create compact run cards before consuming a play session.
4. Implement and prove the shared session/observation/presentation boundaries
   through the CVS2 migration and CVS1 pilot. Preserve existing controls and
   cue identity; document intentional changes and game-specific exceptions.
   A written interface alone does not satisfy this deliverable.
5. Investigate and prototype arena/four-player audio early enough to influence
   the contracts. Test participant identity, HUD ownership, depth versus height,
   camera movement and simultaneous cues. Listening evidence is needed before
   calling the design validated.
6. Produce individual Opus work packets using the
   [packet template](collection-task-template.md). Each needs concrete scope,
   traced inputs, fixtures, acceptance criteria, dependencies and explicit
   unknowns. Mark unresolved discovery as research, not ready implementation.

This front-loads most anticipated Astra effort. Keep later Astra checkpoints
for evidence conflicts, architecture changes, difficult runtime failures and
completed-title reviews. Do not promise all future Astra needs can be removed.

## Relationship to the existing stages

- Stage 0: baseline is committed and checked; maintain evidence accounting.
  Do not rerun the whole baseline merely to start a fresh session.
- Stage 1: Astra leads the cross-game trace campaign. Opus may carry out
  explicit mechanical extraction or bounded trace tasks with reviewable output.
- Stages 2–3: Astra leads the shared-system extraction and pilot proof. Small
  implementation pieces can go to Opus once their interfaces are concrete.
- Stages 4–5: Opus implements prepared conventional-fighter packets; Astra
  resolves remaining discovery and reviews title milestones.
- Stage 6: Astra leads the uncertain spatial/audio design and proof; Opus can
  implement the agreed mixer/controller changes against specified tests.
- Stage 7: Opus integrates the prepared Power Stone adapters. New spatial or
  lifecycle evidence returns to the shared design review.
- Stages 8–9: Opus handles coverage completion, regressions, packaging and
  documentation. Astra reviews unresolved semantics and the parity evidence.

These are default responsibilities, not mandatory approval gates. Neither
model should ask the user for permission merely because the work crosses one
of these categories. Continue within the authorized scope and report decisions.

## When an implementation packet is ready

- Exact starting commit and dependencies are identified. The implementer can
  find the governing instructions without the previous conversation.
- Relevant trace records establish the access path and ABI. An online address,
  a matching structure shape or a familiar title name is insufficient.
- Shared interfaces exist and the packet names their actual symbols/files.
  Otherwise the packet is explicitly an interface-development task.
- Expected output and negative cases are concrete. Real fixtures establish
  game semantics; synthetic fixtures cover edge cases without pretending to
  prove a field's meaning.
- Required code validation can run with available tools. Any live acceptance
  dependency has a run card and remains open until observed.
- Scope is one reviewable capability or coherent group, not “port the game.”
  The packet names which F-numbers, ROMs and modes it covers.
- Outstanding uncertainties are listed with their impact. Unrelated unknowns
  do not block the packet; unknowns essential to its decoding do.

## Opus execution and return contract

Read CLAUDE.md, the packet and its referenced evidence. Verify the base commit
or inspect subsequent relevant changes. Use the same checkout sequentially,
or separate worktrees if concurrent work is explicitly arranged; avoid two
sessions editing the same files simultaneously.

Implement through common systems. Keep native decoding and justified policies
local. Do not copy a speech queue, mixer, hotkey handler or settings writer into
a game adapter. Do not fill unknown offsets, caps or phases with guesses.

If code or captures contradict the packet, document the smallest reproducible
contradiction and revise the evidence. Astra's prior conclusion is not an
authority overriding the executable. Continue independent work; do not claim
the dependent feature is complete.

Return a commit plus an updated packet recording:

- What changed, and any shared contract or exception affected.
- Trace/fixture provenance and newly established facts.
- Exact commands, exit codes and relevant results; distinguish tests run from
  tests merely recommended. A test that never compiled did not pass.
- Remaining unknowns and unperformed live checks.
- A short explanation of why the implementation meets its acceptance criteria.

Run checks appropriate to the change. Changes to common text/lifecycle behavior
require the existing 28 runners plus new relevant adapter/contract checks and
a DLL build. Compilation may require access to the installed WinGet compiler
outside the sandbox; report environmental failures separately from code failures.
Do not broaden testing endlessly after required checks pass.

## Astra checkpoint and escalation criteria

Use Astra for a focused review when evidence conflicts, a hook's argument or
lifetime contract remains uncertain, a fix requires shared-interface changes,
or a crash/race persists without an established cause. A completed title also
gets a review against F01–F19 and the actual tested ROM/mode coverage.

Supply the commit/diff, exact trace instructions, fixture metadata, reproduction,
expected versus observed output and attempted hypotheses. “It doesn't work”
without those artifacts is not an effective handoff. A routine compiler error
or straightforward test failure can be resolved by Opus directly.

Both models use the same completion standard. Neither can substitute reasoning
effort or confidence for instruction evidence and live verification. Start at
the user's chosen effort; increase it for a specific unresolved problem rather
than treating every assembly task as requiring the highest setting.

## Session continuity

The next Astra session should use the [upfront brief](collection-astra-brief.md).
At each session boundary, update task status and evidence, commit cohesive work,
and leave the next concrete action. Keep task packets in `docs/port-tasks/` as
they are created. Maintain their statuses in this document or a linked index.

## Handoff update — 2026-09-27

The first stage-1 investigation increment started from clean `5eb3245` and is
recorded in the [bounded lifecycle trace](collection-lifecycle-trace.md) and
[reproduction manifest](collection-lifecycle-manifest.json).

- Reproduced the original eight-game/nine-initializer audit.
- Traced all nine setup calls, primary/secondary root publication sizes,
  and update-wrapper calls. Extended alternate Alpha 3 beyond its old excerpt.
- Mapped all 18 ROM names through native descriptors and lookup code. Alpha 3
  uses S+0x2b4 zero for indices 6/7, nonzero for 16/17. The visible setting
  label and all entry-path ordering remain open.
- Corrected CFJ's separate context slot, `0x142CF9D48`, and separated its
  cleanup callees from initialization. Recorded common and PWS2 cleanup edges.
- Added `tools/collection_lifecycle_audit.py`: 45 bounded spans with bytes,
  resolved references, section identity and hashes. Six offline audit-tool
  tests passed. No runtime source changed; the 28 DLL runners/build were not
  rerun for this research/tooling increment.
- No new live checks, capture, deployment or implemented reader. Root lifetime,
  safe hook ABI and feature acquisition remain unresolved. Shared boundaries,
  CVS1 pilot and arena/four-player audio proof remain pending.

Next concrete work: trace arena destruction and reset/load invalidation; follow
CVS1's update into selection/phase and name consumers. Prepare a tested bounded
metadata capture tool and compact run card before requesting a CVS2 → CVS1 →
shell transition session and four-variant Alpha 3 cross-check. The trace names
the exact missing observations. Continue independent offline tracing meanwhile.

Opus runtime implementation packets remain draft/not ready pending essential
contracts and pilot evidence. The broader campaign is in progress, not complete.

## Handoff continuation — 2026-09-27, from 9f10a12

Started from clean `9f10a12`. This is another stage-1 research/tooling
increment. See the [new trace](collection-lifecycle-continuation.md) and
[metadata run card](collection-metadata-run-card.md).

- Reproduced the original audit and six lifecycle tests before editing.
- Traced the shell destructor's arena release edge and its deleting wrapper.
  The explicit singleton clear targets `0x141C14F90`, not S's slot
  `0x141C14F98`. Destructor event coverage and indirect cleanup remain open.
- Traced a CVS1 primary/secondary copy-and-restore branch, its direct caller,
  and copy-helper instruction evidence. Contents can be replaced while the
  root address remains unchanged. The visible reset/load action is not yet
  associated with this branch.
- Established CVS1 P = R+0x3e58, the native-update-to-phase dispatch, and two
  subdispatch leads. Selection phase meanings and name consumers remain open.
- Expanded the audit/manifest from 45 to 57 bounded instruction spans plus
  two raw phase-table entries. No new safe hook ABI is certified.
- Added `collection_metadata_capture.py`: read-only, manifest-signature-gated,
  bracketed raw metadata, explicit failure records, fresh output paths, and
  labelled bounded sampling. Full samples request 488 bytes in 32 reads.
  Roots/contexts are recorded as pointers and never dereferenced by the tool.
- Prepared the run card for CVS2 → CVS1 → shell and four Alpha 3 identities.
  No CFC2 attachment, live capture, listening session or deployment occurred.

Validation actually run:

- `python tools/collection_port_audit.py`: exit 0, eight games/nine initializers.
- `python tools/collection_lifecycle_audit.py --manifest-out docs/collection-lifecycle-manifest.json`:
  exit 0, nine paths, 18 ROMs, 57 spans.
- `python tools/test_collection_lifecycle_audit.py`: exit 0, six tests passed.
- `python tools/test_collection_metadata_capture.py`: exit 0, eleven tests
  passed. Synthetic failure/transition controls and real dump signatures;
  Windows read transport tested against this test process only, not CFC2.
  An initial test incorrectly expected 34 reads; enumerated calls established
  32 and corrected the assertion before the final passing run.
- `python tools/collection_metadata_capture.py --help`: exit 0.
- `git diff --check`: exit 0. Runtime source unchanged; the 28 DLL runners and
  DLL build were not rerun for this increment.

Next concrete work: use the prepared run card for the missing labelled live
observations. Independently follow CVS1 tables `0x141217568`, `0x141222398`,
and `0x141222420` through actual selection/name consumers; trace callers and
flags around `0x1405C5D50` / `0x1405ADEE0`; resolve destructor vtable consumers
and allocator +0x60. Metadata equality cannot validate in-place restore or ABA
reuse, so session epoch design still requires a traced discontinuity source.

F01–F19 acquisition coverage, implemented shared boundaries, CVS1 pilot and
arena/four-player listening proof remain pending. Runtime Opus packets remain
draft/not ready; the broader campaign is not complete.

## Handoff continuation — 2026-09-27, from ed547da

Continued offline from clean `ed547da`. The new
[CVS1 acquisition trace](cvs1-acquisition-trace.md) and
[manifest](cvs1-acquisition-manifest.json) preserve this increment.

- Established the nested route P+0=2 → P+1=1 → P+2=0 to the existing
  restore-preparation function, subject to the traced gates. Adjacent table
  storage does not establish a flat selector domain.
- Traced the post-copy pool rebuild at `0x1405ADEE0`, reinforcing that root
  pointer equality cannot establish state continuity. The originating visible
  reset/load action remains unassigned.
- Traced the guarded P+0x48 mode dispatch to two different P+3 tables.
  Concrete selection leads are recorded, but native highlighted IDs and name
  consumers remain unresolved. Do not reuse a substate map across the branches.
- Traced CVS1 positioned prompts through formatting, queue reset/drain, and
  glyph rendering. Record headers are three bytes; strings begin at +3.
  The renderer interprets caret escapes and has a distinct style-7 path.
  CVS2's +8 text offset and decoding policy cannot be copied into this reader.
- Traced one blinking free-play/coin/start producer and five selected literals.
  F17/F18 have partial code evidence only, not live visibility or a reader.
- Added `tools/cvs1_acquisition_audit.py`: 19 bounded spans, seven explicit
  table edges, five literals and ten guarded mode entries. It is separate
  from the metadata capture's lifecycle manifest; live read scope is unchanged.

Validation actually run:

- `python tools/cvs1_acquisition_audit.py --manifest-out docs/cvs1-acquisition-manifest.json`:
  exit 0, counts above.
- `python tools/test_cvs1_acquisition_audit.py`: exit 0, five tests passed,
  including changed table/literal rejection and nested-selector preservation.
- `git diff --check`: exit 0. No runtime source changed; no DLL build,
  deployment, CFC2 attachment, live capture or audible check. The previous
  17 lifecycle/metadata tests were not rerun because those files did not change.

Next concrete offline work: follow `0x141237D80` and `0x141240D98` through
the real choice writers and name/sprite consumers; trace style-7 renderer
`0x140F31050`; establish a bounded text capture ABI and frame/overlay ownership
before asking for a text session. Continue the outstanding destructor/event
trace. The existing metadata run card remains ready for the lifecycle/Alpha 3
session, but cannot prove text rendering or in-place state replacement.

The shared-boundary implementation, CVS1 pilot and arena/four-player listening
proof are still pending. Runtime Opus packets remain draft/not ready.

## Handoff continuation — 2026-09-27, from c50c11c

Continued offline from clean `c50c11c`. Updated the
[CVS1 acquisition trace](cvs1-acquisition-trace.md) and its manifest.

- Traced branch B's actor dispatch to a navigation writer. The selected byte
  is P+0x78+4*owner+slot; owner comes from actor+0x688 and slot from
  P+0xa0+owner. Controller identity and legal ID bounds remain open.
- Traced the branch initializer's object+0x18 callback publication and a
  separate subtype-2 handler that uses the same selected byte for coordinates.
  Pinned four descriptors that publish subtype 2 and yield owners 0,1,0,1.
  Scheduler invocation and character-name
  mapping are still missing. No live selection reader is certified.
- Resolved the style-7 leaf: signed byte <=0 ends its glyph loop, including
  high-bit bytes. It has no ordinary-path caret escape. Preserve style/raw
  bytes in any eventual capture contract.
- Expanded the separate acquisition audit to 25 bounded spans and 11 explicit
  table edges plus four selected object descriptors; five literals and ten
  guarded mode entries remain. Lifecycle
  capture scope and runtime sources are unchanged.

Validation actually run: acquisition audit/manifest regeneration, exit 0;
seven acquisition tests, exit 0; `git diff --check`, exit 0. No DLL build,
deployment, game attachment or live/audible validation. Other test suites
were not rerun because their code and manifests did not change.

Next offline work: trace actor+0x688 initialization and controller association,
the object scheduler call, then the native ID's
name/resource consumer. Branch A still needs its own choice trace. Exact
text-hook ABI, frame/overlay ownership, lifecycle discontinuities and the
existing metadata live observations remain outstanding. Shared-boundary
implementation, CVS1 pilot and arena/four-player proof remain pending;
runtime Opus packets are still draft/not ready.

## Handoff continuation — 2026-09-27, from b29ef63

Continued offline from clean `b29ef63`. Extended the
[CVS1 acquisition trace](cvs1-acquisition-trace.md) and reproducible audit.

- Traced two explicit owner-initialization loops that write actor indices
  0 and 1. One is on the already-pinned phase-2/substate-0 route. Later
  ownership changes and physical-controller association remain open.
- Traced the branch-B input helper's current/previous/new/repeat words and
  the branch's conditional copying from one side to the other. Owner index
  alone cannot identify the physical input source.
- Closed object allocation → list-4 insertion → callback scheduling →
  coordinate consumer. The pool insertion target is now an explicit checked
  table entry. Final composited visibility remains unproved.
- Traced the offset-based graphic-resource lookup and render-callback
  enqueue. The specialized list-renderer lead is `0x1405E1980`, through
  callback `0x1405DDCD0`; the nearby generic list renderer is not this route.
- Acquisition evidence now has 33 spans, 12 table edges, four selected object
  descriptors, five literals and ten guarded mode entries. No runtime reader,
  lifecycle-capture scope or implementation-packet readiness changed.

Validation: regenerated the acquisition manifest, exit 0; eight acquisition
tests passed, including rejection of a changed pool-insertion target;
`git diff --check`, exit 0. No DLL build, deployment, game attachment,
live capture or listening check. Unchanged runtime/lifecycle tests not rerun.

Next work: follow `0x1405E1980` render-type branches and the resource's
publication/record consumers toward the selected native ID's name/graphic
mapping; trace writers of input-copy flag `0x1422D2B85` and input-source
publication before assigning controller identity. Continue branch A and
the outstanding lifecycle discontinuity investigation independently.
Prepared lifecycle/Alpha 3 live measurements, shared-boundary proof, CVS1
pilot and arena/four-player work remain pending. Opus packets remain draft.

## Handoff continuation — 2026-09-27, from 215b70a

Continued offline from clean `215b70a`. Updated the
[CVS1 acquisition trace](cvs1-acquisition-trace.md) and audit manifest.

- Found the input-copy flag's overlapping dword reset at `0x1405fec5b`
  and mode-4 write from P+0x18 at `0x1405fecf9`. Added bounded write-footprint
  extraction so exact-address lookup does not hide the reset. Allowed flag
  values and physical-controller meaning remain unproved.
- Traced the specialized list renderer's active-object gate and guarded
  twelve-entry render-type table. Selection-coordinate objects are initialized
  with type 10, which calls `0x1405DF8E0`.
- Connected object coordinates/scaling and resource+0x90 to the shared
  `0x140F36200` wrapper, then `0x140F34CF0`. This is an offline draw path;
  character-name mapping and final composited visibility remain open.
- Traced three selected compressed resource routes through publication of
  R+0x52b0 as context-buffer+0xa46800. Other loader routes remain separate.
  The shared draw routine walks structured records and submits commands;
  downstream command interpretation and character labels remain open.
- Audit now pins 42 spans, 12 explicit pointer-table edges, the twelve-entry
  render table, four selected descriptors, five literals, ten mode entries,
  two overlapping flag writes in the reviewed initializer, and three selected
  resource routes/layout rows.

Validation: regenerated the acquisition manifest, exit 0; eleven acquisition
tests passed, including valid-render-branch swap rejection and preservation
of the wider flag reset, plus resource-destination mutation rejection;
`git diff --check`, exit 0. No runtime source change,
DLL build, deployment, live capture or audible check. Other suites unchanged
and not rerun.

Next offline work: trace resource fixups and the command consumer downstream
of `0x140F34CF0`, then find the selected ID's character graphic/name consumer.
Determine whether the dump contains the populated resource buffer before
preparing a targeted capture. The coordinate objects may describe a marker rather
than the character itself. Trace P+0x18 and input-source publication before
assigning controller identity. Branch A, lifecycle discontinuities, prepared
live observations, shared-boundary proof and the broader feature campaign
remain pending. Runtime Opus packets remain draft/not ready.

## Handoff continuation — 2026-09-27, from 48db0a8

Continued offline from clean `48db0a8`; extended the
[CVS1 acquisition trace](cvs1-acquisition-trace.md).

- Verified that this dump's CVS1 root and common-context pointers are null.
  Populated selection assets cannot be recovered through those pointers;
  asset-content verification requires a prepared live resource capture.
- Connected the non-special selected byte to actor+1, two subtype-10 object
  descriptors, a change-triggered refresh request and an asset-address consumer.
  Side 1 remaps IDs 9, 0x12 and 0x19 to 0x23, 0x24 and 0x25; no names assigned.
- Traced staged refresh: request 3 handles side 0 and leaves side 1 pending.
  Mapped mode-dependent resource slots and the bounded registration check in
  `0x140F31BE0`; final upload and visual content remain unproved.
- Traced resource offset fixups and shared command enqueue. This is still
  upstream of final rendering, not proof that a queued graphic is visible.
- Corrected the choice-writer span label/prose to say excerpt: code continues
  past `0x14060690A` into confirmation logic. Do not treat it as a full CFG.
- Audit now records 49 spans, 13 explicit pointer-table edges, six selected
  descriptors, graphic-mode/resource-subtype tables, one command-type entry,
  and dump pointer snapshots, in addition to the prior evidence.

Validation: regenerated acquisition manifest; twelve acquisition tests passed,
including a changed graphic-mode route; `git diff --check` passed. No runtime
changes, deployment, game attachment, live asset capture or audible validation.
Unchanged suites and DLL build not rerun.

Next work: prepare a separately tested bounded selection/resource capture
using the traced side/slot/refresh provenance, and inspect `0x140049170` plus
the downstream graphics queue consumer. Character-name mapping still needs
asset or live evidence. The 0x23 special path, full confirmation CFG, physical
controller association, branch A and lifecycle discontinuities remain open.
Existing metadata capture remains ready for its own lifecycle/Alpha 3 session;
it cannot substitute for the new selection measurements. Shared-boundary proof,
CVS1 pilot and the broader feature campaign remain pending; Opus packets draft.

## Handoff continuation — 2026-09-27, selection capture from 30be231

Prepared the next concrete measurement rather than extending offline labels.
See the [CVS1 selection run card](cvs1-selection-run-card.md).

- Added `tools/cvs1_selection_capture.py`, reusing the read-only Windows
  transport and raw metadata observation. Startup verifies both manifests'
  code spans and the relevant dispatch/descriptor bytes. No game writes/hooks.
- CVS1 ROM/callback/publication checks gate native-state reads. The phase
  pointer must match R+0x3e58. Two observations retain raw phase, actors,
  owners, slots, refresh flags, input-copy flag and resource provenance.
- Optional asset prefixes are capped at 4 KiB per side with conservative
  phase/owner/slot/actor/cooldown/ID gates and the traced side-1 remaps.
  Special 0x23 is skipped; no character name or image decoding is claimed.
- Full state samples request 1,282 bytes in 52 reads; optional maximum
  prefixes raise this to 9,474 bytes in 54 reads. Startup signatures are
  separate. Matching brackets cannot exclude ABA or in-place asset changes.
- Prepared screen-reader-friendly commands and a small movement/control
  session. No live game attachment, capture, deployment or audible check.

Validation: nine new selection-capture tests passed (dump signatures/data
mutation, remap/raw payload, default no-asset read, wrong-identity no-root
read, root transition, unreadable/partial reads, special-ID/slot/owner/cooldown
gates, phase gates, asset cap); eleven shared metadata/transport tests passed;
CLI `--help` and `git diff --check` passed. No runtime source changes, DLL
build or unchanged acquisition/lifecycle test reruns.

Next: run the prepared selection session and retain action notes, native
values, failures and optional raw prefixes. Review the results before mapping
names or increasing capture scope. Independently continue `0x140049170`,
the graphics queue consumer, special-choice/confirmation paths and lifecycle
proof. The capture is ready for measurement; the runtime implementation
packets, shared-boundary proof and broader collection campaign remain pending.

## Handoff continuation — first live control, 2026-09-28

After `ddefd10`, the user reported CVS1 selection ready. Read-only capture
attached to PID 12508 and passed the signature/identity gates. See
[the live control record](cvs1-live-selection-control.md) and its two lossless
compressed JSONL fixtures (eight control samples plus 120 requested-movement
samples). No assets, hooks, deployment or speech changes.

The captured screen uses native mode 0 / phase bytes 2,1,1,6, outside the
branch-B asset gate. Side-0 selected and actor bytes remained 16; side-1
selected byte remained −1. All 128 brackets matched with no read errors.
This is a first live state/identity control, not a verified movement or
character-name test. Actual input actions and mode label await user report.

On resumption, the complete sample file survived but the process handle was
gone and CFC2 was not running. Asked the user to clarify the earlier action
and relaunch/return to selection when ready. Next coordinated capture should
allow time for focus switching. Independently follow branch A's P+3=6 lead
`0x1405DBC30` and its P+4 dispatch; do not widen asset gates by analogy.

Validation for this evidence increment: both fixture gzip copies round-trip
to the original JSONL bytes and recorded hashes; parsed all samples and
compared brackets/selection transitions; `git diff --check` passed. No source
changes or test/build reruns. Runtime packets remain draft/not ready.

## Handoff continuation — confirmed movement window, 2026-09-28

The user clarified that the first window probably had no requested movement,
then reported CVS1 character select open again. Recorded a new 60-second
state-only window from PID 29680; process exit 0, all 240 samples retained.
See [movement window 02](cvs1-live-movement-02.md) and its lossless fixture.

The user confirmed the requested Right/pause/Left inputs were made with the
game focused and produced movement sounds; highlighted names were unknown.
The native side-0 selected and actor bytes changed together through
1→0→16→0. Mode 0 / phase bytes 2,1,1,6,6 remained stable. Three transitions
versus two reported inputs means direction/timing correspondence is not
proved. Do not invent a name or navigation order from these numbers.

There were 239 equal brackets outside the branch-B asset gate and one phase
block mismatch, no read errors or asset reads. Side 1 stayed inactive in the
sampled fields. No runtime changes, hooks, deployment or speech feature.

Updated the earlier control's action provenance. Verified the new gzip fixture
round-trips to the original JSONL and recorded its hash; parsed all rows and
transition values; `git diff --check` passed. Source/tests unchanged and not
rerun. Next independent work is branch-A writer/consumer tracing for actor
state 4 against the retained live sequence. Names and runtime packets remain
unverified/draft; no further user inputs are needed for this finished window.

## Handoff continuation — branch-A trace from ac1ef14

Traced the active branch against movement window 02. See
[branch-A selection evidence](cvs1-branch-a-selection.md).

- P+3=6 and P+4=6 lead through `0x1405DBC30` to the actor dispatch;
  actor state 4 selects `0x1405D9E70`. Pinned each individual table edge.
- The actor handler reads the selected slot, updates group/position fields
  using input words, and copies the selected byte into the actor indexed by
  owner+2*actor[5]. With the live actor[5]=0 this matches the captured field.
- Located a separate list-4 selection-object writer. Its group/position match
  gates publish object+0xc6 into the active selected slot at `0x14060b431`.
  Traced object creation/callback publication and both nested dispatches.
- Object catalog rows supply native IDs; setup caller semantics, input-word
  publication, character names and exact input/transition timing remain open.
- Existing capture omits P+0x104/+0x10c/+0x111 and actor+0x5be/+0x5d8;
  it cannot reconstruct the navigation decisions behind 1→0→16→0. Do not
  extend the branch-B asset contract by analogy.

Acquisition audit now pins 57 spans and 17 explicit pointer-table edges plus
the branch-A P+4=6 RVA entry. All added spans are <=4 KiB; larger actor
handler evidence is explicitly split into bounded excerpts, not a full CFG.

Validation: regenerated manifest; thirteen acquisition tests passed, including
branch-A dispatch mutation rejection; nine selection-capture tests passed
against the expanded signatures; `git diff --check` passed. No live reads,
game inputs, capture-scope expansion, runtime changes or deployment this turn.

Next: trace setup/catalog and input-word publication; add the specific group,
position and input fields to state-only capture with failure tests before
requesting further movement. The code/live evidence now connects native
choice publication to the actor copy, but names, branch-A graphics and runtime
packets remain unverified/draft.

## Handoff continuation — branch-A input publication and schema 2

Continued from `1ce9d1b` in the Astra investigation phase. Traced the
immediate actor input publishers and the native update call order; details
are appended to [branch-A evidence](cvs1-branch-a-selection.md).
Actor+0x5be receives the native newly pressed mask through staged input
records. Actor+0x5d8 copies +0x5c6, sourced from input record+0x38; its
upstream meaning and physical button mapping remain unproved. Added two
bounded spans, bringing the acquisition signature manifest to 59 spans.

Expanded the read-only selection tool to schema 2: phase through +0x111,
eight group counts, and two actor input blocks. Fixed-domain decoding
retains invalid groups without using them as memory indices. The new bytes
participate in both observations and their comparison. State-only bounds
are 1,478 bytes / 58 reads per pair; optional maximum assets make that
9,670 bytes / 60 reads. Branch-B asset eligibility is unchanged.

Validation: regenerated acquisition manifest; 13 acquisition, 11 selection
capture, and 11 metadata/transport tests passed. Selection tests cover exact
read budgets, raw navigation/input values, invalid group values, partial
reads and between-observation changes. No expanded live capture, game
inputs, runtime changes, DLL build or deployment. Old live fixtures remain
schema 1. Updated the run card for a future state-only measurement; short
input pulses may still be missed by polling.

Next: use schema 2 for a coordinated control/movement window when the user
is ready, retaining action notes. Independent tracing remains available:
input-record+0x38 producer, intervening input helpers, branch-A setup/catalog
semantics, and character-name provenance. Runtime packets remain draft;
the larger collection proof work is still pending.

## Handoff continuation — repeat-mask producer after 8b457f5

CFC2 was not running when continuation began. Asked the user to return to
CVS1 character select for schema-2 capture; no attachment or live reads
were attempted. Continued the independent input trace meanwhile.

Resolved input-record+0x38 to `0x1405a9c00`: clear output, compare current
and previous masks, reset counter on change, otherwise emit the held mask
when the incremented counter exceeds 35 and reset to 30. The native update
calls this for both records after copying their previous repeat outputs to
actors; mode 5 skips it, observed mode 0 does not. The optional input
override helper requires mode 4 and phase 2,1,2,2, excluding the captured
mode 0 at its first gate. Details and limits are in the branch-A evidence.

Added repeat routine and override-gate prefix to the audit, now 61 bounded
spans. Regenerated manifest; all 13 acquisition and 11 selection-capture
tests passed. No capture source changes, runtime changes or deployment.

Next live work remains the expanded control/movement window once the user
reports readiness. Do not equate this repeat-path evidence with an explanation
of the old three-transition sequence: that fixture lacks input words.
Remaining offline leads include setup/catalog naming, device-bit mapping,
and intervening helpers. Runtime packets remain draft.

## Handoff continuation — first schema-2 live movement

User returned to CVS1 character select. Completed eight control samples and
240 movement samples from PID 34768 with the schema-2 tool at `641044b`.
User confirmed both requested Right/Left taps with the game focused.
See [schema-2 live evidence](cvs1-live-schema2.md) and its two compressed
fixtures. No game writes, assets, deployment or runtime source changes.

Mode 0 / phase 2,1,1,6,6 persisted. Side-0 group stayed 1 with count 16;
position/selected pairs changed 0/0 -> 1/1 -> 0/0 -> 15/16 -> 0/0.
Held-word samples caught 0x400 and 0x800; press/repeat pulses were not caught.
The user later clarified four actual taps, resolving the apparent transition
count discrepancy caused by recording the requested two-tap schedule. One bracket
changed in raw phase; all other brackets matched; no read errors.

Verified gzip round trips and original hashes, parsed all samples and
transition values, and checked the diff. No source changes or test reruns.
The movement window is complete; no further user inputs needed for it.
No extra run is needed to explain the transition count. Names and
implementation packets remain unverified/draft.

## Handoff continuation — catalog check after b60d3bf

Completed review of the pending selector-0 catalog extraction and startup
verification. The acquisition manifest now pins 63 spans, the catalog
pointer and its 35 records. Setup sources side-0 selector from P+0x76;
the captured zero selector and group filter reproduce counts 10,16,6,3.
Every before/after side-0 position in the 240-sample movement fixture maps
to its recorded selected ID, including position 15 mapping to ID 16.
See the catalog continuation in [branch-A evidence](cvs1-branch-a-selection.md).

All 26 CVS1 tests passed, including pointer/data mutation rejection and
fixture checks. Availability has additional state-dependent gates, so the
raw catalog is not a general selectable roster. No character names, new
live reads, runtime changes or deployment. The user subsequently clarified
four actual taps, consistent with four transitions. Next work is
character-name provenance and the remaining acquisition contracts;
implementation packets remain draft.

## Astra allocation clarification and session-boundary continuation

The user explicitly wants this Astra thread used for all upfront Astra work.
Do not default back to bounded character-name mapping after proposing it as
Opus-suitable work. Keep that as a separate research assignment. Prioritize
cross-game lifecycle and acquisition contracts, implemented shared-boundary
proof, arena/four-player audio design, and concrete handoff packets here.
No model switching or subagents were requested.

Corrected the schema-2 action account: the user made four taps. The former
two-tap premise came from the requested schedule. Four observed transitions
are consistent with the corrected count; timestamps/direction attribution
remain unmeasured. No extra run is needed to explain the count.

The [session-boundary review](collection-session-boundary-review.md) records
seven instruction-aligned direct initializer call sites, the complete
dispatcher and the preparation gate. Lifecycle manifest now has 65 spans.
Eight lifecycle tests and 26 CVS1 tests passed; diff whitespace checks passed.
No runtime edits, DLL build, deployment or new live capture.

Common-router implementation must cover teardown before initialization,
in-place restore without root replacement, and both frame-thread and worker
audio ownership. S+0x7dc is not an approved universal generation counter.
Next trace the remaining teardown/restore boundaries and hook ordering before
implementing generation-aware output. The larger Astra campaign remains open.

## Astra continuation — all nine in-place restore branches

Added [collection restore evidence](collection-restore-trace.md), a dedicated
offline audit and compact manifest: nine save/restore routines and nine
instruction-aligned preparation prefixes. All check S+0x7b4/+0x828/+0x80c
before testing signed secondary-control B[0]. Save and bypass calls are not
restore events. Power Stone uses 16-byte payload headers; other paths use 8.
Both Alpha 3 branches and Project Justice selectively restore regions, so a
universal full-block memcpy hook cannot provide complete event coverage.

Preparation already changes native state before the restore routines; copy
return also precedes repair work. Shared design now explicitly separates
temporary suspension, replacement-generation change and readiness to resume.
Earliest boundaries, UI event ownership, thread ordering and hook ABI remain
unproved; no runtime router is installed on this evidence alone.

Pinned the shell-owner constructor prefix and its deleting-destructor vtable
slot in the lifecycle audit, now 66 spans. Actual virtual invocation remains
open. All 25 collection audit/metadata tests passed, including five new
restore tests and the destructor target mutation check. Whitespace checks
passed. No runtime source edits, DLL build, deployment or live capture.

Next Astra work: follow preparation ownership/shared state-load consumers,
complete lifecycle capture contracts, then implement and prove the common
router/output boundaries. Bounded character-name mapping remains Opus work.

## Astra continuation — shared snapshot and repeated-update ownership

The [snapshot trace](collection-snapshot-trace.md) adds 16 pinned spans and
seven mutation/manifest tests. A shared callback replaces the registered
S+0x1b0 state block in place and increments S+0x76c. Two traced consumers
load an earlier snapshot and repeatedly invoke the same native update while
S+0x770 is set. The shell also sets +0x770 for intermediate batch updates;
it is not a replay-exclusive flag or a worker synchronization primitive.

The shared contract now separates owner generation, state revision and
eligible publication sequence. Restoring state does not automatically re-arm
accepted speech. Copy completion and ordinary native-update invocation are
insufficient to establish that an observation should reach output.

The restore audit now has 32 spans, eight validated preparation/task slots
and seven tests. Dispatch and post-restore processing are pinned across all
nine runtime branches. Alpha 3 task loops and later helpers in other games
rule out publishing at restore-helper return. All 34 collection tests pass.
No runtime changes, deployment or new live capture.

Remaining Astra prerequisites: callback/root-registration lifetime, complete
outer completion and teardown ownership, hook ABI/thread ordering, then the
shared implementation proof. Per-feature acquisition and arena/four-player
audio work also remain open; this is not completion of the campaign.

The snapshot continuation now pins 31 spans and six virtual slots. Both
ordinary and callback-driven updates pass through the shared shell routine.
The callback finish path can poll and replay again after a normal game update,
so native-update return alone cannot authorize publication. Explicit callback
object close is traced; it zeros the object slot but does not clear the root
registration table in the reviewed body. Nine snapshot tests and all 36
collection tests pass. The outer shell caller continues through another helper;
actual presentation, transitive writes and live thread ordering remain open.

Nine additional post-update wrapper prefixes are now pinned in the restore
audit (41 spans total). All can copy smaller native regions back from
S+0x465c when S+0x4654 is 3, after the game update returns. These transfers
are distinct from full-root snapshot loads; UI semantics remain unlabelled.
Nine restore tests and all 38 collection tests pass. The capture boundary
must cover wrapper continuation as well as the native update and replay.

## Boundary probe implementation

An opt-in [boundary probe](collection-boundary-run-card.md) now records five
reviewed entry/exit sites, thread identity, shallow state metadata and nesting.
It uses bounded nonblocking capture and worker-only file output, preserves
the original call arguments/load result/last-error state, refuses changed
whole-body fingerprints, and rolls back partial hook installation. It is
enabled only by `cfc2_collection_probe.on`, respecting `cfc2_hook.off`.

The probe has synthetic forwarding/overflow/installation tests and a real
test-thread completion race. The CSV analyzer distinguishes incomplete and
lossy files from complete observations. The first run card covers a labelled
CVS2-to-CVS1 switch; snapshot load needs a separate exercised path. No new live
capture or deployment has occurred. The production router and larger Astra
deliverables remain open.

Final probe validation: all 29 PowerShell runners, all 50 collection Python
tests and the DLL build passed. A delayed producer cannot race the completion
footer into falsely reporting a clean capture. The final DLL hash and exact
live exercise are in the run card. No deployment has been performed.

## PWS2 constructor-order continuation — 2026-10-01

Continued from clean `622c0be`. The [PWS2 trace](pws2-actor-trace.md) now
retains the complete constructor matrix-combination helper: it premultiplies
`argument * current`, whereas the composite helper postmultiplies. The
alternate setup therefore builds slot 3 as P * (T * B), with B the mutable
template, T its translation-modified copy and P the projective constructor's
local matrix. No camera role, identity-template invariant or angle unit is
assigned. The manifest now retains 39 spans; its five offline tests pass.
No runtime source change, deployment, attachment, capture or audible proof.

Next: bound the two projective math callees, mutable-template writers and
ordinary-pass slot-3 initialization. HUD ownership, participant semantics,
shared integration and arena/four-player listening proof remain open.

## PWS2 projective setup continuation — 2026-10-01

Continued from clean `0e1fbe8`. The [PWS2 trace](pws2-actor-trace.md) now
retains 43 spans. The fixed constructor argument `0x1d28` is scaled by
approximately 2*pi/65536/2; the two math helpers follow cosine/sine polynomial
paths for that input in both SSE and AVX/FMA variants. Code and separately
pinned constants establish these paths; all-input math-library behavior remains
outside this result.

Initial and alternate-pass slot-3 setup use depth parameter b=12000; the
post-main-draw reconstruction uses b=120000. The ordinary main branch consumes
inherited slot-3 contents before that reconstruction. This prevents certifying
the earlier draw from a later matrix snapshot. The global template is identity
in the dump, but no writer or live invariance has been established.

Validation: audit/manifest generation exited 0; all six PWS2 audit tests passed,
including changed-math-data rejection; `git diff --check` exited 0. No runtime
source changed and no DLL build, deployment, attachment, capture or audible
check occurred. Other suites were not rerun for this isolated trace increment.

Next: follow the three later draw helpers and their matrix preservation, then
the indexed transform-record writers. Actor callback publication, HUD ownership,
ordinary-pass entry values and audible-perspective selection remain open.

## PWS2 transform-production continuation — 2026-10-02

Continued from clean `bad08f5`. The [PWS2 trace](pws2-actor-trace.md) now
retains 58 spans. Transform records begin at R+0x51d58; the drawing matrix
is at record+0x38. Ordinary updates build record 0, alternate updates build
0/1, and subsequent 0x308-byte copies replicate 0 to 2 and 1 to 3 (plus
0 to 1 on the ordinary branch). Transform identity is independent of actor
identity. Phase gates can skip matrix updates while allowing drawing.

The matrix writer adjusts two local vectors, constructs a basis and optional
table-driven rotation, stores the drawing matrix and rewrites a separate copy.
The later queue projects via slots 2/3 without the earlier viewport-composite
helper. The other two later draws walk lists 7/8. Their complete transitive
matrix preservation remains unproved; the main-pass matrix must still be
observed at its own stage.

Validation: audit/manifest generation exited 0; all six PWS2 audit tests passed;
`git diff --check` exited 0. No runtime source changed, DLL build, deployment,
attachment, live capture or audible proof. This evidence supports bounded
offline projection tooling, not a production camera/participant adapter.

## Ready Opus tooling handoff — 2026-10-02

[PWS2-T01](port-tasks/pws2-projection-reference.md) is ready for a sequential
Opus implementation session. Evidence base: `a065ca9`; the packet and its
fixture are additional prerequisites in the containing handoff commit.

The task is an offline two-point projection reference with strict sample
validation, independent actor/transform/pass metadata and streaming JSONL
output. The supplied fixture has 24 synthetic cases, including composition
order, perspective divide, boundary inequalities, viewport independence,
aliasing and malformed inputs. Sixteen numeric cases were checked using
exact-rational arithmetic during preparation. Eight others specify validation
outcomes. Fixture JSON/hash checks and `git diff --check` passed.

No reference tool or new runtime reader was implemented while preparing the
packet. Its implementation tests are acceptance commands to be run by Opus,
not claimed passing results. The existing six audit tests passed with the
58-span evidence increment. This handoff can proceed without a play session;
production projection, controller/HUD ownership, eligible audio pass selection
and listening acceptance remain incomplete.

Astra continuation after this handoff: trace the transform mode callbacks
`0x140ae82a0` / `0x140ae82b0` and `0x140aebde0` / `0x140aebe40`;
resolve transitive slot-3 preservation and actor callback/HUD publication;
prepare a tested stage-specific capture before consuming a live session.
Shared session integration, CVS1 pilot and broader F01-F19 acquisition remain
on the campaign rather than being declared complete by this tooling packet.
