# Opus execution plan: deliver CVS1 selection speech

## Implementation update, 2026-10-13

The user wants the required reusable mechanisms completed before handing their
ports to Opus, not an infrastructure demonstration. See the
[implemented interfaces and remaining shared work](../collection-porting-interfaces.md). CVS2 now uses the
worker-owned selection channel, shared presenter and existing speech backend
in the DLL build; its native acquisition is retained. Earlier descriptions of
those selection components as offline-only are historical. Opus should reuse
this implemented selection path instead of rebuilding it. Shared gameplay,
transition and arena-audio work still has explicit gaps; do not claim all
feature families are ready. Finish the mechanism needed for the next feature
without reopening the optional protected-reader redesign. This update governs
conflicting implementation status and next actions below. Live acceptance of
the changed DLL is not yet claimed.

Corrected 2026-10-13 after `c70c565`. This replaces the previous execution plan.
The previous plan incorrectly made an optional CVS2 acquisition redesign and
collection-wide output machinery prerequisites for useful game-port delivery.

## Objective and boundaries

Deliver usable, explicitly scoped CVS1 character-highlight speech, preserving
working CVS2 behavior in Arcade, Versus and Training. Reuse existing shared
presentation and speech mechanisms. Build only the integration this feature
needs. The wider goal remains applicable F01-F19 support across all eight games.

CVS2 already has a native reader. Keep its current acquisition path; the proposed
protected replacement is deferred. Completing native lifetime research, a new
application-return producer, or the global atomic output dispatcher is not a
prerequisite for sharing already-copied selection data or implementing this pilot.
Existing contracts still describe the stronger designs if those are later chosen;
do not claim their guarantees for a smaller implementation.

This does not establish that the existing reader is safe or that another game's
addresses can be read by analogy. Justify the actual CVS1 access path, bounds,
identity and lifecycle behavior needed for this feature. Address demonstrated
hazards in that path. Do not turn an unresolved local question into a requirement
to prove every callback and wait in the collection.

## 1. Inspect the implementation and define the pilot

Read AGENTS.md, the current-direction notice in CLAUDE.md, this plan,
`docs/port-tasks/cvs1-branch-a-names.md` (including its completion record),
`docs/cvs1-selection-visits.md`, and the referenced acquisition evidence.
Inspect the working tree and later commits before editing. Reuse
`src/cvs2_select.{h,c}`, `src/game_selection.{h,c}`, `src/game_session.{h,c}`,
`src/text_channel.{h,c}` and their existing tests where appropriate.

The starting CVS1 evidence is scoped to game 0, ROM 1/cvs1u, branch A,
mode P+0x48=0, phase prefix 2,1,1,6,6, side 0, catalog selector 0.
Do not silently expand that scope. The names packet currently records no
certified ID/name associations. A scoped pilot is not full CVS1 completion.

Write the acceptance checklist for this feature: correctly named highlights,
settling, repeated selection in a new slot, leave/re-entry, backend rejection
retry, and no continued CVS1 announcements after leaving its supported context.
Preserve current CVS2 and shell speech. State unsupported owners/modes/ROMs.

## 2. Resolve only the CVS1 facts needed to speak correct choices

Reuse the selected-ID writer/consumer traces and retained captures. Establish
ID-to-name associations from the actual resources or labelled independent
observations; never infer them from roster order. Keep unknown IDs and 0xff
unmapped. Check visit and phase rules for the scoped feature.

Use the existing bounded names packet, not a new collection-wide research
campaign. If required runtime assets are absent offline, prepare one short
capture request that identifies the exact missing associations. Do not repeat
the already-explained four-tap experiment. Retain provenance and honest limits.

Exit: the supported choices have reproducible names and visit semantics.
A partial map may be committed as progress, but cannot be called a complete
character-selection feature. Keep progressing on independent implementation.

## 3. Implement the smallest reusable presentation integration

Decode owned CVS1 fixture bytes into bounded copied selection choices. Keep
native offsets, names and phase rules in the game adapter. Reuse `game_selection`
and `tc_say` where they fit; use worker-owned session bookkeeping only to the
extent required by the actual path. Do not invent native completion evidence
or pretend worker labels protect memory from concurrent destruction.

Use one selection presenter per active path, two distinct samples for settling,
accepted-output history, bounded labels and backend retries. Distinguish an
unavailable observation from a real exit from selection. Do not add a speech
FIFO, second text channel, global dispatcher or native hook framework merely
to deliver this feature.

Validate common behavior against the existing CVS2 decoder/replay fixtures.
If a small CVS2 presentation refactor makes reuse concrete, keep its acquisition
unchanged and preserve wording, P2/grouped speech, stage, groove, ratio and level
behavior. Do not remove sibling quote/text/audio work from its worker path.
A wholesale CVS2 runtime migration is not a prerequisite for the CVS1 pilot.

Exit: the scoped CVS1 decoder and shared presentation work on retained inputs,
with relevant parity, invalid-data and acceptance/retry tests passing.

## 4. Connect the scoped CVS1 acquisition and deliver the feature

Use the traced CVS1 acquisition path and existing capture infrastructure as
inputs to the implementation, not as automatic lifetime certification. Document
the actual build/ROM admission, pointer chain, bounds, read cadence, phase checks,
owned copy and exit/reset behavior. No native pointers may escape into delayed
presentation. Add guards appropriate to the actual implementation and keep
unsupported contexts inactive. Test invalid reads and stale observations.

If a concrete acquisition hazard blocks this feature, identify the exact read,
allocation or mutation edge and the smallest needed fix or observation. Continue
independent work. Do not resume the parked CVS2 lifetime campaign, enable an
unapproved application-return producer, or demand universal scheduler proof.
Escalation to Astra is optional for a specific difficult blocker, not a scheduled
phase of every port. Do not conceal a blocker by claiming unproved safety.

Integrate the needed sources in `build.ps1`; preserve existing CVS2 acquisition
and working features. The exit is a build that can speak the scoped CVS1 choices,
not merely more standalone infrastructure tests.

## 5. Validate, then perform a focused listening check

Run the relevant CVS1 decoder/acquisition and shared presentation tests. Reuse
`tools/run_cvs2_test.ps1`, `tools/run_game_selection_test.ps1` and
`tools/run_game_session_test.ps1` when those components change. Verify retained
fixture hashes rather than silently substituting synthetic samples.

For common speech or runtime wiring changes, run all native runners with each
exit code checked, the collection Python suite, affected CVS1 tests, and the DLL
build/import checks. Run `git diff --check`. Do not repeatedly run full suites
without changes or failures that justify it. Baseline counts are historical:
121 collection tests, 35 native runners; the CVS1 packet records 28 Python tests.

Prepare the tested build before requesting a short live exercise. Verify the game
is closed before DLL replacement and retain a verified backup/hash. Ask for the
specific CVS1 choices and transitions needed for acceptance, with plain-language
instructions. Check preserved CVS2 behavior for any affected presentation path.
Do not require more CVS2 diagnostic captures for an unrelated CVS1 feature.

Separate offline validation from actual audible acceptance. No broad proof of
native memory safety is implied by successful playback or passing tests.

## 6. Commit and continue toward all games

Commit cohesive validated code, tests, documentation and retained evidence before
handoff, as AGENTS.md requires. Report delivered behavior, precise supported
scope, checks, commit hash and remaining user-visible gaps. If blocked, name the
specific dependency and retain completed work; do not describe infrastructure
progress as a playable port.

After the pilot, expand CVS1 owners, modes and applicable features, then continue
CFJ, Alpha 3 Upper, Project Justice, Plasma Sword and Power Stone 1/2 according
to `docs/collection-port-plan.md`. Reuse shared mechanisms as each feature needs
them. Track per-game selection, announcements, gauges, position, clock and text
coverage. Arena/four-player audio is work for those games, not this pilot's gate.

## Parked work and retained evidence

The optional CVS2 protected-reader redesign, complete arena-mutating callback
coverage, generic/retirement wait research, loss-reduction capture work and global
atomic output dispatcher are deferred unless a concrete delivered feature or
observed defect requires them. Do not restart them merely because older documents
list them as next actions. Preserve their evidence and design contracts.

The retained Training capture establishes Change Character returning to stage
selection and matched successful batch waits, with 11,225 dropped records. It
neither exercises reset nor proves lifetime safety. The 4096 bypass remains a
conditional counterexample, not an observed failure. The lifetime marker was
removed; no diagnostic deployment is requested by this plan.

## Instruction to Opus

Read `docs/port-tasks/opus-shared-selection-execution.md` and execute its corrected
CVS1 feature-delivery sequence. Preserve the working CVS2 acquisition path. Reuse
shared presentation with only the integration needed for usable CVS1 selection
speech. Commit validated increments. Do not make the optional CVS2 reader
replacement, global dispatcher or further collection-wide lifetime research a
prerequisite. Report a concrete local blocker if one arises and continue
independent work.
