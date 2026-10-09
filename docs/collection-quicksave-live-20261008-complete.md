# Completed CVS2 Quick Load capture

Captured 2026-10-08; analysis recorded 2026-10-09. The user identified the
second new launch as the valid load exercise and the first as accidental CFE.
They confirmed that speech kept up and loading restored the saved point after
the final-boss victory. Their clarification remains important: usual saves
load at the Vs. screen; this particular save is after the final match ends.
These are user-reported checkpoints, not a measured checkpoint-producer rule.
The exact offline mode was not recorded.

## Retained evidence

- Build: 7ecfeeb, installed DLL SHA-256
  cc259d9d107d2ced154322bd09bbb62286b1423fae4f26980342f88dda3ef63f.
- Valid process: 31516; capture cfc2_boundary_31516_170225437.csv.
  CSV SHA-256: 5730410fa0c69d7f87603d6446ca5344ef15970bf33c328d931d66949273ddff.
- Committed fixture: tools/fixtures/collection-boundary-quicksave-20261008.csv.gz.
  Gzip SHA-256: ca965a778a593872d88fca4e6cc6b4f2b14815b6eacfc19ffa6f3ead6a682566.
- CSV, copied log and parser report are under build/quicksave-live-20261008-fixed.
  The accidental first launch, process 21452, is retained there separately;
  it has 9,352 events, zero drops and no Quick Load event. It is not load evidence.
- The valid second capture itself also contains game 2 / ROM 5 updates and a
  release before the CVS2 interval. Preserve those records; the user's launch
  label does not justify rewriting recorded game identities.

## Observed transaction

The complete schema-2 capture has 10,232 readable events, zero dropped events,
5,116 matched calls and no unmatched boundaries. All recorded events use thread
26136. Calls comprise 5,113 outer updates, one cleanup, one screen release and
one Quick Load. There are no snapshot-load or root-registration events.

Times below are relative to the first event, not launch or installation:

- Sequence 3010, +34.0244469 s: outer-update exit, depth 1, CVS2 game 1 /
  ROM 3, session 0x141c14fa0, root 0x17ab1000, size 136880.
- Sequence 3011, +34.0244502 s: Quick Load entry, depth 0, with the same
  identity and root. Forwarded owner argument is 0x2a5cc020.
- Sequence 3012, +34.0362852 s: Quick Load exit, depth 0, result 0. The
  session, game, ROM, size and owner argument are unchanged; root is now
  0x17cb1000. The measured interval is 11.835 ms, including diagnostic overhead.
- Sequence 3013, +34.0450556 s: the next outer-update entry observes that new
  root, 8.7704 ms after Quick Load exit. Its exit is sequence 3014.

No cleanup-wrapper event is nested inside this Quick Load pair. The one
cleanup in the capture is earlier, sequences 1380/1381 inside release
1379/1382, with game 2 / ROM 5. This agrees with the static trace that Quick
Load invokes cleanup directly, bypassing the monitored wrapper. Absence of
snapshot callback events does not mean absence of state restoration.

## Discovery fix and completion

The log records probe start at 14:23:33.977 EDT and successful completion at
14:25:34.215, approximately 120.238 seconds later, with saved=1 and dropped=0.
It records incremental English-blob resolution at 14:24:36.983 and indexing of
4,165 strings at 14:24:37.455. There are no synchronous gmdfind scan lines in
this retained log. Together with the user's report, this supports responsive
speech and prompt capture completion in this run, not a universal latency bound.

The probe marker was removed and the user was told they could close the game
after completion was confirmed. The fixed DLL remains installed; future
launches do not enable the probe without its marker.

## Consequences and next work

Static follow-up, 2026-10-09: the [completion trace](collection-quickload-completion-trace.md)
now identifies the CVS2 phase-dispatch consumer, both local repair paths,
subsequent wrapper writes and callback-mode exclusion. Its 23-span audit does
not establish the live completion update or a supported offline-mode allowlist.

Quick Load can replace the root while retaining session/game/ROM identity.
Revoke at transaction entry and require a new generation. This capture does
not exercise same-root-address reuse, and pointer inequality must not become
the condition for revocation. Its depth-zero transaction follows an outer
update on the same observed thread; this is not proof about every mode/thread.

The remaining acquisition dependency is secondary-to-primary restoration and
repair completion in the following updates. Trace those consumers and the
offline-mode exclusion gate before choosing a lifetime-safe copy boundary.
Neither Quick Load return nor the next update's mere existence proves that
completion. No additional user load exercise is needed to establish the
transaction pair already captured here.

Cross-thread output revocation and shell/audio arbitration still require
implementation and concurrency controls. Session/selection remain unwired;
CVS1 name provenance and cancellation/re-entry remain unresolved.
