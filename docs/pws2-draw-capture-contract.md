# PWS2 bounded matrix-load diagnostic, version 1

Baseline: 5f9e3fc. This contract is for copied raw diagnostic evidence,
not production session authority, accepted rendering, visible pixels or audio.
The offline reference is tools/pws2_draw_contract.py; its synthetic tests
exercise acquisition bounds and copied command parsing. There is no native
invocation wrapper, transport integration or installer yet. The continuation
from 904b955 adds unwired native reader and batch-storage components below.

## Native component checkpoint, 2026-10-05

`src/pws2_draw_reader.{h,c}` implements the exact acquisition sequence with
explicit errors, issue bits, valid-copy mask and charged attempts/bytes/queries.
Failed or short copies are zeroed, not retained as successful raw evidence.
The native backend uses at most two VirtualQuery calls followed by the existing
TLS fault guard. It rejects an already armed guard without altering it. The
worker must first pass mem_guard_install and the page-size check; installation
is not performed in the reader. The caller supplies a checked identity and QPC
deadline. Neither the API nor matching brackets grant invocation admission.

`src/pws2_draw_batches.{h,c}` supplies eight fixed batches, 32 event slots each,
and FREE/WRITING/READY/READING transitions using full-barrier interlocked
operations. Only the owning producer calls event/publish; every returned slot
must be filled by the reader before publication. The worker releases its own
READING batch. Stale acquisition increments a loss counter and drops the batch.
The worker still needs to recheck the diagnostic revision before output. No
timeout path can seize WRITING storage. Components remain excluded from the
production DLL and have no sinks, arming or teardown integration.

`tools/run_pws2_draw_reader_test.ps1` passed 234 reader checks and 70 batch
checks with the installed compiler. Native tests exercise readable boundaries,
NOACCESS and unconsumed GUARD pages, guard contention, injected short/faulted
reads, all ten deadline boundaries, exhaustion, stale publication/consumption,
two simultaneous producers and return after a paused producer interval. The
initial fixture reused its call counter and missed its injected identity change;
resetting that fixture counter corrected the failing test. These tests do not
prove concurrent protection changes safe: the existing guard deliberately does
not catch a guard-page exception introduced between query and copy.

Still required: invocation admission and enclosure ABI, revision/lifecycle
integration, worker output/footer and timeout races, full installation signature
and rollback tests, hook timing, and the prepared live exercise. Batch tests
alone do not constitute full transport or timeout/shutdown acceptance.

## Stage and association

Use the tested src/pws2_draw_entry component at helper 0x140f32440, filtered
to return address 0x140a5df81. Enclose list-8 draw 0x140a5de70 in a separate
invocation observer. Require an admitted, same-thread, nonnested invocation;
the helper's caller filter alone is insufficient. The enclosure must preserve
the full incoming and returned machine state, including incidental RAX,
flags and floating-point state, even though the reviewed caller ignores the
result. The existing helper ABI test does not test this enclosure.

Name the event `list8_before_matrix_copy`. RDI is the object; RBX is the
resource; RCX must equal resource+0x34. The incoming resource matrix is the
matrix about to be copied. The selected slot has NOT yet been overwritten:
the four slot snapshots are explicitly pre-copy values. No observer may
replace resource+0x34 with the pre-copy slot-1 contents.

Read the object's signed +0xd8 resource count and at most eight +0xe0 array
entries. Require exactly one occurrence of the incoming resource and report
its ordinal. Reject counts outside 1..8, absent matches and multiple matches.
Do not truncate an oversized association array and then claim uniqueness.
Validate resource alignment in R+0x55010, 640 records of 0xa8 bytes. These
checks do not prove allocation or lifetime. Never walk object+0x18 links,
follow parent/packet payload pointers, search the pool, or retain an owner cache.

Retain object+0 (list), +1 (unsigned layout), +2 (signed fixed actor owner),
+8 callback, +0x28 state, +0x38 wrapping word, +0xc0 enable, +0xc4 flags and
+0xd8 count. Indexed callback 0x140afab70 and special callback 0x140af9a90
remain distinct. Owner must be 0..3 for an ownership interpretation; layout
0xff is valid raw special-layout evidence, not owner -1. State 2 is retained.
The wrapping word and native addresses are not participant incarnations.

Resource bytes retain type, flags, scalar, matrix, parent override identity
and local components without dereferencing the override. Unknown callbacks,
wrong list/type, disabled object/resource, invalid owner, negative packet
header, wrong selected slot and nonfinite matrices produce explicit issues.
Preserve their raw bytes for diagnosis; they cannot become validated owners.
Null packet/range failures reject the partial record. Layout is never a
fallback for invalid ownership.

## Exact acquisition budget

For one admitted event, the maximum is ten logical read attempts and 904
requested bytes. Failed/short attempts consume budget and are not retried:

1. Root slot 0x14262f0b0: 8 bytes; require the independently admitted PWS2 root.
2. Context slot 0x142c79b40: 8 bytes; require nonnull and checked arithmetic.
3. Object header: 0xe0 bytes.
4. Object+0xe0: count*8 bytes, at most 64.
5. Incoming resource: 0xa8 bytes.
6. Resource's packet pointer: only the 0x18-byte packet header.
7. Context+0x1f80a4: 0x108 bytes (selector, intervening raw word, four slots).
8. Globals 0x141c00210: 0x80 bytes (viewport then mutable template).
9. Root slot again: 8 bytes.
10. Context slot again: 8 bytes.

Check additions before forming pointers. Resource matrix, slot matrices and
the two global matrices are checked for nonfinite floats; raw local components
are not all declared to be floats. No packet segment or command-buffer memory
is read by this event. Additional pass globals and final-renderer state are
outside v1; adding them requires a revised budget and evidence contract.

Native implementation must use the existing checked-copy behavior: readability
validation followed by a fault-guarded copy, with exact-length success required.
Never nest the TLS fault guard; report guard contention instead. The guard
deliberately does not catch guard-page exceptions (src/memsafe.c), so the scalar
lifecycle probe's omission of VirtualQuery is not a safe general copying policy.
Do not use bare memcpy or assume a successful query proves lifetime.

Every range above is at most 264 bytes. Verify at installation that the system
page size is at least 264 bytes; then each range can intersect at most two
memory regions. Budget at most two VirtualQuery calls per attempted range,
twenty per event and 640 per admitted invocation, with no pool/page scan.
Do not add another query before mem_copy or scan whole objects/pools. Record
read attempts and query counts, and enforce the elapsed budget below. At four
admitted invocations per second the theoretical ceiling is 2560 queries/second;
this is a bounded short diagnostic, not an acceptable assumed steady-state
cost. Measure actual hook time before live use and reduce admission if needed.
Test the compiled reader on inaccessible, guard and boundary-crossing pages.
The Python reader injection does not prove native guard safety or performance;
concurrent protection/lifetime changes remain a native review obligation.

## Admission, completion and losses

Concrete implementation limits, not measured native performance:

- One explicitly armed capture lasts at most 30 seconds and admits at most
  120 outer invocations. A process-wide nonblocking reservation admits at
  most one invocation per 250 ms; other calls forward without memory reads.
- Admit at most 32 helper encounters per invocation, including rejected
  read attempts. Thus at most 320 reads / 28,928 requested bytes per admitted
  invocation, and 3,471,360 bytes per complete arm. Stop new reads at an
  elapsed two-millisecond invocation budget. Check time before every read;
  this is a stop-admitting deadline, not a hard OS scheduling latency bound.
- Reserve a preallocated batch before starting an invocation. Use eight
  batches of 32 event slots, with a compile-time event-size limit of 1280
  bytes (327,680 bytes of event storage plus fixed headers). A header has
  its own completion/loss fields; never depend on a spare event slot for it.
- States are FREE, WRITING, READY and READING. A single producer owns each
  WRITING batch. The worker can only acquire READY batches. The producer
  publishes READY with release ordering after its final writes; the worker
  acquires before copying. A batch is returned to FREE only by its reader.
  Exhaustion drops the invocation; it never blocks a render thread.
- Store arm ID, monotonic invocation/encounter sequence, QPC time, thread ID,
  depth, build-manifest identity, checked ROM/session/root bracket, and
  diagnostic boundary revision. IDs are not derived from native addresses.
  Record suppressed nesting, budget exhaustion, read failure and loss counters.
  Repeated resource encounters stay separate; never deduplicate by pointer.
- An invocation returns COMPLETE only if its closing identity/revision label
  matches, every encountered admitted record was accounted for, and no limit,
  fault, nesting or contention loss occurred. Otherwise publish INCOMPLETE
  with reasons. COMPLETE still means a completed observation interval, not
  atomic memory, complete scene, or successful draw.
- Increment in-flight admission before consulting active, following the
  collection probe's footer-race rule. Stopping disallows new admissions.
  Worker completion waits nonblockingly for admitted producers to exit and
  drains READY batches before writing a clean footer. A hung producer is
  reported as incomplete after timeout; never read or recycle its WRITING
  storage and never free active trampolines.

Reuse the collection probe's worker-side output/loss and installation
patterns when extracting a shared diagnostic transport; do not bolt large
matrix records onto its scalar ring or fork another speech/audio queue.
The lifecycle probe currently has no production router generation API.
Use arm-local diagnostic labels, explicitly marked `boundary_order_unverified`.
Observed load/register/cleanup/release entry or exit increments the diagnostic
revision and invalidates an open batch. Recheck revision before publication
and consumption. Reject stale batches at consumption, retaining only a loss
summary. Missing lifecycle events or same-address ABA can remain undetected;
matching root/context brackets cannot upgrade this limitation.

## Installer and offline acceptance boundary

Choose MinHook helper-entry plus list-entry enclosure, rather than modifying
the internal matrix-copy call. Verify full retained list/helper bodies and
the enclosing reviewed callsite before creating either hook. Set original
trampolines and sinks before enable; enable only after both sites are created.
Any failure makes capture inactive and rolls back all created sites. If disable
fails, retain passive forwarding code and trampoline storage until process
exit. No concurrent sink replacement or detach/free while producers may run.

Implementation must test both signature mismatches, creation/enable failures
at each site, rollback failure, original called exactly once on every path,
list enclosure state preservation, unrelated helpers, nested invocations,
two producer threads, timeout/return races, stale revision at consumption,
overflow without an available worker, guard contention and inaccessible pages.
The existing helper ABI suite and Python reference cover only their stated
components. Installer/transport/native-reader acceptance remains unperformed.
This is a concrete offline implementation contract, not deployment readiness.

## Copied command snapshots are a separate tool

`decode_commands` accepts already copied bytes, with a 64-command / 65,536-byte
limit. It requires a complete window, 24-byte headers, positive payload size,
header-inclusive bounds, exact matrix-command lengths/counts, and no trailing
bytes. Unknown commands remain raw. Command 4 requires 128 bytes and command 7
64, each count 1. `command7_rows` preserves bits while selecting input words
0,4,8,12; 1,5,9,13; 2,6,10,14. This decoder neither attaches to a process nor
claims that a local matrix-load event corresponds to one of these commands.

## Prepared live exercise after implementation

First pass every affected native/transport test and required build regression.
Keep the boundary run card as a separate lifecycle-order exercise. Then arm a
short draw capture with recorded ROM/mode/build and explicit user actions:
ordinary layout; fixed owners 1 and 3 compacted to layouts 0 and 1; state-2
appearance; special layout 0xff; damage to distinct fixed owners; camera
movement; departure and restore/replay at the same address. Compare copied
owner/callback/matrix evidence with observed HUD behavior, and inspect the
completion/loss footer before interpreting absences. Unexercised branches
remain open. This run does not authorize spatial audio or normalized health.
