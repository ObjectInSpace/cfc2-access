# Collection transition metadata run card

Prepared from `9f10a12`, 2026-09-27. Tool:
`tools/collection_metadata_capture.py`. This card is ready for a targeted run;
no CFC2 captures or audible observations have been collected with it yet.

## Question and scope

Observe which identity, callback, context and root pointers change or remain
after CVS2 → CVS1 → shell, including re-entry at the same address. Cross-check
the four Alpha 3 ROM identities and record the actual visible option label.
These observations constrain lifecycle policy; they do not establish gameplay
fields, root readiness, hook ABI, or an atomic snapshot.

The tool opens the supplied PID for read/query access only. It verifies all
reviewed instruction spans against the checked-in lifecycle manifest once,
before creating the output. This is a bounded code-signature check, not a full
live executable hash. The recorded section hashes identify the reviewed dump.
Installed DLL identity and audible results are separate observations.

Each sample takes two metadata observations, up to 32 ReadProcessMemory calls
and 488 requested bytes in total. It reads the session slot; S+0..0x5f;
S+0x1b0..0x1cf; the dword S+0x2b4; a bounded 16-byte ROM name; nine native root
slots; and both context slots. It never dereferences roots/contexts, scans an
arena, installs a hook, or adds work to the speech thread. Null/unreadable S
still allows recording the fixed slots, which may retain stale values.

## Preparation

Run from `C:\Users\amock\CFC2 Access` with 64-bit Python:

```powershell
python tools/test_collection_lifecycle_audit.py
python tools/test_collection_metadata_capture.py
Get-Process | Where-Object ProcessName -Like '*CAPCOM*' | Select-Object Id, ProcessName
```

Launch the game normally through Steam if needed. Use the displayed numeric
game PID in place of `12345` below. Do not use PowerShell's reserved `$PID` as
a variable. No DLL rebuild, deployment or diagnostic switch is required.
Use a fresh output path for each run; the tool refuses overwrite.

## Smallest useful session

1. In CVS2, take a steady-screen control. Record mode and observed shell/ROM
   language; use `unknown` where unobserved. Example:

   ```powershell
   python tools/collection_metadata_capture.py 12345 --out build/collection-metadata/cvs2-control.jsonl --label "CVS2 steady screen" --mode "Arcade" --language "shell English; ROM unknown" --count 8
   ```

2. Start a transition window, then return focus to the game and use the normal
   accessible controls to leave CVS2. Label the actual action. The window is
   about 30 seconds plus read/output time:

   ```powershell
   python tools/collection_metadata_capture.py 12345 --out build/collection-metadata/cvs2-to-shell.jsonl --label "CVS2 exit to shell" --mode "Arcade to shell" --language "shell English; ROM unknown" --count 120
   ```

3. Repeat a window for shell → CVS1, then a CVS1 steady control and CVS1 →
   shell. Use distinct paths/labels. If launching both games cannot fit one
   window, use separate windows; do not rush navigation to fit the probe.
   Add a CVS1 re-entry control to test address reuse. Separately note the
   observed screen and approximate transition time. The label describes the
   window's intended action, not every sample's screen.

4. For Alpha 3, take steady controls for both regions with each of the two
   version settings. Record the visible setting name/value rather than naming
   it from our hypothesis. Expected static identities are `z3u`/`a3u`, indices
   6/7 with zero S+0x2b4; `z3uu`/`a3uu`, indices 16/17 with nonzero S+0x2b4.
   Contradictions are evidence to retain, not failed captures to delete.

5. Optional reset/load controls can record retained metadata before/after the
   action. This tool does not sample native state or hook discontinuities, so
   unchanged metadata cannot validate reset/load invalidation.

## Interpretation and failure handling

- First JSONL line is provenance; remaining lines are indexed samples with
  UTC start, measured elapsed milliseconds, and raw before/after records.
- `matching` means the two raw observations matched. It does not mean active,
  initialized, or coherent. Same-address reuse and changes that revert between
  reads can escape this check. Sampling can miss short transitions entirely.
- `changed` retains both observations. Do not join their fields into one state.
- `unreadable` includes short reads, malformed names, or unavailable ranges.
  It is not “no game running.” Raw readable records and errors are retained.
- A signature error produces no capture file. Check PID/unpacking/build rather
  than bypassing validation. An interrupted run retains complete flushed lines;
  compare the sample count to `requested_count` and treat it as partial.
- Commands report metadata, not speech correctness. Record any heard stale cue,
  delayed key response or shell access failure independently with its timing.

Keep the capture files and observation notes together. After review, retain
their hashes and actual ROM/mode coverage in the lifecycle trace. The next
analysis compares session identity and callbacks with root/context retention;
it must not turn a matching pointer into a new session epoch by assumption.
