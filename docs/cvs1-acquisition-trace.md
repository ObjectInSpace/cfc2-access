# CVS1 acquisition: nested restore dispatch and positioned prompts

Continued from clean `ed547da`, 2026-09-27. Offline instruction/data evidence
for CVS1, not an implemented adapter or a live-validated screen map. Both ROM
routes 0/1 reach the shared CVS1 initializer; neither was captured in this
increment. Preserve the language/mode qualification until observed.

Reproduce with:

```powershell
python tools/cvs1_acquisition_audit.py
python tools/test_cvs1_acquisition_audit.py
```

The audit produces `build/cvs1-acquisition-audit.json`: 19 bounded instruction
spans, seven individually checked pointer-table edges, five checked prompt
literals, and the guarded ten-entry mode jump table. The compact
[manifest](cvs1-acquisition-manifest.json) retains bytes for the data records
and hashes for all spans/sections. The shared Dump loader refuses code other
than SHA-256 `9c340b8ce40a0c15a3eccd3c47207b02a59e38365bf99ae7605afc26bb84f95b`.
These spans are separate from the 57-span collection lifecycle manifest, so
acquisition research does not silently expand the metadata tool's live reads.

## F12/F19: the restore edge is nested

As established in the [prior trace](collection-lifecycle-continuation.md),
R = `*0x1422D2B90`, P = `*0x1422D2BA0` = R+0x3e58, and
B = `*0x1422D2B98` = R+0x32ab0. All are native host pointers.

One instruction-derived route is:

1. `0x1405AE5F0` dispatches signed byte P+0 through `0x141217568` when its
   P+0x1a gate permits. Entry 2 calls `0x1405C5770`.
2. Subject to `0x140036650(1)` returning zero, `0x1405C5770` dispatches
   signed byte P+1 through `0x141222420`. Entry 1 reaches `0x1405C5D30`.
3. `0x1405c5d37` loads signed byte P+2; `0x1405c5d3c` loads the distinct
   table `0x141222430`; `0x1405c5d43` tail-dispatches through it.
4. Entry 0 of that nested table is `0x1405C5D50`. After preparing state,
   it tail-jumps to the primary/secondary copy routine at `0x1405c5e46`.

The nested table starts only 16 bytes after its parent's base. Merely reading
successive qwords from `0x141222420` would label the restore routine “parent
entry 2.” That arithmetic is true about storage, but it omits the demonstrated
P+1=1 / P+2=0 route. The audit therefore records explicit selector/edge pairs
and does not infer valid selector ranges from adjacent code pointers. Other
routes or out-of-range states are not ruled out by this bounded trace.

On the restore branch, `0x1405a68f5` copies B+8 into R, length 0x32ab0.
The following `0x1405ADEE0` is now traced through its return:

- It publishes R+0x5358 at R+0x30918 and R+0x306c8 at R+0x30920.
- It clears 0x12c records of size/stride 0x250 starting at R+0x5358.
  The loop back-edge is `0x1405adf92`; records other than the last get a
  next-record pointer at offset +8 via `0x1405adf77`.
- It clears qword arrays and word counters at R+0x30928 onward, and stores
  word 0x12c at R+0x309dc at `0x1405ae153`.

This is a concrete pool rebuild after state copy, with the same R throughout.
Its objects' gameplay/render meaning is still unresolved. Neither the copy
completion nor this pool reset alone is a certified coherent-frame hook.
The exact shell action invoking this state sequence remains open.

## Next selection leads: two mode-dependent dispatches

Nested entry 1 at `0x141222438` reaches `0x1405C5E50`. It sign-extends P+0x48,
checks the resulting unsigned value against 9, and indexes ten image-relative
dwords at `0x1405C5E80`. The checked data routes:

- Values 0, 2, 6, 8, 9 to `0x1405D8B10` through `0x1405c5e74`.
- Values 1, 4, 5, 7 to `0x140602440` through `0x1405c5e79`.
- Value 3 to return at `0x1405c5e7e`. Negative sign-extended bytes and values
  above 9 also take the unsigned guard's return path.

Both targets dispatch on signed byte P+3, but use different tables:
`0x141237D80` at `0x1405d8b1c`, and `0x141240D98` at `0x140602502`.
Do not copy a P+3 meaning between those paths. The field's visible mode name
and F02–F05 screen meanings are not established yet.

The first branch has concrete next leads `0x1405D8C90`, `0x1405DBB60`,
`0x1405DB830`, and `0x1405DB3A0`; the second has `0x140602530`,
`0x140602B10`, `0x140602B70`, and `0x140602CB0`. These were inspected only
as exploratory excerpts, not fully traced or included as acquisition contracts.
Native character IDs, highlighted choice writers and name/sprite consumers
remain unresolved. No F02 reader can be enabled from these dispatches alone.

## F17/F18 acquisition: formatted record to glyph renderer

The native update calls phase work at `0x1405a6640`, then later calls
`0x1405AF730` at `0x1405a664a`. These produce and consume one positioned
text-record queue. This is a native draw path, independent of archive-string
search results.

Producer `0x1405AF6B0`:

- Rejects work when byte R+0x529e is nonzero.
- Loads signed dword count at `0x1422D0624`, rejecting exactly 0x80.
  This equality check relies on the game's own count invariant; a future
  external reader must reject negative and greater-than-128 counts as well.
- Computes record = `0x1422D0630 + count*0x40`.
- Stores CL at record+0, DL at +1, and R8B at +2. These are **three bytes**,
  not the CVS2 positioned-record layout. The renderer supplies evidence that
  the first two feed position and the third selects a style path.
- Formats from R9 into record+3 using `0x1405AF8A0`, passing a pointer to the
  stack arguments. At `0x1405af70a` it increments the count, then at
  `0x1405af713` writes a terminator at record+3+sign-extended formatter result.
  A record offers at most 61 bytes from +3 to the end, including terminator.

The format wrapper passes a size of -1 to `0x140EF3CB4`; this trace does not
prove formatting is bounded by the record. Future capture must enforce its
own 61-byte terminated-text limit rather than trust the native formatter or
copy arbitrary lengths. No patch to game formatting is proposed here.

Reset `0x1405AF7C0` clears counts `0x1422D0620` (callbacks) and
`0x1422D0624` (records), gated by R+0x529e. The phase routine skips that reset
when P+0x1a is nonzero. A record remaining in memory therefore does not by
itself prove fresh production during the current update.

Drain `0x1405AF730` first executes queued callbacks, then loads the record
count/base at `0x1405af76c` / `0x1405af772`. Its loop passes each record in
RCX to `0x1405C8DB0` at `0x1405af783` and advances by 0x40.
This pins the renderer's record argument; complete original-call forwarding,
detour relocation, thread/cadence and visible-frame ownership remain separate
requirements before a hook is implemented.

Renderer `0x1405C8DB0..0x1405C9071` has two paths:

- Style byte 7: passes position bytes to `0x140F30C20`, then record+3 in RCX
  to `0x140F31050`. The deeper path is now traced below.
- Other styles: selects a 16-byte descriptor, computes screen vertices from
  the first two bytes, then walks text bytes until zero. Each glyph reaches
  the draw call `0x140F375D0` at `0x1405c8fdd`.
  At `0x1405c8f40`, `^` consumes the next byte: `^Z` becomes byte 0x5c,
  otherwise the next byte is increased by 0x3c (truncated to a byte). The
  resulting byte is offset by 0x20 and split into glyph-grid coordinates.

Do not speak raw caret escapes or assume this is a CP932 renderer. The glyph
atlas and special glyph-to-spoken-text mapping remain unverified. Renderer
entry is a useful candidate for capture of actually consumed records, but
still does not prove final composited visibility, overlay ownership or live
execution on every prompt.

### Style-7 stopping rule

`0x140F31050..0x140F311D0` saves the string argument from RCX in RBX
and reads the common context through `0x142C79B40`. At `0x140f31151`
it sign-extends the first byte, tests it, and exits on signed less-than-or-equal
to zero. The loop repeats that signed test at `0x140f311ae..0x140f311c2`.
Thus zero and every byte 0x80–0xff stop this path; only bytes 0x01–0x7f
reach its glyph draw `0x140F36A70` at `0x140f31194`. The glyph index is
multiplied by eight. This leaf does not implement the ordinary path's caret
escape. A future capture must retain style and raw bytes: neither CP932
decoding nor one shared caret decoder represents both native renderers.

## Branch-B choice writer and coordinate consumer

This is offline evidence for one branch, not a character-name map or a live
selection reader. Branch A remains separate. The explicit P+3 table at
`0x141240D98` has index 0 → `0x140602530` and index 2 → `0x140602B70`.
The audit records individual entries, not an inferred selector range.

`0x140602B70..0x140602CA3` dispatches actors at R+0x3fb8 and R+0x46b8
(stride 0x700). Each dispatch is gated by P+0x65 AND actor+0x58c. The
zero-extended actor+4 indexes `0x141240DC0`; entry 6 is `0x140606290`.
Actor ownership is read from signed actor+0x688. Initialization is traced
below; association with a physical controller remains unproved.

The choice-update excerpt `0x140606290..0x14060690A` reads:

- Owner from actor+0x688 at `0x1406062ba`.
- Current slot from P+0xa0+owner at `0x1406062e6`.
- Selected native byte from P+0x78+4*owner+slot at `0x140606310`.

After input/cooldown handling, `0x1406063e5` forms direction+8*selected.
Modes P+0x48=4 or 7 add four, then `0x1406063f9` reads a signed byte
from `0x141240C40` plus that index. This is a navigation lookup, not a
name table. Mode-dependent substitutions, duplicate checks against up to
four slots, availability-mask tests and helper calls can reject or replace
a candidate. Their complete gameplay meanings and the table's legal ID
domain remain unresolved. Do not implement this as an unconditional lookup.

At `0x14060657c` the owner is reloaded, and `0x14060658f` writes BL to
P+0x78+4*owner+saved_slot. Actor+5 is set to 6 earlier at `0x140606317`.
The special 0x23 branch has additional cycling behavior; no character or
"Random" label is assigned to it here.

Initializer `0x140602530..0x140602AE8`, after its entry gate, loads
callback `0x140602EC0` into R12 at `0x14060299b`. Its descriptor loop
conditionally obtains an object from `0x1405ADA90`, checks for null, and
stores R12 into object+0x18 at `0x140602a12`. It stores the descriptor's
subtype in object+0x34 and loop index in object+0x37. This establishes
callback publication. The list insertion, scheduler and route to the shared
draw wrapper are now traced below; final composited visibility remains open.

The loop covers 215 four-byte descriptors at `0x1412403E0`, using fields
resource byte, subtype, position index and mode mask. Four individually
pinned subtype-2 rows are indices 211–214 at `0x14124072C..0x14124073C`:
`2c020007`, `2e020007`, `2d020007`, `2f020007`. Their mask is 7; creation
still depends on the initializer's selected mode mask and allocation success.
Their published object+0x37 indices yield handler owner values 0,1,0,1.
Resource bytes are not character IDs or verified character names.

That callback indexes `0x141240E10` with unsigned object+0x34. Entry 2
is `0x140603220..0x140603468`. Its initial owner is
(object+0x37−1) AND 1, saved at object+0x36. It clears object+0x150,
requires P+3 <= 2 and the corresponding actor+5=6, then sets object+0x150
to 1. With actor cooldown word+0x30 zero, it reads signed actor+0x688,
the current slot, and the same selected byte at `0x140603304`. With
nonzero cooldown it instead uses actor+1.

The selected byte indexes eight-byte coordinate records at `0x141240B20`;
`0x140603313..0x140603321` copies the two components into object+0x3c
and object+0x40. Later mode/ID branches adjust them. This connects the
choice writer to a selection-coordinate consumer, but does not prove a
character name, portrait, final visibility, or owner/controller identity.

The next bounded checks are physical-controller attribution, final visibility,
and a name/resource consumer for this native ID. A live selection capture also needs phase,
mode, owner, slot, cooldown and special-ID context; raw P+0x78 alone is
insufficient. No live reader or new hook is installed by this increment.

## Actor ownership, input routing and object scheduling

Two concrete owner-initialization paths are now pinned:

- `0x1405C4070..0x1405C41F4` zeroes two 0x700-byte actors, then writes
  loop index 0 or 1 to R+0x4640+0x700*index at `0x1405c40de`.
  It also writes actor+0x58c masks 1 and 2 at `0x1405c4135` and
  `0x1405c4146`, and resets phase bytes into the P+0=2 route. Its external
  entry event and CL argument meaning remain unassigned.
- The already-established P+0=2, P+1=0 table entry leads to
  `0x1405C5890..0x1405C5D22`. Subject to its entry gate, it increments
  P+1 and runs a two-actor loop. ESI is zeroed at `0x1405c5bfd`;
  EBX/EDI start at zero at `0x1405c5c9f`. At `0x1405c5ccb` it writes
  loop index BL to actor+0x688, advances the actor offset by 0x700, and
  stops at index 2. This is an initialization path, not proof that no later
  writer or restore changes ownership.

`0x1406079E0..0x140607A72` takes a side index in ECX. It reads a word
from R+0x3c76+0x3c*side, saves the previous word from
`0x1422D3E60+2*side` into `0x1422D3E64+2*side`, and stores the current
word back into the former. The newly pressed word at
`0x1422D3E68+2*side` is current AND NOT previous. The repeat word at
`0x1422D3E6C+2*side` starts at zero. When input is unchanged, a byte
counter is incremented; after the signed comparisons against 0x1e and
0x23 pass, it resets to 0x1e and publishes the current word as repeat.
Changed input resets the counter. This proves bit-word mechanics, not
button names or elapsed repeat timing.

Branch B at `0x14060245e` reads byte `0x1422D2B85`. When its signed
value is <=0, it updates sides 0 and 1 independently. With positive g,
it updates side g−1, then copies four words (current, previous, newly
pressed and repeat) from side g−1 to side 2−g. No upper bound for g is
proved here. Values 1 and 2 therefore mirror one side's input to the other;
the flag's mode/controller meaning needs its writer and live observation.
The choice writer's OR of newly pressed and repeat words cannot by itself
identify a physical controller.

The initializer calls `0x1405ADA90` with RCX=0, DL=4 and R8B=0.
`0x1405ADA90..0x1405ADB5F` returns null if the free count R+0x309dc is
zero; otherwise it removes a 0x250-byte record from the free list, clears
it, stores list number DL at object+3 and increments that list's count.
Original R8B indexes `0x141217548`; explicit entry 0 points to
`0x1405ADC60..0x1405ADCD7`. That helper receives the object in RDX and
list number in R8B. It publishes the empty list's head/tail or prepends
the object to the existing head, linking next through object+0x10 and
previous through object+8. Thus these successfully allocated objects enter
list 4, whose head is R+0x30928+4*8.

After the branch's P+3 handler returns, `0x140602509` sets CL=4 and
`0x14060250b` calls `0x1405AE460..0x1405AE495`. This scheduler loads
the selected list head, passes each object in RCX, calls object+0x18 at
`0x1405ae483`, and follows object+0x10. This closes the offline route
from descriptor creation through callback dispatch to the coordinate
consumer; it is not a claim that every descriptor is visible each frame.

The branch also enqueues callback `0x1405DDCD0` at `0x140602517` through
`0x1405AF7E0..0x1405AF810`. The enqueue gate is R+0x529e and the count
limit check is equality with 16. It writes the callback into the array
at `0x1422D2A30`, drained by the already-traced `0x1405AF730` before
text records. Exploratory follow-up shows that callback uses the specialized
list renderer `0x1405E1980` with CL=4. Its type-10 route is now traced below;
deeper resource interpretation remains the next draw investigation. Do not
substitute the nearby generic list renderer `0x1405AE4A0`.

The pinned resource helper `0x1405C3170..0x1405C318E` uses base
B=*(R+0x52b0), computes T=B+signed_dword(B), then returns
B+unsigned_dword(T+4*ECX). Descriptor resource bytes feed this lookup.
This offset-based record is not established as a text string or character
name. Tracing the resource publication, record consumer and selected native
ID remains necessary before assigning speech labels.

## Input-copy flag writer and render-type-10 route

`0x1405FEC40..0x1405FED2E`, called by the pinned phase initializer at
`0x1405c5be4`, first clears four bytes starting at `0x1422D2B82` at
`0x1405fec5b`. This covers input-copy flag `0x1422D2B85`. An exact-address
cross-reference lookup misses this reset because its target is three bytes
earlier. The audit now extracts RIP-relative write footprints overlapping
the flag within this function, including both this dword reset and the
one-byte write described next. This is a bounded writer inventory, not proof
that aliases or other routines never write the flag.

When P+0x48 equals 4, the routine sets P+0xb7 to 1 and copies P+0x18
to the flag at `0x1405fecf9`; otherwise it returns with the flag zero.
The mode-4 path also calls `0x1405C0330` for both actors. The earlier input
copy rule thus has a concrete mode-dependent source. P+0x18's complete
allowed values and controller meaning still need proof: do not silently
clamp the flag or assign a mode name from this numeric comparison.

The render callback `0x1405DDCD0..0x1405DDDAF` calls the specialized
list renderer with CL=4 at `0x1405ddd6d`. The renderer
`0x1405E1980..0x1405E1EAD` starts from R+0x30928+8*list, skips objects
whose word+0x150 is zero, and dispatches on dword(object+0x9c) AND 0xf.
An unsigned <=11 guard bounds its twelve-entry RVA table at
`0x1405E1EB0`. The audit checks the exact ordered targets, so swapping two
otherwise valid branches is rejected.

The selection initializer sets object+0x9c to 0x0a at `0x140602a46`.
The subtype-2 coordinate handler does not overwrite that field in its
reviewed span. Type 10 reaches `0x1405e1de2`, which passes the object in
RCX to `0x1405DF8E0` at `0x1405e1de5`. This ties the traced coordinates
and resource pointer to a concrete render route, subject to object lifetime,
the active word and any other writers.

`0x1405DF8E0..0x1405DFB62` applies view/transform state according to
object+0x9c and object+0x98. In particular, bit 0 of object+0x98 passes
object+0x3c to `0x140F33520` at `0x1405df9b2`, connecting the coordinates
written by the subtype-2 handler. Bit 4 passes object+0x58 to
`0x140F33450` for scaling. The coordinate handler sets these flags to
0x11. This identifies transform inputs without asserting their final
screen-space values.

At `0x1405dfb2a` the routine loads object+0x90 into RCX. Bit 0x20 of
object+0x98 selects `0x140F36220` with another value loaded into XMM1;
otherwise `0x1405dfb3f` calls `0x140F36200`. The latter bounded wrapper
sets R9D=2, clears EDX, loads XMM2 from a constant and tail-jumps to
`0x140F34CF0`. It forwards the resource pointer. The shared resource
command-submission routine is traced below; downstream interpretation and
final compositing remain open. This is a draw invocation path, not proof of
visibility or a certified detour ABI.

Next resource work: trace publication of R+0x52b0 for this selection route,
decode the record consumed by `0x140F34CF0`, and relate the native selected
ID to a name or character graphic rather than merely the selection marker.
No glyph/texture content is assigned a character label in this evidence.

### Resource publication and command submission

For the initializer's calls to `0x1405ABD30`, selected CL values 0x29,
0x2a and 0x35 go through a two-stage dispatch. The prefix sign-extends CL,
checks unsigned <=0x3c, reads a byte at `0x1405AC588+selector`, then a
DWORD RVA at `0x1405AC540+4*index`. All three individually pinned entries
have index 12 and target `0x1405AC282`. This does not make all initializer
branches reachable in every mode; calls to other loaders remain separate.

That branch passes ECX=0x400 and EDX=selector to `0x1405AABA0`.
The helper reads four DWORD offsets from `0x141217070+16*selector`:
0xa2d000, 0xa46800, 0x900000, 0x9e0000 for these three selected rows.
It adds them to the buffer at +8 of the common context reached through
`0x142C79B40`. After processing/copying resources through native helpers,
`0x1405aac5d` publishes the second address (buffer+0xa46800) at R+0x52b0.
The source buffer, size lookup and fixup-helper contents are not themselves
captured here. The subsequent loop uses the published resource count and
the existing offset lookup. A nonzero result from the call at `0x1405aaca9`
branches at `0x1405aacb0` to a self-loop at `0x1405aace0`. No recovery
behavior is inferred from that path.

`0x140F34CF0..0x140F34E34` accepts the resource pointer in RCX and starts
at resource+0x18. It processes records while their leading signed DWORD is
negative. Each record advances by 0x50 plus its signed DWORD at +0x4c.
Depending on context+0x1f855c, it either passes the record pointer onward or
copies the record into a context staging buffer, checking the accumulated
size against 0xe0000. At `0x140f34ded` it calls `0x140F2F390` with the
wrapper's R9D value in ECX, a stack descriptor in RDX and R8D=0x130.
This proves structured graphics-command submission, not text decoding or
the final interpretation of each payload. No externally safe resource
length or complete capture ABI follows from the native record walk.

Next: inspect resource/fixup data and the submitted command consumer; seek
the selected native ID's character resource separately from the coordinate
marker. The raw data may require a targeted live resource capture if the
reviewed dump lacks the populated game buffer.

## A concrete blinking-prompt producer

One route is P+0=1 → `0x1405C4200`, P+1=6 → `0x1405C5590`, which calls
`0x1405C4560` at `0x1405c55aa`. That function decrements R+0x32aa9 and,
on zero, toggles R+0x32aa8 and reloads a duration of 20 or 15. The actual
elapsed time depends on update cadence, which is not measured here.

Its branches select the five manifest-pinned literal strings:

- R+0x3c50=1 and blink byte=1: `FREE PLAY`.
- Otherwise R+0x3c54=2 and blink byte=1: `PRESS START BUTTON`.
- R+0x3c54=1 and blink byte=1: `INSERT MORE COIN(S)`; it can additionally
  queue `1 CREDITS TO START` when R+0x3c51 is nonzero.
- Other R+0x3c54 values and blink byte=1: `INSERT COIN(S)`.

Those strings are selected by actual producer branches and reach the traced
queue/renderer. The fields' broader coin/mode semantics and every caller of
the producer remain open. F17 is partially code-traced; it is not captured,
implemented or live-validated. F18 can reuse the eventual record capture but
must include empty-frame replacement and capture freshness.

## Character-graphic refresh and dump availability

The reviewed image-section dump contains zero at both CVS1 root slot
`0x1422D2B90` and common-context slot `0x142C79B40`. The audit records
these snapshot values. It cannot follow either pointer to a populated
resource buffer. This is a limitation of this snapshot, not a finding that
the game never allocates these resources. Graphic-content inspection needs
a separately prepared live capture; the existing metadata tool does not
dereference roots or collect resource payloads.

The choice writer's earlier recorded span is an excerpt, not a complete
function. It continues past `0x14060690A` into slot-confirmation logic and
has branches beyond that end. Its audit label now states this explicitly;
no complete control-flow or hook ABI claim follows from the excerpt.

Within that excerpt, the non-0x23 path writes selected BL into actor+1 at
`0x14060671c`. The special 0x23 path has separate behavior and must not be
collapsed into this assignment. The two selected descriptor rows 60 and 61
are `3b0a0007` and `3c0a0007`: resource IDs 0x3b/0x3c, object subtype 10,
position index zero, mask 7. The object dispatch table's entry 10 points to
`0x1406048D0..0x140604C51` (the following bytes are a jump table, not code).

The handler dispatches initialization on resource−0x39 with an unsigned
<=5 guard. The checked six-entry table maps resources 0x3b/0x3c to the
branch setting object+7=1. It saves side=object+0x37 AND 1 and initializes
object+0x36 to 0xff. Thus these rows yield sides 0 and 1 respectively.
After phase/actor gates, its object+7=1 path compares actor+1 against the
saved byte. On change, it saves the new byte at `0x140604a66`, sets
R+0x30bfb=1 and ORs side+1 into R+0x30bfc. This is a change-triggered
graphic-refresh request, not a string producer.

The native update calls `0x1405AF690`, which calls `0x1405C8BE0` after
the phase update. With the refresh flag nonzero, request value 1 calls
`0x1405C8AF0(0)`; value 2 calls it with side 1. Value 3 refreshes side 0,
leaves flag=1/request=2 and returns, scheduling the other side for a later
invocation. The other reviewed exit paths clear the flag/request. Request
4 follows a different asset path and is not given the same interpretation.
This staging is another reason one memory sample cannot certify both
sides' final images.

`0x1405C8AF0..0x1405C8BAD` reads actor+1 for its side. Side 1 remaps
native values 9→0x23, 0x12→0x24 and 0x19→0x25; side 0 does not apply
those substitutions. It computes a source pointer as common-context buffer
(context+8) +0xab0000 +(mapped_value<<15). It supplies resource slot
0x400+side+mode_offset to `0x140F31BE0`. The checked mode table selects
offset 1 for modes 0,1,2,6,8,9; 2 for 3,4,5; and 3 for 7. The out-of-range
branch does not establish a safe default, so no external reader should invent
one. Numeric native values and remaps still have no verified character names.

`0x140F31BE0..0x140F31C44` adds context+0x1e0098 to the slot, requires
unsigned slot<0x1000 and a nonzero word in the corresponding 24-byte entry,
then calls `0x140049170`, forwarding the source pointer on the stack and
three words from that entry. This establishes the selected byte's concrete
asset-address consumer. The asset format, final upload and character-name
labels remain open.

Two supporting resource/queue spans are now pinned. Fixup helper
`0x1405AD9B0..0x1405ADA33` normalizes the resource header to 0x10,
subtracts the header delta from offsets, and adjusts a sentinel-terminated
array before calling common resource helpers. Its mutation means on-disk
and post-fixup offsets cannot automatically be interchanged.
`0x140F2F390..0x140F2FB39` is a command enqueue routine: the type-2 table
entry reaches `0x140f2f69b`; the eventual queue path checks a count below
0x800, records type/pointer/size metadata, copies the descriptor into the
context staging buffer and increments counts. Its entry also checks the
accumulated staging size against 0xe0000. It is not the final GPU consumer;
queue acceptance does not establish visible output.

Next measurement preparation should retain phase/mode, side, slot, selected
byte, actor+1, both pending-refresh bytes and resource-slot/source provenance
alongside any bounded raw asset capture. Include special 0x23 behavior and
side-1 remaps before defining a character-ID map. No live resource-capture
tool, detour or speech reader is introduced by this trace.

The subsequent [selection run card](cvs1-selection-run-card.md) now provides
a tested, read-only state/prefix capture tool. It uses conservative identity,
phase and per-side gates and retains raw observations. This prepares the
missing measurement; it does not change this trace's offline-only status.

## Validation and next measurement

Twelve audit tests passed, exit 0: manifest reproduction; nested selector
preservation; changed nested-pointer rejection; changed literal rejection;
actor/object selector separation; changed object-subtype rejection;
changed pool-insertion target rejection; overlapping flag-reset preservation;
render-branch swap rejection; resource-destination mutation rejection;
graphic-mode route mutation rejection; and
jump-table escape rejection. They use the pinned dump and synthetic
mutations, not gameplay fixtures. No runtime files changed or DLL deployed.

The existing metadata run card still answers identity/lifecycle questions;
it does not capture this text queue or prove renderer calls. Before a text
play session, prepare a separately tested bounded renderer capture with exact
ABI/signatures, session/overlay ownership and an empty-frame signal. Observe
one steady prompt, a complete blink-off interval, a coin/start-state change,
and game → shell. Retain raw 64-byte records and timing alongside the heard or
visually confirmed text; do not convert queue contents into a screen claim.

Independent next tracing: follow both P+3 tables to the actual choice writers
and name consumers, extending the branch-B evidence above; resolve glyph
substitutions and final visibility; link
the restore sequence to its concrete entry events before choosing epochs.
