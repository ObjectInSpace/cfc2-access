# Power Stone 2: fixed records, filtered lists and input slots

Offline investigation, 2026-10-01. Reproduce with:

```powershell
python tools/pws2_actor_audit.py --manifest-out docs/pws2-actor-manifest.json
python tools/test_pws2_actor_audit.py
```

The manifest retains thirty-nine instruction-aligned spans and the main code SHA-256
`9c340b8ce40a0c15a3eccd3c47207b02a59e38365bf99ae7605afc26bb84f95b`.
This is initial F06–F12/F19 acquisition evidence, not an implemented adapter.
No PWS2 attachment, live capture, deployment or audible validation occurred.

## Entry and phase

Use the collection lifecycle evidence for exact ROM routing and root
publication. R is read through `0x14262f0b0`; published root size is 0x8cd40.
The native update `0x140a47290..0x140a472c2` calls input update
`0x140a490a0`, another input/state helper `0x140a48dd0`, then phase
dispatch `0x140a48cc0`. It writes R+0x530e8=1 afterward. That write has
no certified frame-publication meaning.

Phase dispatch reads unsigned bytes R+0x52bd8 and R+0x52bd9. Top-level 0
performs initialization then falls through to the top-level-1 work. Under
that work, two byte gates R+0x52d2b/R+0x52d2e can bypass dispatch. Nested
value 2 reaches `0x140a4ca30`, already retained by the restore audit;
value 1 reaches `0x140a4d5c0`, value 0 reaches `0x140a5ff10`, and 99
reaches `0x140a4ec10`. These values are not yet labelled with visible screens.

## Four fixed records and a separate general pool

`0x140a4bf10..0x140a4c3d2` rebuilds object storage. It first resets a
separate pool of 200 records at R+0x4b10, stride 0x4c8. Do not confuse
that pool or its list heads with a four-player array.

Later it clears exactly 0x10ae0 bytes at R+0x40818, covering four records
of size 0x42b8. Their bases are R+0x40818, R+0x44ad0, R+0x48d88 and
R+0x4d040. The routine assigns byte +1 the values 0,1,2,3, respectively,
and word +0x38 the values 1,2,3,4. These are distinct fields; neither is
yet certified as a user-facing player label.

Before clearing, it preserves each record's byte +2 and byte +0x4028,
then restores them in a four-iteration loop. It links the records through
previous/next pointers at +0x10/+0x18, sets list-zero head R+0x40750 to
the first record, and also installs paired pointers at +0x20. The gameplay
meaning of the paired relationship is unverified.

Calls to `0x140a644a0` with EDX=19 and `0x140a65330` with EDX=2
allocate subordinate resources into each record's arrays at +0xe0 and
+0x188, with counts at +0xd8 and +0x180. Their small integer arguments
are allocation counts, not callback IDs or player numbers. Resource names
and renderer roles remain unassigned.

The existing restore trace proves this rebuild can occur after replacing
root contents while retaining the root address. Record index and address
are therefore insufficient to distinguish a new participant incarnation.

## Filtered list membership and dispatch

`0x140a65a60..0x140a65cc1` clears the four records' previous/next links
and R+0x40750. It rebuilds a chain containing records whose byte +0x401c
is nonzero, in increasing fixed-index order. The concrete tested bytes are
R+0x44834, R+0x48aec, R+0x4cda4 and R+0x5105c. The fourth can become
the head if the earlier records are excluded. This body does not establish
whether exclusion means absent, eliminated, hidden, CPU-controlled or another
state. Do not label the byte "alive" or "human" from this alone.

`0x140a4c3e0..0x140a4c415` receives a signed list index in ECX, loads
R+0x40750+8*index, passes each record as RCX to its callback at +8, and
walks next at +0x18. Its body has no index-bounds check. A future reader
must not copy its unchecked access or retain native linked-list pointers.
The actual callbacks assigned to these four records remain to be traced.

A list ordinal is not stable identity: omitting an earlier fixed record
changes later records' ordinals. This is the concrete shared-audio design
constraint established here. Keep fixed record identity, participant
incarnation, controller assignment and HUD position separate until their
relationships are traced and live checked.

## Four input records

`0x140a490a0..0x140a49132` updates four input records at
R+0x53114+0x28*index, index 0..3. It passes that index to the shared
helper `0x140048bb0..0x140048c81`, which rejects unsigned indices >=4,
reads S+0x218+4*index, and converts bits through mask pairs. Those mask
values and physical-button labels are not certified by this trace.

Each input record stores 16-bit current/previous masks at +0x10/+0x12,
newly set bits (`current & ~previous`) at +0x14, and newly cleared bits
(`previous & ~current`) at +0x16. The companion reset
`0x140a49140..0x140a49178` clears the four records' first dword and the
eight-byte range +0x10..+0x17, and writes 0x80808080 at +8.

The full helper `0x140a4e870..0x140a4eb83` copies all four 0x28-byte
records to R+0x531b4+0x28*index when R+0x52d2b and R+0x52d2e are
both zero, subject to the earlier helper's state dispatch. This is an actual
copy gate, not a certified pause flag or atomic capture boundary.

`0x140a7ce00..0x140a7d790` connects these copied inputs to fixed actor
records on specific branches. Its first loop starts both indices at zero,
advances input offset by 0x28 and actor offset by 0x42b8, and runs four
times. For signed R+0x52ee5=1 and actor+0x4028=0, the copy at
`0x140a7d1e4..0x140a7d216` transfers the indexed input's 0x28 bytes
to actor+0x4b0. Mode value 2 has a similar copy at `0x140a7cffa`, with
an additional float-zero gate at actor+0x1dc. The float's gameplay meaning
is unverified, and its native comparison also allows unordered values through.

This establishes index-preserving input routing on those branches. It is not
a general human/CPU classifier. Other branches clear masks or call other
producers. Following the copy, the routine expands bits 1/2 to bits 4/8 in
the masks and can apply further suppression. Actor masks are therefore not
necessarily identical to the earlier input snapshot. Controller assignment,
network-peer identity, mode labels and HUD corners remain unverified.

## Processing order is also separate from identity

The same actor routine later chooses one of six four-byte rows at
`0x1417308d0`, using R+0x52f10 modulo 6, and obtains a starting index
from `0x140a46770` before limiting it to four positions. The retained table
contains the permutations `[0,1,2,3]`, `[0,1,3,2]`, `[0,2,1,3]`,
`[0,2,3,1]`, `[0,3,1,2]`, `[0,3,2,1]`. Its exact bytes are guarded
separately from the main code hash. The start helper's semantics are not
assigned by this trace.

At `0x140a7d603` a signed table byte chooses the fixed actor; the code
checks its inclusion byte and passes that record to `0x140b14950`. It
advances the table position modulo four. Thus callback execution order can
differ from fixed record order as well as from filtered list order. No audio
owner should be inferred from "the first callback this frame." The bounded
caller `0x140a4c710..0x140a4c7d6` invokes this routine before additional
list dispatches; the visible mode and all its entry paths remain open.

## Remaining work and validation

The draw continuation `0x140a5d5a0..0x140a5d8a6`, called at the phase
dispatcher tail except for nested value 99, exposes indexed transform data.
On the R+0x52fdc=0 branch it takes signed R+0x52e88 into global
`0x14297a9e0`, addresses R+0x51d90+0x308*index, and passes that address
to `0x140f32440`. The latter copies exactly 64 bytes into the current
indexed slot of the shared rendering context from `0x142c79b40`. This
establishes the copied transform block, not its world/view/projection convention.

The nonzero R+0x52fdc branch uses a parameter table, sets pass global
`0x14297a9dc` to 2, configures the transform through `0x140a5d8b0`,
and calls `0x140a5d950`. It then advances the transform index modulo four,
sets the pass global to 3, configures again and calls the draw routine again.
It restores the globals afterward and clears R+0x52fdc at the function end.
Parameter-table bounds and values are unreviewed. Neither that field nor the
two passes are labelled split-screen, reflection, or a user-visible camera.

The retained prefix `0x140a5d950..0x140a5da1f` uses further float fields
from the same indexed 0x308-stride storage, then visits all four fixed actor
records. It calls `0x140a5e060` only if actor+0x401c and actor+0xc0 are
nonzero; it also calls `0x140a548d0` for each record. Their bounded
continuations are retained below. The prefix is not the complete drawing
routine or proof of final visible
output. This repeated-pass structure means a per-draw callback cannot alone
be a once-per-frame observation or select the audible spatial perspective.

Next trace the writers of each actor callback and the HUD owner's selected
actor. Extend the established input branches to actual controller/mode labels.
Trace health, visible transformation/
stone resources, position axes and camera projection through actual writers
and render consumers. Do not reuse CVS2's midpoint camera or assume a world
axis is height. Prepare small labelled captures once those candidate paths
are bounded; no full-arena sweep or user input is needed for this first trace.

The audit reproduces thirty-nine spans. Five offline tests cover manifest agreement,
changed code/table/projection-constant rejection and incomplete instruction
refusal. These checks do
not prove live gameplay semantics. No runtime source changed, so the DLL
build/regression set was not repeated for this research increment.

## Transform mechanism continuation

The matrix-slot selector `0x140f326e0..0x140f32724` accepts unsigned
indices below four, saves the previous index at context+0x1f80a8, writes
the current index at +0x1f80a4, and publishes
context+0x1f80ac+64*index through `0x142c79b48`. The context itself
comes from `0x142c79b40`. These are matrix slots, not player identities.
The draw path uses slots 1, 2 and 3 at different stages; a matrix slot number
must not be confused with the independent transform-record index or pass ID.

`0x140f31c60..0x140f31d04` transforms a three-float input through the
matrix currently referenced by `0x142c79b48`. With M as sixteen consecutive
floats and input `(x,y,z)`, its outputs are:

```text
x' = x*M[0] + y*M[4] + z*M[8]  + M[12]
y' = x*M[1] + y*M[5] + z*M[9]  + M[13]
z' = x*M[2] + y*M[6] + z*M[10] + M[14]
```

There is no perspective divide, clipping or viewport mapping in this body.
It is an affine transform, not by itself a screen-position decoder. Float
operation ordering remains defined by the retained instructions; the formula
summarizes the math and is not a bit-exact emulation implementation.

The resource-extent prefix `0x140a5e060..0x140a5e2b2` loads actor+0x78
into matrix slot 1, calls `0x140f32190` with a null argument (its full
matrix modification remains unreviewed), then visits 19 subordinate resource
pointers from actor+0xe0. For each resource it passes resource+0x34 to
`0x1409bad20`, then transforms the resulting vector with the affine helper.
It accumulates extrema of the first and third components. This does not
establish their screen or height meanings.

The complete helper `0x1409bad20..0x1409bad45` temporarily pushes/loads
the supplied 64-byte matrix through `0x140f331c0`, extracts the current
matrix's +0x30/+0x34/+0x38 translation through `0x140f31f80`, then pops
one matrix through `0x140f33130`. Push can fail when no stack capacity
remains; this caller does not test that return before extracting/popping.
The native normal-capacity path is established, not a universally safe helper
for a mod to invoke. Read/copy evidence rather than calling native rendering
helpers from the worker.

The other actor draw consumer `0x140a548d0..0x140a54a0d` reads the
three-float vector at actor+0x3c, adjusts its second component on one path,
reads another vector at +0x4134, and passes both plus an index-selected value
to `0x140f380a0`. Numerous phase, inclusion, resource and pointer guards
precede that call. The primitive projection is traced below; its visual meaning
is unverified;
neither consumer proves HUD ownership or a health gauge. Numeric offset matches
in unrelated record types were excluded from evidence.

The common spatial contract must capture the actor, transform record, matrix
role and eligible pass together. A worker read of the mutable current-matrix
pointer can observe a temporary resource matrix or a different stage. The
projection continuation below establishes composition and primitive rejection;
selection of the eligible pass and HUD ownership remain acquisition tasks.
No runtime hook or spatial cue is enabled here.


## Two-point perspective and viewport continuation

The wrapper `0x140f380a0..0x140f380b6` passes opcode 8 to
`0x140f37ca0..0x140f37f34`. This primitive transforms each of the two
actor-linked vectors, divides XYZ by positive W, performs whole-primitive
rejection tests, and submits a record to `0x140f2f390`. Submission and final
rasterization are not traced here. The wrapper returns zero regardless of the
primitive's result; it cannot serve as a visibility observation.

The complete homogeneous helper `0x140f31d10..0x140f31deb` uses the same
XYZ equations as the affine helper and adds
`w' = x*M[3] + y*M[7] + z*M[11] + M[15]`. It writes four floats;
the caller performs the divide. With finite values, the primitive rejects if
either W is nonpositive, or if both endpoints are beyond the same bound:
Z below 0 or above 1, X below 0 or at least 640, Y below 0 or at least 480.
This is trivial rejection, not a complete segment-clipping implementation.
NaN handling in the native comparisons is not a finite-value validation
contract; a future accessibility decoder must validate its own inputs.

`0x140f33830..0x140f3388b` temporarily preserves the current matrix,
postmultiplies it by slot 2, slot 3 and the matrix at `0x141c00210`,
then copies the result into context+0x1f8200 and restores the current matrix.
The complete multiplication body `0x140f32c20..0x140f3310f` computes
`out[row,col] = sum(current[row,k] * argument[k,col])` in row-major
storage. Thus a row-vector point follows `point * current * slot2 * slot3 *
viewport` on the actor path with a distinct current slot. Do not generalize
this formula to aliasing calls that modify one of the later source slots.
The primitive temporarily loads the composite separately for each endpoint.
The stack-capacity caveat above applies to these helpers too.

The viewport matrix is mutable. The retained writer prefix
`0x140f30830..0x140f30a1c` selects dimensions by mode, stores width and
height at context+0x1f82a4/+0x1f82a8, and writes width/2 to matrix
entries 0 and 12, -height/2 to entry 5, and height/2 to entry 13.
The dump snapshot has diagonal `(320,-240,1,1)` and translation
`(320,240,0)`. That snapshot is not a live invariant. The primitive's
640-by-480 rejection constants are independently pinned by the audit;
they are native drawing coordinates, not a Windows client-size observation.
The composition helper does not consume the caller's apparent width/height
XMM arguments, so those loads alone do not establish its dimensions.

This closes the math path for this specific two-point primitive. It does not
certify a fighter-body anchor, a health gauge, HUD ownership, the audible
camera pass, or a worker-safe sampling point. A useful capture must retain
actor identity, input/phase eligibility, pass and transform-record indices,
all required matrices, and the resulting point together at the draw stage.
No runtime projection decoder has been enabled.


## Matrix setup and excluded camera shortcuts

Six more complete helper spans bound the setup calls. The draw prefix calls
`0x140f364e0`, which only writes 1 to context+0x1f8518. Despite being
called with matrix slot 2 selected, it does not modify a matrix. Likewise,
`0x140f36390` writes the three coordinate differences into
context+0x1f850c/+0x1f8510/+0x1f8514 and tail-calls `0x140f33f90`.
That helper squares and sums the vector components, calls `0x140fd661c`,
then divides the components by the result, with an explicit zero-result
branch. The math-library callee has not been retained/reviewed here.
Neither helper writes through the current-matrix pointer. These differences
cannot be labelled a view-matrix constructor based on the nearby slot switch.

The alternate-pass setup selects slot 3, calls `0x140f33610` to copy the
64-byte template at `0x141c00250` into the current matrix, then calls
`0x140f334d0`. The latter copies that template to a local matrix, replaces
its translation entries with three arguments, and passes it to
`0x140f32730`. The next call, `0x140f338f0`, constructs a projective
matrix from the low 16 bits of ECX, three float arguments, context+0x1f8240
and +0x1f8244, and two math-library calls. It also passes its local matrix
to `0x140f32730` and clears context+0x1f8518. The template is mutable
data; its role must not be inferred solely from its dump snapshot.

The constructor's matrix-combination helper `0x140f32730`, math-library
callees, template writers and ordinary-pass slot-3 initialization remain the
next bounded setup trace. No projection parameters, angle units or camera
role are certified from names or conventional matrix patterns. The earlier
composite/endpoint arithmetic remains independently established.

## Constructor multiplication order continuation

Continued offline from clean `622c0be`, 2026-10-01. The complete constructor
combination helper `0x140f32730..0x140f32c17` reads the argument matrix
through RCX and the current matrix through `0x142c79b48`. In row-major
storage it computes `out[row,col] = sum(argument[row,k] * current[k,col])`.
This is premultiplication, distinct from the postmultiplication helper
`0x140f32c20` used by the composite path. For example, the first output
uses argument offsets 0/4/8/12 and current offsets 0/16/32/48. The next
three argument rows begin at +0x10, +0x20 and +0x30. All source matrix
loads precede the first output store at `0x140f32b90`; four 16-byte stores
replace the current matrix, ending at `0x140f32c09`. The body has no calls,
branches, perspective divide or current-slot selection. It assumes readable
matrix pointers and does not provide a worker-safe invocation contract.

Let B be the mutable template at `0x141c00250`, T the local template copy
whose translation entries the translation constructor replaces, and P the
local projective matrix built by `0x140f338f0`. On the retained alternate
setup path, slot 3 receives B, then T * B, then P * (T * B). These are
native sequential floating-point operations; this notation does not authorize
reassociation for bit-exact decoding. The setup subsequently selects slot 2
and copies the indexed transform record there. The composite helper still
postmultiplies the actor's current matrix by slot 2, slot 3 and viewport.
Thus the constructor order changes the interpretation of slot 3 without
changing the independently traced composite order. No assumption that B is
identity is needed, and no camera role or angle unit follows from this order.

The audit now retains 39 instruction-aligned spans under the existing code
hash. Validation: audit/manifest regeneration and all five PWS2 audit tests
passed; `git diff --check` passed. No runtime source changed; no DLL build,
deployment, game attachment, live capture or audible check was performed.

Next bounded work: retain/review the projective constructor's two math callees
`0x140efdc30` / `0x140efe130`, trace writers of the mutable template and the
ordinary-pass slot-3 setup. Actor callback writers and HUD ownership remain
independent acquisition work. Sampling eligibility and the audible perspective
are still unproved; this continuation does not enable a projection reader.
