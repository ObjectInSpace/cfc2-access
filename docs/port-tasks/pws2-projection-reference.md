# PWS2 offline two-point projection reference

## Identity and scope

- Task ID: PWS2-T01. Kind: bounded offline tooling implementation.
- Status: ready for implementation. Prepared 2026-10-02.
- Expected executor: Opus in a sequential session. This packet does not start
  an agent, switch models or authorize a production PWS2 adapter.
- Evidence base: `a065ca9`, including earlier `bad08f5` and `0e1fbe8`.
  This packet and its fixture are the additional prerequisite; inspect their
  containing commit and subsequent relevant changes before starting.
- Game: PWS2 native path in the pinned CFC2 dump. Lifecycle routing lists
  the PWS2 ROM variants, but there is no labelled PWS2 live sample here and
  no per-ROM/mode/language behavior is certified by this packet.
- Features: F09/F19 research tooling prerequisite. No end-user audio or
  speech behavior is delivered by this task.
- Deliverable: a streaming offline reference tool that evaluates the traced
  actor-linked two-point projection from supplied matrices and points,
  preserving independent identity metadata and reporting reference rejection.
- Expected files: `tools/pws2_projection_reference.py`,
  `tools/test_pws2_projection_reference.py`, a short usage document, this
  packet's completion record and the workflow index. Extend the supplied
  fixture only with labelled synthetic cases and documented expectations.
- Exclusions: process attachment, game memory reads/writes, hooks, DLL changes,
  deployment, sound, native-function execution, camera reconstruction, matrix
  constructors, actor anchoring, HUD ownership and live capture validation.
  The later projected queue at `0x140a5e9b0` is a different path and excluded.

## Established evidence

Read `CLAUDE.md`, `docs/collection-workflow.md`,
`docs/pws2-actor-trace.md` and `docs/pws2-actor-manifest.json`.

Main code SHA-256:
`9c340b8ce40a0c15a3eccd3c47207b02a59e38365bf99ae7605afc26bb84f95b`.
Reproduce the 58 retained spans with `tools/pws2_actor_audit.py`. Its full
output includes bytes and resolved references; the committed manifest is
compact. No external references are needed for this task.

The actor-linked path is `0x140a548d0` -> `0x140f380a0` ->
`0x140f37ca0`. Numerous guards and a modified actor vector precede it.
Neither raw actor+0x3c nor successful primitive submission is a certified
visible fighter-body anchor. This tool accepts the two already-selected
input vectors; it does not reconstruct them from actor data.

`0x140f33830` composes current * slot2 * slot3 * viewport using three
postmultiplications through `0x140f32c20`. `0x140f31d10` transforms
row-vector `(x,y,z,1)` through that composite. `0x140f37ca0` divides XYZ
by W when W is positive, then performs whole-primitive rejection. For finite
values, in native decision order:

1. Either W <= 0 rejects the primitive.
2. Both divided Z < 0 reject; then both Z > 1 reject.
3. Both X < 0 reject; then both X >= 640 reject.
4. Both Y < 0 reject; then both Y >= 480 reject.
5. Otherwise it passes these tests. This does not establish visibility.

The inequalities are asymmetric at the upper bounds: Z=1 is allowed; X=640
and Y=480 count as beyond their respective bounds. Opposite-side endpoints
can pass even when the segment misses the rectangle. Do not implement full
segment clipping, clamp points or replace the constants with viewport size.

The viewport at `0x141c00210` is writable global state. All four matrices
must be supplied for each sample; do not substitute the dump's viewport or
identity template. The native stack helpers assume capacity and the current
matrix can alias later source slots. This task supports only current slot 1,
distinct from slots 2/3, and the normal-capacity algebra. It cannot validate
that a real capture met those conditions.

Actor records start at R+0x40818 with stride 0x42b8. Transform records
start at R+0x51d58 with stride 0x308; their drawing matrix is +0x38.
Transform copies replicate records independently of actors. Pass IDs 0,2,3,
fixed actor indices, transform indices and capture sequence are separate.
Root address equality does not establish a session epoch. There is no
implemented common observation interface to integrate in this offline task.

## Concrete tool contract

Expose `replay(sample: dict) -> dict`. It must be pure: no mutation of the
input, no game access, no global last-sample cache. Add a CLI:

```powershell
python tools/pws2_projection_reference.py --input build/pws2-samples.jsonl
```

The CLI streams one JSON object per line and emits one JSON result per line
to stdout; diagnostics go to stderr. It does not write a capture file or
modify the input. An input line is limited to 1 MiB, checked with a bounded
read. Abort with exit 2 on an oversized line or file/CLI failure; earlier
output may exist, so exit 2 must never be described as a complete replay.
For ordinary input, exit 0 if every sample is valid (including reference
rejections), exit 1 if any sample is invalid. Malformed JSON emits an
`invalid_sample` / `invalid_json` result with null sample ID and continues.
Refuse JSON NaN/Infinity through the parser; direct API calls also validate
finiteness. No third-party math or rendering dependency is required.

Every sample has these required fields, illustrated by the fixture:

- `schema`: integer 1.
- `sample_id`: nonempty string, at most 128 characters.
- `evidence_kind`: `synthetic` or `unverified_capture`. The tool cannot
  promote either to a verified capture or infer provenance from values.
- `code_sha256`: exactly the pinned main code hash above.
- `path`: `actor_two_point`.
- `arithmetic_model`: `float64_reference`.
- `session_epoch` and `capture_sequence`: nonnegative integers, at most
  2^63-1, carried as caller-supplied labels, without freshness claims.
- `actor_fixed_index` and `transform_record_index`: independent integers
  in 0..3. `pass_id`: integer 0, 2 or 3. `current_slot`: integer 1 only.
- `matrices`: object with `current`, `slot2`, `slot3`, `viewport`, each
  exactly sixteen finite numeric values in row-major order.
- `points`: exactly two arrays, each of three finite numeric values.

Boolean values are not valid numbers or indices. Missing required fields,
wrong container types, overlong IDs, unsupported schema/path/arithmetic model
or evidence kind, and out-of-range identity labels produce `invalid_metadata`.
Use `unsupported_build` for a differing build hash and
`unsupported_current_slot` for an integer current slot other than 1. Invalid
matrix/point arrays produce `invalid_matrix` / `invalid_points`. Do not
silently coerce numeric strings, fill missing matrices, wrap indices or use
previous records. Extra keys may be ignored; they cannot expand claim scope.

Use Python float arithmetic as an explicitly labelled mathematical reference.
Compose the three matrix products in order, with four products summed from
index 0 through 3 for each entry; then transform and divide the two points.
Check every multiplication, addition and division result for finiteness.
Overflow/conversion failures produce `invalid_numeric_result` where they
arise during arithmetic; unrepresentable input numbers are invalid input.
This is not SSE emulation, a binary32 rounding model or a native-bit oracle.
Do not add a bit-exact option to this packet. Near-boundary native agreement
remains unproved even when the reference result is finite and plausible.

For a valid sample, echo `sample_id`, `evidence_kind`, `arithmetic_model`,
and an `identity` object containing the six epoch/sequence/actor/transform/
pass/current-slot labels. Output `claim_scope: "reference_math_only"` in
every result. Do not add a player/controller name, visibility bit, audio
eligibility decision, or a claim of coherent capture. No data joins across
samples: equal sequences, different epochs and repeated actor draws are
independent inputs.

The three result fields fixed by the fixture are:

- `status`: `reference_passed`, `reference_rejected` or `invalid_sample`.
- `reason`: `none`, `nonpositive_w`, the first matching boundary reason
  listed in the fixture, or the validation reason above.
- `projected`: two XYZ arrays after divide for either boundary outcome;
  null for invalid samples or nonpositive W. Do not emit NaN/Infinity.

If either W is nonpositive, skip all division and return `nonpositive_w`.
This reference simplification intentionally avoids unused calculations the
native body performs before its final rejection. It does not model native
exception flags or stack-failure behavior. For invalid results, retain a
valid string sample ID when available; identity echo is optional. Invalid
numeric values must never leak into serialized output.

## Fixtures and acceptance

Fixture: `tools/fixtures/pws2-projection-reference-v1.json`.
SHA-256: `f8135758018fadb1b8594779b4af41f5eca5ec848145ee89d077184ce6b9d06d`.
The scoped `.gitattributes` rule preserves LF bytes across Windows checkouts.
All 24 cases are synthetic. No PWS2 live matrix/vector fixture exists here.
Each case has a `sample` for `replay()` and an `expected` subset to compare.
Preparation verified 16 numeric/rejection cases with independent exact-rational
row-vector evaluation; the other eight specify invalid-input outcomes.
That preparation did not execute native game code or this proposed tool.

Accept only when:

- All 24 fixture cases pass. Compare the exact status/reason; the fixture's
  small dyadic coordinates should be exactly representable. Identity metadata
  must be unchanged, especially actor 3 / transform 1 / pass 2.
- Noncommuting translation/scale detects reversed composition. Mutable
  viewport data is honored while native 640/480 rejection bounds remain fixed.
- Tests cover API NaN/Infinity, nonfinite arithmetic, invalid points, missing
  fields, booleans, unsupported path/schema, and current-slot aliasing.
- A subprocess CLI test exercises valid/rejected/invalid rows, malformed JSON,
  an oversized line and nonexistent input. Check exit codes, line counts,
  stderr/stdout separation and strict JSON output.
- Input objects remain unchanged. Replaying reordered/duplicate samples or
  samples with different supplied epochs cannot inherit state from prior calls.
- Documentation states finite float64 reference limits and distinguishes
  reference rejection, native submission and final visible output.

Commands for the implementer, from repository root:

```powershell
python tools/pws2_actor_audit.py
python tools/test_pws2_actor_audit.py
python tools/test_pws2_projection_reference.py
python tools/pws2_projection_reference.py --help
git diff --check
```

All must exit 0 (the test suite checks intentional CLI error exits internally).
The new tool/test commands do not exist at handoff time and have not been run.
No DLL build or broad runtime regression run is required when runtime sources
remain untouched. Live acceptance is not required for this offline tool, but
no production projection/camera feature may be marked ready on this basis.

## Execution and return

1. Inspect the evidence base and reproduce the existing audit before editing.
2. Implement the pure reference function, validation and streaming CLI.
3. Implement fixture/negative/subprocess tests and run the commands above.
4. Add the short usage/limitations document, update this completion record and
   the workflow index, and commit the bounded result.

Return exact commands, exit codes, fixture provenance and remaining limits.
Do not describe this packet as implemented four-player accessibility. Common
session/observation/presentation integration stays with the wider Astra work.

If instructions contradict the equations or a supplied expected result, retain
the smallest contradictory sample and instruction addresses before changing
the fixture. Independent parser/validation tests can proceed. Unresolved live
camera selection does not block this reference tool; it does block applying
its results to audio. Do not choose a pass based on first/last draw order.

## Completion record

- Result commit: pending Opus implementation.
- Behavior delivered at preparation: 58-span evidence base and 24 synthetic
  acceptance cases; no reference tool has been implemented.
- Preparation checks: existing audit/tests passed in the evidence commit;
  fixture JSON/provenance and 16 exact-rational numeric cases validated.
- Live status: no attachment, capture, deployment or listening test.
- Open limitations: float64/native rounding, real sample coherence, stack
  conditions, ROM/mode coverage, body anchor, HUD/controller ownership and
  audible pass selection.
- Next action: implement this packet in a sequential Opus session.
