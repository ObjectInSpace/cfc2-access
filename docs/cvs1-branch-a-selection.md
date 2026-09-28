# CVS1 branch-A selection writer and consumer

Offline continuation from `ac1ef14`, compared with the retained
[movement window](cvs1-live-movement-02.md). Uses the same pinned dump and
`cvs1_acquisition_audit.py`; no new live reads or input requests.

## Dispatch matching the live phase

The live phase was P[0..4]=2,1,1,6,6 and mode P+0x48=0. The existing
nested phase/mode trace reaches branch A at `0x1405D8B10`.
Its P+3 table `0x141237D80`, entry 6, selects `0x1405DBC30`.
That routine checks unsigned P+4<=9; the specific RVA-table entry 6 at
`0x1405DBFE8` selects `0x1405DBE21`.

At `0x1405dbe4d` it passes actor R+0x3fb8 in RCX and dispatches on
unsigned actor+4 through `0x141237DC0`. The second actor R+0x46b8 has
the same dispatch at `0x1405dbe71`. Entry 4 reaches `0x1405D9E70`,
matching the recorded side-0 actor state. These are individually pinned
table edges, not inferred table lengths or screen labels.

After the actor handlers, `0x1405dbfb0` runs list-4 object callbacks through
the already-traced scheduler. This order matters: an object can update the
choice after the actor has copied its prior value.

## Navigation fields and actor consumer

`0x1405D9E70..0x1405D9F30` reads signed owner from actor+0x688.
P+0xa2+owner below 0x30 takes an entry/confirmation gate; the rest of the
reviewed path begins at `0x1405d9edf`. It reads slot P+0xa0+owner and
loads the current native byte from P+0x78+4*owner+slot into EBP at
`0x1405d9f2a`.

The navigation excerpt `0x1405D9F30..0x1405DA306` examines actor words
+0x5be and +0x5d8. It uses P+0x10c+owner as a group index and
P+0x104+4*owner+group as a position. It increments/decrements positions,
wrapping against the corresponding word count at
R+0x327bc+2*(4*owner+group). Group movement wraps 0..3 and checks bits in
P+0x111. Owner-dependent 0x400/0x800 tests and 0x1000/0x2000 tests select
paths. The upstream input-word writers and exact physical-button meanings
are not established here; numeric masks are not assigned direction names.

The actor consumer at `0x1405DA306..0x1405DA3BC` skips the copy when
the loaded byte is 0xff. Otherwise it forms actor index
owner+2*actor[5], uses stride 0x700, and writes the native byte into
R+0x3fb9+0x700*index at `0x1405da327`. It also updates related actor
records at +0x4642 and +0x45d0+slot. The live actor[5] was zero, so the
first destination is the captured side-0 actor byte.

These are bounded excerpts of a larger handler ending at `0x1405DAF03`;
confirmation branches and the full function are not certified. In particular,
the write to P+0x78 at `0x1405da45f` belongs to later confirmation logic
and should not be mistaken for the only movement writer.

## The object that publishes the highlighted byte

`0x140610CF0..0x140611189` creates list-4 objects through the existing
pool helper and installs callback `0x14060B500` at object+0x18. Its first
side's publication at `0x140610e56` sets:

- object+0xc0 from the incoming catalog selector;
- object+0xc1 from the incoming group argument;
- object+0xc2 to side 0;
- object+0xc3 from the filtered position counter;
- object+0xc6 from the current two-byte catalog record's first byte.

The catalog pointer comes from `0x141237C78+8*selector`. Allocation and
availability checks affect which objects exist; this is not a flat array
of every playable character. The second-side publication uses the same
callback at `0x14061108c`. The setup's callers and catalog-to-name map
still need tracing; no catalog rows are assigned names in this increment.

The callback dispatches on unsigned object+4 through `0x141242468`.
Entry 1 reaches `0x14060A730`, which dispatches on unsigned object+5
through `0x141242450`. Its entry 1 reaches
`0x14060A800..0x14060B44E`.

At `0x14060b3e1` that handler reads side from object+0xc2 and group from
object+0xc1. It requires the group to equal P+0x10c+side, then requires
object+0xc3 to equal P+0x104+4*side+group. It obtains the active slot
from P+0xa0+side and writes object+0xc6 to
P+0x78+4*side+slot at `0x14060b431`.

This closes the choice publication and actor-copy relationship for the
reviewed code paths. It does not prove that these exact callbacks executed
on every live sample, or that no other writer changed the fields.

## What the live sequence now supports

The user-confirmed movement window recorded side-0 selected/actor values
1→0→16→0, with owner=0, slot=0, actor state=4 and actor[5]=0.
Those observations fit the traced publication and copy fields. They do not
identify characters or establish one sampled transition per key press.

The capture retains P+0..0xff only. Group+0x10c, position+0x104 and
availability+0x111 were not recorded; actor input words +0x5be/+0x5d8 were
also outside its small header read. Consequently these captures cannot
reconstruct the intervening navigation decisions. Do not extrapolate the
branch-B navigation table or asset gate into this branch.

Next: trace the catalog/setup caller and input-word publication, then extend
the state-only capture with these specific bounded fields and tests before
requesting another targeted session. Retain the existing asset gate until a
branch-A graphic/name consumer is established. No runtime reader, live
capture expansion or speech implementation is introduced here.
## Input publication continuation after 1ce9d1b

Same dump/code hash as the preceding evidence. Added audit spans
`0x1405aa1c0..0x1405aa5dd` and `0x1405aa5e0..0x1405aa702`, with exclusive
ends. These decode continuously from function entries; byte-scan candidates
were only leads. The acquisition manifest now checks 59 bounded spans.

Native update calls `0x1405aa1c0` at `0x1405a65f9`, before phase dispatch
at `0x1405a6640`. For two sides, stride 0x3c, it saves R+0x3c76 to +0x3c78,
calls `0x140048bb0` with side 0/1, and stores returned AX at +0x3c76.
Instructions `0x1405aa216..0x1405aa229` calculate
`(~previous) & current` into word R+0x3c7a. This is a newly pressed bit
mask at this native boundary; physical control assignments remain unproved.

The copies at `0x1405aa296..0x1405aa323` transfer the two 0x3c-byte input
records from R+0x3c76/+0x3cb2 to R+0x3d66/+0x3da2. The stores at
`0x1405aa376` and `0x1405aa410` then copy R+0x3d6a/+0x3da6 to
R+0x4576/+0x4c76, which are the two actors' +0x5be words. Previous actor
+0x5be values are retained at +0x5c0 before these stores.

The same function copies R+0x3d9e/+0x3dda into actor+0x5c6 at
`0x1405aa3bc` / `0x1405aa456`. Calls at `0x1405aa592` / `0x1405aa5a5`
pass the two actor addresses to `0x1405aa5e0`. Its store at `0x1405aa605`
copies actor+0x5c6 into actor+0x5d8 before its conditional direction-bit
transformation of other fields. Thus +0x5d8 is sourced from record+0x38;
the upstream writer and semantic meaning of that source remain open.
Do not call it a repeat mask without tracing its producer.

This pins immediate publication, not the absence of intervening writers:
native update calls other helpers before phase dispatch, and optional
`0x1405a9d80` runs inside input publication. Upstream device mapping and
those helpers remain relevant to full input provenance.

Schema 2 capture retains P through +0x111, sixteen bytes at R+0x327bc,
and 32 bytes per actor at +0x5ba. It decodes fixed-side group/positions,
counts, delta, enabled mask and offset-labelled input words. All added
fields participate in the before/after comparison. Old fixtures remain
schema 1 and cannot retroactively supply these bytes. No expanded live
capture has been performed. Sampling can miss short input pulses and
cannot prove frame ordering or eliminate ABA.

## Repeat producer continuation after 8b457f5

The next trace resolves the record+0x38 producer described above.
`0x1405a9c00..0x1405a9c31` is a complete leaf routine, now pinned in the
audit. RCX is the input-record address. It clears word +0x38 on entry,
compares current word +0 with previous word +2, and resets word counter
+0x3a to zero when they differ. If equal, it increments that 16-bit counter.
Unsigned counter values through 35 emit nothing; when the incremented value
exceeds 35 it writes the current mask to +0x38 and resets the counter to 30.
Thus a continually unchanged nonzero mask produces a repeat after 36 equal
comparisons, then every six equal comparisons. This is counted in calls,
not a measured wall-clock duration. A zero held mask still produces zero.

The already-pinned native update calls this helper at `0x1405a6619` and
`0x1405a662c`, passing R+0x3c76 and R+0x3cb2. Mode 5 skips these calls;
the observed mode 0 does not. Both calls occur after `0x1405aa1c0` copied
the records into actors. Therefore the normal copy path publishes the
previously generated repeat value to actor+0x5d8 before generating the next
record value. This establishes staged ordering within the update; it does
not prove that other paths cannot overwrite the state between updates.

The optional helper `0x1405a9d80` called within input publication starts
with mode=4 and phase 2,1,2,2 gates. Its pinned prefix ends at
`0x1405a9dbf`. The captured mode 0 fails its first gate and branches to the
epilogue at `0x1405aa1b7`; its body is not evidence of a mode-0 input rewrite.
Other intervening helpers and device-to-bit mapping remain open.

The capture keeps offset-labelled words; this evidence supports interpreting
+0x5d8 as a staged repeat-mask value on the traced path. It does not assign
directions or account for the three earlier transitions by itself. The
schema-1 movement fixture lacks these words, and polling may miss pulses.

## Catalog continuation after b60d3bf

Reviewed the pending catalog extraction against the same pinned dump. The
audit now retains 63 bounded spans, including setup
`0x1405d9900..0x1405d9a73` and availability
`0x14060a430..0x14060a4bf` (exclusive ends).

Setup passes signed P+0x76 as catalog selector for side 0, and P+0x77 for
side 1, to `0x140610cf0`. Calls supply group arguments 0 through 3. The
schema-2 movement fixture has side-0 selector P+0x76=0 throughout. Pointer
slot `0x141237c78` contains `0x141237be0`; the audit pins that pointer and
70 catalog bytes, comprising 35 two-byte records. No other selector or
pointer-table bound is inferred.

When P+0x4b is zero, the availability helper first requires the second
record byte masked with 3 to match the requested group. It also performs
additional state-dependent checks for IDs 13, 14, 30, 15 and 31 through
`0x1422d2ee0 + 0x20*side`. Consequently grouping raw catalog records alone
is not a general availability model.

The four raw group lengths are 10, 16, 6 and 3. In the retained movement
fixture, P+0x4b is zero and the side-0 counts match these lengths in every
before and after observation. Group 1's native IDs, in catalog order, are:

`0, 1, 2, 3, 6, 4, 32, 14, 30, 24, 21, 19, 18, 20, 17, 16`.

Every captured side-0 position selects the recorded native ID from that
group: position 0 gives ID 0, position 1 gives ID 1, and position 15 gives
ID 16. This checks the observed wrap against catalog order. It neither
names the characters nor establishes physical-button mapping. The user
subsequently clarified that four taps were made, consistent with the four
recorded transitions; the earlier two-tap premise was incorrect.
Other availability configurations and side 1 remain unvalidated.

The capture startup verifier now checks the catalog bytes and pointer.
Offline mutation tests reject either changing; fixture checks cover both
observations of all 240 movement samples. All 26 CVS1 acquisition/capture
tests passed. No new live capture or runtime speech change was made.
