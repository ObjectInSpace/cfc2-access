# PWS2 actor and display ownership continuation

Offline investigation, 2026-10-04, from b01dc6f. Reproduce with:

```powershell
python tools/pws2_hud_audit.py --output docs/pws2-hud-manifest.json
python tools/test_pws2_hud_audit.py
python tools/test_pws2_ownership_capture.py
```

The original increment retained nine instruction-aligned spans and nine pinned
numerical operands. The continuation below expands this to 40 spans and 21
data ranges. The actor and transform-target manifests are unchanged.
This establishes a native gauge/display ownership relationship, not a live
HUD label, controller assignment, health range contract or audio permission.

## Fixed actor index and layout index diverge

R is the root pointer loaded from slot 0x14262f0b0. The complete publisher at
0x140afa010..0x140afa45c branches on byte R+0x5300e. Its zero branch
walks four fixed records, requires nonzero actor+0x401c, and passes the same
index in ECX and EDX to 0x140afb400 and three companion publishers.

The nonzero branch instead passes fixed index in ECX and a separate compacted
ordinal in EDX. It excludes records with zero actor+0x401c or actor+2 equal
to 12 or 13. EDI advances for every fixed record; ESI advances only after an
included record's publisher calls. Thus fixed actors 1 and 3 alone can be
assigned layout ordinals 0 and 1. Allocation failure does not prevent ESI from
advancing. This is a publication-time relationship, not proof of dynamic
relayout after elimination. The visible meaning of the branch remains open.

0x140afb400..0x140afb695 preserves ECX in EBP and EDX in ESI. It stores
the layout ordinal at object+1 and fixed actor index at object+2, and publishes
callback 0x140afab70 at object+8. It creates six subordinate resources.
Their first five +0x80 fields receive 159*layout-280; the sixth receives
159*layout-240. These are local resource components, not normalized screen
coordinates or an established left/right HUD mapping.

The nonzero publisher branch also constructs special objects. For its
three-resource object, object+1 is 0xff and object+2 receives a separately
selected index; callback is 0x140af9a90. The selection scans actor+2 for
12/13 with later matches replacing earlier matches. If none matches, ESI
retains the prior compacted count. The body does not establish a safe fallback
index in that case. Do not map this special object using its +1 value or infer
the gameplay meanings of 12/13.

## These callbacks belong to general-pool display objects

0x140aefc30..0x140aefc98 forwards the caller's ECX (8 here) to
0x140a4bc00 with EDX=1,
then dispatches a resource initializer through a pointer table. It returns null
on allocation failure or releases the object on initializer failure. The
initializer's transitive contract remains outside this increment.

0x140a4bc00..0x140a4bcfb takes the free object at R+0x407f0 and appends
it to the selected list through head/tail arrays R+0x40750/R+0x407a0.
For index 8 those are R+0x40790/R+0x407e0. The earlier pool-rebuild trace
establishes that these are 0x4c8-byte general-pool objects, separate from
the four 0x42b8-byte actors. Existing actor-processing caller evidence invokes
list-8 callbacks; the retained later-list-eight draw body consumes its resources.

This closes one display callback publication path. It does not prove that
fixed actors themselves use callbacks at +8. The known actor-processing loop
calls 0x140b14950 directly. A shared header layout is not proof of identical
dispatch for the two pools.

## Actor value feeds gauge animation

In callback state object+0x28=3, 0x140afab70 uses signed object+2 to read
R+0x40818+0x42b8*owner+0x1dc. It indexes seven four-element float arrays
by that same owner, starting at R+0x5327c and ending at R+0x532dc:

- +0x5327c is a delay initialized to 60 on a changed value, decremented by 1.
- +0x5328c receives the current actor value.
- +0x5329c retains a value that subsequently steps toward the current value.
- +0x532ac records the prior value used to choose the direction.
- +0x532bc/+0x532cc/+0x532dc feed different resource components.

The update writes resource+0x98 for object resources +0xe8/+0xf0/+0xf8
using (value/1000)*71/100+0.29, in the native scalar operation order. A
separate component at resource+0x80 uses object+1 for placement while still
using object+2 for the value. The special callback 0x140af9a90 uses the same
actor/array indexing but different resource arithmetic: value*0.885/1000+0.115.
The manifest pins the actual float32 constants, not their rounded decimal text.

This is strong static evidence for a health-gauge candidate and a direct
actor-to-display join. It is not proof of final visible geometry, exact screen
pan, every gauge mode, or a universal maximum of 1000. The initializer prefix
at 0x140a669b0..0x140a66aa8 writes actor+0x1dc=1000.0, but other writers
and mode-specific limits still need review. The arrays include animation
history, so they must not be treated as seven independent gameplay gauges.

These native callbacks do not validate the signed owner index before accessing
actors/arrays. A reader must validate 0..3, reject nonfinite values, and prove
current publication and visibility rather than copy that unchecked behavior.
State 0/1/2 performs delayed appearance and initial scaling; a published object
does not immediately imply a visible gauge or an eligible audio cue.

## Capture and shared integration consequences

Ownership capture schema 2 adds bounded raw reads of R+0x5300e and
R+0x5327c..R+0x532eb inclusive. It checks the HUD manifest's code spans
alongside lifecycle and actor spans. It does not follow general-pool pointers,
read display objects, decode ownership, or certify draw timing. Matching metadata
still cannot exclude torn data, ABA reuse or same-address reconstruction.

The next draw-stage probe needs both identities: fixed actor index and the
published display object's layout index. Retain callback identity, object
address, object state/visibility fields and lifecycle revision with them.
An actor sample alone cannot reconstruct which display object was published.
The special callback is a separate capture case. Capture resources at their
actual list-8 draw consumer to resolve local +0x80/+0x98 through the matrix
path; do not normalize the local layout constants directly to audio pan.

For the preserved audio_scene API, participant slot/incarnation must remain
stable when layout changes. Only gauges[].hud_pan follows proven display
placement; screen_pan follows independently proved actor projection. The
session router must own epoch/sequence and invalidation. Do not call
audio_scene_begin from these native callbacks or derive epochs from root
addresses. The shared router and the stage probe remain unimplemented.

Original next offline edges: review the publisher's caller at 0x140a6e60e
(the enclosing entry is corrected below), resource initialization and list-8
matrix construction; trace
actor+0x1dc damage/range writers. Then prepare the reviewed hook ABI and test
bounded capture, drop/overflow, repeated-pass and lifecycle invalidation before
deployment. Live exercises need ordinary and compacted layouts, a missing early
actor, the special-object branch, damage to distinct owners and camera movement.

## Validation

Four HUD-audit tests, eight ownership-capture tests, seven actor-audit tests and
eleven metadata-capture tests passed. HUD controls reject changed numerical
operands, an owner/layout publication mutation and a partial instruction span.
Capture controls preserve raw display values and reject unreadable display state.
Capture CLI help passed. The preserved audio suites passed 35 scene checks and
44 mixer checks. The first audio invocation could not access the installed
compiler in the sandbox; the same runners passed with compiler access.
The transform-target manifest reproduced unchanged, all nine HUD code spans
passed the capture verifier against the offline dump, and `git diff --check`
passed.

No runtime sources changed, and no DLL build, deployment, attachment, live HUD
observation or listening acceptance occurred.

## Publisher and resource continuation, from e308e64

Offline investigation, 2026-10-04. The tree began clean at e308e64. The
recovered audio and ownership files are preserved. This continuation retains
29 instruction spans and 14 pinned data ranges in the same HUD manifest.

### Publisher entry conditions and corrected function boundary

The call at 0x140a6e60e belongs to 0x140a6e250..0x140a6e7d7,
not 0x140a6dc30. The latter ends with a return at 0x140a6e24f; the next
instruction starts the publisher's state machine without intervening INT3
padding. A backwards INT3 search alone misidentifies this boundary.

The retained outer dispatcher prefix at 0x140a6ed60 first initializes
R+0x52ea8..R+0x52eb1 when +0x52ea8 is zero. It then sign-extends
R+0x52ea9, checks it against 10 with an unsigned comparison, and uses an
image-relative table. Entry 2 targets 0x140a6ee6c, which calls
0x140a6e250. The adjacent entry 3 calls 0x140a6dc30. Both table entries
and all six inner dispatch targets are pinned independently.

The inner dispatcher sign-extends byte R+0x52eaa and permits table indices
0..5. Entry 2 targets 0x140a6e555. It increments +0x52eaa to 3 before
performing initialization, invokes a callback selected by R+0x52ee7, and
walks the four actors, calling 0x140a56ad0 for included actors whose +2
is neither 12 nor 13. After that loop it checks R+0x52bd9:

- Equal to 1: jump to 0x140a6e6a1, bypassing HUD publication.
- Otherwise: call 0x140afa010 at 0x140a6e60e.

R+0x53027 and R+0x52edd are tested after that call; they do not gate
this publication. The inner state advances even if a resource allocation
fails inside a publisher. Ordinary repeated entry at inner state 3 does not
retry publication through case 2. Other state writers and callback effects
are not exhaustively covered, so this is a local control-flow condition,
not a universal once-per-match guarantee or a named gameplay phase.

The direct-call/tail-jump encoding scan found one candidate for each of
0x140afa010 and 0x140a6e250, at the retained sites above. Instruction
alignment confirms those two edges; it does not exclude indirect calls.

### Resource allocation, binding and reuse

The indexed publisher passes list=8, resource type=0 and count=6 to
0x140aefc30. Type-zero initializer table entry 0x141745700 points to
0x140a63ca0..0x140a63d25. It appends resources to object+0xe0,
starting at the current signed object+0xd8 count. For each resource it
decrements R+0x70810, pops a pointer from the table starting at R+0x6f410,
stores that pointer, sets resource+1 to zero, and increments object+0xd8.
An empty pool writes a null pointer at the next position and returns zero.

The wrapper releases the partially built object on that failure. The retained
object releaser walks only the successfully counted resources, dispatching
through the signed resource+1 type. Type-zero release entry 0x14172af48
points to 0x140a64d20. It clears 0xa8 bytes, returns the resource to the
free table, restores flags +6=0xf, enabled byte +0=1, scalar +0x30=1,
and three scale components +0x98/+0x9c/+0xa0=1. The initializer at
0x140a643d0 builds 640 such 0xa8-byte records at R+0x55010 and their
free table, with the same defaults. The three float32 defaults are pinned.

The object releaser unlinks the object, clears its 0x4c8 bytes, preserves
the next link for traversal, and returns it to the free list. Both release
and allocation add 8 to the 16-bit R+0x53110 counter and copy it to
object+0x38. This word wraps and is not a session epoch or a sufficient
ABA defense. Retain it as diagnostic metadata alongside router revision.

The binding helper 0x140a60aa0 takes resource RCX, record index EDX,
group index R8D and asset base R9. It resolves a signed relative group
offset from base+0x2c+32*group, then a 0x3c-byte record. It resolves
the record's signed relative packet offset into resource+8, clears
resource+0x10/+0x18/+0x78, copies three components to +0x80..+0x88,
and three dwords to +0x8c..+0x94. This binding call does not build
resource+0x34's matrix. The asset bounds and every later binding writer
remain unproved; a reader must not traverse these asset tables unchecked.

### Matrices are constructed after callback dispatch

At the end of 0x140a4c710, list 8 callbacks run through 0x140a4c3e0;
then ECX=8 is passed to 0x140a7c600..0x140a7c8ff. This second routine
walks the list, selects matrix slot 1 per object, and conditionally builds
object+0x78 and each resource+0x34. Enable bytes do not gate this builder.
They gate the later draw. The mask controlling a rebuild is 0x13f, tested
against object+0xc4 or resource+6. With no masked bits, the stored matrix
is retained, not replaced with an identity matrix.

For the branch without bit 0x20:

- Object construction copies the shared template, optionally copies an
  override matrix from object+0xb8, then applies flagged translation
  (+0x3c), rotations (+0x50, +0x4c, +0x48 in that order), and scale
  (+0x54). It copies the result to object+0x78.
- Resource construction copies the matrix pointed to by resource+0x78,
  or object+0x78 when that pointer is null. It applies translation
  (+0x80), rotations (+0x94, +0x90, +0x8c), then scale (+0x98),
  and copies the result to resource+0x34.

The flag bits for translation, the three ordered rotations and scale are
1, 8, 4, 2 and 0x10 respectively. The retained translation and scale
helpers copy the mutable shared template, replace the translation row or
three diagonal entries, and premultiply the current matrix using
0x140f32730. Each rotation helper returns immediately for a zero low
16-bit angle; otherwise it copies the template, replaces four entries
using the two previously traced lookup helpers, and premultiplies. Do not
assume a live identity template or replace these operations with a guessed
matrix order. A replay needs the actual template and angle lookup evidence.

Bit 0x20 takes a separate path. Object construction first saves its local
rotations/scale to +0x78, then loads R+0x51dd0+0x308*i, where i is the
signed byte R+0x52e88. It overwrites that current matrix's translation
with object+0x3c, premultiplies the saved local matrix, and saves again.
For a resource it first transforms +0x80 through the chosen parent into
a temporary vector, builds local rotations/scale from the template, loads
the same indexed transform block, overwrites translation with that vector,
premultiplies the saved local matrix, and saves resource+0x34. The native
index is unchecked. These branches establish dataflow, not camera ownership.

### Actual list-8 consumption and capture boundary

The existing draw dispatcher constructs slot 3 immediately before the later
lists at 0x140a5d821..0x140a5d86b, then selects slot 1 and calls list 8
at 0x140a5d85c. A raw pass-global value alone cannot distinguish this
stage from earlier actor passes. Keep an explicit stage identity.

The list-8 draw routine requires nonzero object+0xc0 and a positive signed
object+0xd8 resource count. For each resource it can first call
0x140aee2e0 when resource+0x10 is nonnull and global 0x14297a9dc
is not 3. That helper transforms and rewrites packet geometry using other
resource matrices; its asset-driven counts and indices are unchecked.
Binding the indexed gauge resources initially clears this pointer, but that
does not prove it stays null. This conditional call occurs before the
resource enable/packet checks.

Submission additionally requires resource+0 nonzero, resource+8 nonnull,
and the packet's first signed dword nonnegative. Only then does
0x140a5df7c call 0x140f32440 with RCX=resource+0x34. Its return
address is 0x140a5df81. This is the exact matrix-copy consumer for the
candidate capture; list entry alone is too early to establish submission.
Object+0xc4 and resource+6 then select rendering helpers, including
resource or object scalar arguments. Reaching this consumer is evidence of
the native submission path, not proof of final pixels, clipping or audible
eligibility. The queue continuation below establishes further rejection paths.

The indexed gauge callback sets object+0xc0=1 in state 1 when its word
countdown reaches zero, advancing to state 2. State 0 clears several local
scale components and starts a countdown of 60. Thus state 2 can reach
the draw path; requiring state 3 alone would discard its appearance animation.

### Packet segments, queue acceptance and matrix payloads

The default-scalar wrapper 0x140f35ef0 supplies 1.0 and queue type 0 or 2;
the explicit-scalar wrapper 0x140f36410 supplies its scalar and type 1 or 3.
The choice within each pair depends on context+0x1f8518. Both tail-call
0x140f34cf0. Its ordered scalar<=0 branch returns without submission;
readers must still reject nonfinite values independently. It clamps the scalar
above at 1.0 into payload+0xcc, with the explicit/default mode at +0xc8.

Segments start at packet+0x18. Only a negative signed segment header enters
the loop, which advances by 0x50 plus signed segment+0x4c. Optional copying
into the native segment buffer has a 0xe0000-byte capacity check. Each
iteration calls 0x140f2f390 with the selected type and a 0x130-byte payload.
Consequently a resource matrix load can precede zero queue attempts.

The queue builder rejects payload use above 0xe0000 bytes and later rejects
a full 0x800-entry bucket. Its pinned type table sends types 0/1 and 2/3
through different matrix-copy branches:

- Both copy slot 3 (context+0x1f816c) into payload+8 and slot 1
  (context+0x1f80ec) into payload+0x88.
- Types 0/1 copy the shared mutable template into payload+0x48.
- Types 2/3 copy slot 2 (context+0x1f812c) into payload+0x48.

Thus a slot-1-only record omits two matrices used by this renderer. The
accepted path copies the payload into context+0x100030+used, writes a
32-byte descriptor containing type, payload pointer, size and sorting data,
then advances bucket/group counts and byte use. The function has no explicit
success-result contract. Depth classification, including the types-2/3 helper
0x140f2ee80, still needs review; it is not a validated screen projection.

The queue drain's pinned table sends all four types to 0x140f2eb29.
It submits command 4 for 128 bytes starting at payload+8, command 7 for
64 bytes at payload+0x88, then calls 0x140f340b0 with the descriptor.
The retained renderer prefix pushes payload+0x88; its retained tail submits
vertices through 0x1400487c0 and pops the matrix. These prefix/tail spans
do not certify the entire intervening vertex pipeline or clipping.
Command copying through 0x140048810 is itself conditional on session+0x774,
nonzero size and a two-megabyte buffer bound. None of these stages proves
visible pixels. Keep matrix-load, segment-attempt, accepted-queue and drain
events distinct; do not label the first as a completed draw.

### Further actor-value writers

The retained fixed-record call loop at 0x140a6dfd0 derives RCX from
R+0x40818 and advances by 0x42b8 before calling 0x140ad44f0 with
DL=0x10. That callee requires actor+0x401c nonzero and returns for an
ordered nonpositive actor+0x1dc comparison. Its COMISS/JAE pair does not
reject an unordered value; readers still need their own finite check. It
sets +0x1dc to 0 and conditionally replaces it
with 1.0 when a selected descriptor's +0x12 has bit 0x10 set. It then
routes through 0x140ad2230. Descriptor selection, other damage writers
and mode-specific maxima remain open. In particular, neither a 1000.0
initializer nor a zero/one transition establishes a universal health range.

A second retained caller at 0x140ad1296 derives an actor using signed
object+1, the fixed 0x42b8 stride and R+0x40818, stores a descriptor at
actor+0x4a8, and calls 0x140ad17b0 with EDX=1. The callee starts from
actor+0x1dc and rejects ordered nonpositive values. A signed word from
descriptor+2, a conditional helper and mode/actor/context multipliers
determine the subtraction at 0x140ad1906. The pinned multipliers include
approximately 0.6, 0.5 and 0.25; an additional descriptor-selected factor
uses a table. The descriptor helper is not fully traced, and a signed
subtraction operand is not proof that every call loses health.

The depleted-value branches conditionally write 0.0 or restore 1.0, using
context+0x828, descriptor flags, the incoming mode and actor fields. This
establishes additional floors and a fixed-actor join, not a universal upper
bound. Exhaustive writer coverage, descriptor semantics and mode-specific
maxima remain open.

### Next implementation boundary and actual validation

See [the draw-stage research packet](port-tasks/pws2-draw-stage-capture.md).
It records the tested entry ABI component and unresolved bounded capture,
lifecycle and renderer work. It is not ready for runtime installation.
The existing shared lifecycle investigation still requires real thread/order
evidence before its hooks can grant production output generations. The
preserved audio scene API must remain unwired until that boundary and the
gameplay decoding contract are established.

All 25 PWS2 Python tests passed (ten HUD audit, eight ownership capture,
seven actor audit), as did eleven metadata-capture tests. The HUD tests now
pin the two dispatch tables and exclusion branch, callback-before-builder
ordering, resource override path, draw gates before matrix consumption,
queue matrix selection and capacity rejection, and indexed subtraction.
Every pinned data range has a mutation rejection control. The recovered
audio scene's 35 checks and mixer's 44 checks passed. The first compiler
attempt was denied by sandbox visibility; the same runners passed with
compiler access.

The new src/pws2_draw_entry.{h,c,S} is an unwired helper-entry ABI prototype,
excluded from build.ps1. It filters the reviewed return address, passes the
RDI/RBX object/resource identities to an observer, preserves last-error and
machine state, and tail-forwards to the original. Its sink does no native
reading because no sink implementation or installer exists yet. The synthetic
runner compiled the production filter and passed active, inactive, nested and
unrelated-caller controls, checking arguments, return path, volatile registers,
flags, SSE/x87 state and last-error. This is baseline SSE preservation; upper
AVX lanes are outside the traced helper contract. No attachment, deployment,
live HUD observation or listening acceptance occurred.
