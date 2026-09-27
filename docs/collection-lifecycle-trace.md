# Collection identity and bounded lifecycle trace

Investigation begun at `5eb3245`, continued 2026-09-27. This advances stage 1;
it does not complete the upfront campaign or implement an accessibility reader.
All new evidence here is offline. No game launch, process attachment, deployment,
new live capture or audible validation occurred.

## Reproduction and build identity

Run from the repository root:

```powershell
python tools/collection_port_audit.py
python tools/collection_lifecycle_audit.py
python tools/test_collection_lifecycle_audit.py
```

The first command reproduced eight dispatcher entries and nine initializers.
The audit now records 57 instruction-aligned bounded spans, all nine paths,
and all 18 ROM routes in `build/collection-lifecycle-audit.json`. Each instruction
has its address, bytes, disassembly and resolved RIP references. Each span has
an exclusive end and SHA-256. The checked-in
[manifest](collection-lifecycle-manifest.json) retains the reviewed relationships,
span hashes and section identities. Regenerate it deliberately with
`--manifest-out docs/collection-lifecycle-manifest.json` after reviewing changes.

The primary code section is 16,625,664 bytes, SHA-256
`9c340b8ce40a0c15a3eccd3c47207b02a59e38365bf99ae7605afc26bb84f95b`.
The audit refuses a different code hash before attaching reviewed labels.
Data-section hashes identify this dump capture, not every run of the executable.
The six tests include reproduction, altered-code rejection, missing-range
rejection, split-instruction rejection, missing-callback rejection, and a
duplicate-ROM-descriptor falsifier. All six passed with exit code 0.

The callback matcher is deliberately specific to this pinned build's local
publication sequences. It is not a general symbolic executor. Spans do not
constitute complete control-flow graphs or safe-hook certifications.

## ROM routing and the Alpha 3 discriminator

Let S be the qword loaded from `0x141C14F98`. At `0x140052470`, RCX is the
shell object. These instructions establish the base routing:

- `0x140052476`: `mov edx, dword ptr [rcx + 0x1ac]`.
- `0x14005247c`: `lea rbx, [rip + 0x11a3a8d]`, resolving to `0x1411F5F10`.
- `0x14005248a`: `mov dword ptr [rax + 8], edx`, where RAX is S.
- `0x14005248d`: `movsxd rdx, dword ptr [rcx + 0x1b0]`.
- `0x14005249b`: `mov dword ptr [rax + 0xc], edx`, where RAX is S.
- `0x1400524a5`: `movsxd rcx, dword ptr [r8 + 8]`, where R8 is S.
- `0x1400524a9`: `lea rax, [rdx + rcx*8]`.
- `0x1400524ad`: `mov rax, qword ptr [rbx + rax*8 + 8]`.
- `0x1400524b2`: `mov qword ptr [r8 + 0x30], rax`.
- `0x1400524f0`: call `0x1400B6250` with the name in RCX; `0x1400524fc`
  stores its EAX result in S+0x44.

Thus names come from `0x1411F5F10 + game*0x40 + 8 + region*8`.
The earlier `0x1411F5F18` description points to the first name field, not the
beginning of the 0x40-byte descriptor. The name lookup compares the first
record before incrementing its counter; indices are zero-based. Its no-match
path also returns zero, so zero alone cannot distinguish CVS1 from failure:
retain the existing exact-name cross-check.

`0x140049DA0` overrides Alpha 3 routing. Its exact conditions are
`cmp dword ptr [rcx+8],3` at `0x140049dab`, then
`cmp dword ptr [rcx+0x2b4],0` at `0x140049db1`. Only game 3 with a nonzero
discriminator reaches `0x140049dba`, which loads signed S+0xc and indexes the
two name pointers at `0x1411F5FE8`. They point to `z3uu` and `a3uu`.
It stores the selected name in S+0x30 at `0x140049dc9`, reruns the name lookup
at `0x140049dea`, and stores S+0x44 at `0x140049df6`.
The initializer dispatcher independently tests the same S+0x2b4 at
`0x14004c5df`: zero reaches `0x14086BC20`; nonzero reaches `0x140DCB360`.

One traced writer is `0x14003feb0`, loading S, followed by
`0x14003feb7: movzx ecx, byte ptr [rbx+0x12]` and
`0x14003febb: mov dword ptr [rax+0x2b4],ecx`.
This establishes a packed option byte widened to a dword, rather than a ROM
index test. Other settings paths also write the field. The visible option
label and all entry-path ordering still need tracing/capture. Do not infer
original hardware, guest CPU, or a guest RAM mapping from either branch.

The descriptor/name lookup and dispatcher together give this static map:

- 0 `cvs1`, 1 `cvs1u`: game 0, initializer `0x1405A5A40`.
- 2 `cvs2`, 3 `cvs2u`: game 1, initializer `0x1406A97D0`.
- 4 `cfj`, 5 `cfe`: game 2, initializer `0x1407CACA0`.
- 6 `z3u`, 7 `a3u`: game 3, discriminator zero, `0x14086BC20`.
- 8 `pws1`, 9 `pws1u`: game 4, initializer `0x140972E50`.
- 10 `pws2`, 11 `pws2u`: game 5, initializer `0x140A46190`.
- 12 `mjus`, 13 `pjus`: game 6, initializer `0x140B926F0`.
- 14 `stg2`, 15 `pswd`: game 7, initializer `0x140CCB1C0`.
- 16 `z3uu`, 17 `a3uu`: game 3, discriminator nonzero, `0x140DCB360`.

All 18 associations are code/data traced. This is not a claim that all 18 have
been launched or cross-checked live.

## State ownership and publication

At `0x140048283`, EDX is `0x10000000`; at `0x140048288`, R8D is `0x1000`.
The allocator call through vtable+0x40 at `0x140048291` returns the arena A,
stored to S+0 at `0x1400482a6`. Per-game initializers carve context/state
regions from A. These published native blocks are not established guest RAM.

Correction to the first audit's prose: CFJ uses context slot **0x142CF9D48**,
not `0x142C79B40`. At `0x1407cacc1`, `lea rax,[rbx+0x8000000]` is followed
by `0x1407cacc8: mov [rip+0x252f079],rax`, resolving to `0x142CF9D48`.
Its setup reloads that slot at `0x1407cb664`. The other eight paths use
`0x142C79B40`. CFJ therefore cannot inherit a universal context-slot constant.

Each setup reads context+0x10, publishes a game-specific native root slot,
clears its primary block through `0x140EECA00`, and publishes the root as a
qword at S+0x1b0 and its length as a dword at S+0x1b8. A second block begins
at root+primary-size and is published at S+0x1c0/+0x1c8. These fields are
little-endian host values. The secondary length is not always primary+8.

- CVS1: setup `0x1405A6440`, root slot `0x1422D2B90`, primary `0x32AB0`,
  secondary `0x32AB8`. Root publication is `0x1405a6479`; primary size store
  `0x1405a6487` is `c780b8010000b02a0300`.
- CVS2 control: setup `0x1406AA320`, root slot `0x1422DEBD0`, primary
  `0x216B0`, secondary `0x216B8`. Publication `0x1406aa35b`; size store
  `0x1406aa369` is `c780b8010000b0160200`.
- CFJ: setup `0x1407CB660`, root slot `0x1422F3A80`, primary `0x26E80`,
  secondary `0x26E88`. Publication `0x1407cb699`; size store
  `0x1407cb6a7` is `c780b8010000806e0200`.
- Ordinary Alpha 3: setup `0x14086CFC0`, root slot `0x1422FB960`, primary
  `0x12F98`, secondary `0x12FA0`. Publication `0x14086cff9`; size store
  `0x14086d007` is `c780b8010000982f0100`.
- Alternate Alpha 3: setup `0x140DCC280`, root slot `0x142C44050`, primary
  `0x1B528`, secondary `0x1B530`. Publication `0x140dcc2b9`; size store
  `0x140dcc2c7` is `c780b801000028b50100`.
- PWS1: setup `0x140973B10`, root slot `0x1423188C0`, primary `0x428F0`,
  secondary `0x42900`. Publication `0x140973bc7`; size store
  `0x140973bdb` is `c780b8010000f0280400`.
- PWS2: setup `0x140A46EB0`, root slot `0x14262F0B0`, primary `0x8CD40`,
  secondary `0x8CD50`. Publication `0x140a46f67`; size store
  `0x140a46f7b` is `c780b801000040cd0800`.
- MJUS: setup `0x140B930D0`, root slot `0x142996830`, primary `0x9E8A8`,
  secondary `0x9E8B0`. Publication `0x140b93109`; size store
  `0x140b93117` is `c780b8010000a8e80900`.
- STG2: setup `0x140CCC940`, root slot `0x142BE6370`, primary `0x511B8`,
  secondary `0x511C0`. Publication `0x140ccc99c`; size store
  `0x140ccc9aa` is `c780b8010000b8110500`.

Every size instruction above is `mov dword ptr [rax+0x1b8], immediate`,
with RAX reloaded from S's slot immediately beforehand. The reproduction
retains the preceding root/context loads, clear-call arguments and second-block
publication. Initialization continues after publication: these stores alone
do not prove readiness or a coherent snapshot.

## Update and cleanup edges

All nine initializers publish four qword callbacks in S+0x10/+0x18/+0x20/+0x28.
The manifest lists exact publication instructions and target addresses.
At `0x14003a70d`, RAX loads S; `0x14003a714` (`ff5010`) calls S+0x10.
The wrappers then call these distinct native update routines:

- CVS1: wrapper `0x1405A5C60`, call `0x1405a5c85` → `0x1405A65F0`.
- CVS2: wrapper `0x1406A9A10`, call `0x1406a9a19` → `0x1406AA7A0`.
- CFJ: wrapper `0x1407CAEE0`, call `0x1407caeee` → `0x1407CB8B0`.
- Ordinary Alpha 3: wrapper `0x14086BE70`, call `0x14086be79` → `0x14086D160`.
- Alternate Alpha 3: wrapper `0x140DCB970`, call `0x140dcb979` → `0x140DCC3F0`.
- PWS1: wrapper `0x140973090`, call `0x140973099` → `0x140974100`.
- PWS2: wrapper `0x140A46420`, call `0x140a46429` → `0x140A47290`.
- MJUS: wrapper `0x140B92930`, call `0x140b92939` → `0x140B93500`.
- STG2: wrapper `0x140CCB440`, call `0x140ccb449` → `0x140CCCB60`.

Each wrapper also tail-jumps to another routine; targets are retained in the
manifest. The caller sets no new explicit argument immediately at these update
calls, but this is insufficient to certify `void(void)` or a detour ABI.
Incoming register use, original-call forwarding, update phase gates, callback
cadence and post-update coherence still need inspection and live checks.

S+0x18 is separately called at `0x14003a78a`, subject to S+0x780 and the result
of `0x14003ADE0`. It is not interchangeable with S+0x10. CFJ's +0x18 target
is `0x14004A600`; the other paths use `0x1405A5A20` → `0x140F2FBA0`.
S+0x28 is another independent callback and is not yet given a semantic label.

At `0x140050049`, the shell calls S+0x20 before invoking the initializer
dispatcher at `0x14005005b`. Another +0x20 call is `0x14004add1`.
Both guard on unsigned S+8 <= 7. The cleanup targets differ:

- CVS1, CVS2, both Alpha 3 paths, PWS1, MJUS and STG2 use
  `0x1405A5A30` → `0x1405A5D80` → `0x140F2F1F0`.
  That body iterates 4096 context records at +0x1e00a0, stride 0x18, releasing
  nonzero entries through `0x140048FC0`, then 256 entries through `0x140048F70`.
  The latter helpers call object vtable+0x28 and clear shell pointer-array
  entries. This is a bounded resource-cleanup trace, not arena destruction.
- PWS2 uses `0x140A46180` → `0x140A468D0`, which clears 0x400 bytes at
  `0x142632D50` before tail-jumping to the same `0x140F2F1F0`.
- CFJ uses `0x1407CAC90` → `0x1407CB000`. Its first calls are
  `0x1407CC090`, `0x1407D3200`, and `0x140F42810`; it conditionally releases
  handles and tail-jumps to `0x140F3D070`. These are cleanup-path leads, not
  extra initialization calls as their position in the old excerpt suggested.

The inspected common cleanup body/helpers do not clear S+0x1b0/+0x1b8 or
the native root slot. This does not prove that no other teardown path does.
Never equate a retained nonzero pointer with an active game or a new session.
Arena destruction, root invalidation, reset/load replacement and CFJ's deeper
cleanup callees remain explicit open edges.

Continuation from `9f10a12`: [destruction and CVS1 phase/copy trace](collection-lifecycle-continuation.md)
adds a destructor arena-release edge, distinguishes the cleared shell singleton
from S's slot, and establishes a CVS1 in-place restore path plus phase dispatch.
These narrow the open edges above; event coverage and hook ABI remain unresolved.
The [bounded metadata capture tool/run card](collection-metadata-run-card.md)
is now prepared and tested. No game captures have been obtained with it.

## Next measurements and independent work

The smallest useful live observation is a labelled CVS2 → CVS1 → shell sequence
recording S, arena, game/ROM/name/discriminator, context slot, native root,
S+0x1b0/+0x1b8, and four callbacks before/after each transition. Add all four
Alpha 3 ROM identities to falsify the static branch map. This observation must
distinguish stale retained pointers from fresh initialization, including reuse
of the same arena/root address. No capture has yet been made for this trace.

The bounded, read-only metadata capture tool and compact run card are now
prepared; use the linked card above for the outstanding play session. The existing
`cvs2_state_snapshot.py` still contains historical field labels; do not copy
them into a collection capture. A metadata tool needs no player-field guesses,
arena scan, injected hook, or speech-worker work. Bracketed reads can detect
some transitions but cannot prove an atomic snapshot or exclude ABA reuse.

Offline work can continue immediately: trace the arena destructor and all
entry paths through `0x140049DA0`; follow CVS1's update into phase/selection
dispatch and name consumers; then resolve the frame capture ABI. F01–F19
acquisition, the implemented shared boundaries, CVS1 pilot, arena audio proof
and evidence-backed implementation packets remain pending. No runtime packet
is ready on the strength of these lifecycle records alone.
