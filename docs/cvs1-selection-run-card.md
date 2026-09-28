# CVS1 selection evidence run card

Prepared from `30be231`. Tool: `tools/cvs1_selection_capture.py`.
Ready for a targeted run after synthetic tests; no live game capture or
audible validation has been performed with this tool.

## What this answers

Record phase, mode, side, slot, selected byte, actor byte, cooldown,
pending graphic refresh and input-copy state during selection. Optionally
retain a small raw asset prefix to compare the traced source addresses.
This does not identify character names, export an image, prove visibility,
or provide accessibility speech. Prefixes alone may not identify a graphic.

The read-only process transport is shared with the metadata tool. Startup
checks both manifests' bounded code signatures and the relevant checked
dispatch/descriptor data; it refuses mismatches before creating output.
It does not compare mutable dump pointer snapshots with live pointers.
No DLL build, deployment, hook or game-memory write is involved.

Before dereferencing CVS1 state, it requires game 0, ROM index/name pair
0/`cvs1` or 1/`cvs1u`, all four CVS1 callbacks, a nonzero root matching
the published primary block with size 0x32ab0, and nonzero common context.
The phase pointer must equal root+0x3e58. These are observational gates,
not proof against stale pointers or same-address reuse.

A full sample reads 1,282 bytes in 52 requests, across two observations.
With the maximum optional 4,096-byte prefix for each eligible side, that
becomes at most 9,474 bytes in 54 requests. Startup signature reads are
additional. Sampling defaults to one observation pair; the nominal schedule
is capped at 60 seconds, excluding read/output time.

Asset reads require phase bytes 2,1,1,2 and numeric mode 1,4,5 or 7, plus
owner=side, slot 0..3, selected byte=actor byte, actor state/activity=6,
zero cooldown, nonzero buffer and native ID 0..0x22. This conservative
collection domain is not a verified character-ID range. Special 0x23,
unsupported modes and incomplete selections retain state but skip assets.
Side 1 applies the traced 9→0x23, 0x12→0x24 and 0x19→0x25 address remaps.
The prefix length is an operator cap, not a proven asset size.

## Preparation

From `C:\Users\amock\CFC2 Access`, use 64-bit Python:

```powershell
python tools/test_cvs1_selection_capture.py
Get-Process | Where-Object ProcessName -Like '*CAPCOM*' | ForEach-Object { "$($_.Id) $($_.ProcessName)" }
```

Launch normally through Steam. Replace `12345` below with the numeric game
PID. Do not assign PowerShell's reserved `$PID`. Use a fresh filename each
time; existing output is never overwritten. Commands print linear sample
statuses suitable for a screen reader. Ctrl+C stops a running capture;
already-written JSONL rows remain available.

## Smallest useful session

1. Navigate to CVS1 selection using the usual controls. Take a state-only
   control. Supply the actual mode label if known; otherwise use `unknown`.
   The example label does not assert what each sample contains.

   ```powershell
   python tools/cvs1_selection_capture.py 12345 --out build/cvs1-selection/steady.jsonl --label "CVS1 selection steady control" --mode "unknown" --language "shell English; ROM unknown" --count 8
   ```

2. Capture a 30-second state window, return focus to the game, pause briefly,
   move once in one direction, pause, and move once back. Note which
   controller/side you operated and the action order in a companion text
   note. Record a character name only if independently known; `unknown`
   is useful evidence and does not require visual assistance.

   ```powershell
   python tools/cvs1_selection_capture.py 12345 --out build/cvs1-selection/move-side-0.jsonl --label "one move then back; first operated side" --mode "unknown" --language "unknown" --count 120
   ```

3. If the control contains `matching` samples, take a short optional raw-prefix
   window, using a distinct path for each operated side or known choice.
   Start with 256 bytes. Asset rows may still say `skipped`; do not force the
   gates or interpret skipping as a missing character.

   ```powershell
   python tools/cvs1_selection_capture.py 12345 --out build/cvs1-selection/prefix-side-0.jsonl --label "steady choice on first operated side" --mode "unknown" --language "unknown" --count 8 --asset-bytes 256
   ```

4. When practical, repeat for the other side, then a normal confirmation and
   return-to-shell window with state-only sampling. Do not rush navigation.
   Special-choice or side-remap cases can follow after reviewing the initial
   data; they are not prerequisites for a useful first run.

## Reading the result

- `matching`: both observations agree and the numeric branch-B phase gate
  passes. It does not mean the screen or raw asset content was stable.
- `changed`: some observed metadata/state changed. Retain the sample as
  transition evidence; do not assign its asset bytes to a settled choice.
- `outside_scope`: identity/publication or branch-B gates do not pass.
  This can be an expected shell, branch-A or other-phase control.
- `unreadable`: an exact read failed or returned too few bytes. Retain the
  error. Do not replace unreadable state with zeros or treat it as matching.

Each side's optional asset row separately reports `read`, `skipped`, or
`unreadable`. Asset content is sampled once between the state observations;
even `matching` cannot exclude an in-place asset change or ABA transition.
All decoded numbers remain native values without speech labels. Keep the
provenance row, raw records, failures and action notes together.

The next review compares moves against selected/actor bytes, staged refresh,
side remaps and asset-prefix hashes. A verified character-name map and any
larger asset capture require further evidence. The existing collection
metadata run card remains the separate lifecycle/Alpha 3 measurement.
