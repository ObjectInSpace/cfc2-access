# CVS2 Quick Load restoration and mode exclusion

2026-10-09, continued from 1d145e4. This is static evidence from the pinned
unpacked image, not another live exercise. Reproduce with
`python tools/collection_quickload_completion_audit.py --manifest-out docs/collection-quickload-completion-manifest.json`.
The audit retains 23 instruction-aligned spans and three exact dispatch slots.
The full instruction report is `build/collection-quickload-completion-audit.json`.
Code identity remains SHA-256
`9c340b8ce40a0c15a3eccd3c47207b02a59e38365bf99ae7605afc26bb84f95b`.

## Import and later consumer

Use S for the session from 0x141c14f98, R for the CVS2 primary root from
0x1422debd0, B for the secondary block from 0x1422debd8, and P for the phase
pointer from 0x1422dec30. Setup publishes P=R+0x78 at 0x1406aa3c9..0x1406aa3cd.
The shell owner from 0x141c14f90 is a separate object.

Quick Load imports into B through 0x1400b5f90 after reinitialization. Import
bypasses when S+0x1a8 is nonzero, B is null, or the slot is neither 0 nor 3.
On its copy path it restores six metadata dwords, copies S+0x1c8 bytes from
buffer+24 into B, and sets S+0x774=1. For CVS2 the secondary length is 0x216b8:
an eight-byte game header followed by the 0x216b0-byte primary payload.
That is still separate from the packaging helper's 24-byte metadata header.

The later primary consumer is 0x1406aab40. Its three bypass dwords
S+0x7b4/+0x828/+0x80c must all be zero. Signed B[0] <= 0 saves R into B+8;
positive B[0] restores B+8 into R. The copy at 0x1406aabf1 is followed by:

1. 0x1406ace60, returning at 0x1406ad0e1. It rebuilds pointers at
   R+0x1b1b8 and R+0x1b1c0, zeroes 300 entries of stride 0x168 starting at
   R+0xbd8, links successive entries through their +8 fields, clears ten
   pairs of list pointers and ten word counters, and writes 300 at R+0x1b27c.
2. For B[0] in {2,3,4}, 0x1406b0520 with ECX=3. This builds resource records
   through 0x1406b0690, then loops through them calling 0x1406a9bf0. That
   helper copies resource-table bytes into the native backing allocation.
   These are additional copies outside the primary-state memcpy; the trace
   does not assign character names or a visible purpose to the resource IDs.
3. S+0x528=1 at 0x1406aac1d, then return at 0x1406aac2b.

On a confirmed restore branch, that local return is after both repair paths.
Entry/return alone cannot distinguish restore, save and bypass. In particular,
S+0x528 is also set by the save path and by the separate checkpoint producer
0x1406aac30. Neither that field nor B[0] is a completion counter.

## Dispatch and enclosing work

The ordinary callback S+0x10 points to 0x1406a9a10. It calls the native update
0x1406aa7a0, which calls phase dispatch 0x1406b1ca0 at 0x1406aa8b0. When
P+0x24 is nonzero, the phase-table call is skipped. Otherwise the following
exact table entries connect phase prefix 2,1,0 to preparation:

| Selector | Slot | Target |
| --- | --- | --- |
| P[0]=2 | 0x141282a30 | 0x1406ba870 |
| P[1]=1 | 0x14128cd70 | 0x1406baba0 |
| P[2]=0 | 0x14128cd78 | 0x1406babc0 |

Preparation changes P[2] to 1, clears later phase bytes and changes native
state before tail-jumping to the save/restore consumer at 0x1406bad41.
Restoration overwrites these preliminary changes with the saved payload.
After its return, phase dispatch still calls 0x1406bcf60 and other helpers;
the native update also continues. These slots establish one reachable
consumer path, not the number of updates needed after Quick Load.

There is an earlier header consumer at 0x1406b20d0: after other numeric mode
branches, positive B[0] supplies B+4 to P+0x2d and tail-calls 0x1406b9a60.
That helper establishes phase 2,0,0. This is evidence that the secondary
header influences startup, not proof of the complete boot-to-2,1,0 timeline.
The captured load did not record P or B, and its exact offline mode is unknown.

The callback then tail-jumps to 0x1406a9de0. Its S+0x4654==3 path copies
0xb00 bytes from S+0x465c back into R+0x203ec, resets the transfer control,
and clears additional bytes. Later it publishes phase/status into S and can
modify native health-related fields. When P[0] != 2 it tail-jumps to
0x1406a9d20, which clears B[0] and S+0x528 among other fields. A zero sampled
afterward therefore does not prove a restore completed.

The non-callback-mode outer update 0x14003a290 can skip S+0x10 when
S+0x780 is nonzero and contains a count-controlled loop through S+0x798.
After that it may call S+0x18 and tail-drains a work queue through 0x14006d530.
The already-traced screen caller continues after this outer function.
Consequently neither the first update after Quick Load nor the local repair
return establishes final publication readiness or a lifetime-safe worker read.

## Supported offline-mode gate

There is a concrete callback exclusion discriminator: shell-owner byte
+0x1cd. At 0x14003a305, nonzero routes to 0x14003a7a0; zero routes to the
ordinary loop. The setter 0x1400520b0 clears it only after callback close and
scratch cleanup; its nonzero path stores the requested byte directly.
The established join-match route enables it. This is stronger evidence than
the absence of snapshot events, but zero is not an offline UI-mode allowlist.

For a conservative future CVS2 Quick Load pilot, reject a missing/unreadable
owner/session or unknown identity; exclude nonzero owner+0x1cd, nonzero
S+0x7b4/+0x828/+0x80c, and nonzero S+0x1a8 on the secondary-import path.
These are necessary exclusion checks, with distinct meanings:

- owner+0x1cd selects the callback route;
- the three session dwords bypass the native checkpoint consumer;
- S+0x1a8 bypasses secondary import.

Do not label the three dwords as online/training/replay without tracing their
writers. All-zero values merely pass these exclusions. They do not establish
supported mode, completed restoration, quiescence, or pointer lifetime. Mode
and identity must be checked at a protected native boundary and carried with
the copied observation; a worker sampling them later cannot close a race.

S+0x774 is unsuitable as a pending-restore/completion flag. The separate
0x140048e90 resets command-buffer bookkeeping and assigns it from ECX;
0x1400487c0 tests it before forwarding a command to 0x14004ca10. Its write
by secondary import is not an exclusive restore signal.

## Integration consequence and remaining evidence

Keep the existing rule: revoke at Quick Load entry and require a new owner
generation even if addresses are reused. Track a positively identified
restore branch through local repairs, then through its enclosing native,
wrapper and outer operations under the same revocation label. Save/bypass
must not fulfill a pending load. Closing callback mode must not itself acquire
an owner. Unknown modes and lost boundary records remain ineligible.

The next implementation prerequisite is a protected native observation design
that combines branch completion, enclosing-operation completion and immediate
cross-thread revocation. Before an offline mode can be advertised as supported,
trace its entry/writers and validate its labels; the old live fixture lacks
those fields. A targeted completion probe would need P, B control, gate values
and branch/completion events, plus a tested ABI. No additional load is needed
to prove the already captured Quick Load transaction.

Eleven audit tests reproduce the manifest and reject changes to import/bypass
polarity, signed control, repair call/range/count, preparation slot, later
reverse transfer and callback exclusion. They validate extraction, not native
execution. No DLL changes, staging, probe activation or save-file operations
were made. Shared session/selection wiring and CVS1 speech remain pending.
