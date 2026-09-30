# Collection in-place restore boundaries

Offline Astra continuation from b60d3bf plus pending lifecycle/catalog work.
Reproduce with `python tools/collection_restore_audit.py`; compare the compact
result to `collection-restore-manifest.json`. The pinned code SHA-256 is
`9c340b8ce40a0c15a3eccd3c47207b02a59e38365bf99ae7605afc26bb84f95b`.
No new live observations or detours. All nine native runtime branches now
have an individually checked save/restore routine and a direct caller prefix.

## Shared gates, distinct operations

Each reviewed routine loads S from `0x141c14f98` and returns without copying
if any dword at S+0x7b4, S+0x828 or S+0x80c is nonzero. These numeric gates
are not assigned UI or network meanings. With the gates clear, it loads the
secondary block B from the per-game secondary pointer slot and compares
signed B[0] against zero:

- B[0] <= 0 saves the primary root R into B+header, for the published primary
  size. The copy target is `0x140eec5a0`, whose direction was already traced.
- B[0] > 0 takes the in-place restore branch. The routine does not need to
  publish a new root address. Subsequent native repair helpers differ.

Thus a hook on routine entry alone cannot label every invocation a restore.
A save or bypass would otherwise spuriously reset speech and audio history.
Conversely, watching root-pointer changes cannot detect the restored state.

## Individually checked paths

Addresses below are entry, restore-branch entry, and preparation edge,
respectively. The manifest retains exact ranges, hashes, gates and caller
prefixes, including which preparation edges are calls versus tail jumps.

- CVS1: `0x1405a6840`, `0x1405a68ea`, `0x1405c5e46`.
  Save size 0x32ab0, header 8. Restore copy at `0x1405a68f5` writes
  B+8 to R; post-copy helper `0x1405adee0` follows.
- CVS2: `0x1406aab40`, `0x1406aabe6`, `0x1406bad41`.
  Save size 0x216b0, header 8. Restore copy at `0x1406aabf1` writes
  B+8 to R; `0x1406ace60` follows.
- CFJ: `0x1407cbb00`, `0x1407cbba6`, `0x1407ddeb8`.
  Save size 0x26e80, header 8. Restore copy at `0x1407cbbb1` writes
  B+8 to R; `0x1407cf5d0` follows.
- Ordinary Alpha 3: `0x14086d590`, `0x14086d63d`, `0x140899780`.
  Save size 0x12f98, header 8; restoration is selective, described below.
- Alternate Alpha 3: `0x140dcc840`, `0x140dcc8ed`, `0x140dfa7c7`.
  Save size 0x1b528, header 8; distinct selective restoration below.
- Power Stone: `0x140974180`, `0x14097422d`, `0x140976cec`.
  Save size 0x428f0, header 16. Restore copy at `0x14097424c` uses B+16.
  Four restored bytes are retained around `0x140996360`, then written back;
  additional repair calls follow. Copy return is not the end of restoration.
- Power Stone 2: `0x140a472f0`, `0x140a473a6`, `0x140a4caf3`.
  Save size 0x8cd40, header 16. Restore copy at `0x140a473d9` uses B+16.
  Eight restored bytes are retained around `0x140a4bf10` and `0x140a4ba60`,
  then written back, with additional processing afterward.
- Project Justice: `0x140b93630`, `0x140b936d6`, `0x140b9e1e6`.
  Save size 0x9e8a8, header 8; selective vector-copy restoration below.
- Plasma Sword: `0x140ccccc0`, `0x140cccd69`, `0x140cde377`.
  Save size 0x511b8, header 8. Restore copy at `0x140cccd74` writes B+8
  to R; `0x140cdad50` follows, plus a conditional helper for B[0]==2.

## Why a universal copy hook is insufficient

Ordinary Alpha 3 begins with inline vector stores from B+0x30 to R+0x28
at `0x14086d65a`. It later performs three helper copies:

- `0x14086d778`: B+0x1b8 to R+0x1b0, length 0x1540.
- `0x14086d798`: B+0xe638 to R+0xe630, length 0x1100.
- `0x14086d7b8`: B+0xfdd4 to R+0xfdcc, length 0x800.

Alternate Alpha 3 begins the corresponding inline store at `0x140dcc90a`
but uses different later ranges:

- `0x140dcca38`: B+0x200 to R+0x1f8, length 0x1600.
- `0x140dcca58`: B+0x15490 to R+0x15488, length 0x17c0.
- `0x140dcca78`: B+0x1737c to R+0x17374, length 0x1000.

Project Justice's positive-control branch starts with vector stores from
B+0x2030 to R+0x2028 at `0x140b936ed`, followed by additional inline
copies/loops. There is no direct `0x140eec5a0` call on that branch in the
reviewed body. These are concrete selected-region replacements; they do
not establish a complete transitive write inventory for called helpers.

Both Alpha branches and Project Justice therefore invalidate the proposal
to detect all restoration by intercepting one full-block memcpy. Even the
six full-copy paths continue native processing after that copy returns.

## Earlier state changes and the router contract

Every retained preparation prefix contains state changes before invoking the
save/restore routine. Additional dispatch and continuation evidence is retained
below. Named quick-load/reset commands and all transitive callers remain open.

The safe design must distinguish three concepts:

- Temporary capture/output suspension while native state is changing.
- A state revision when a confirmed replacement occurs, separate from owner
  generation and speech event/visit identity.
- Resumption only after the relevant native completion boundary and readiness
  checks; a copy-helper return is not sufficient.

Do not increment the generation on every preparation call without proving its
semantics. Do not read replacement fields concurrently merely because the root
address still matches. Determining the earliest suspension boundary, exact ABI,
thread ordering and completion boundary remains Astra work before deployment.

## Preparation dispatch and continued processing

The schema-2 audit retains 32 spans, including 14 additional dispatch,
scheduler and continuation bodies. Eight exact table slots are validated;
their neighboring bytes are not treated as proof of table length.

- CVS1, CVS2 and CFJ have preparation dispatch routines at `0x1405c5d30`,
  `0x1406baba0` and `0x1407ddd60`. Each uses signed phase+2 as an index;
  entry zero of its table points to the reviewed preparation routine.
- Alpha 3's scheduler `0x14086e700` uses a task table whose index 3 points
  to `0x140895c50`. That task calls the preparation table at
  `0x140895cf1`; entry zero is `0x140899700`. After preparation returns,
  the task can perform more work and loop back to `0x140895c90`.
- Alternate Alpha 3 has a separate scheduler at `0x140dcd5b0`, task-table
  index 3 targeting `0x140df6c50`, and preparation-table entry zero
  targeting `0x140dfa750`. Its task can also loop. These are separate
  schemas, not one shared set of offsets.
- Power Stone's outer update at `0x140973300` calls preparation at
  `0x140973523`. Preparation calls restore at `0x140976cec`, continues
  through player/helper processing, and has a tail transfer at
  `0x140976d54` to `0x1409a8760`. The outer update also continues.
- Power Stone 2's outer update at `0x140a48cc0` conditionally calls
  preparation at `0x140a48d80`, then continues through helpers. The
  preparation body continues after restore call `0x140a4caf3` into mode
  dispatch. Its retained code ends before the table at `0x140a4cdb8`.
- Project Justice's scheduler `0x140b93b60` dispatches task-table index 5
  to preparation `0x140b9e170`. Preparation continues after restore call
  `0x140b9e1e6` through a mode dispatch, ending with a tail transfer to
  `0x140b93ce0` at `0x140b9e40a`. Trailing table bytes are excluded.
- Plasma Sword's native update `0x140cccb60` calls preparation at
  `0x140cccba4` when root byte zero is 3. It continues afterward and
  eventually tail-transfers to `0x140ccdc60` at `0x140cccc0d`.

These findings reject publication at the restore helper's return. They do
not certify the outer update as a presented frame: shared replay can invoke
that update repeatedly, as the snapshot continuation explains.

## Destructor ownership continuation

A data-pointer search finds `0x140048700` in slot `0x140fe12f0`.
Constructor prefix `0x140047e00..0x140047e61` loads that table address at
`0x140047e17`, writes it to object+0 at `0x140047e25`, and publishes the
same object to `0x141c14f90` at `0x140047e5a`. The previously traced
destructor body writes the same table address on entry and clears that same
owner slot later. This establishes which object the deleting-destructor
table entry belongs to; it is distinct from session slot `0x141c14f98`.

The lifecycle audit now pins that constructor prefix and vtable target.
The actual virtual call site and its UI/lifetime trigger remain unresolved.
No direct call/jump encoding candidate to the deleting wrapper was found;
absence of direct calls is not absence of destruction.

## Verification and next evidence

Seven restore-audit tests pass: manifest reproduction, nine-branch coverage
and differences, signed-control mutation rejection, Power Stone header
mutation rejection, bypass-polarity mutation rejection, alternate task-slot
mutation rejection and post-restore tail mutation rejection. The collection
test group passes 34 tests, including snapshot, lifecycle and metadata checks.

The [shared snapshot continuation](collection-snapshot-trace.md) now traces
an additional load callback, its registration and two restore-and-replay
consumers. It requires separate owner, revision and publication identities;
one generation reset for every restoration would re-arm speech during replay.

Next: complete preparation ownership and shared state-load lifetime to
identify which events require suspension versus a new generation. Keep the
existing live metadata run card for labelled transitions; pointer observations
alone cannot validate inline restore execution. An event probe requires its
own bounded, tested ABI contract before asking the user to exercise it.
