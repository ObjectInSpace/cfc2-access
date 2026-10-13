# Opus execution plan: shared selection and the CVS1 pilot

Prepared 2026-10-13 against clean `a936faa`, following the user's request for
an implementation handoff. This is a plan, not completed implementation or
authorization to declare native acquisition safe. Follow AGENTS.md: commit
completed, validated task code, tests, documentation and retained evidence
together; keep unrelated work out. Use lists and screen-reader-friendly prose.

## Outcome and scope

The next delivery milestone is existing CVS2 selection behavior through shared
session/presentation mechanisms, followed by a bounded CVS1 character-highlight
feature using the same mechanisms. Preserve CVS2 Arcade, Versus and Training,
including adjacent stage, groove, ratio and level speech. The full collection
objective remains applicable F01-F19 support in all eight titles.

Execute steps 1-5 below now. Steps 6-8 are conditional delivery steps, not
permission to bypass their evidence gates. If a gate remains unresolved,
commit the completed offline work and give the exact missing dependency.
Continue independent ready work; do not start another broad lifetime campaign.
Do not claim that the offline milestone itself delivers a new playable game.

## 1. Establish the current baseline once

Read this plan, AGENTS.md, CLAUDE.md, and these current contracts:

- `docs/port-tasks/cvs2-shared-selection-handoff.md`, including its latest
  continuation, for implementation and mode requirements.
- `docs/port-tasks/shared-selection-milestone.md` for the scoped CVS1 target.
- `docs/shared-output-dispatch-contract.md` for output race semantics.
- `docs/collection-lifetime-probe.md` for the latest evidence and its limits.
- `docs/collection-workflow.md` for shared-first delivery; historical model
  assignments are task allocation, not proof that only Astra can solve a task.

Inspect `git status`, later commits, `build.ps1`, `src/game_session.{h,c}`,
`src/game_selection.{h,c}`, `src/cvs2_select.{h,c}`, `src/text_channel.{h,c}`
and their test runners. Reuse existing code and decoders. Shared session and
presentation are currently offline components, absent from the DLL build.

Recorded baseline: 121 collection Python tests; 35 native runners and build/
import checks passed for `e7456b5`. At `a936faa`, the 121 tests were rerun and
the retained live artifact hashes and full report reproduction verified.
These are historical checks until rerun; don't repeat every audit merely to
resume. Run the three relevant native runners before changing their behavior:
`run_cvs2_test.ps1`, `run_game_session_test.ps1`, `run_game_selection_test.ps1`.
Inspect each process exit code; an enclosing PowerShell command can hide a
failed child. Preserve existing unrelated compiler warnings accurately.

The deployed DLL remains the tested diagnostic build, SHA256
`f5fa279ec1c47fb9c251c64beb7f62ce77e52be3f101aed01c28997596a135aa`.
The lifetime marker was removed after the completed capture. Do not reinstall
or enable diagnostics as part of the offline steps.

## 2. Implement the worker-owned offline bridge

Use the existing handoff's step 1 to add `collection_session.{h,c}` or an
equivalently small module. Own `game_session` and `game_selection` under one
serialized worker. Inputs are bounded owned lifecycle/selection records;
there are no native reads, hooks, pointer retention or direct speech calls.

Specify record bounds and adapter/ROM/mode, revocation identity, enclosing
invocation, sequence/time, completion and loss fields. Keep native revocation
labels separate from session generation/revision. Process invalidation before
eligible records, reject stale acquisition, and resume only matching completion.
Do not assign a new identity to an old observation on arrival. Reuse existing
transport mechanisms; do not introduce a speech FIFO for selection.

Preserve two distinct settling samples, 500-ms age limits, owner grouping,
backend retries and generation-versus-revision history. INACTIVE rearms an
owner; UNAVAILABLE breaks settling without erasing accepted history. Invalid
or short capture is suspension/unavailability, not a NULL decoder call that
manufactures INACTIVE. Add meaningful lifecycle, reordering, loss, overflow,
counter exhaustion and stale-publication tests with a fake backend.

Exit: deterministic bridge tests pass; production CVS2 behavior is unchanged.
Commit this bounded implementation before expanding the work.

## 3. Extend decoder-to-presenter parity

Extend `shared_sample`/`shared_replay` in `tools/test_cvs2_select.c` to use the
bridge. Preserve legacy assertions as an independent wording/behavior baseline.
Cover alternate selection, P2, simultaneous changes, repeated characters in
new slots, cancel/re-entry, busy or inconsistent samples and backend rejection.
Preserve stage/groove/ratio/level wording as well as character names.

Verify the four local replay inputs against the hashes in the existing handoff.
If a required fixture is absent, report that specific gap; do not silently
replace recorded evidence with synthetic data. Clearly label synthetic mode,
lifecycle and timing inputs. No additional live character-navigation capture
is needed merely to implement this parity step.

Exit: shared output matches the existing behavior on the supported fixtures;
tests distinguish lost/unavailable data from an observed selection exit.

## 4. Implement the output dispatcher behind a fake backend

Implement `docs/shared-output-dispatch-contract.md` as a bounded standalone
module before wiring it into shipping speech. Inventory actual `speech_say`
and `speech_stop` callers. Preserve `tc_say` acceptance semantics and existing
text policies; do not build another text channel or queue.

Use actual target-supported atomics and barrier-controlled concurrency tests.
Test reservation versus revocation, acceptance versus revision/generation
change, shell takeover, delayed old stops, rejected backend attempts, loss,
bounded CAS failure and exhausted versions. Native callbacks must never wait
for speech. Document the contract's permitted already-reserved attempt; do
not claim immediate cancellation of audio already accepted by NVDA.

Keep dispatch IDs/leases and retirement bookkeeping attached to their original
owners. Integrate the bridge with the fake dispatcher in offline tests. Do not
partially route production speech while claiming that every backend call is
ordered. Any production routing change requires the complete caller inventory
and regression coverage. Audio scene activation is separate: its clock-event
path must exist before enabling ownership that rejects legacy clock ticks.

Exit: deterministic concurrency controls and bridge/dispatcher integration
tests pass. Commit the tested component even if native admission remains open.

## 5. Complete the bounded CVS1 name prerequisite

Follow `docs/port-tasks/cvs1-branch-a-names.md` and its completion record.
Reuse the acquisition/selection/visit traces and retained control/movement
fixtures. The latest packet records 28 passing CVS1 Python tests, 67 pinned
spans and 19 dispatch edges, but no certified ID/name associations.

Trace native selected IDs to their displayed resources or use explicitly
labelled independent evidence. Do not infer names from roster order. Unknown
IDs and 0xff stay unmapped. Preserve exact scope: game 0, ROM 1/cvs1u, branch A,
mode P+0x48=0, phase prefix 2,1,1,6,6, side 0, catalog selector 0. Other scopes
remain pending. Deliver a reproducible map with provenance and negative tests.

If assets needed for association are absent offline, prepare one small capture
request naming the missing associations. Do not repeat the old four-tap test
or solve unrelated renderer questions. A partial verified map is useful research
but not a complete F02 port. A decoder for owned fixture bytes can proceed once
its needed names and visit semantics are established; native activation cannot.

## 6. Review the smallest runtime activation gate

Before enabling a replacement producer, explicitly assess the lifetime and
per-mode requirements in the CVS2 handoff. The existing shipping reader is a
compatibility baseline, not proof of protected native access. Neither pointer
brackets, guarded reads, thread identity nor passing tests prove exclusion.

The latest live capture confirms Free Play Training Change Character returns
to stage selection, then groove, then character. Application invocation 24189
contains matched cleanup/init/pause/application returns and five successful
batch joins. The window dropped 11,225 records. Reset was not exercised;
generic/retirement outcomes, signal outcomes and full arena-mutating descendant
coverage remain open. The 4096 bypass is conditional, never an observed failure.

Do not repeat that exercise by default. If native admission still depends on
unproved exclusion, stop activation and present a focused review packet: the
exact proposed reader, ownership boundary, unresolved code edge and smallest
needed observation. Astra is optional escalation for this difficult review,
not a dependency for steps 2-5. No new native copy boundary is approved by this
plan. Do not quietly relax an existing contract to make the milestone pass.

## 7. Integrate and validate only admitted paths

When the activation and dispatcher gates are satisfied, link the relevant
modules in `build.ps1` and route the admitted producer through the bridge.
Exactly one selection presenter emits. Preserve sibling quote/text/audio calls
currently sharing the CVS2 worker path. Keep native offsets and name tables
inside adapters. Deliver the bounded CVS1 feature through the same presenter.

Run relevant tests after each change. Changes to common speech, guards or
runtime wiring require the full native runner sweep with checked exit codes,
collection Python tests, affected CVS1 tests, DLL build and import checks.
Run `git diff --check`. Retain evidence of actual commands/results.

Prepare a tested build and a short, plain-language live run card before asking
the user to exercise it. Verify the game is closed before replacing its DLL;
retain a verified backup and installed hash. Test CVS2 Arcade, Versus and
Training, re-entry, rejection/retry and shell takeover. For CVS1, state the
supported ROM/mode/owner/choices plainly. Offline tests are not live acceptance.

## 8. Close the milestone and continue the collection ports

Report separately: implemented behavior, offline validation, audible live
acceptance, and remaining restrictions. Commit before handoff and give the
hash. If only offline work is complete, say so explicitly and name the exact
runtime blocker. Update existing handoffs instead of creating conflicting status.

After validated CVS2/shared plus CVS1 character speech, extend CVS1's applicable
features, then follow `docs/collection-port-plan.md` for CFJ, Alpha 3 Upper,
Project Justice, Plasma Sword and Power Stone 1/2. Track features per game.
Keep PWS2 renderer/four-player audio work out of the first selection milestone.
Resolve game-specific facts when their next delivered feature needs them.

## Suggested first instruction to Opus

Read `docs/port-tasks/opus-shared-selection-execution.md` at `a936faa` or its
descendant and execute steps 1-5 in order, committing validated increments.
Start with the offline bridge and CVS2 parity, then the fake-backend output
dispatcher and bounded CVS1 names work. Preserve shipping CVS2 in all three
modes. Do not enable a new native producer, redeploy, or repeat broad lifetime
research to complete those offline tasks. Review steps 6-8 for the eventual
delivery gates. Continue independent ready work when one evidence item blocks;
report any remaining blocker with its smallest concrete evidence request.
