# CFJ port packet: Capcom Fighting Evolution

Audited 2026-10-13 against `9ecc31c`. Status: BOUNDED DISCOVERY NEEDED for
every game-specific feature.

**2026-10-14: F02 character select is built** (`src/cfj_select.c`) from one
live capture and the code that writes it. The user confirmed it in the game
the same day. Evidence: [cfj-selection.md](cfj-selection.md). Match audio is
built and confirmed by the user: [cfj-match-fields.md](cfj-match-fields.md).

## Identity and traced acquisition

- Game 2. ROM indices 4 `cfj` and 5 `cfe`. Japanese and English ROM names can
  differ, so name maps are per ROM.
- ⚠ Context slot is `0x142CF9D48`, NOT the `0x142C79B40` the other titles use.
  Do not share a context constant.
- Root slot `0x1422F3A80`, primary block 0x26E80, setup `0x1407CB660`, update
  wrapper `0x1407CAEE0` calling `0x1407CB8B0`. Cleanup callback `0x1407CB000`
  (it owns `0x1407CC090` and `0x1407D3200`, which are not update calls).
- Traced below the root: phase R+0x21960, select sub-state R+0x21961, human
  mask R+0x2197C and the select records at R+0x26E00. Fighter records start
  near R+0x1E678, stride 0x660, with committed IDs at +0x5A0 (R+0x1EC18).
  No player-health, text or camera field is traced yet.

## Feature mapping to the shared interfaces

- F01, F13, F19: shared. The adapter adds its own signature and ROM gates.
- F02 selection: `selection_channel`. Needs the select-screen phase, cursor
  fields, native ID and a certified name map per ROM.
- F03: not a six-groove selector. The combat system follows the chosen
  character unless tracing shows a separate choice. Speak it with the name only
  if the game shows it.
- F04: no CVS2 allocation. Audit any real handicap screen.
- F05, F11, F14 to F17: per-mode screen audit needed before any reader.
- F06 transitions: `transition_channel`. Team formation, active fighter and any
  between-round choice need tracing.
- F07 to F10 match audio: `match_audio`. F08 needs per-character or
  per-system resource schemas with observed maxima. A uniform guessed maximum
  is not acceptable. Camera projection must be traced for F09.
- F12: phase and motion-signature fields for `in_match` and pause.
- F18: one `tc_register` source after a text path is decoded.

## First bounded task

Follow setup `0x1407CB660` and update `0x1407CB8B0` into phase dispatch and
the player records, as was done for CVS1. Stop at named missing fields. Then
take one labelled capture session (selection and in-match states) using a
read-only tool cloned from `cvs1_selection_capture.py`, with CFJ's own
gates.
