# Power Stone 2: fixed records, filtered lists and input slots

Offline investigation, 2026-10-01. Reproduce with:

```powershell
python tools/pws2_actor_audit.py --manifest-out docs/pws2-actor-manifest.json
python tools/test_pws2_actor_audit.py
```

The manifest retains thirteen instruction-aligned spans and the main code SHA-256
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

Next trace the writers of each actor callback and the HUD owner's selected
actor. Extend the established input branches to actual controller/mode labels.
Trace health, visible transformation/
stone resources, position axes and camera projection through actual writers
and render consumers. Do not reuse CVS2's midpoint camera or assume a world
axis is height. Prepare small labelled captures once those candidate paths
are bounded; no full-arena sweep or user input is needed for this first trace.

The audit reproduces thirteen spans. Four offline tests cover manifest agreement,
changed code/table rejection and incomplete instruction refusal. These checks do
not prove live gameplay semantics. No runtime source changed, so the DLL
build/regression set was not repeated for this research increment.
