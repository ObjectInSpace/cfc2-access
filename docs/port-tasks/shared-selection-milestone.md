# Shared selection delivery milestone

Selected 2026-10-05 at 6cc26c8, following the shared-first priority in
collection-workflow.md. Feature: F02 character-highlight speech. This packet
does not redefine the full F01–F19 collection delivery objective.

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
