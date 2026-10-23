# Power Stone selection decoder, 2026-10-22

Both games now have a C decoder that converts copied native fields into the
existing shared selection choices. It is compiled and tested offline, **not
wired into the DLL or deployed**. The next gate is labelled live capture of
selection and the transition to a match, starting with PWS1.

## Reproduce

```powershell
python tools/pws_selection_audit.py --check
python tools/test_pws_selection_audit.py
python tools/extract_roster_names.py --check
.\tools\run_pws_select_test.ps1
```

The audit checks the unpacked code image hash and six pinned data anchors,
including the PWS1 ID map, both phase-table targets, and PWS2's three cursor
adjacency tables. It retains fifteen bounded code spans in
`docs/pws-selection-manifest.json`; full instructions are reproduced in
`build/pws-selection-audit.json`. Names reuse `character_name_table.h`.

`src/pws_select_decode.c` is shared by the test and its optional snapshot
inspection mode. With an uncompressed snapshot from collection_state_capture:

```powershell
.\build\test\test_pws_select.exe 4 1 build\pws1-camera-live\state-01.bin
```

Arguments are game (4 PWS1 / 5 PWS2), variant (0 Japanese / 1 US), and path.
The report lists fixed slots, raw cursor/state/shuffle and candidate names.
It does not speak. The capture tool writes `.bin` snapshots; `.gz` is used
when retaining compressed fixtures later.

## PWS1 traced path

- Root `*0x1423188C0`, size 0x428F0. `0x140976B50` admits its running
  branch when R+0x27E58 is 1. `0x140976CF8` gates dispatch on both
  R+0x27F8B and +0x27F8E being zero.
- R+0x27E5D indexes the jump table at `0x1409772D8`. Entry 2 calls
  `0x1409A67E0`, which reaches choosing at R+0x27E5E == 2.
- R+0x27FF1 selects owner 0 for 1, owner 1 for 2, both for 3. The
  driver supplies actors R+0x18428 and R+0x1FCB8 (stride 0x7890).
- Actor+3 is used as the slot index. Side state R+0x27DC2+slot*0x78
  must be 2 for the cursor path; other states handle entry/confirmation.
- Cursor R+0x27DCC+slot*0x78 indexes `0x14109C510`:
  `[8,0,6,2,4,3,5,7,1,9,10]`. Thus cursor 1 means Falcon/Fokker,
  cursor 2 Ayame; cursor values are not native fighter IDs.
- The mapped ID is written to actor+4 at `0x1409A0F8C`. Confirmation
  publishes it to shell+0x578+4*slot at `0x1409A10CD`, the shell name-ID
  space used by the existing roster extractor.
- Holding the tested direction mask for 60 frames enables actor+0x7874;
  while held the cursor randomizes every fourth frame. Suppress until zero.

## PWS2 traced path

- Root `*0x14262F0B0`, size 0x8CD40. R+0x52BD8 == 1 and
  R+0x52BD9 == 2 reach `0x140A4CA30`, subject to R+0x52D2B/2E == 0.
- That routine requires R+0x52BDC == 1. R+0x52BDD indexes the table
  at `0x140A4CDB8`: **entry 1**, not entry 3, calls `0x140A692C0`.
  It reaches `0x140A695F0` at R+0x52BDE == 1. Choosing is substate
  R+0x52BDF == 2.
- R+0x54B49 bits 0..3 individually call the cursor driver with indices
  0..3. This is selection participation, not a gameplay human/CPU classifier.
- Side state R+0x529D2+slot*0x98 == 1 runs choosing. The actor is
  R+0x40818+slot*0x42B8, slot byte +1, native highlighted ID byte +2.
- Three adjacency tables select among IDs 0..11, optionally 15 or 14/15.
  Boss IDs 12/13 are absent. The random picker also skips 12/13.
- Confirmation publishes the same actor+2 ID to shell+0x578+4*slot at
  `0x140A6B5EA`. There is no extra cursor-to-ID map.
- Shuffle is actor+0x4258 (R+0x44A70 for slot zero), likewise triggered
  after a 60-frame hold and updated every fourth frame. It stays unavailable
  until released, separately for each slot.

## What remains before production wiring

The root-state predicates are **candidate gates**, not complete screen or
lifetime eligibility. In particular, PWS2 `0x140A4CAFF` also branches on a
global mode byte outside the root; branches 0 and 4 need live mode labels.
No worker should infer an active game from persistent root bytes alone.
Integrate with verified ROM/root checks, signature/data validation and an
executing-update liveness witness as the other recent adapters do. Confirm
pause/shell overlay behavior and re-entry before enabling speech by default.

First live session, PWS1, using the prepared camera plan:

1. Record the actual game mode, ROM version and controller slot. At select,
   press F9, move one cell and press F9; take additional cell snapshots if
   practical. Screenshots establish names independently of the decoder.
2. Hold a direction long enough to shuffle; release it, then press F9.
   The continuous trace records the hold even between snapshots.
3. Confirm, start a fight, walk in both arena directions, jump, pause/resume,
   and take the plan's snapshots. The same trace covers phase transitions
   and the newly traced position/camera candidates.
4. Repeat in Versus/Training as applicable, and on the second selecting side.
   A single Arcade capture does not certify other modes or both ROMs.

PWS2 later needs all four selecting slots, optional fighters, confirmation
and re-entry independently. The pure decoder already preserves four owners.

## Validation and limits

3,932 synthetic C checks passed under GCC with `-Wall -Wextra -Werror`.
Tests include every cursor byte, every alternative required phase byte,
both name variants, invalid roots, missing slots, shuffle, and four owners
through the existing shared presenter. Rejected output retries, confirmation
rearms the same name, and departed sessions cannot emit pending choices.
Two evidence tests pass, including corruption rejection for all six data
anchors. These results are implementation checks, not labelled gameplay.

The camera continuation separately retains candidate actor positions and
raw slot identity for both games. Health, stones/transformation and arena
sound design remain open; this increment does not add match audio.

## First live attempt

The October 22 US PWS1 capture reached selection, shuffle and subsequent
gameplay according to the traced native fields. Thirteen full root snapshots
are retained, compressed, under `tools/fixtures/pws1-observations-20261022`.
Their manifest preserves original hashes, timestamps and capture provenance.
Run `python tools/test_pws1_observations.py` after building the C test runner.
All thirteen replay with the expected decoder results: inactive, Kraken,
Ayame, two unavailable shuffle samples, then eight inactive samples.
These are memory observations, not independently certified names/screens.

The capture plan labels are instructions, not ground truth: snapshot 1 has
the gameplay phase; snapshot 5 still has shuffle enabled despite its release
prompt; snapshot 6 is in the pre-match transition. Do not train screen gates
or jump/transform/round-end semantics from those labels.

Screenshot validation failed. GDI returned stale desktop imagery even when
the foreground HWND belonged to CFC2. A window-only Pillow probe was black,
and a DirectX duplication probe produced no frame. The game subsequently
exited. No screenshots are retained as fixtures and no live speech acceptance
is claimed. The capture tool now rejects background/minimized windows and
focus changes during capture, but this does not solve the observed stale
fullscreen surface. Display mode confirmation and a usable visual capture
are still needed before production integration. Local raw trace sessions
were interrupted; only independently saved, hash-checked snapshots are used.
