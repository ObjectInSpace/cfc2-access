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
