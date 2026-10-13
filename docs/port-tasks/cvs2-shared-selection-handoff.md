# CVS2 shared selection implementation handoff

Reviewed through 2026-10-12 against base commit `90fe595`.

## Identity, outcome and readiness

- Task: CVS2-S01, migrate existing F02 character-highlight speech through the
  shared session and copied-selection presenter.
- Kind: shared-system integration and CVS2 adapter implementation.
- Status: ready for bounded offline implementation; native activation awaits
  the lifetime, dispatch-ordering and mode-specific evidence listed below.
- Expected executor: implementation owner under the collection workflow;
  unresolved native contracts return to evidence review.
- Required modes: **Arcade, Versus and Training**. Preserve current CVS2
  behavior in all three. Arcade Quick Load is an intermediate research pilot,
  not an admission requirement for existing character announcements.
- Identity: game 1, existing ROM indices 2 and 3; retain build validation.
  Existing English labels remain the wording baseline. Decoder admission of
  both ROMs does not establish live acceptance for both.
- Result: settled character highlights, grouped simultaneous changes, retries
  only until backend acceptance, and correct new-slot/re-entry behavior, with
  stale output rejected across departures and mutations.
- Preserve adjacent groove, ratio, stage and level selection behavior because
  it uses the same decoder and output path. This packet does not relabel the
  level screen or claim live verification of stage names.

Authoritative inputs are the [shared milestone](shared-selection-milestone.md),
[handoff contract](../collection-handoff-contract.md),
[offline gate trace](../collection-offline-gate-trace.md),
[Quick Load completion trace](../collection-quickload-completion-trace.md) and
[output invalidation review](../shared-output-invalidation-review.md).
The continuation adds the [native boundary trace](../collection-native-boundary-trace.md)
and [dispatch contract](../shared-output-dispatch-contract.md). The immediate
application loop, same-path deletion ordering, later unit-task dispatch and
Versus/Training Change Character routes are now traced. Training has a separate
same-address mutation. Exclusive native scheduling and complete per-mode
coverage remain open.
The wider milestone still requires the scoped CVS1 F02 delivery; this packet
does not complete that feature or the collection's F01–F19 port.

## Review of the existing reader

1. `src/mod.c`, worker call to `cvs2_select_tick`, is the shipping selection
   entry. `src/cvs2_select.c` checks the audited instruction signature, session
   game/ROM, arena/context/root relationships and phase pointer, then copies
   exactly `CVS2_STATE_SIZE` (`0x216b0`) bytes. Active sampling is throttled to
   33 ms. Root/session rereads bracket the copy. Selection speech does not
   depend on a drawing callback.
2. `cvs2_select_decode`, `alternate_active`, `cvs2_groove_decode`,
   `cvs2_ratio_decode` and `cvs2_stage_decode` already encode native phase,
   player ownership, busy state and consistency rules. The Arcade character
   path accepts steps 1/4/7. The alternate path uses six handler variants,
   checks the published ID against the fighter ID, and supports the existing
   Versus/Training selection paths. Keep these rules and name tables.
3. `cvs2_selection_copy` already produces four bounded owner slots; only the
   first two are CVS2 players. It preserves labels, kind/value and visit.
   Alternate character visits include the team count as well as the selection
   step. Stage output is shared once. Do not replace this with a new decoder.
4. `cvs2_select_observe` consumes that copied format but still owns root-keyed
   pending/spoken history and directly calls `tc_say`. Two equal samples settle
   a choice, simultaneous changes form one utterance, and rejection retries.
   Root equality is a compatibility baseline, not lifecycle authority.
5. `src/game_session.{h,c}` and `src/game_selection.{h,c}` provide tested
   serialized ownership/publication and presentation. They are absent from
   `build.ps1`; the copied-label function itself is already in the DLL.
   Native callbacks must not call these components concurrently.
6. The worker also invokes gauges, announcements, quotes, story and positioned
   text. `cvs2_text_observe(root)` performs additional native reading; migrating
   selection does not make that read protected. `src/cvs2_frame.c` independently
   produces frame-timed audio. A selection-only router cannot certify the
   lifetime or output ownership of all those paths.

The newer evidence invalidates assumptions about eligibility, not the selection
decoders: release retains roots, Quick Load directly cleans up outside the old
wrapper, an in-place restore can retain addresses, and the manager can replace
a screen after its update returns. Readability checks and pointer brackets
cannot establish allocation lifetime or detect every such mutation.

## Mode contracts to implement separately

Shell mode codes and CVS2's internal handler selector at state+0x1ffef are
different namespaces. Never substitute one for the other.

- **Arcade:** explicit shell mode code 0 follows Offline Play entry. Preserve
  ordinary selection. For the narrow Quick Load route only, require correlated
  aGame request and validated initialization, current owner/session checks,
  import eligibility and a positively identified restore/repair branch through
  enclosing completion. All-zero fields cannot create provenance.
- **Versus:** shell code 1 writes O+0xd0338=1; the traced session writer sets
  S+0x80c=1. That bypasses this checkpoint consumer. It does not mean selection
  is unsupported. Retain alternate decoding and establish Versus initialization,
  normal update, rematch/re-entry and any applicable reset/load lifecycle before
  enabling its shared native producer. Do not wait for an Arcade restore branch
  that this mode bypasses.
  The Rematch Yes route is now statically traced: category-4 cycle unit waits
  for the native result signal, category-25 dialog confirms Yes, and the parent
  performs cleanup/init at `0x1400b49d1`/`0x1400b49dd`. Require new generation
  and enclosing completion; No follows a separate exit path. Outcome coverage
  and lifetime protection still need validation.
- **Training:** shell code 2 writes O+0xd0330=1 and O+0xd0334=0; the session
  writer derives S+0x828=1. Preserve alternate selection and establish Training
  initialization, reset, character-change/re-entry and applicable load mutation
  boundaries. Neither the word “reset” nor its UI presence proves snapshot
  callback coverage.
- Code 3 has a Training (Boss) handler and label, but the reviewed constructor
  does not prove CVS2 exposes it. Do not silently equate it with code 2 or claim
  it validated. Unknown codes remain explicit evidence gaps.

O is the shell owner from `0x141c14f90`; S is the session from `0x141c14f98`.
O+0x1cd and S+0x7bc exclude the known callback route for the narrow pilot.
S+0x7b4 has special clears, so it is not a universal online flag. Retain positive
mode records plus current consistency checks; discard provenance on departure
or an unknown/different mode. Quick Load may carry validated route provenance
under a fresh transaction label, never an old owner generation.

Missing mode-specific evidence keeps the new producer for that path unready;
it does not authorize dropping the mode from delivery or adding an Arcade-only
gate to the shipping reader. Existing shipping behavior remains the baseline
until the integrated replacement satisfies acceptance.

## Bounded implementation sequence

### 1. Implement the offline bridge and preserve decoder parity

Add a small worker-owned integration module, proposed
`src/collection_session.{h,c}`, without native pointer reads or hook installation.
It owns `game_session`, `game_selection` and the mapping between native handoff
labels and worker session labels. Keep native revocation labels distinct from
the serialized component's generation/revision counters.

Its inputs must be bounded copied lifecycle records and copied selection
choices, carrying adapter/ROM, route/mode identity, revocation label, native
invocation identity, capture sequence/time, completion and loss status. Define
the record limits and loss behavior in the implementation; reuse existing
bounded transport mechanisms rather than adding a speech FIFO. Never retag an
old copied observation with the current session merely because it arrives now.

Process invalidation before eligible records. Reject stale acquisitions, then
use `game_session_begin_if_current`; successful begin still leaves capture
disabled. Resume only on a matching evidenced completion. Once a copied sample
is matched to the current eligible owner, use `game_session_capture`,
`game_session_publish`, `game_selection_offer` and `game_selection_drain` under
the same worker serialization. Bind emit to `tc_say(text, TRUE, "cvs2-select")`
through the reviewed output arbiter from step 3.

Preserve two distinct publications for settling; draining or replaying one
sequence twice is not another sample. Keep the existing 500-ms age policy,
future-time rejection, owner order, accepted-history semantics and retry rules.
A revision change clears pending settling but retains accepted choices; a new
generation clears both. An eligible INACTIVE choice rearms its owner;
UNAVAILABLE breaks settling without rearming accepted history.

Do not feed NULL/short copies to the decoder to represent lifecycle suspension.
`cvs2_selection_copy(NULL, 0, ...)` yields INACTIVE, which would rearm history.
Suspend/drop an invalid capture at the bridge; preserve the distinction between
unavailable observation and an observed exit from selection.

Extend `tools/test_cvs2_select.c`'s existing `shared_sample`/`shared_replay`
helpers to replay the existing alternate, P2, simultaneous-player and slot
transition fixtures through the shared presenter. Keep legacy assertions for
wording parity. Add bridge tests for lifecycle ordering and loss; do not invent
native mode semantics from synthetic samples. This step can proceed now.

### 2. Close native lifetime and per-mode completion evidence

The updated copy candidate is synchronous post-original **application update
`0x140057ae0`**, outermost admitted invocation. Manager update `0x1401a6200`
handles traversal and deferred replacement, but unit-task updates follow it at
`0x140057cee`. Post-manager return is therefore not the full enclosing boundary.
Re-resolve current aGame membership at the eventual protected boundary; a
remembered entry root or cleared descriptor is insufficient. A newly constructed
screen is not automatically initialized. Neither candidate is approved for use.

Before installing a producer, establish exclusive native ownership covering
every admitted mutation/destruction path, or a real shared lifetime guard.
The immediate caller is resolved: application update `0x140057ae0` calls
manager slot+0x30 at `0x140057cd4`. Application release-all calls slot+0x28;
application deletion destroys the manager before the shell arena. Outer routine
`0x14020bb10` updates then eventually deletes that same application object.
The unit manager supports direct and batched +0x40 dispatch, with a wait-all-shaped
import at `0x1401f80a7`; its result is unchecked. Resolve pause-task category
flags and dispatch across modes, wait outcomes, generic asynchronous pool coverage and
release-all callers next. Relate these to Quick Load, Training mutation and
arena release. Local ordering does not prove
exclusive ownership. Observer-only locks, thread-ID checks and revision brackets
cannot protect a concurrent free.

Training's S+0x28 callback reaches `0x1406aa960` and changes existing native
storage. Its screen path skips ordinary update that iteration. Revoke before
this mutation and require enclosing completion. Deferred cleanup/reinitialization
requires a fresh generation. Versus and Training Change Character commands 8/9
both enter state 14 (`0x1400a6bf0`): cleanup/init when S+0x840 is nonzero,
with additional session writes after init. Revoke at mutation, then require
validated init plus enclosing completion. Selecting the command without mutation
does not create a generation. The Restart shortcut excludes these two modes.
Versus Rematch now maps from native S+0x7d0 writers through its cycle unit and
Yes/No dialog. Yes requires cleanup/new generation and completion through
`0x1400b4b86`; counters and state transitions follow initializer return.
The configured Training Coin Button restart now maps through option+0x1c and
`0x14004c790` to that same-allocation callback, with S+0x838 modifiers. Remaining
Versus outcome and other Training reset coverage is explicit in the trace.

Reuse `collection_probe` diagnostics and retained captures for a bounded
completion probe with reviewed ABI/original forwarding, invocation nesting,
route/mode records, initialization, positive restore/repair completion,
revocations and drop counts. Do not promote the five-site diagnostic to a
production router. No repeated character selection research or drawing hook
is required by the current evidence.

At the protected point, retain build/ROM/field bounds and guarded copying,
then run the existing copy decoder on owned bytes. If label formatting is
too expensive for the measured native budget, decode the bounded owned state
on the worker; native pointers must never cross that boundary. Measure reads,
copied bytes and worst callback duration before enabling. Preserve prompt
worker responsiveness and frame-timed health. The old comment that 33 ms is
faster than a 60-Hz frame is incorrect: 60 Hz is about 16.7 ms. Use actual
latency measurements, not that comment as a sampling guarantee.

### 3. Implement revocation and output ordering

Revoke immediately at screen-release entry and independently at cleanup entry,
before original execution. Track nesting so cleanup return cannot reopen an
enclosing release. Release return stays departed. Quick Load entry requires
departure/new-generation semantics even with identical addresses; transaction
return, initializer return, save/bypass return or flag clears cannot resume it.
Same-owner snapshot mutation suspends the revision before execution and resumes
only after every enclosing mutation completes under matching labels.

Native callbacks publish immediate monotonic revocation and bounded records;
they do not invoke the worker-owned session or wait on speech, disk or mixer.
Loss, overflow, exhaustion or an unobserved ordering path disables eligibility
until a fresh validated acquisition. Empty nesting alone is not readiness.

Specify and test the dispatch/revocation linearization before runtime wiring:
which dispatches win, how an in-flight backend call is retired, and how shell
takeover prevents a delayed old-owner `speech_stop` from canceling new speech.
A token check followed by `tc_say` leaves a race; a post-call check prevents
stale bookkeeping but cannot unspeak accepted NVDA output. The dispatch contract
now specifies reservation versus revocation, labelled acceptance and retirement
before newer speech. Its implementation and concurrent tests remain prerequisites;
these properties do not follow from `game_session`.

Apply labels at publication and consumption on existing `text_channel` FIFO
and frame transports, including manual read-current-text. Audit late story/text
producers and match/round/quote calls; do not claim whole-reader safety from
selection tests alone. Preserve each reader's accepted semantic history policy.

For audio, choose revision-aware scene acceptance or a fresh monotonic audio
lease per eligible generation/revision. Reject stale publish and stop, retain
existing scene ownership guards, and preserve frame-timed health. Scene ownership
currently rejects legacy clock ticks: provide and test the clock event path
before enabling scene ownership. Do not introduce a second mixer or queue.

### 4. Wire and validate the integrated replacement

Only after steps 2 and 3 are satisfied, add the session/presenter and bridge to
`build.ps1`, connect native copied records, and replace the legacy selection
output call in the worker path. Ensure only one selection presenter emits.
Keep the decoder and compatibility tests. Reconcile sibling readers explicitly
instead of removing `cvs2_select_tick` and accidentally removing their behavior.
Keep native offsets/name tables inside adapters; shared modules know neither.

## Fixtures and acceptance

The existing replay loads local `build/cvs2-state` captures. SHA-256 values
checked for the four shared replay inputs on this review:

- `ryu-return-08.bin`, Ryu:
  `8f133b3407610b1d2f3b33c4fff1c487b072065d84d70920b74d6f5447a73333`.
- `sagat-07.bin`, Sagat:
  `3bdb0395ac0516b1ee86bae99ceebdfdcfb85c5d4d0b85ba8831c289c9ebfd26`.
- `moved-03.bin`, Yun:
  `cda8b12c7a11f8d41669a39ff8925f00513e38e6cbd1eb31c438442ffc906e7e`.
- `return-ryu-06.bin`, inactive selection:
  `aae8f1423f8f6109305df6c8b08e796b133b4ce9d6b7d679f622b7573b9079b6`.

Capture semantics and prior instruction evidence are retained in
[the state-access trace](../cvs2-state-access-trace.md). The unpacked code
identity for the newer lifecycle evidence is SHA-256
`9c340b8ce40a0c15a3eccd3c47207b02a59e38365bf99ae7605afc26bb84f95b`.
The Ryu/Sagat/Yun shared replay uses synthetic session labels/timestamps; it
does not validate native acquisition. Existing alternate/stage cases include
instruction-derived fixtures, not three-mode live lifecycle acceptance.

Required offline controls extend existing tests:

- Preserve exact `Ryu`, `Sagat`, `Yun`, `Ryu. Sagat`, groove, ratio, stage and
  level wording. Cover P1/P2, repeated character in a new slot, inactive/re-entry,
  inconsistent IDs, busy phases, simultaneous changes and rejection retry.
- Reject old publication/output, reordered acquisition completion, same-address
  replacement, invalid ROM/build, missing mode records, lost events and exhausted
  counters. Retain accepted choices on same-owner revision invalidation.
- Replay release/cleanup nesting and direct Quick Load cleanup; neither nested
  return nor local repairs authorize premature publication. Save/bypass must
  not satisfy pending restore. Versus/Training bypass flags must not disable
  their ordinary selection paths.
- Use controlled concurrent producer/worker/backend interleavings for departure
  after copy, revocation at dispatch, backend rejection across switch, shell
  takeover, delayed stop, old audio publish/stop and native copy versus destruction.
  Serial session tests cannot substitute for these tests or native lifetime proof.

Reuse these commands, then broaden checks when their layers change:

```powershell
.\tools\run_cvs2_test.ps1
.\tools\run_game_selection_test.ps1
.\tools\run_game_session_test.ps1
python -m unittest discover -s tools -p 'test_collection_*.py'
```

For shared text/audio/runtime wiring, run all current `tools/run_*_test.ps1`
runners, the relevant existing sabotage controls, and `build.ps1` with its
import check. Use the current runner inventory rather than a historical count.
Do not deploy as a side effect of offline verification.

Required live acceptance uses a labelled run card for **each** of Arcade,
Versus and Training, recording ROM/build, mode provenance, event loss and
native ordering. Exercise highlights, both available player sides, new slots,
leave/re-enter, backend recovery and return to shell. Include mode-appropriate
rematch/reset/character changes and any supported load path. Exercise
CVS2-to-CVS1 ownership transfer without stale speech/audio, plus clock/health
parity. Record inapplicable actions with evidence rather than skipping a mode.
Reuse the completed Quick Load transaction capture; only collect the missing
completion/lifetime/mode information in a subsequent exercise. Audible latency
and preserved behavior require user listening acceptance.

## Completion record and next action

This review produced a handoff, not runtime implementation or live acceptance.
On 2026-10-09 the existing CVS2 replay passed, the shared presenter passed
209 checks, the session passed 73 checks, and all 90 collection Python tests
passed. The initial compiler invocation failed because sandbox access to the
installed WinLibs path was denied; the approved rerun passed all three native
runners. No source change or fresh DLL build was needed for this document.

Next implement step 1 with existing fixtures. Native activation remains blocked
on the named lifetime and dispatch contracts and per-mode completion evidence;
continue offline bridge/parity work while those are resolved. Any proposed
change to labels, decoder semantics or mode scope requires concrete contradictory
evidence. Keep the native packet awaiting evidence rather than advertising
the entire implementation as ready or completed.

Continuation through 2026-10-11: the native audit now reproduces 62 aligned spans,
virtual slots, command/input tables and labels, with 36 changed-byte
refusal controls. All 96 collection Python tests pass. Application-loop ordering,
later unit dispatch, Training mutation and both modes' Change Character routes
are traced. The output dispatch proposal defines race outcomes and implementation
tests. Native runtime code and existing decoders are unchanged; no DLL was built
or deployed in this continuation.

Further research closes pause-task registration into unit-manager category 3 and
the configured Training Coin Button restart route. The generic work submitter
has one discovered direct caller, which normally polls job completion but skips
that poll at a submission distance of 4096, even without slot overwrite. Its
geometry-group threshold of 10 selects parallel execution, not maximum capacity.
Pinning that code narrows the remaining task:
prove queue-capacity/completion invariants and the enclosing virtual-call path,
not merely the existence of a worker. Neither the search nor serial audit proves
all indirect submissions absent. Live category flags and wait results remain open.

Versus Rematch Yes/No and two native result-signal writers are now traced. Yes
requires cleanup/new generation; No exits separately. Required remaining mode
work is outcome/ordering coverage and additional reset/re-entry paths, rather
than rediscovering this dialog.

The first 2026-10-12 lifetime continuation pinned 63 spans and 44 changed-byte refusal
controls. The generic submitter has no local capacity check; the consumer index
and callback pointer both change before callback completion. The caller checks
submission distance only before entering its context polling loop, so a bound
at poll entry alone cannot protect against later slot reuse. A conditional
two-worker schedule model distinguishes 4095 submissions (slow first callback
blocks) from 4096 (first callback bypassed, caller can return). This is not a
live failure reproduction. The geometry-group destructor and thunk now identify
the group/member allocations whose lifetime must be covered. Actual CVS2 group
membership, arena access, global submission bounds and enclosing completion
remain open; see the native boundary trace for addresses and assumptions.

The subsequent live-assisted continuation pins **73 spans and 58 changed-byte
refusal controls**. It resolves the outer kind-0 worker's completion signal:
that signal follows its own callback loop, with no explicit generic-ring drain.
Later unit-manager phases dispatch member +0x50/+0x48; their geometry-group
implementations also provide no explicit generic completion barrier. Generic
startup initializes tickets to 1 and configures up to 8 workers. Its semaphore
maximum of 4096 constrains wake tokens, not already-running callbacks.

The user-labelled Arcade run (PID 35472, game 1, ROM 3) supplies actual category
flags and a bounded worker inventory. The final 12-sample series found 8 generic
workers, producer=consumer=1, empty generic records and no exact reviewed
geometry group in the traversed unit lists. Pause category 3 had flags 0x3fd,
selecting queued dispatch; the caller can still execute batch work. Eleven
brackets matched and one changed; none was unreadable or truncated. These
observations narrow this scene without proving a global bound or every mode.
Nine live IAT targets, including both wait APIs and the event/semaphore APIs,
resolved against DLL exports with matching prefixes. Wait outcomes remain
unobserved. The compressed retained fixture and reproduction commands are in
the native boundary trace; `collection_worker_capture.py` and
`collection_worker_imports.py` perform read-only research without hooks.

The user-confirmed Versus character-selection follow-up in the same process
also retained producer=consumer=1 and an empty generic ring in all 24
observations. S+0x80c=1 and the expected Versus cycle unit were present; 11
units were traversed. Ten brackets matched and two changed, with no read errors
or truncations. The separate Versus fixture is linked in the native trace.
Neither series covers rematch/reset transitions or proves callback descendants
cannot touch the arena. Queued category configuration also does not establish
a separate worker: batch count 1 takes the caller-only path.

The user-confirmed Training character-selection capture completes the three
bounded selection-scene inventories. Game=1, ROM=3, S+0x80c=0 and S+0x828=1
were recorded. All 24 observations retained eight generic workers,
producer=consumer=1, empty generic records and no reviewed geometry group.
Eight units occupied categories 0 through 5, including the three additional
Training UI vtables documented in the native trace. Nine brackets matched and
three changed, with no read errors or truncations. The retained Training fixture
does not cover reset, character change or callback descendants.

Validation after all three selection-scene captures: all 111 collection Python tests
pass, including recorded-evidence, read-budget, unreadable-memory and changed
sample controls. No runtime source, DLL build, deployment or hook installation
was part of this continuation. The next native evidence is mutation/re-entry
coverage, arena-access coverage of observed callbacks, and successful wait
outcomes; the 4096-submission schedule was not reproduced in these scenes.

The concrete next implementation is step 1: the worker-owned offline bridge and
existing-fixture parity. Step 3's dispatcher can be implemented and concurrency
tested without enabling native reads. The remaining evidence packet is bounded:
prove arena-mutating task coverage and completion, map remaining rematch/reset
routes, then validate a protected application-level producer across all three
modes. Preserve the existing captures and decoder research. This handoff is
complete; shared runtime delivery and live acceptance are not.

Independent continuation audit against `46bf563`: the starting 111 tests and
retained three-mode evidence reproduce. The manifest now pins 86 spans, with
113 collection tests passing and 77 changed-byte refusal controls. Historical
captures still validated 73 spans; they have not been relabelled as fresh runs.

Training reset has a forced-request consumer: bit 7 of the low byte of S+0x830,
under the existing low-nibble and mode gates, sets S+0x838 and bypasses coin
polling/cooldown. Its full writer inventory and live UI mapping remain open.
The countdown's slot-3 helper copies from native storage into the slot, so its
return is not restore completion. Change Character continues through the pause
task return at `0x1400a6ef4`; its slot-1 call to `0x1400b5f90` bypasses copying
and cannot supply positive restore evidence. Preserve the cleanup/new-generation
contract and enclosing application completion requirement.

The three additional Training category-5 update callbacks now have pinned
descendants. One writes session statistics. All three also inherit a +0x48
phase that reaches virtual action dispatch through `0x140216460`; their +0x50
is an empty return. Neither UI naming nor +0x40-only inspection establishes
arena-write exclusion. Full descendant and receiver-lifetime coverage remains
open, including the other observed unit types.

Wait handling is now separated by call site: both worker wake waits check EAX,
whereas the batch join and both retirement joins do not. A failed/nonzero batch
wake path skips its completion signal. Actual wait outcomes remain unobserved;
neither read-only capture tool nor the existing six-site probe records those
return values. The native trace now specifies five exact result sites and the
bounded correlation/loss requirements for a future diagnostic. No diagnostic
extension, runtime deployment or fresh live transition is claimed. The 4096
case remains conditional, not an observed game failure.

The next continuation adds a separately opted-in, schema-3 diagnostic for the
five reviewed wait callers plus application, Training callback, initialization
and pause-update pairs. It reuses the existing bounded probe ring and file
writer and does not feed the session/presenter. See the
[lifetime run card](../collection-lifetime-probe.md). Whole-body guards, exact
API forwarding, raw DWORD results, last-error preservation, owned handle copies,
invocation matching and loss controls are tested. A compiler-eliminated guard
arming store was reproduced in its synthetic unreadable-memory test and fixed
with volatile TLS plus compiler barriers. This is not a live-game failure.
The native manifest now has 87 spans; all 121 collection Python tests pass.
Actual Training/reset and wait outcomes still require the labelled exercise.
All 35 native runners subsequently passed with individually checked exit codes;
the DLL build/import checks passed. Logs and the synthetic CSV/report are
retained in the offline validation fixture linked from the run card. On
2026-10-13 the verified DLL was staged with a verified prior-DLL backup and
the lifetime marker enabled while the game was closed. The user was notified
to launch for the first labelled Training exercise; no live outcome is yet
claimed.
