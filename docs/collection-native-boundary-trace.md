# Application scheduling and CVS2 mode mutations

Reviewed through 2026-10-12 against the same unpacked build as the
[offline gate trace](collection-offline-gate-trace.md). Reproduce with
`python tools/collection_native_boundary_audit.py --manifest-out docs/collection-native-boundary-manifest.json`.
The [manifest](collection-native-boundary-manifest.json) pins 86 aligned spans,
references and virtual slots. These establish local ordering, not exclusive
thread ownership. No production hook or native read is enabled by this result.

## Application owns the screen manager

The type registration at `0x14002aac0` names the manager `sArea` and points
through descriptor `0x141018b10` to its allocating constructor. The actual
application constructor uses the in-place constructor `0x1401a5a50`:

- `0x140056a62` constructs it; `0x140056a71` stores it at application+0x3ffc0.
- Its vtable is `0x141018b60`; singleton publication is to `0x1422c8358`.
- Application update `0x140057ae0` loads that field at `0x140057cca` and calls
  slot+0x30 at `0x140057cd4`, resolving to manager update `0x1401a6200`.
- Earlier in this application update, `0x140057b49` calls shell slot+0x30,
  resolving to `0x140050a10`, through application+0x40100.
- Application release-all `0x140057e30` calls manager slot+0x28 at
  `0x140057e50`, resolving to `0x1401a6390`.

The application vtable is `0x140fe2140`: +0 is deleting wrapper
`0x140057360`, +0x28 is release-all, and +0x30 is update. This closes the
previously missing immediate engine caller. The outer scheduling path below
resolves update/deletion ordering, but not every singleton access.

## Outer application loop

Registration `0x1400274d0` names `sAppMain`, descriptor `0x141c1b6c0`,
descriptor vtable `0x140fe2008`. Its +8 factory `0x1400565b0` allocates
0x40198 bytes and tail-calls application construction. Bootstrap supplies this
descriptor in R9 at `0x140047050`, then calls driver initialization at
`0x140047090`; that routine retains R9 in RBP. The driver copies a
type ID from descriptor+0x34 to driver+0x2a98 at `0x14020c163`.

Thread-creation excerpt `0x14020c3c1..0x14020c40a` supplies routine
`0x14020bb10` and driver context to import slot `0x140fdc3c0`, saves the
returned handle at driver+0x2a90, and initialization later invokes slot
`0x140fdc3d0` on that handle. These have CreateThread/ResumeThread argument
shapes; the static audit pins slots. The live capture below resolves their
export identities for that process, without observing return values.

The routine loads driver+0x2a98, calls registry lookup `0x1400bcc90`, calls
the resulting descriptor's factory+8 at `0x14020bb44`, and retains the
application in R13. It calls application update+0x30 at `0x14020bd77`.
The continuation either loops at `0x14020bd88` or eventually deletes that
same R13 object at `0x14020bdd7`. This deletion path follows completed
updates on the application thread. It does not establish that the GUI message
thread runs updates or that all mutations use the application thread.

## Unit updates occur after the manager

Application construction calls `0x140178f60` at `0x140056755` and stores
the unit manager at application+0x3ff78. Its vtable is `0x141015d18`.
After screen-manager update at `0x140057cd4`, application update invokes
unit-manager+0x30 at `0x140057cee`, resolving to `0x14017aee0`. This invokes
category-update+0x48 at `0x14017af29`, resolving to `0x14017ac60`.

The category traversal either calls each active unit's +0x40 directly at
`0x14017ae88`, or queues a unit/thunk pair into application+0xc8/+0xd0
at `0x14017ae70` and `0x14017ae78`. Thunk `0x140179c1c` calls unit+0x40.
The pause-task constructor publishes vtable `0x140fed360`, whose +0x40 is
`0x1400a65e0`, the command state machine described below. This establishes
the dispatch chain. Screen initialization constructs this pause task at
`0x140038c57`, passes it in R8 with category EDX=3 to `0x140179e00` at
`0x140038c72`, using the unit-manager singleton `0x1422c80b0`. That insertion
routine links the task into category head/tail fields +0x50/+0x58 with stride
0x30. The constructor publishes that singleton at `0x1401798eb`. Thus pause
membership in category 3 is now traced; its live flags and worker choice remain
to be recorded. Category flag bit 1 skips update; bit 0 chooses queued execution.

Queued categories invoke batch launcher `0x1401f7eb0` at `0x14017aebe`.
It launches/wakes task workers, executes a share on the caller via
`0x1401f86f0`, collects completion handles, then invokes import slot
`0x140fdc0c8` at `0x1401f80a7` with count, handle array, wait-all=1 and
infinite timeout. Its result is not checked before the next operation.
This is a completion-barrier candidate, not a certified successful wait.

Manager return is therefore not the end of all application work. Investigate
**application-update return** as the encompassing copy candidate. Still require
successful completion, current screen membership, mode provenance, no pending
transaction and lifetime protection. Never publish between cleanup,
initialization and later UI writes merely because one inner call returned.

A separate generic pool remains a coverage gap: worker entry `0x1401f7d40`
reaches `0x1401f7d60`, which pops application work records and invokes function
pointers after releasing the queue lock. A queue lock does not protect the
lifetime of objects used by those jobs. The batch barrier above cannot be
assumed to drain this separate pool. The submission/completion trace below
narrows this gap without asserting that no other jobs exist.

### Generic pool submission and completion

Submitter `0x1401f6ca0` reserves an index from application+0xb0 and masks it
to 4096 slots. Each 32-byte record at application+0x1fac8 stores context,
callback, argument and dispatch kind. The worker advances the consume index
at +0xb8, releases the queue lock, invokes the callback, then clears the
record's context. Its kind-zero call is `0x1401f7e9a`; the following context
clear is `0x1401f7e9c`. Popping a job does not mean its callback has finished.

An E8/E9 encoding search across the three dump code sections found one direct
submitter caller, `0x14056e074`, within `0x14056dff0`. This search does not
exclude indirect/computed callers. That routine submits member updates using
thunk `0x14056e480`, saves returned indices, and polls each corresponding
context until it is zero. The thunk dispatches member+0x40. The caller itself
is virtual, with a pointer at `0x1410693b0`; no direct callers were found.

There is a material exception: `0x14056e0ae..0x14056e0b6` skips the poll if
the current producer index minus the saved index is at least 4096. Therefore
this caller waits while the submission distance is below 4096, but reaching
the threshold cannot be promoted to proof of callback completion. Resolve capacity invariants,
other possible submitters and the enclosing virtual call before certifying
that application return excludes these jobs. This is a bounded remaining
question, not a reason to add a lock used only by the accessibility reader.

The caller is now identified as `uGeometry2Group`: registration `0x140034ca0`
names it, descriptor factory+8 points to `0x14056dbe0`, and that factory
publishes vtable `0x141069370`. Its +0x40 is the queue caller `0x14056dff0`.
The base-constructor chain reaches native unit constructor `0x1401ab510`
through `0x14059c030`. This connects the caller to the unit update interface;
actual live group membership, descendants and category flags remain unverified.
Nested unit execution must be considered rather than assuming the two pools
are independent in time.

Factory default group+0xb4=10 is a **parallel-execution threshold**, not a
maximum group size. The update chooses the queue path when group+0xb0 is set
and member count exceeds this threshold. It submits every member before polling.
Neither this comparison nor the submitter supplies a proven 4096-job capacity
bound. Do not mark the reuse exception safe merely because the default is 10.

The equality case matters: after reserving indices 0 through 4095, producer
index 4096 minus saved index 0 already triggers the bypass, even before a slot
is overwritten. A possible counterexample is a slow first callback with later
callbacks completing on other workers: the caller skips the first record and
can finish polling the rest while the first callback still runs. This is a
schedule allowed by the examined code, not an observed game failure. A capacity
claim of "at most 4096 jobs" alone is insufficient. The proof must establish
a global submission distance strictly below 4096 while a required callback is
outstanding, or another barrier that covers that callback. Include nested groups
and concurrent submitters in that bound.

### Completion predicates and the equality witness (2026-10-12)

The complete submitter span has no capacity check: it reads producer +0xb0
at `0x1401f6ce5`, increments it at `0x1401f6cf0`, masks the saved index
at `0x1401f6cfa`, and writes the record. It does not compare the consumer
index or wait for a previous occupant's context to clear. The later wake-shaped
import cannot retroactively protect those writes. An external capacity invariant
is still possible, but it is not enforced by this routine.

Worker `0x1401f7e33` advances consumer +0xb8 before callback execution. In
the kind-zero path it also clears record+8 (the callback pointer) at
`0x1401f7e8b` **before** calling the saved callback at `0x1401f7e9a`.
Consequently neither producer==consumer nor a null callback pointer proves
completion. The context clear after return is the relevant observed marker,
provided that the slot has not been reused.

The caller reads the producer and subtracts the saved index at
`0x14056e0a4..0x14056e0ab`. Its unsigned `jae` bypass includes equality.
After entering the context polling loop, the back edge at `0x14056e0e4`
returns to `0x14056e0d0`, not to the distance check. Thus a distance measured
below 4096 at poll entry is insufficient if other submissions can reuse the
slot during polling. The proof must protect the slot throughout the wait.

The audit tests now contain a deterministic, conditional schedule witness:
worker A claims job 0 and pauses inside its callback; worker B completes jobs
1 through N-1. With N=4095, the first context blocks the caller. With N=4096,
the first record is bypassed and every remaining context is zero, permitting
caller return while job 0 is outstanding. Both cases can have producer==consumer;
neither requires slot overwrite. This assumes at least two available workers,
a group taking the queued path with N valid submissions, and the described
callback timing. It does not establish live group size, worker count, game
failure or access to the CVS2 arena. The model tests the insufficiency of the
completion predicate, not native scheduling or memory visibility.

The lifetime dependency also includes the group itself: thunk `0x14056e480`
reads group+0x60 and group+0x70 before dispatching the selected member. The
group's deleting destructor, vtable slot `0x141069370` -> `0x14056ddc0`,
deletes members at `0x14056de27`, frees their pointer array at `0x14056de4c`,
and frees the saved-index array at `0x14056de79`. These operations are now
pinned. This identifies allocations that require protection; it does not prove
concurrent destruction or exclude synchronization inside untraced callees.

Next resolve actual group membership and descendant callbacks in the admitted
CVS2 paths, the global submission bound across nested/concurrent groups, and
any enclosing barrier that covers still-running callbacks. A successful wait
for an outer batch task alone is insufficient if that task can return from this
inner poll early. No native producer is authorized by this static witness.

## Worker completion continuation

### Worker construction, outer completion and later phases

The newly pinned startup excerpt `0x1401f65de..0x1401f682e` initializes
producer and consumer to **1**, at `0x1401f6654` and `0x1401f6688`.
The earlier job-0 example uses relative ticket numbering; the executable model
now starts at the native value. The equality counterexample is unchanged:
after tickets 1..4096, producer 4097 minus ticket 1 equals 4096.

Startup stores `min(application+0x3fe58, 8)` at +0xac, creates that many
generic workers using entry `0x1401f7d40`, and stores handles at
+0x3fc20 with stride 0x38. The constructor calls slot `0x140fdc3e0` with
initial count 0 and maximum 4096, storing its result at +0xc0. The live export
resolution below identifies this as CreateSemaphoreA. This maximum limits wake
tokens, not lifetime of already-consumed callbacks: the worker waits before
claiming a record and advances the consume index before calling it. Even a
successful ReleaseSemaphore does not make producer distance a completion count.

For the outer pool, entry `0x1401f8410` dispatches application+0x78 to
`0x1401f8430`. Batch kind 0 (used by the category updater) takes jump-table
entry `0x1401f86cc` to `0x1401f84b7`, claims from application+0x9c and
invokes application+0xc8 records at `0x1401f84fb`. After its own callback loop,
`0x1401f8698..0x1401f869c` signals worker-context+0x20. These are the
completion-event handles collected by `0x1401f8075` and waited on at
`0x1401f80a7`. The caller-side kind-0 path similarly invokes its own records
at `0x1401f8778`. Neither path explicitly drains the generic ring. Thus even
a successful outer wait does not repair an inner group callback that returned
early. This closes the proposed outer-batch-barrier explanation at that level;
descendant callbacks or another enclosing mechanism remain separate questions.

Two later application calls are also resolved:

- `0x140057d08` -> unit-manager+0x68 -> `0x14017b800` -> category+0x60
  -> `0x14017b720`, which dispatches unit+0x50 at `0x14017b7dc`.
- `0x140057d15` -> unit-manager+0x58 -> `0x14017b0c0` -> category+0x50
  -> `0x14017af60`, which dispatches unit+0x48 directly or in another batch.

Both category paths can also call unit+0x40 while admitting newly initialized
units. The geometry-group +0x50 and +0x48 methods (`0x14056e420` and
`0x14056e140`) iterate member+0x50/+0x48 respectively. Their own bodies
contain no explicit generic completion check. Unreviewed member implementations
are not ruled out as barriers. The application-return candidate still needs
coverage of every such invocation and descendant.

### Live Arcade evidence, 2026-10-12

The user supplied an Arcade session, PID 35472. The read-only worker inventory
validated all 73 reviewed code spans. Game=1, ROM=3, and raw S+0x7b4,
S+0x80c and S+0x828 were zero. The mode label is user-supplied provenance;
those zero values alone do not establish Arcade. An accompanying existing-tool
state capture retained phase bytes `0201010100000000`; it is a separate
observation, not an atomic join to the worker inventory.

Retained evidence is in
[`collection-worker-arcade-20261012.json.gz`](../tools/fixtures/collection-worker-arcade-20261012.json.gz),
SHA-256 `b2c4205cd5f2ab9b067c66aed600f495fc641cae60d68e064870bc0c28aac3e6`.
It includes the first inventory, the final 12-sample series, and live import
resolution, with original artifact and tool hashes. Intermediate exploratory
captures remain under `build/worker-arcade-20261012-*`.

The final series contains 24 observations over about 2.8 seconds. All showed
8 generic workers, producer=consumer=1, no occupied generic records, and no
exact reviewed geometry-group vtable in the traversed lists. Ten unit nodes
were recorded, versus five in the earlier inventory. The final nonempty
categories were 0,1,2,3,4,23,24,26,27. All 32 category flags were 0x3fd:
bit 0 selects queued dispatch, bit 1 does not skip the category. Category 3
contained the known pause-task vtable `0x140fed360`, with unit flags 0xf4c1a.
This resolves its observed registration and queued configuration for this run,
not the thread on which every pause callback actually executed. The batch
launcher can execute work on the caller, including a width-1 path.
Specifically, batch count 1 branches at `0x1401f7fd0` to `0x1401f80b6`,
temporarily sets width 1 at `0x1401f80c3`, and invokes caller-side execution
at `0x1401f80cd`. The observed pause category has one member. With a clean
batch accumulator and unchanged membership, that category therefore takes the
caller-only branch; the samples do not correlate a particular pause invocation
with that branch.

Eleven sample brackets matched; one changed as outer batch metadata changed.
There were no read errors or truncations. Each final bracket took about
1.3–1.8 ms externally; this is not a native hook budget measurement. The
generic pool's startup-valued counters and empty records are evidence that
this sampled scene did not expose the 4096 workload. They do not prove that
transient groups, reflective writes, reset/re-entry or another mode cannot do so.
The tool traverses reviewed category lists and exact group-member layouts,
not arbitrary containers or every callback's descendants.

Nine IAT targets resolved to KERNEL32 exports in this live process. Each target's
first 16 bytes matched the corresponding local DLL export; the report retains
module base, RVA and full DLL file hash:

| Import slot | Live export |
| --- | --- |
| `0x140fdc0c8` | WaitForMultipleObjects |
| `0x140fdc350` | WaitForSingleObject |
| `0x140fdc3a0` / `0x140fdc3a8` | SetEvent / ResetEvent |
| `0x140fdc3b0` / `0x140fdc3e0` | ReleaseSemaphore / CreateSemaphoreA |
| `0x140fdc3c0` / `0x140fdc3d0` | CreateThread / ResumeThread |
| `0x140fdc1d0` | CloseHandle |

This resolves identities for the captured process, not successful wait/signal
outcomes, full module integrity, or all future builds. The static manifest's
import-identity flag remains false because its evidence source is the dump.

Reproduce against a new user-labelled live scene (fresh output paths):

```powershell
python tools/collection_worker_capture.py PID --mode Arcade --label character-selection --count 12 --out build/worker-next.json
python tools/collection_worker_imports.py PID --out build/worker-next-imports.json
```

Use Versus and Training labels only for those observed modes. Next capture
reset/rematch/re-entry and Arcade mutation transitions, identify any newly
observed callback/container types, and establish wait outcomes and lifetime
coverage. Current evidence does not justify treating the generic group bypass
as an active Arcade-selection failure, nor certify the production copy boundary.

### Live Versus follow-up

The user then confirmed Versus character selection in the same PID. The
12-sample capture again retained game=1, ROM=3, but now S+0x80c=1 and
S+0x828=0. All 24 observations retained producer=consumer=1, empty generic
records and no reviewed geometry-group node. The lists contained 11 units,
including the expected Versus cycle vtable `0x140fefa88` (+0x40=`0x1400b46b0`).
Nonempty categories and 0x3fd flags matched the final Arcade series. Ten
brackets matched and two changed; no read errors or truncations occurred.
External bracket durations were about 1.3–1.9 ms.

The retained
[`collection-worker-versus-20261012.json.gz`](../tools/fixtures/collection-worker-versus-20261012.json.gz)
has SHA-256 `6840974e1f9c5968052269498e0d29005aafdacc5201841fa4781b989adc3dd2`.
This supplies a second mode's bounded membership/counter observation, not
Versus rematch coverage or proof of wait success. Mode switching did not produce
a different generic counter value in these endpoint samples; the switch itself
was not continuously traced.

### Live Training follow-up and three-mode scope

The user confirmed Training character selection, again at PID 35472. The
12-sample series validated all 73 code spans and recorded game=1, ROM=3,
S+0x80c=0 and S+0x828=1. All 24 observations retained eight configured
generic workers, producer=consumer=1 and empty generic records. Eight unit
nodes were traversed, with no exact reviewed geometry-group vtable. Nonempty
categories were 0 through 5; all 32 category flags remained 0x3fd.

The three additional category-5 vtables and their +0x40 targets were
`0x140fe5818` -> `0x1400789d0`, `0x140fea3e8` -> `0x1400917d0`, and
`0x140fea618` -> `0x140092160`. The previously observed Versus cycle vtable
was absent from this inventory. These are concrete next callback targets for
arena-access coverage; their presence does not certify their descendants.

Nine brackets matched and three changed; no read errors or truncations occurred.
External bracket durations were about 1.3–1.9 ms. The retained
[`collection-worker-training-20261012.json.gz`](../tools/fixtures/collection-worker-training-20261012.json.gz)
has SHA-256 `347229ce3a273955a1d6db73cd6841556c77537e659588790e3d73b93261b646`.

There are now 12-sample selection-scene series for Arcade, Versus and Training:
36 brackets / 72 observations in total, plus the initial Arcade inventory.
Every observation in those series retained startup-valued generic counters,
empty generic records and no reviewed group node in the traversed lists.
The 4096-submission counterexample remains a conditional static schedule;
these captures did not reproduce it. They do not cover reset/rematch/re-entry,
all containers or callback descendants, successful wait outcomes, or continuous
execution between samples. Native lifetime protection remains unproven.

## Destruction order and its limit

Application destructor body `0x140056e10..0x140057169` calls `0x1401f8130`
at `0x140056e36`, then deletes the manager at `0x140056f21`, then the shell
at `0x140056f63`. Manager deleting destructor `0x1401a5c10` walks eight
screen slots backwards, calls release+0x40 and delete+0, clears slots, clears
the manager singleton and calls its base destructor. Release-all has the same
backwards release/delete pattern. It is a separate invalidation boundary.

Shell deletion reaches body `0x140048380`; the allocator call at
`0x1400485f6` frees the session arena. Thus this application destructor orders
screen teardown before arena release. It does not prove a concurrent worker
has finished reading either allocation.

`0x1401f8130` sets application+0x7f, wakes workers, visits handles including
application+0x3fc20, invokes a single-handle infinite-wait-shaped import and
closes handles. It also visits the separate five-thread task pool. This narrows
the retirement candidate. The live capture below resolves the wait and close
imports; successful outcomes and complete task coverage remain unresolved. Likewise, the
manager base constructor/destructor invoke imports on object+8. Their presence
does not prove an acquired shared lifetime guard around update, copying and
destruction. Do not assign lock semantics from their shape alone.

## Training changes state without ordinary update

Use the existing shell mode derivation: Training code 2 derives S+0x828=1.
S is the session loaded from `0x141c14f98`; O is the shell owner loaded from
`0x141c14f90`. These are distinct from CVS2's internal handler selector.

The game-screen update at `0x140038f90` gates a path on S+0x828, S+0x780,
the low nibble of S+0x830 and S+0x838. Its call at `0x1400391f1` reaches
`0x140050fd0`, which checks game ID <=7 and calls the session function pointer
S+0x28 at `0x140050fe1`. This is a session callback, not a vtable slot or the
checkpoint restore callback.

CVS2 initialization publishes `0x1406a9a30` into S+0x28 at `0x1406a98e8`.
That callback checks S+0x828 and conditionally tail-jumps at `0x1406a9a3e`
to `0x1406aa960`. Searches limited to direct E8/E9 calls miss this edge.

The mutation reads the existing root from `0x1422debd0`, preserves two native
words, writes phase bytes through `0x1422dec30`, invokes `0x1406b2390` three
times and writes into existing root storage, including root+0x1f180. The
nested helper dispatches through a phase-indexed function table. These are
concrete same-address content mutations; this trace does not assign new
gameplay meanings to the individual fields or enumerate every nested write.

After the callback, the screen path changes additional session fields and
jumps at `0x140039254` to its epilogue. It skips ordinary update
`0x14003a290` and post-update `0x140039ed0` on this iteration. Therefore an
ordinary-update-return hook cannot be the sole Training completion boundary.
Revoke the revision before this mutation and require enclosing completion
before publication. The post-application candidate includes the outer screen
and later unit work, subject to lifetime proof and current membership checks.

### Configured Coin Button restart route

The existing live-caption mapping in `src/menu_speak.c` identifies zero-based
System Settings row 3 as `MENU_OP_COIN_BTN` (551), Coin Button. The GMD help
record 552 explicitly says it restarts the match in Training. Native row-value
selection at `0x1400a3534` uses task+0x338+row*4, hence task+0x344 for this
row. Settings commit reads that field at `0x1400a328b` and writes option+0x1c
at `0x1400a3297`; the option allocation comes from O+0xd04b8.

Screen update calls `0x14004c790` at `0x1400390fb`. This helper reads the
same option+0x1c, checks the configured input through tables `0x1411f7c70`
and `0x1411f7cb0`, and returns the input result. A positive result writes
S+0x838=1 at `0x140039111`; modifier handling can change that value to 2/3.
The existing Training gates and countdown still apply. A nonzero S+0x838
reaches S+0x28 via `0x1400391f1`, then the already traced CVS2 mutation.

This closes one named Training restart route from configuration to mutation.
It remains a same-generation revision change when no cleanup/reinitialization
occurs, and must wait through the screen's later writes before publication.
It does not establish every Training UI command or live input/latency behavior.

## Reinitialization is a separate transaction

The screen's deferred branch sees O+0xd034b, calls cleanup `0x14004adc0`,
option setup `0x140038280`, and initialization `0x14004c540`, then clears the
flag and returns early. UI branch `0x1400a6df4..0x1400a6e14` sets that flag.
Another UI branch at `0x1400a6bf0` calls cleanup and initialization directly.
Screen initialization `0x140038af0` also continues substantial work after the
initializer returns. Treat cleanup/reinitialization as departure and new
generation, with enclosing completion required. A flag clear is not readiness.
The mapping below closes Change Character reachability for two modes;
it does not inventory every Training reset or Versus rematch.

## Versus and Training Change Character

Offline pause-menu construction `0x1400a5db0` checks S+0x7b4=0, then
Training S+0x828 at `0x1400a5ecd` or Versus S+0x80c at `0x1400a5f1b`.
Both append command 8 or 9 using the game-dependent byte table at
`0x141207da0`. Label table `0x141207d30` maps both commands to record 756,
`CHARA_CHANGE`, **Change Character**, in local `msg.arc/menu_eng`.

Selected commands dispatch through `0x1400a6f50`; both entries target
`0x1400a689a`, requesting state 14. State table `0x1400a6ef8` maps 14
to `0x1400a6bf0`. When S+0x840 is nonzero this branch calls cleanup,
initialization and `0x1400b5f90`, then writes S+0x840=2, clears S+0xca4
and conditionally clears S+0x850. When S+0x840 is zero it closes without
this mutation. Revoke before actual cleanup; do not mint a generation from
the menu selection alone. Correlate successful initialization and enclosing
completion before admitting the new generation.

The Restart shortcut checks S+0x828=0 and S+0x80c=0 at
`0x1400a7603..0x1400a7615`. Therefore the deferred O+0xd034b route is
not evidence for this shortcut in Training or Versus. The separately examined
Versus Menu update `0x1400b4cf0` commits match options; its name alone does
not establish a rematch transaction. Training's callback mutation and this
character-change generation replacement require separate controls.

## Versus Rematch dialog and generation replacement

This is separate from the pause menu's Versus Menu options task. Screen init
tests S+0x80c at `0x140038e8e`, constructs `0x1400b45c0` at
`0x140038eae`, and inserts that object into unit category 4 at
`0x140038ec9`. Its vtable `0x140fefa88` +0x40 resolves to `0x1400b46b0`.

The unit's state 0 writes S+0x7d0=1 and clears S+0x7d4/+0x7e0. State 1
waits for S+0x7d0=0, updates match counters, suspends gameplay via session
fields and transitions to state 2. State 2 tail-calls `0x1400b4900`.
That routine constructs child `0x1400b5320` and inserts it into category 25.
Child vtable `0x140fefea8` has initialization+0x28=`0x1400b5a10` and
update+0x40=`0x1400b54f0`.

Child initialization supplies label 945 (`ASK_REMATCH`, **Rematch?**) at
`0x1400b5d4c`. Its selected value at child+0x35c maps to label 25 or 26,
**Yes** or **No**. Following confirmation and animation completion, value 0
selects state 4, which sets child+0x2a1 at `0x1400b5727`. Value 1 selects
state 5, which sets child+0x2a2 at `0x1400b577b`. The exact state table is
retained at `0x1400b5784`.

The parent checks +0x2a2 first, taking its exit path if set; it waits while
+0x2a1 is zero. A Yes completion enters cleanup `0x14004adc0` at
`0x1400b49d1`, initialization `0x14004c540` at `0x1400b49dd`, then calls
`0x1400b5f90` with ECX=1 and `0x140049b30`. Session writes follow, including
S+0x780/+0x778 clear and S+0x840=2 at `0x1400b4a27`, followed by counters,
possible seed reset and a state transition. The enclosing routine returns at
`0x1400b4b86`. Thus neither initializer return nor the later import-helper
call establishes readiness, and the Arcade restore gate must not be imposed
on this route.

**Implementation:** revoke before cleanup, require fresh generation even with
unchanged addresses, correlate validated init and completion of the whole
transaction, then admit only at the protected application-level boundary with
current membership. No, missing child and incomplete confirmation must not
create an acquisition. The dialog's category 25 and parent's category 4 also
mean the confirmation and cleanup need not occur in the same update invocation;
correlate the transaction rather than treating a frame counter as its identity.

### Native producer of the result signal

The option writer's Versus branch, tested at `0x140038819`, sets S+0x7c0=1
at `0x140038843`. Two CVS2 paths under this gate clear the S+0x7d0 flag
consumed by the unit above:

- `0x1406d6610` reaches a counter threshold of 0xb4 under its native state
  gates, derives S+0x7e0 from P+0x111, and clears S+0x7d0 at `0x1406d66bf`.
- `0x1406ffe60` has a phase-indexed branch that counts down P+8 and clears
  S+0x7d0 at `0x14070005d` once below 0xb4. Other gates and branches remain
  significant; this is not an unconditional completion notification.

These are positive native-writer-to-shell-consumer links. They do not establish
every win/loss/draw outcome, all native phase reachability, or live dialog timing.
The existing selection decoders remain unchanged. Do not assign a universal
meaning to a phase value or a readiness flag from these two conditional writers.

## Concrete remaining evidence

1. Resolve task submissions that can touch the arena, including the generic
   pool and application release-all paths; verify wait outcomes. Import identities
   and three-mode selection category flags are captured above; actual callback-thread
   coverage and transition scenes remain open. Close generic-ring reuse/completion invariants. Establish native
   exclusivity across manager traversal, UI Quick Load, Training mutation,
   reinitialization and arena release, or identify a real shared lifetime guard.
2. Validate outcome coverage for the now-traced Versus Rematch Yes/No route
   and remaining re-entry/Training reset paths; inventory additional mutations.
   Change Character is statically mapped for both modes; the configured Training
   Coin Button restart is also mapped to the same-allocation callback mutation.
   Versus Rematch Yes is mapped to cleanup/new generation, with two native
   result-signal writers and separate No handling. Live mode acceptance remains open.
   Arcade's existing positive restore/repair completion evidence remains valid
   for its narrow route. Neither other mode should inherit that admission gate.
3. Only then define the bounded native producer/probe and validate actual
   nesting, event loss and per-mode ordering. Reuse existing Quick Load captures
   and CVS2 decoders. No drawing callback is implied by this evidence.

The [implementation handoff](port-tasks/cvs2-shared-selection-handoff.md) and
[dispatch proposal](shared-output-dispatch-contract.md) separate work that can
proceed offline from native activation. The mutation-sabotage tests protect
these static claims; they do not certify scheduling or audible behavior.

## Independent continuation audit (2026-10-12)

Rechecked against base `46bf563`, the hashed dump, aligned disassembly and the
retained fixtures. The starting 111 collection tests passed. The existing
conclusions about manager/application ordering, selection-scene inventories,
same-allocation Training mutation and conditional 4096 bypass withstand this
review. No retained record contains a wait return value. The new manifest has
86 spans; it does not retroactively increase the 73 spans checked in each
historical live capture. No new live transition or wait-success claim is made.

### Training reset and Change Character continuation

The reset request has an additional statically visible input to the existing
consumer. After testing `(S+0x830 & 0xf)==2`, `0x1400390a6` tests **DL**, the
low byte of that field. When its sign bit is set, `0x1400390aa` writes
S+0x838=1. The nonzero test at `0x1400390b8` then branches directly to the
callback path at `0x1400391ea`, bypassing the configured-input polling and its
cooldown check. This identifies a forced-request consumer, not the UI action
or every writer that can set bit 7. It is not a second proven live reset route.

The earlier S+0x830==1 countdown branch calls `0x1400b60b0` with ECX=3 at
`0x140039058`. That helper copies **from** S+0x1c0, length S+0x1c8, to
slot-3 storage plus its 0x18-byte header, subject to its pointer gates. This
is a save-direction copy, not a positive restoration completion. Afterwards
the screen sets S+0x830=2, clears S+0x838 and sets cooldown S+0x83c=30.
The actual reset callback also has later screen writes: it conditionally clears
S+0xca8 or S+0xcac for S+0x850 values 3/4, clears the request and resets the
cooldown before taking the early screen return. Callback return alone is still
too early for publication.

Change Character's cleanup/init branch continues via `0x1400a6e02`, calls
`0x14007d090` to change task state, joins at `0x1400a6e8f`, may perform later
UI calls and finally returns from the pause update at `0x1400a6ef4`. The
initializer's return is not the end of this invocation. The branch passes
ECX=1 to `0x1400b5f90`; that helper permits only indices 0 or 3 past
`0x1400b5fbc..0x1400b5fc3`. Thus this particular slot-1 call bypasses the
copy at `0x1400b6070`. It cannot be used as restore evidence. Cleanup/init
still requires a new generation, and later application work still needs to
finish under lifetime protection. The S+0x840==0 path remains a non-mutating
close with respect to this cleanup/init transaction.

### Training callback coverage now has concrete descendant edges

The retained category-5 objects resolve as follows. Names below describe the
examined work, not an assertion that the whole object is harmless UI.

| Observed vtable | +0x40 update | Gated descendant | Concrete remaining edge |
| --- | --- | --- | --- |
| `0x140fe5818` | `0x1400789d0` | `0x140078b30` | Session-statistic writes; indirect call at `0x140078e3b` |
| `0x140fea3e8` | `0x1400917d0` | `0x1400918a0` | Formatting/helpers; indirect call at `0x140091f1b` |
| `0x140fea618` | `0x140092160` | `0x140092220` | Session-counter reads/helpers; indirect call at `0x1400923b9` |

For example, `0x140078b36` loads the session singleton; `0x140078b4c` clamps
S+0x8d4 to 99999, and `0x140078bc2` updates S+0x8f4. These are actual
session writes, not proof of writes into the CVS2 root allocation. Each
descendant's indirect call uses slot+0x60 on its receiver; the slot number
alone does not identify the unit, allocator or arena ownership.

All three observed vtables have +0x50=`0x140036b70`, an empty return, but
their +0x48=`0x1402160e0` tail-calls `0x140216460`. The latter dispatches
virtual +0x150 and action slots +0x158/+0x160/+0x170/+0x190/+0x198/+0x1a0,
and invokes `0x140308050` on a held object. The pinned span includes code
after the first return, since branches reach `0x140216661..0x1402166b9`.
Therefore +0x40-only inspection cannot exclude later arena access or establish
completion. These helpers, their receiver lifetimes, initialization and the
other observed unit types remain in the coverage inventory. No claim that
all arena-mutating callbacks have been enumerated is justified yet.

### Wait results: exact observation sites, still no live outcomes

The previous single `wait_return_checked=false` field describes the **batch
join**, not all waits. The manifest now records each continuation separately.
Live export identities from the retained Arcade report apply to that process;
the following result-handling claims come from the static call sites.

| Purpose | Call | First instruction after call | Result handling |
| --- | --- | --- | --- |
| Generic worker wake | `0x1401f7d9c` | `0x1401f7da2` | Tests EAX; nonzero retries without claiming a record |
| Batch worker wake | `0x1401f8479` | `0x1401f847f` | Nonzero skips callbacks and completion signal, then checks stop/retries |
| Batch completion join | `0x1401f80a7` | `0x1401f80ad` | No result test before another call |
| Generic worker retirement join | `0x1401f81b3` | `0x1401f81b9` | No result test before closing the thread handle |
| Batch worker retirement join | `0x1401f8202` | `0x1401f8208` | No result test before closing handles |

The batch wake failure branch reaches `0x1401f86a2`, **past** the completion
signal at `0x1401f869c`. It must not be summarized as signalling completion
on every return from the wake wait. Neither unchecked joins nor a conditional
failure branch establish that any wait actually failed.

The read-only inventory/import tools cannot capture these ephemeral register
results, and the existing six-site diagnostic does not record them. A fresh
inventory, a matching IAT export or a signalled handle sampled later cannot
substitute for the game's own result at the reviewed call. Actual outcomes
remain unobserved in this continuation.

The next bounded diagnostic must correlate call/return by thread and invocation,
copy raw EAX immediately at the five continuations above, retain the original
handle or bounded wait-all handle list, timeout and return-site identity, and
report loss/overflow and unmatched entries. Preserve original arguments,
return values and last-error state; a WAIT_FAILED record needs the error value
before diagnostic API calls overwrite it. Classify caller-only batch execution
as **no join executed**, not successful wait. Retain wake/signal outcomes too,
and keep shutdown joins in a separate shutdown exercise. A successful outer
join still does not prove that descendants drained the generic ring.

Training exercise records must join the request and mutation with cleanup/init
when present, the pause/screen invocation and the outermost application update.
Run plain and modifier reset, Change Character with and without the cleanup
gate, and re-entry under user-confirmed mode provenance. Record uncaptured or
inapplicable actions explicitly. This specifies missing evidence; it does not
claim a ready or installed completion probe. The 4096 witness remains a
conditional counterexample, with no observed failure in any retained scene.
