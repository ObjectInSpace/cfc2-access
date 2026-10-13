# Opus execution plan: shared groundwork, then collection ports

Progress as of 2026-10-13: steps 1 to 5 are done, and step 6 has CVS1 largely
ported. See [handoff-2026-10-13.md](handoff-2026-10-13.md).

Current direction, 2026-10-13, implementation baseline `a800114`. This replaces
the earlier CVS1-only execution sequence. Opus owns both the remaining shared
implementation and subsequent game adapters. Astra is optional escalation for
a specific difficult blocker, not a required phase or approval gate.

## Objective and scope

Deliver applicable F01-F19 accessibility features across the collection using
shared code. Finish the required ordinary-game mechanisms before multiplying
adapters. Preserve working CVS2 and shell behavior. Power Stone 1/2 have a later,
explicit arena/four-player phase; they do not block the other games.

CVS2 already reads native state. Keep its acquisition path while extracting
presentation and control logic. Its optional protected-reader replacement,
complete callback/wait investigation and global atomic dispatcher are deferred.
No successful test or pointer bracket establishes native lifetime safety.
A concrete access defect must still be addressed for the affected feature.

Read AGENTS.md and follow its commit requirement. This plan governs conflicting
historical sequencing/model assignments in CLAUDE.md and older handoffs. Use
screen-reader-friendly prose and lists. Do not spawn agents solely because
older documents mention separate model responsibilities.

## 1. Confirm the baseline and reuse the completed selection path

Inspect the working tree and later commits. Read
[implemented interfaces](../collection-porting-interfaces.md),
[feature inventory](../collection-feature-inventory.md), and
[collection port plan](../collection-port-plan.md). Use existing source and
fixtures; do not repeat completed native research merely to resume.

At `a800114`, `selection_channel` owns shared selection presentation and binds
to `tc_say`; CVS2 uses it in the DLL build. Do not rebuild this bridge or copy
the old private CVS2 settling loop. Existing text transports, read-current-text,
mixer, volume controls and persistence are also reusable.

Recorded checks: 36 native runners, 121 collection Python tests, DLL build and
import checks passed. The DLL has not been deployed or live-accepted. Retained
logs and source/build hashes are linked from the interfaces document. Counts
are historical until actually rerun. Preserve unrelated changes and warnings.

## 2. Implement the common gameplay-audio controller

Inspect `src/cvs2_select.c`, `src/cvs2_frame.c` and `src/audio.{h,c}` before
choosing extraction boundaries. Retain the existing mixer and settings.

Define bounded adapter-supplied observations for health/current maximum,
resource/current maximum, screen-relative position, stance, clock, active
owners and match/pause state, with explicit validity. Keep native offsets,
resource caps, camera projection and action-ID interpretation in adapters.
Preserve frame-timed health and the current timing of other cues.

Extract shared gauge/clock edges and pause/exit/reset handling. Implement the
minimal active-adapter/source ownership routing needed to stop old-game cues
and reject late updates/stops after a switch. Review the actual worker/frame
producer synchronization; worker-only selection labels do not solve concurrent
audio publication. Reuse existing scene mechanisms where appropriate. If scene
ownership is enabled, implement its compatible clock-event path first so the
legacy clock is not silently rejected. Do not require the optional global
speech dispatcher or a universal native-memory ownership redesign.

Wire CVS2 through the implemented controller while retaining its acquisition
and decoded values. Test damage, KO, resource gain/spend and maxima, position/
stance, decreasing clock warnings, round reset, pause/resume, exit, switching,
invalid/stale samples, late old-owner stops, muted cues and backend/device
unavailability. Preserve settings and measured frame/worker responsiveness.

Exit: shared controller is integrated into CVS2, tests and build checks pass,
and the adapter inputs/threading/reset responsibilities are documented. This
is production extraction work, not a synthetic-only framework.

## 3. Implement shared transition announcements

Extract the policy currently in `cvs2_announce_observe`: match/team/style
introductions and active-fighter changes. Accept copied names, team membership,
styles/ratios when applicable, active slots and explicit player ownership.
Handle team size without assuming every title uses CVS2's three-member limit.
Keep title rules and native IDs inside adapters. Use truthful player labels
when local ownership is unknown; do not assume every user is P1.

Use `tc_say` and existing speech mechanisms. Preserve grouping and appropriate
transition timing. Accepted history advances only after backend acceptance;
a rejection remains retryable, while departure or a newer transition cancels
obsolete pending output. Test repeated teams/fighters, valid new visits,
rejection/retry, switch/reset, unavailable data, and P2/unknown ownership.
Record deliberate corrections to existing latch-before-acceptance behavior.
Do not add a second speech backend or queue implementation.

Exit: CVS2 uses the shared transition implementation, compatibility tests pass,
and an adapter can supply observations without copying announcement policy.

## 4. Close shared integration and live acceptance

Inventory the actual ownership boundaries across selection, transitions,
text/read-current and audio. Ensure only the active supported adapter publishes,
old sources retire on switch, and shell/pause behavior is preserved. Implement
only routing/invalidation needed by these real paths. Preserve distinct text
policies: latest selection, settled quote, ordered story and positioned text.
If an asynchronous source exposes a specific ordering gap, fix and test that
gap; do not assert that serialized tests prove concurrent correctness.

Run affected unit/replay tests while implementing. After common speech or
runtime wiring changes, run all native runners with each process exit checked,
the collection Python suite, affected game-specific tests, DLL build/import
checks and `git diff --check`. Repeat broad checks only for new changes or
unresolved failures. Distinguish synthetic tests from recorded-game evidence.

Prepare a tested build and short listening checklist. Verify the game is closed
before replacing its DLL; retain a verified backup and installed hash. Exercise
CVS2 Arcade, Versus and Training selection, match cues, transitions, pause,
re-entry and exit as affected by the changes. This also accepts the previously
un-deployed selection integration. Do not request unrelated diagnostic captures.
A pending user check does not block independent packet-audit work.

Exit: shared code is implemented, integrated, regression-checked and its live
acceptance status is explicit. Do not call pending listening checks passed.

## 5. Audit game packets against the implemented interfaces

For CVS1, Capcom Fighting Evolution, Alpha 3 Upper, Project Justice and Plasma
Sword, create or update bounded feature packets tied to the actual APIs above.
Use the existing traces, manifests and captures before investigating further.
For each applicable F01-F19 feature, record:

- Supported build/ROM/branch/mode/owner and required shell access.
- Actual source/field or renderer path, bounds, validity and lifecycle behavior.
- Mapping into the shared interface; game-specific names, resource semantics,
  camera/projection and text encoding/visibility rules.
- Existing evidence and fixtures, exact missing inputs, expected output,
  implementation steps and automated/live acceptance checks.

Mark each packet ready, bounded discovery needed, or blocked with a concrete
reason. A missing name map must not become a guessed roster. This audit does
not require resolving every game's field before any port can proceed. Opus
owns the bounded discovery too. A packet is not ready merely because its native
values look plausible. Keep regional and language gaps visible.

CVS1 specifically still needs the associations in
[cvs1-branch-a-names.md](cvs1-branch-a-names.md). Existing scope is game 0,
ROM 1/cvs1u, branch A, mode P+0x48=0, phase prefix 2,1,1,6,6, side 0, catalog
selector 0. No certified name associations are claimed by that packet. Unknown
IDs/0xff remain unmapped. Request new evidence only for named missing facts;
do not repeat the explained four-tap experiment.

## 6. Execute non-arena ports through the shared implementations

Start with CVS1, then CFJ and Alpha 3 Upper, followed by Project Justice and
Plasma Sword as in the collection plan. Resolve only the bounded facts required
by each feature. Implement selection, applicable choices, announcements, match
cues and text/read-current through the common systems; no copied per-game speech
or mixer policy. Stage delivery in coherent validated increments, expanding
owners/modes/ROMs explicitly rather than calling a narrow path full support.

Each acquisition path still needs its own justified reads, bounds and exit/
mutation behavior. Native pointers must not survive into delayed presentation.
An unsafe concrete path is a local blocker, not authorization to ignore it or
to restart the parked collection-wide lifetime project. Preserve native-code
identity and provenance. Shared bugs are fixed centrally with affected-game
regressions; title-specific differences remain in adapters.

Validate applicable Arcade, Versus and Training flows, character changes,
results/continue, resets/load, return to shell and switching between supported
titles. Track F01-F19 per game through implementation, automated tests and live
acceptance. Preserve CVS2 throughout. Complete required shell routes that block
entry/configuration/pause/exit. Follow the collection plan for revision,
language and online-ownership gaps; do not silently narrow overall scope.

## 7. Complete Power Stone adaptations, then collection-wide release checks

Keep Power Stone work separate until its arena/four-player requirements are
needed. Reuse retained PWS2 evidence. Resolve title-specific projection,
resources and ownership; extend common spatial/audio handling for depth,
transformations and up to four distinguishable players. Validate cues by
listening. Existing four-owner selection support is not four-player audio.

After feature delivery, perform collection-wide switching/regression and
supported revision/mode/language checks, installer/package/import verification,
and accurate user-facing coverage documentation. Unknown applicability remains
open, not silently excluded. Partial ports may be delivered with precise limits;
complete collection support requires applicable features accepted per title.

## Execution, escalation and handoff

Commit validated code, documentation, tests and retained evidence in cohesive
increments before handoff. Report actual delivered behavior, checks, hash and
remaining restrictions. Do not present growing test/span counts as game support.

Opus should proceed through this plan without waiting for Astra. Escalate only
when a specific architectural contradiction or native-behavior problem cannot
be resolved within a bounded task. Supply the affected feature, minimal evidence,
proposed options and exact decision needed; continue independent work. No broad
research, capture campaign or model change is an automatic prerequisite.

The optional protected CVS2 reader, full callback/wait proof and global atomic
dispatcher remain parked unless a concrete defect or feature requires them.
The retained Change Character capture has record loss and does not prove lifetime
safety. The 4096 bypass remains a conditional counterexample, not observed failure.

## Instruction to Opus

Read `docs/port-tasks/opus-shared-selection-execution.md`. Finish the remaining
shared gameplay-audio and transition implementation, integrate and validate it
with CVS2, audit game packets, then execute the collection ports through those
interfaces. Reuse the completed selection channel. Preserve existing acquisition
unless a concrete issue requires a change. Commit validated increments and keep
actual live acceptance separate from offline tests. Astra is optional escalation
for a named blocker, not a prerequisite for this work.
