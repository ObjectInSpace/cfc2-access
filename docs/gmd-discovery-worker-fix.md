# Bounded message discovery after the Quick Load capture stall

2026-10-08. The [incomplete live capture](collection-quicksave-live-20261008.md)
showed repeated eight-second gmd_find calls on the shared output worker. The
caller retried on every poll and restarted the address-space walk on failure.
Speech and diagnostic CSV draining were delayed on that same worker.

## Implemented behavior

gmd_text_ready now advances worker-owned discovery state across calls. A call
copies at most four 64-KB chunks and performs at most eight address-space walk
queries, plus the stack query and bounded candidate verification. A two-ms
elapsed budget yields between chunks; this is not a hard deadline for OS calls.
Chunk overlap retains entries crossing copy boundaries. Successful completion
of an unsuccessful pass resets the cursor and backs off for one second.
An unresolved poll returns false without reporting the whole search absent.

Every chunk has a fresh VirtualQuery and a guarded copy. Unreadable/guarded
regions are skipped. Changed allocation, region end or protection drops
accumulated probe hits. VirtualQuery's interior-page suffix retains progress
within an unchanged region; comparing each returned BaseAddress for equality
would silently discard probes accumulated by earlier chunks.

The existing probe hashes, geometry vote, live-table classification and label
chain verification identify a candidate. Its index-zero label chain is checked
again on every subsequent candidate poll. One language blob is tested per
poll, using the existing shipped-language identification and Spain/Mexico
tie-break. Known offsets retain their priority, with the existing 64-KB-offset
search through four MB when the requested language is not at a hint. The
existing verified fallback remains available if no requested-language blob is
found. Trial blobs are cleared before returning, so pending discovery does
not expose a trial language to other worker consumers.

Language changes clear the old text index and restart discovery. A change in
the requested language during candidate resolution restarts that candidate's
language search. Explicit text invalidation also clears discovery progress.
The Spanish tie-break now skips strings using bounded block reads instead of
one guarded read per byte, retaining the 8,192-byte per-string rejection limit.

The synchronous gmd_find remains an offline diagnostic used by existing tests;
the runtime resolver no longer calls it or its diagnostic text sweeps.
Native hooks, session/selection wiring and probe recording bounds are unchanged.

## Verification

The new real-memory regression restricts address enumeration to a 32-MB
allocation with a table 16 MB into it, a stale French blob at the first hint,
and English at a non-hint offset. Before the fix, one production resolver
call made 515 scanner queries and failed the yielding/bounded-work controls.
After the fix it resolves across polls, without restarting at the beginning.

Additional controls cover a real language change, language changing mid-search,
a candidate protected between scan and resolution, unconsumed guard pages,
complete unsuccessful passes and cooldown, a table appearing on a later pass,
and long/malformed strings in the Spanish traversal. The existing language
suite covers all 14 shipped locales and the Spanish tie-break.

Offline timings measure the test process, not live-game latency. A live run
still needs to demonstrate responsive speech and prompt probe draining before
the previous capture problem can be called resolved in-game.

All 35 native runners and 67 collection Python tests pass. The new runner
passed 160 checks in the saved full-suite run, with at most six scanner queries
per call and an observed maximum of 1.292 ms per resolver call. This count
includes per-poll controls and is not a count of independent scenarios.
Build/import checks pass with pre-existing compiler warnings, including the
synchronous diagnostic's longjmp warnings; no new discovery warnings appeared.
Saved evidence: build/test/discovery-regression.json,
build/test/discovery-run_gmd_discovery_test.log, and build/discovery-build.log.

## Staging and next live check

Staged with the game closed on 2026-10-08. Installed SHA-256 was verified as
cc259d9d107d2ced154322bd09bbb62286b1423fae4f26980342f88dda3ef63f.
The preceding DLL is backed up under build/deployment-backups with its hash
76596752567f14f636b6eb547c3d0650a76a4f2aa31cc9b6b1e071fb38942896.
The probe marker is enabled for the next launch; no game was launched.

Repeat only the existing-save CVS2 load exercise. Compare the user's saved
post-final-boss-victory point, not the earlier Vs. screen expectation. Report
whether speech keeps up, and leave the game open until the assistant has
collected a completion footer. Check the log for absence of synchronous GMD
scans, the capture for Quick Load entry/exit, and the footer for event loss.
Do not interpret improved offline discovery timing as live acceptance.
