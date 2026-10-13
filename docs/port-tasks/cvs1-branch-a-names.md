# CVS1 branch-A native ID/name provenance

## Current direction, 2026-10-13: deliver CVS1 selection speech

The [corrected Opus execution plan](opus-shared-selection-execution.md) supersedes conflicting sequencing
and model assignments below. Preserve the existing CVS2 native acquisition
path. Reuse shared presentation and implement only what the scoped CVS1 feature
needs. The optional protected CVS2 reader replacement, global atomic output
dispatcher and complete collection-wide lifetime investigation are deferred;
they are not prerequisites for this pilot or sharing already-copied data.
CVS1 still needs evidence-backed names and an appropriately justified acquisition
path. Resolve concrete feature blockers without claiming unproved safety.
Existing stronger-design contracts remain valid for those designs if pursued,
but do not govern a smaller compatibility-preserving presentation integration.
Historical research and captures remain evidence, not automatic next actions.

## Identity and scope

- Task ID: CVS1-R01. Kind: bounded research. Status: ready for research.
- Expected executor: Opus, in a separate sequential session. This packet does
  not start an agent or assign runtime implementation.
- Base: `21da6ff`. Read `CLAUDE.md` and `collection-workflow.md`; inspect later
  changes to the referenced evidence before editing.
- Exact live scope: game 0, region 1, ROM 1 / `cvs1u`, branch A, mode P+0x48=0,
  phase prefix 2,1,1,6,6, side 0, catalog selector 0. Language of displayed
  names remains to be established. Other ROMs/modes are not certified.
- Feature: F02 name decoding prerequisite. Result: an evidence-backed native
  ID/name map for this branch, with unresolved entries explicit. No speech
  behavior is delivered by this research task.
- Expected changes: a new provenance document and compact machine-readable
  map/manifest, bounded audit extraction and relevant offline tests. Extend
  `tools/cvs1_acquisition_audit.py` only where evidence belongs in that audit.
- Exclusions: production readers, hooks, shared lifecycle/audio architecture,
  branch B, confirmation semantics, other catalog selectors, and full ROM or
  language coverage. Astra owns the shared-boundary proof separately.

## Established evidence

Read `cvs1-branch-a-selection.md`, `cvs1-acquisition-trace.md`,
`cvs1-acquisition-manifest.json` and `cvs1-live-schema2.md` first.

The pinned main code section SHA-256 is
`9c340b8ce40a0c15a3eccd3c47207b02a59e38365bf99ae7605afc26bb84f95b`.
Use the audit's section identities and span hashes; an address alone is not
build verification. Any archive subsequently used needs its own identity/hash.

R is the published CVS1 root; P=R+0x3e58. The branch-A route through
`0x1405d8b10`, phase dispatch `0x141237d80` entry 6, and
`0x1405dbc30` reaches substate 6 at `0x1405dbe21`. The active selected byte
is P+0x78+4*owner+slot; slot is P+0xa0+owner. Navigation uses group
P+0x10c+owner and position P+0x104+4*owner+group. All offsets are native
host state, not a claimed guest RAM layout.

`0x140610cf0` constructs list-4 objects with selector/group/side/position/ID
at +0xc0/+0xc1/+0xc2/+0xc3/+0xc6. Callback `0x14060b500` reaches the
handler `0x14060a800..0x14060b44e`; matching side, group and position writes
the object's native ID to the selection byte at `0x14060b431`. The actor
consumer at `0x1405da306..0x1405da3bc` skips 0xff and propagates the ID.
These establish selection flow, not a character's readable name or graphic
visibility. Relevant resource routes already retained in the acquisition
manifest are leads to inspect before creating a new search.

Selector 0's pointer slot `0x141237c78` points to `0x141237be0`, containing
35 two-byte catalog records. Group 1's raw ID order is
`0,1,2,3,6,4,32,14,30,24,21,19,18,20,17,16`. Availability has extra
state-dependent filtering, including IDs 13,14,30,15,31. Raw catalog order
does not prove every entry is selectable in every configuration.

No native ID/name association is certified yet. A shell roster, original
game reference, filename or familiar character order is a lead only. Trace
the CFC2 ID to its displayed text/resource consumer, or document independent
labelled visual evidence and its exact limits. Do not assign a name by list
position or assume alternate costumes/forms share a base name.

The live window contains four actual user taps, consistent with four recorded
transitions. There is no extra-transition discrepancy to investigate. Individual
tap timing/direction and physical input-bit mapping remain unmeasured.

## Shared integration contract

This task exports evidence/data only; proposed adapter symbols are not an
implemented interface. No runtime pointer retention, hook ABI, callback thread,
speech policy or mixer integration is required here. State replacement can
occur at the same root address; do not promote these captures into a lifetime
contract. Unknown IDs and 0xff must remain unmapped, never fall back to a
plausible roster entry. Record ROM, branch and language provenance per mapping.

## Fixtures and acceptance

Committed gzip fixtures and SHA-256 of the compressed files:

- `tools/fixtures/cvs1-control-schema2-20260928.jsonl.gz`:
  `8d31bc5c8478e84bb01fa1f146c44c4e867a4e86342bef4ed36f0654dfdef21c`.
- `tools/fixtures/cvs1-movement-schema2-20260928.jsonl.gz`:
  `b1e61aa3c784f4322dfa81b30cfd52a7f063d70c5fb3fd7a16104c4d6d7351d1`.

The capture document records original uncompressed hashes, timestamps and
startup checks. Eight control and 240 movement samples establish observed
native choices 0/1/16 and catalog positions 0/1/15. They do not label names.
Matching read brackets are not proof of atomicity or visibility.

Acceptance requires each mapped ID to have a reproducible CFC2 evidence
chain, a source identity/hash, and explicit branch/ROM/language limits. Retain
unresolved entries and conflicting evidence. A partial verified map is a
valid research result only if labelled partial; it is not a complete F02 port.
Test extraction against changed pointer/data bytes and unknown/sentinel IDs
where applicable. Synthetic fixtures test the extractor, not name semantics.

Run from the repository root:

```powershell
python tools/cvs1_acquisition_audit.py
python -m unittest discover -s tools -p 'test_cvs1*py'
git diff --check
```

At the base, the CVS1 suite has 26 passing tests. Add meaningful tests for new
extraction, and record commands/results actually run. A docs/data-only result
does not require a DLL rebuild. Any proposed runtime change is outside scope.
If names cannot be proved offline, prepare a compact labelled capture run card
listing only unresolved associations. Do not request another movement test to
resolve the already-corrected tap count. Live visibility/listening acceptance
for a future reader stays open; research has no latency target.

## Execution and escalation

1. Verify source hashes and reproduce the existing bounded audit.
2. Follow retained selected-resource routes to actual text/graphic consumers;
   extract only the evidence needed for the scoped mapping.
3. Record each association and uncertainty, with reproducible extraction and
   appropriate negative tests. Update the evidence and this completion record.
4. Commit a coherent result with test results and unperformed live checks.

Return for Astra review if the traced ID is not the ID actually consumed for
the visible name, ownership changes invalidate this branch model, or evidence
requires changing a shared boundary. Supply the smallest conflicting span or
fixture and build identity. Continue independent entries/extraction while an
unrelated association is unresolved; never fill the gap with a guessed name.

## Completion record

- Research checkpoint: continuation from 29c5771, recorded in
  [the visit and resource trace](../cvs1-selection-visits.md).
- Delivered behavior: none; no names mapped and no runtime change.
- Validation: audit regenerated with 67 spans and 19 explicit dispatch edges;
  all 28 CVS1 Python tests pass, including slot reset/advance and type-2 draw
  route checks. These are extraction checks, not name-semantic acceptance.
- Offline status: partially investigated; no association meets acceptance.
- Live status: existing native selection capture only; names unvalidated.
- Next action: follow later resource-pointer writes and other ID-dependent
  asset consumers under the stated scope. The constructor's fixed resource-1
  lookup and type-2 draw helper alone do not establish names. The pinned image
  has null root/common-context slots, so populated runtime assets remain absent.
