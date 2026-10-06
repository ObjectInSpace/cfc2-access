# First live collection boundary capture

Captured 2026-10-06 with source at 3708458. The user completed the requested
CVS2 -> collection menu -> CVS1 -> collection menu exercise and reported:
"Nothing was different, CVS2 still works and CVS1 still doesn't."
This is a user-reported compatibility observation, not CVS1 speech acceptance.
The diagnostic build does not enable the shared router or CVS1 speech.

## Build and retained evidence

- DLL SHA-256: c4b916da66edc95cfe08db40f12b8d0be6705852726d62cb80ad679803d2a36f.
- Before staging: the saved 34 native runners all passed; the matching DLL
  build/import check passed. All 52 collection Python tests were rerun and passed.
- Process 21928; the log records capture start at 14:09:37.751 EDT and
  completion at 14:11:37.729. The CSV carries a 10,000,000 Hz QPC frequency.
- Original CSV: cfc2_boundary_21928_227239062.csv. SHA-256:
  3a6cacc5849883236398d346165642a04f7b99f829dd32b22e768457c8264432.
- Committed deterministic gzip: tools/fixtures/collection-boundary-switch-20261006.csv.gz.
  Compressed SHA-256:
  7ee75d22bd3d2acdef9c0c2064590bff9d58f3ca3339e1d6685937218b1c74ea.
- Local original CSV, copied log, parser report, staging record and previous
  installed DLL backup are under build/live-boundary-20261006.
- The opt-in marker was removed after collection. The completed probe remains
  a passive forwarder in the current process; it will not install next launch.

## Observed timeline

The parser reports 6,738 readable events, zero drops, a completion footer,
3,369 matched calls and no unmatched entries/exits. All recorded calls use
thread 10284. There are 3,365 outer-update pairs, two cleanup pairs and two
screen-release pairs. There are no snapshot-load or root-registration events.

Times below are relative to the first recorded event, not launch or probe
installation. The shell interval before the first update is not represented.

- Sequence 1: CVS2 update entry, game 1 / ROM 3, session 0x141c14fa0,
  root 0x15781000, reported size 136880 bytes.
- At +26.504159 seconds, sequence 3181 enters screen release. Sequence 3182
  enters game cleanup, 3183 exits cleanup, and 3184 exits screen release at
  +26.530510 seconds. All four records still carry the CVS2 identity.
- At +39.004384 seconds, sequence 3185 enters an update with CVS1 game 0 /
  ROM 1, root 0x13f81000 and size 207536 bytes. The session address is unchanged.
  No recorded event identifies the exact publication instant between games.
- At +68.755149 seconds, sequence 6735 enters screen release. Sequence 6736
  enters cleanup, 6737 exits cleanup, and 6738 exits screen release at
  +68.798033 seconds. All four records still carry the CVS1 identity.
  Sequence 6738 is the final event before the completion footer.

Both cleanup calls are nested inside screen release, outside an observed
outer update (depth zero). Outer-update records have depth one. The same
screen argument 0x67c00a0 and cleanup argument 0x29d9c020 are reused across
the games; neither address alone defines a new owner generation.

## Integration consequences and remaining evidence

Readable metadata after cleanup still describes the departing game. This
does not establish that its native allocation is safe to dereference. Root
nonnull/readable is therefore insufficient as an eligibility condition.
Revoke at the departure boundary before cleanup, retaining the outer screen
release interval rather than reopening eligibility on the nested cleanup return.

This run supports that boundary ordering for these two exits. It does not
prove all updates, mutations or future switches use this thread. The next
game's first observed update is a candidate acquisition point, not a proved
completion boundary for every initialization path.

Snapshot-load/root-registration nesting remains unobserved. A separately
labelled restore/replay exercise must reach those callbacks; an ordinary
switch does not substitute for it. No further character movement test is
needed to interpret this capture. Keep shared runtime integration unwired
until revocation/handoff and the remaining mutation coverage are resolved.

Reproduce the parser report by decompressing the committed fixture and passing
the CSV to tools/collection_probe_report.py with --out REPORT.json. The fixture
test also verifies both file identities and the observed pairing totals.
