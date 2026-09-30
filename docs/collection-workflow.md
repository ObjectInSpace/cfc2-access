# Astra investigation and Opus implementation workflow

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
