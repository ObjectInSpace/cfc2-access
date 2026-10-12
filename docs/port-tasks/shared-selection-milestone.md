# Shared selection delivery milestone

Selected 2026-10-05 at 6cc26c8, following the shared-first priority in
collection-workflow.md. Feature: F02 character-highlight speech. This packet
does not redefine the full F01–F19 collection delivery objective.

User clarification, 2026-10-09: required mode coverage includes **Arcade,
Versus and Training**. The Arcade-only Quick Load research pilot is temporary
and does not narrow delivery scope. Track Versus and Training integration and
live acceptance as required work, with mode-specific lifecycle evidence.

The [CVS2 implementation handoff](cvs2-shared-selection-handoff.md) reviews
the existing reader at `90fe595`, separates ready offline bridge/parity work
from native activation prerequisites, and specifies required acceptance for
all three modes. It reuses the current decoders and test fixtures.

The 2026-10-10 continuation resolves application-loop update/deletion ordering,
later unit-task dispatch, Training's mutation that skips ordinary update, and
Versus/Training Change Character cleanup/reinitialization; see the
[native boundary trace](../collection-native-boundary-trace.md). The
[dispatch contract](../shared-output-dispatch-contract.md) defines reservation,
revocation and retirement ordering for implementation. Neither result proves
exclusive native scheduling or completes Versus/Training lifecycle coverage.
The encompassing copy candidate is now application-update return: unit updates
follow the earlier manager-return candidate. Both remain unapproved for native
copying. The 62-span audit and all 96 collection Python tests pass; the bounded
offline bridge and dispatch implementation are next. Runtime delivery and live
acceptance remain open.

The later continuation also traces pause-task registration into category 3 and
the configured Coin Button Training restart. The separate generic queue's known
caller polls normal completion but bypasses polling after ring reuse. Resolve
that invariant and its enclosing virtual caller before native activation; retain
Versus outcome coverage and other untraced reset paths as remaining evidence.

Continuation, 2026-10-11: Versus Rematch now has a static chain from two native
result-signal writers to the category-4 cycle unit and category-25 Rematch?
dialog. Yes reaches cleanup/init plus later session writes; No exits separately.
This requires new-generation semantics and protected enclosing completion.
The generic queue caller is identified as a geometry-group unit; its threshold
of 10 selects parallel execution and does not prove capacity or reuse safety.

Continuation correction: reuse the existing CVS2 implementation and recorded
research before proposing another acquisition trace. `cvs2_select_tick` in
`src/cvs2_select.c` already polls and copies state; its decoders distinguish
Arcade and the alternate Versus/Training selection paths, reject inactive/
busy states, and preserve selection visits. `cvs2_selection_copy` already
supplies the shared copied-label format and is exercised by the shared replay.
The existing character speech is not driven by a drawing callback. A proposed
switch to drawing callbacks is not an established prerequisite for this port.
Preserve these decoders, labels and behavior; focus further work on the actual
shared integration gaps. The Arcade Quick Load provenance investigation must
not become a new mode admission requirement for existing selection speech.

Offline continuation from 9dbf135: src/game_selection.{h,c} now implements
bounded copied-selection settling and presentation. Real CVS2 capture replay
passes through it, game_session and the existing tc_say with a fake backend.
The shipping CVS2 observer uses the extracted copied-label/visit decoder but
retains its existing output policy. Native lifecycle wiring and the CVS1
adapter remain unimplemented. The user deferred the manual lifecycle exercise;
no live staging or deployment occurred.

## Delivered behavior and boundaries

Target behavior: CVS2 retains its current character announcements, settling,
simultaneous-player grouping and acceptance-based retries; CVS1 supplies
character-highlight observations through the same session and presentation
path. Native decoders retain their own phase/owner/name/visit rules.

The CVS1 pilot is game 0, ROM 1 (cvs1u), branch A, mode P+0x48=0,
phase prefix 2,1,1,6,6, side 0 and catalog selector 0. This is partial F02
coverage. Other ROMs, branch B, side 1 and other selectors remain pending.
Do not invent confirmation announcements or use native numeric IDs as names.
The pilot must handle leaving/re-entering selection and repeated selection
of the same character in a new slot within its stated scope.

The native selection writer/consumer is already traced. The ready-for-research
CVS1-R01 packet still needs name provenance, and visit/confirmation edges need
review for this scoped behavior. Label every unmapped ID unavailable. A partial
map is useful evidence, but does not complete this scoped selection feature.

## Implemented shared foundation

src/game_session.{h,c} now provides a small serialized state machine, currently
excluded from build.ps1. It stores no native pointers, copies no native memory,
and installs no hooks. Its transitions are:

- Begin an owner: increment generation and revision, discard pending
  publication, and leave capture disabled. Reacquiring identical adapter/ROM
  IDs still changes generation. Build/ROM validation belongs to its caller.
- Queued acquisition: use game_session_begin_if_current with the expected
  generation/revision. A later invalidation or acquisition defeats stale
  completion without changing the current state. It does not infer readiness.
- Suspend: increment revision and disable capture/publication/output while
  retaining the owner. Departure additionally relinquishes ownership.
- Resume: require the expected generation/revision. A stale completion after
  another invalidation cannot resume capture. The caller must independently
  establish that ALL enclosing mutations have finished and publication is
  eligible; this function does not infer quiescence or count native nesting.
- Capture: issue a monotonic sequence attached to generation/revision and
  adapter/ROM identity. Complete publication only after the copy succeeds.
  Older completion cannot overwrite newer publication.
- Output: require the current latest published token. A rejected backend
  leaves it retryable; only accepted, still-current output advances consumption.
- Counter exhaustion disables the instance permanently rather than allowing
  a wrapped token to match old work.

These are latest-selection semantics, not the story FIFO contract. Semantic
selection settling, accepted visit history and wording remain separate from
observation consumption. A state revision must not itself erase accepted
speech history. The component's consumption counter is not that history.

Every operation requires external serialization. This component is not an
atomic native-callback revocation mechanism or an implemented runtime router.
Do not wire callbacks directly to it. The lifecycle/backend integration must
establish ordering and protect output checks against concurrent revocation.
Checking acceptance afterward prevents stale bookkeeping but cannot unspeak
an utterance already accepted by NVDA. Backend interruption and shell/game
ownership arbitration remain explicit integration work.

## Required shared integration

### Copied selection and presentation component

game_selection accepts four fixed owner slots, each with an explicit
VALID/INACTIVE/UNAVAILABLE status, visit, adapter-local kind/value and a copied
256-wchar label. It contains no native field offsets or character-name table.
One observation carries the session token, completeness and capture time.
The proposed worker policy refuses incomplete, future-dated or more than
500-ms-old observations. This age limit is explicit offline policy, not a
measured guarantee of live selection latency.

Two distinct published observation sequences with equal visit/kind/value/label
are required to settle. Re-offering the same sequence cannot count twice.
UNAVAILABLE breaks pending settling but keeps accepted history; INACTIVE
also rearms the owner. New generations clear both; a new revision clears only
pending settling. History is the last accepted choice per owner, not an
unbounded ledger of all historical visits.

Changed owners produce one bounded utterance in slot order. Backend rejection
keeps it retryable; a newer observation replaces obsolete pending choices.
The component rechecks the session after the backend call before marking
acceptance. Reentrant offers/drains are refused. This does not solve concurrent
revocation or cancel speech already accepted; external serialization and
backend interruption remain required. There is no new speech queue. Production
binding is tc_say; the test callback also uses tc_say for the CVS2 replay.

cvs2_selection_copy now owns the existing CVS2 decoding and label formatting,
including character/groove/ratio/stage/level and selection-step information.
Both the shipping legacy observer and the shared offline replay consume this
same copied format. Native sampling, root-keyed legacy output and audible
wording remain the existing path. Four fixed slots do not imply four CVS2
players: its unused two slots are explicitly inactive. CVS1 supplies no data
to this component until its native name/visit contract is established.

Validation: 209 shared presenter/session checks pass with -O2 -Wall -Wextra
-Werror. Four compiled sabotage copies are rejected for single-sample settling,
erased accepted history after restore, advancing on backend rejection, and
allowing a stale token to call the backend. Changed source copies live under
build/test only; compilation failure does not count as a behavioral rejection.
The existing CVS2 suite passes, including real Ryu/Sagat/Yun state captures
through the shared presenter, retry and restore controls, and the original
character/groove/ratio/stage/gauge/quote regression cases. All 34 current
native runners pass; results are in build/test/shared-selection-regression.json.
The final test-only stale-backend assertion also passed in the sabotage
runner's unmodified control. Build output is in build/shared-selection-build.log.

The session and presenter implementations remain outside build.ps1. The
extracted CVS2 copy function is part of the DLL. Its current build and import
check pass with existing unrelated warnings. DLL SHA-256:
c4b916da66edc95cfe08db40f12b8d0be6705852726d62cb80ad679803d2a36f.
This supersedes the earlier preflight hash below for any future staging.

### Native lifecycle and adapter work still required

The first [live switch trace](../collection-boundary-live-20261006.md) is
complete with zero dropped events. Apply the revised
[handoff contract](../collection-handoff-contract.md): departure is explicit,
retained roots are not eligibility, and nested cleanup return cannot resume.
Callback restore coverage and safe acquisition remain open.
For the limited offline pilot, prioritize the
[quick-save follow-up](../collection-quick-save-followup.md). Unverified
online callback modes may remain unsupported; exhaustive replay coverage
must not silently expand the milestone. A reliable exclusion gate still
requires implementation and evidence.

The 2026-10-09 [Quick Load completion trace](../collection-quickload-completion-trace.md)
adds the CVS2 consumer/repair chain and necessary callback/import/restore
exclusions. Local repair return is distinguished from enclosing completion;
all-zero gate values are not an evidenced offline-mode allowlist. The native
gate and lifetime-safe copy boundary remain unimplemented.

The subsequent [offline-mode trace](../collection-offline-gate-trace.md)
establishes Offline Play / Arcade entry provenance and the Training/Versus
flag writers. The proposed pilot gate requires an explicit Arcade mode code,
correlated initialization and current consistency checks; zeros alone do not
admit a session. Manager-update return is the candidate copy placement after
deferred replacement, with native lifetime protection still unresolved.
All 90 collection tests pass. This remains static evidence, not native wiring
or an expansion of the CVS1 pilot scope.

The offline [output invalidation review](../shared-output-invalidation-review.md)
records the remaining queue, copied-frame, backend and audio ownership gaps.
Preserve the recovered scene ownership guards: legacy reset/tick paths already
respect them. Revision invalidation and clock delivery still need integration.
The [CVS1 visit trace](../cvs1-selection-visits.md) proves slot advance and
reset, but does not certify cancellation/re-entry or any character names.

Use the existing boundary diagnostic to resolve actual update, cleanup and
release ordering during a CVS2-to-CVS1 transition. Use a separately labelled
restore/replay exercise for the snapshot path. Missing snapshot events in an
ordinary switch are not evidence of safety. The five-site probe does not
enclose the screen manager or prove every native mutation is observed.

Implement immediate revocation and safe handoff using that evidence, with
checks at copied-observation publication and output consumption. Review both
CVS2's worker and frame audio paths: inactive CVS2 cannot reset another
adapter's audio. Preserve frame-timed health behavior. Reuse text_channel,
audio and the current shell mechanisms; do not copy their queues or mixer.

Migrate the bounded CVS2 selection path with its existing wording and
settling policies. Its current root-keyed path is a compatibility baseline,
not a certified lifecycle boundary. Add the CVS1 native decoder only after
the scoped phase/visit/name contract is evidence-backed. No shared output
module should know a native field offset or assign a character name.

## Acceptance and actual verification

Offline controls must cover departed owners, same-address restore, stale
publication and output, nested mutation, reordered completions, invalid ROM,
backend rejection, switching while speech is pending, and resetting one
adapter without suppressing the other's valid output. Real native integration
needs concurrency tests; serial synthetic tests do not satisfy that part.

Current component verification: tools/run_game_session_test.ps1 passes 55
checks with -O2 -Wall -Wextra -Werror. Cases cover same-identity replacement,
revision revocation, stale completion, latest-publication selection, retries,
accepted-output bookkeeping after departure, and counter exhaustion.

Preflight for the unchanged diagnostic DLL: all 32 existing native runners
passed, as did 52 collection Python tests. build.ps1 and its import check
passed; existing unrelated warnings remain in the build log. The new session
runner passed separately. The session component is not linked into that DLL.
Logs: build/test/shared-preflight-20261005.json and
build/shared-boundary-preflight-20261005.log. DLL SHA-256:
85c44a5022fa87fcfc74d70b302bdc0ef6029a9d29e61d1e177b58f8e19db07a.

Live acceptance is still required: labelled boundary timeline, preserved CVS2
highlight speech, correctly named CVS1 highlights, new-slot/re-entry behavior,
switching without stale speech/audio, and recovery from backend rejection.
No deployment, captured timeline, migrated adapter or usable CVS1 feature is
claimed by this foundation checkpoint.
