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
