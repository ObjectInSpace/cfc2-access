# CVS1 port packet: Capcom vs. SNK Pro

Audited 2026-10-13 against `9ecc31c`. Status: BLOCKED for speech on the name
map. Every other feature needs bounded discovery. No CVS1 runtime code exists.

## Identity and scope

- Game 0. ROM indices 0 `cvs1` and 1 `cvs1u` (index 0 is valid, not "none").
- Live-evidenced scope so far: ROM 1 `cvs1u`, branch A, mode P+0x48=0, phase
  prefix 2,1,1,6,6, side 0, catalog selector 0. Everything else is uncertified.
- Shell access: entry, pause and exit go through the existing shell (F01).

## Traced acquisition (offline, `docs/collection-lifecycle-trace.md`)

- Root slot `0x1422D2B90`, primary block 0x32AB0, setup `0x1405A6440`,
  update wrapper `0x1405A5C60` calling `0x1405A65F0`. Common context slot
  `0x142C79B40`. P = R+0x3E58.
- An in-place restore path can replace state at the same root address
  (`collection-lifecycle-continuation.md`). Root identity alone is a
  compatibility reset, as for CVS2, not a lifetime guarantee.
- External read-only capture exists: `tools/cvs1_selection_capture.py`, with
  signature, ROM, callback and root/size gates.

## Feature mapping to the shared interfaces

- F01, F13 and F19 mechanisms: shared and unchanged. The adapter adds its own
  signature check and gates, as CVS2's tick does.
- F02 character highlight: `selection_channel`, owner = side, kind 0,
  value = native ID, label = certified name.
  - Traced: selected byte P+0x78+4*owner+slot, slot P+0xA0+owner, group
    P+0x10C+owner, position P+0x104+4*owner+group, and the commit/advance
    writers (`cvs1-selection-visits.md`).
  - Live: IDs 0, 1 and 16 at catalog positions 0, 1 and 15 (schema-2 fixtures).
  - BLOCKED: no ID-to-name association is certified (`cvs1-branch-a-names.md`).
    Unknown IDs and 0xFF must stay unmapped.
  - Missing for the visit key: cancel/re-entry writers. The slot advances on
    confirm but is not a unique visit ID.
- F03 groove: candidate toggle P+0x76+owner in actor state 3. The visible
  choice and its label are unestablished. Bounded discovery.
- F04 ratio: budget arithmetic at R+0x32812+owner is traced. The user-facing
  rule is not decoded. Do not import CVS2's sum-to-four rule. Bounded discovery.
- F05 stage: unresolved.
- F06 transitions: `transition_channel` with up to 4 members. Team storage is
  P+0x78 per owner (4 slots). Blocked on names, as for F02. In-match phase
  and active-fighter fields are untraced.
- F07 to F11 match audio: `match_audio` producers. No health, meter, position,
  stance or clock field is traced. Update `0x1405A65F0` is the frame-timing
  lead, analogous to CVS2's hook, and is not yet a validated hook site.
  Bounded discovery: labelled in-match block captures, then writer tracing.
- F12 pause, exit, reset: adapter supplies `in_match` and a motion signature.
  Depends on F07 to F11 fields and on the restore path above.
- F14 to F17 text: a positioned text-record queue (`0x1405AF730`) is traced
  in `cvs1-acquisition-trace.md`. The record layout differs from CVS2, and the
  glyph atlas and special-glyph mapping are unverified. Bounded discovery.
- F18 read-current: register one `tc_register` source once F17's queue is
  decoded.

## Next steps, in order

1. Close the name map with labelled visual evidence. Extend the existing
   read-only capture so that each new selected ID is paired with a window
   screenshot, which Claude can read. Record ROM, branch, side and language
   for every pair. One user session can cover the whole roster for side 0.
2. Implement the F02 adapter for the certified subset only, behind the
   existing gates. Unmapped IDs stay silent and are reported as such.
3. In the same capture session, take labelled in-match block snapshots
   (full, hurt, meter full and empty, crouch, jump, corner, timer) to locate
   F07 to F11 fields offline, as was done for CVS2.

## Acceptance

Per feature: replay tests on the labelled fixtures, a CVS1 adapter suite, all
native runners, and a listening check. A partial name map ships only labelled
partial. Side 1, ROM 0, branch B and other modes stay listed as open.
