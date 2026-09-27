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

Initial queue: Astra discovery/proof brief is ready to start. Opus runtime
implementation packets await traced contracts and pilot evidence. No port is
declared ready solely because this workflow document exists.
