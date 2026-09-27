# Lifecycle continuation: destruction, in-place restore, CVS1 phase dispatch

Offline investigation from `9f10a12`, 2026-09-27. Uses the same code SHA-256
and dump sections as the [lifecycle trace](collection-lifecycle-trace.md).
The audit now contains 57 bounded instruction spans, including the twelve
added here. Bytes, resolved references and hashes are reproduced by
`python tools/collection_lifecycle_audit.py`; the checked-in manifest retains
their identities. No new runtime reader, game attachment or deployment.

## Arena release edge and the two singleton slots

`0x140048700` is a deleting-destructor-shaped wrapper: it preserves RCX in
RBX and EDX in EDI, calls `0x140048380` at `0x14004870f`, then conditionally
releases the object according to the low flag bits. This wrapper supplies a
confirmed caller for the body; which shell lifecycle events invoke the wrapper
still needs tracing. Do not equate it with ordinary return-to-menu cleanup.

The body `0x140048380..0x1400486b1` contains these instructions:

- `0x1400483a1`: `xor ebp,ebp`, establishing the zero used by later clears.
- `0x1400485e2`: loads allocator RCX from `0x14221EF88`.
- `0x1400485e9`: loads RDX from session slot `0x141C14F98`.
- `0x1400485f0`: loads the allocator vtable into RAX.
- `0x1400485f3`: `mov rdx,qword ptr [rdx]`, obtaining arena S+0.
- `0x1400485f6`: `call qword ptr [rax+0x60]`, the allocator release edge.
- Three subsequent +0x60 calls use object fields +0xded18/+0xded20/+0xded28,
  matching the constructor's auxiliary allocations.
- `0x140048635`: `mov qword ptr [rip+0x1bcc954],rbp` clears
  **0x141C14F90**, not session slot **0x141C14F98**.
- `0x1400486ac`: tail-jumps to `0x1402071F0` after other cleanup calls.

The inspected body contains no direct clear of the session slot, arena field,
published root fields or per-game root slots. Its calls and tail target have
not all been traced; this is not proof that none clears them indirectly. The
allocator's concrete +0x60 implementation also remains unresolved. The edge
and distinct cleared slot already invalidate any assumption that this body's
visible singleton clear makes S safe to dereference after destruction.

## CVS1 copies state back into an unchanged root

Let R be `*0x1422D2B90`, B be `*0x1422D2B98`, and P be `*0x1422D2BA0`.
Setup already establishes B = R+0x32ab0 with length 0x32ab8. The extra eight
bytes are used by the path below as control dwords before the saved payload.

At `0x1405A6840`, nonzero dwords S+0x7b4, S+0x828 or S+0x80c skip to return.
Otherwise it loads B into RDX and `0x32ab0` into R8D, then tests signed B[0]:

- B[0] <= 0: `0x1405a6884` sets RCX=B+8; `0x1405a6888` sets RDX=R;
  `0x1405a688f` calls `0x140EEC5A0`. It then sets B[0]=1, copies signed
  byte P+0x18 into dword B+4, and sets S+0x528=1. A conditional path also
  sets S+0x7cc=1. The flags' UI meanings remain unnamed.
- B[0] > 0: `0x1405a68ea` sets RCX=R; `0x1405a68f1` advances RDX to B+8;
  `0x1405a68f5` calls the same copy helper with R8D still 0x32ab0. It then
  calls `0x1405ADEE0` and takes additional branches on B[0].

The helper's entry preserves destination RCX in R11 and source RDX in R10.
For the large nonoverlapping blocks here, its dispatch can reach
`0x140EEC580`: RDI=RCX, RCX=R8, RSI=R10, followed by `rep movsb` at
`0x140eec58e`. The alternative vector prefix reads source via RCX+RDX after
subtracting the destination from RDX, and writes the destination. These are
instruction-derived copy directions, not a symbol-name guess.

The reverse direction overwrites R's contents without publishing a new root.
`0x1405C5D50..0x1405C5E4B` is a confirmed direct caller: it clears multiple
phase/state bytes then tail-jumps to `0x1405A6840` at `0x1405c5e46`.
This establishes a concrete in-place state replacement path. It does **not**
yet identify that path as a particular shell quick-load/reset command or
prove complete rollback handling. Those caller/flag semantics remain open.
Metadata-only captures can miss this replacement even with matching brackets.

## CVS1 phase ownership and dispatch

At `0x1405a6522`, setup loads R. `0x1405a6529` adds 0x3e58;
`0x1405a652f` publishes it to `0x1422D2BA0`. Thus P = R+0x3e58, a native
subregion of the published block, not a guest-address translation.

The complete bounded native update `0x1405A65F0..0x1405A665D` calls
`0x1405AE5F0` at `0x1405a6640`. The latter:

- Tests byte P+0x1a at `0x1405ae5fb`. Nonzero skips the main dispatch, but
  still reaches other work; do not label this byte “pause” without evidence.
- Loads P at `0x1405ae606`, sign-extends byte P+0 into RCX at
  `0x1405ae60d`, loads table `0x141217568` at `0x1405ae611`, and calls
  `[table+RCX*8]` at `0x1405ae618`.
- Still calls `0x1405C2160` after clearing P+0x1a at `0x1405ae65d`.

The audit's `cvs1_phase_table_leads` retains raw pointer bytes and addresses
for table entry 1 → `0x1405C4200` and entry 2 →
`0x1405C5770`. Both bodies load P and sign-extend P+1 for a subdispatch.
Entry 1 uses table `0x141222398` at `0x1405c4210`; entry 2 uses
`0x141222420` at `0x1405c5793`, gated by the result of `0x140036650(1)`.
The bodies are preserved as instruction spans. Table bounds, phase meanings,
selection ownership, native-ID/name consumers, and their visible semantics
are not established by these entries. No F02–F05 decoder is ready yet.

Next static work: trace the subdispatch tables through selection/name drawing;
trace callers/flags around `0x1405C5D50` and `0x1405ADEE0`; resolve destructor
vtable consumers and the allocator release target. Keep every untraced path
explicit before selecting a discontinuity hook or certifying an ABI.

## Capture preparation and validation

The new [run card](collection-metadata-run-card.md) covers CVS2 → CVS1 → shell,
re-entry, and four Alpha 3 identities. The tool retains raw brackets and
explicit failures, checks bounded code signatures, and refuses output overwrite.
It does not label retained roots active or infer gameplay semantics.

Validation commands and outcomes are recorded in the workflow handoff. Tests
use synthetic memory and the pinned dump, plus a Windows transport self-read;
none are game fixtures or audible validation. Shared runtime source is unchanged,
so the 28 DLL runners and DLL build are not required for this increment.
