# User-facing quick-save follow-up

2026-10-07. The user reports that saving/loading generally records at the
start of the match, specifically the Vs. screen showing the teams. This is
user evidence about the checkpoint. It is not yet a measured callback timeline,
an exact frame boundary, or a claim covering every game and mode.

## Established UI evidence

The retained pause-menu evidence in src/menu_speak.c identifies Quick Save
and Load Quick Save from their own captions. The English text corpus contains
LOAD = "Load Quick Save", SAVE = "Quick Save", and separate confirmation,
success and failure messages for each. Training's Change Character action
must not be substituted. The shipping menu reader uses the actual drawn row
text; historical row numbers are not a reliable instruction across modes.

The pause cursor call at 0x1400a749d is inside 0x1400a73b0. Its later input
path tests S+0x7b4, +0x828 and +0x80c at 0x1400a75fa..0x1400a7613,
also seen in the native save/restore gates. That overlap is a lead, not proof
that this input branch invokes Quick Save or identifies the copy mechanism.
A bounded search for embedded hashes of POP_LOAD_CHECK, POP_LOAD_END,
POP_SAVE_CHECK and POP_SAVE_END found none in the dumped code sections.
That negative result does not exclude data-driven message lookup.

## Keep the native mechanisms separate

- Per-game secondary-block routines conditionally save or restore native state,
  with game-specific repairs after copying.
- Post-update transfer wrappers copy smaller payloads through S+0x465c.
- Shared callback registration/load/replay operates on registered state blocks;
  the join-match route is a separate established entry into callback mode.

The user's checkpoint observation does not select one of these mechanisms by
itself. In particular, no snapshot-load callback in a Quick Load capture would
not prove that native state was unchanged. The current five-site probe does
not directly observe all three mechanisms.

## Bounded next work

Trace the pause-menu command dispatch and its confirmation result to the
actual save/load request. Determine whether saving persists an earlier
match-start checkpoint rather than capturing the instant the menu opens.
Follow load through replacement and repair to the first eligible observation.
Retain the Vs. screen report as the expected visible checkpoint to compare.

Prepare a targeted live check only after identifying the relevant observer:
label the chosen game/mode, save and load as separate actions, the success
messages, and the screen reached after load. Existing quick-save data must
not be overwritten incidentally; a load-only check can use an existing save
if suitable. Do not request another generic game switch or join-match run
to answer this user-facing save/load question.

The offline pilot may remain limited to evidenced modes, with decoding
disabled in unsupported paths. Exhaustive online replay coverage is not a
prerequisite for that limited delivery. This scope decision does not itself
implement a reliable mode gate or lifetime-safe acquisition.

The initial follow-up made no runtime changes. The continuation below adds an
opt-in diagnostic site but does not deploy it or operate on save files.

## Command trace and diagnostic continuation

Reproduce with `python tools/collection_quicksave_audit.py --manifest-out
docs/collection-quicksave-manifest.json`. The code identity is pinned by the
lifecycle audit. Ten complete code spans and negative extraction controls are
retained. Full instructions are in build/collection-quicksave-audit.json.

Quick Load's handler is 0x1400a70c0..0x1400a73a9. It supplies message index
269 (the retained POP_LOAD_CHECK corpus entry) to 0x14007fc20. Once the
dialog reaches state 4, result+0x360 equal to zero advances the handler;
the other result closes it. Substate 2 calls 0x14004ffd0 at 0x1400a717a.
Later it supplies index 272, the retained load-success message. These numeric
UI states are local evidence, not a general dialog convention.

The complete operation 0x14004ffd0..0x14005007b:

1. Retrieves buffer slot zero through 0x1400b6360.
2. Copies a matching save-record payload to that buffer via 0x14003fd60,
   then publishes the returned length through 0x1400b6530.
3. Calls S+0x20 directly at 0x140050049 when game ID is at most 7.
4. Calls the initializer dispatcher 0x14004c540 at 0x14005005b.
5. Calls 0x1400b5f90 with slot zero, restoring the buffer into the newly
   published secondary block, then returns a signed 32-bit slot value.

The direct S+0x20 call bypasses 0x14004adc0, the cleanup wrapper monitored
by the original five-site probe. This is an explicit coverage gap for Quick
Load, even if the old probe reports neither cleanup nor snapshot-load events.

Quick Save's handler is 0x1400a8140..0x1400a8621. Its existing-save branch
supplies index 271, the overwrite-confirmation message. Its no-existing-save
branch advances directly to substate 2. That substate calls 0x140051130,
which gathers slot zero through 0x1400b60b0 and writes a save record through
0x140041120. This establishes in-memory packaging; file persistence completion
is not proved by that call alone.

The gather copies S+0x1c8 bytes from secondary pointer S+0x1c0 after a
24-byte metadata header. It does not copy current primary root S+0x1b0.
The inverse copies buffer+24 into S+0x1c0 and sets S+0x774 to one. These
helpers have bypass gates; invocation alone does not prove a copy happened.
The save-record container adds a separate 28-byte header. Neither header
should be confused with each game's secondary-block header.

This supports the distinction between saving a prepared checkpoint and saving
the immediate paused frame. Exact producer timing at the Vs. screen is still
user-reported, not established by these packaging functions. Per-game
secondary-to-primary restoration and repair can happen later during updates.

## Handoff consequence and next targeted capture

Quick Load is a reinitialization transaction, not merely an in-place replay
revision. Revoke before entering 0x14004ffd0 and require a new owner generation
even when adapter/ROM/addresses are unchanged. Keep output ineligible after
return until secondary-to-primary restoration and repairs reach an evidenced
completion boundary. Do not resume on initializer or copy return, and do not
rearm accepted speech on a save operation.

The diagnostic probe now has a sixth site, quick_load, covering this entire
operation. Its wrapper forwards the owner pointer once, preserves signed EAX
and last-error state, and records shallow entry/exit metadata. The reviewed
caller sign-extends EAX at 0x1400a7186. Whole-body fingerprints and rollback
tests include the new site. Schema 2 preserves the signed 32-bit result; the
parser still reads the original schema-1 switch fixture.

A future load-only run should use an existing CVS2 quick save: launch with
the tested probe, enter the relevant offline mode, choose Quick Load and
confirm, then report the screen reached and any speech/audio change. Do not
create or overwrite a save for this exercise. Require a quick_load entry/exit
pair; compare its game/ROM/root and surrounding outer updates. This measures
the transaction's thread/order and address reuse, not final presentation
readiness. No online match is required for this question.

The production handoff remains unwired. A native-safe copy boundary and
cross-thread output revocation still need implementation; this diagnostic
extension does not deliver shared integration or CVS1 speech.

## Verification of the six-site diagnostic build

All 66 collection Python tests and all 34 native runners pass. The probe tests
include signed 32-bit return/argument/last-error forwarding, passive forwarding,
whole-body rejection and rollback at every site. The old live switch fixture
still parses. Build/import checks pass with pre-existing unrelated warnings.
Results: build/test/quicksave-regression.json and build/quicksave-build.log.
DLL SHA-256: 76596752567f14f636b6eb547c3d0650a76a4f2aa31cc9b6b1e071fb38942896.
This is the tested build for a future Quick Load capture; it has not been staged.
