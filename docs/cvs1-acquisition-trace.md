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
  to `0x140F31050`. That deeper text path still needs tracing.
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

## Validation and next measurement

Five audit tests passed, exit 0: manifest reproduction; nested selector
preservation; changed nested-pointer rejection; changed literal rejection;
and jump-table escape rejection. They use the pinned dump and synthetic
mutations, not gameplay fixtures. No runtime files changed or DLL deployed.

The existing metadata run card still answers identity/lifecycle questions;
it does not capture this text queue or prove renderer calls. Before a text
play session, prepare a separately tested bounded renderer capture with exact
ABI/signatures, session/overlay ownership and an empty-frame signal. Observe
one steady prompt, a complete blink-off interval, a coin/start-state change,
and game → shell. Retain raw 64-byte records and timing alongside the heard or
visually confirmed text; do not convert queue contents into a screen claim.

Independent next tracing: follow both P+3 tables to the actual choice writers
and name consumers; resolve style-7 rendering and glyph substitutions; link
the restore sequence to its concrete entry events before choosing epochs.
