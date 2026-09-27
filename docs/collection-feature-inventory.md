# CVS2 feature baseline and collection applicability

Audited 2026-09-26 against the working tree, including the new shared text
channel. This is the acceptance inventory for the [port plan](collection-port-plan.md).
Source implementation, offline regression coverage, and audible live validation
are separate statuses. A compiled reader is not automatically a validated port.

## Feature inventory

- F01 — Collection navigation and speech output. Existing `menu_speak`,
  `menu_cursor`, `gui_text`, `move_list`, GMD/prompt/roster/soundtrack readers,
  NVDA/braille/SAPI through UniversalSpeech, and keyboard focus handling.
  Shared across games already; audit per-game content and P3/P4 settings,
  rather than duplicating the shell. Existing shell completion gaps remain.
- F02 — Character highlight speech for active selection cursors, including
  alternate selection paths and repeated selection of the same character in
  a new team slot. `cvs2_select` uses native state, validates names against
  the current layout, settles changes, and retries rejected speech. Port
  actual highlighted choice, confirmation/cancel/re-entry, and player identity;
  shell roster order is not automatically native character-ID order.
- F03 — Groove selection speech. Six CVS2 grooves; per-game styles require
  their own choices and phase checks. No invented groove screen for games
  without one.
- F04 — Ratio allocation and individual level speech. CVS2 speaks team
  allocations and an alternate path's neutral “Level” label. The visible
  heading for that path is not fully verified. Port visible allocation or
  handicap controls only where they exist; do not copy CVS2's sum-to-four
  inference to another game's modes.
- F05 — Stage selection speech. Shared selection announced once. CVS2's
  alternate stage path is code-traced and fixture-covered, with live validation
  still needed according to the state trace. Distinguish selectable stages
  from a stage assigned by arcade progression.
- F06 — Match/team/style introduction and active-fighter change announcements.
  `cvs2_announce_observe` announces teams/grooves and changed fighters/ratios.
  Adapt to the title's team/order/switch system and speak during safe
  transitions. Current “You/Opponent” wording assumes P1; determine actual
  local ownership for P2, multiplayer and spectators. Audit retry behavior:
  the fighter-change path currently updates its latch before speech acceptance.
- F07 — Health sonification, two owners, current and maximum values. CVS2's
  frame hook feeds health changes, with worker-side gauge handling as well.
  Preserve audible health/range behavior, resets and HUD ownership. New games
  need observed maxima, including transformations and character/mode changes.
- F08 — Super/power sonification. CVS2 uses groove-specific caps and refuses
  unknown caps. Port the visible resource that serves this role; some titles
  need discrete stocks or transformation power rather than one continuous bar.
  Meter capacity, disabled states and spending/refill must be traced separately.
- F09 — Persistent fighter beacons with distinct player identities and
  screen-relative stereo position. Both fighters remain identifiable at the
  same pan. CVS2 uses a two-fighter camera approximation. Preserve the user
  purpose, replace projection for each title, and expand owner count for PWS2.
- F10 — Beacon stance/height: standing, crouching, airborne with rising/falling
  pitch, and knockdown. Current CVS2 action value 3 drives knockdown, backed
  by limited capture evidence. New adapters require independent grounded,
  airborne and down-state evidence; do not map arbitrary action IDs by analogy.
- F11 — Round-clock warning ticks at 30, 15, and each of the last five seconds,
  on decreases only. Not a tick on every second. Reset between rounds and
  suppress outside matches; adapt only to the title's visible clock and rules.
  A game without an equivalent active countdown gets no fabricated timer.
- F12 — Pause, menu, match-exit and session-reset behavior. Beacons stop on
  the CVS2 pause heuristic; gauges/beacons reset when leaving the match or
  invalidating a session. Preserve intent and explicitly test all cue families,
  device failure, lost focus policy, root replacement and save/load transitions.
- F13 — Cue controls and persistence. F5 health, F6 super/power, F7 beacons,
  F8 clock; Shift reverses the step. Levels wrap through 0/25/50/75/100,
  are spoken, and persist in `cfc2_audio.ini`. Shared per-family levels apply
  to all owners. Audio currently requires `cfc2_audio.on`; preserve this
  setting and `cfc2_hook.off`, and keep text readers independent of audio.
- F14 — Completed win/lose quote text, read from the actual chosen draw
  buffers, joined and settled before speech, once per visit, with retries.
  CVS2 currently rejects non-ASCII quotes. Fixture and native-buffer evidence
  exist; the quote document still lists automatic audible validation as pending.
  Port visible completed text, not a predicted character-to-quote lookup.
- F15 — Story, boss/interlude dialogue, endings and ceremony pages. CVS2
  captures the native renderer after name substitution and queues pages in
  order. CP932 conversion exists; multilingual endings and rare live paths
  are not validated. Port actual displayed pages and dialogue choices, where
  present; never narrate a stored script before it is shown.
- F16 — Credits. CVS2 follows an encoded glyph stream and consumed line
  boundaries. A retained real stream and replay tests cover decoding. Audible
  completion of a live ending/credits run remains pending. Each game's stream,
  glyph map and visibility boundary needs its own evidence.
- F17 — Automatic positioned prompts: start/coin/free-play, continue countdown,
  game over, survival/no-challenger labels, title/territory/copyright text.
  CVS2 captures formatted drawn records, suppresses blinking repeats and
  permits reappearance after absence. These are screen categories to audit
  in each game, not a claim every game has all of them.
- F18 — Read current text using the key left of 1. CVS2 includes current
  rendered font strings, positioned records and HUD names, in reading order;
  numeric records are available here without constant automatic narration.
  Empty frames replace old frames; snapshots older than 500 ms are refused.
  Shared registry supplies reasons for unavailable text. Port the same action
  and current-screen contract, with explicit ownership for overlays. Raster-only
  words and unverified glyphs remain gaps until decoded; do not claim OCR.
- F19 — Reliability mechanisms. Signature/build/ROM checks, guarded bounded
  copies, source/session invalidation, render-thread capture with no speech or
  blocking work, worker-thread speech, dedup policy per content type, and
  acceptance-based retries. These are required parity, not optional cleanup.
  Diagnostics and liveness counters should distinguish an inactive feature
  from a reader that failed or never ran.

`cvs2_hit`, archive scanners, snapshot/diff tools and optional emulator probes
are development aids. They may be adapted when a concrete investigation needs
them; enabling them for users is not part of feature parity. Automated play,
hidden opponent state, hitbox assistance and new combat commentary are not
implied by the CVS2 baseline.

## Applicability rules for all seven targets

Every target inherits F01, F02, F06, F07, F09, F12, F13, F18 and F19.
F08 and F10 preserve the same user purpose with title-specific resources and
physical states. F03–F05 and F11, F14–F17 require an explicit per-mode screen
audit: implement each visible equivalent, or record evidence for “not
applicable.” An undiscovered pointer or renderer means “unresolved,” never
“not applicable.” Stories/quotes/credits must be audited even if their path
requires finishing arcade mode. All inherited features are still pending
implementation/live validation for the seven games.

The profiles below cover each departure from that common baseline. The
[research source ledger](collection-port-research.md#external-checks) supplies
the official design facts; exact HUD and mode semantics remain capture tasks.

### CVS1 — Capcom vs. SNK: Millennium Fight 2000 Pro

- ROM indices 0/1: `cvs1`, `cvs1u`. Treat the collection's Pro edition as
  the target, not an arbitrary original CVS1 ROM.
- F03 applies to its own grooves. F04 adapts to its ratio/team-budget rules;
  do not import CVS2's freely assigned ratios. Team storage must allow four
  members for F02/F06 even though only two combatants occupy the screen.
- F05 and individual handicap screens require mode audit. F08 needs each
  groove's actual meter semantics and caps. F09/F10 require fresh camera,
  stage-boundary and stance evidence despite the similar presentation.
- F11 and F14–F17: audit clock, quotes, endings, credits and all native prompt
  categories. F18 includes actual drawn HUD names and text.
- Expected first pilot: close user-facing similarity and analogous native
  initializer structure. Shared offsets and function ABIs are not assumed.

### CFJ — Capcom Fighting Evolution

- ROM indices 4/5: `cfj`, `cfe`.
- F03 is combat-system information associated with the chosen character,
  unless tracing reveals a separate choice; it is not a six-groove selector.
  F04 CVS2 allocation is not a baseline requirement; audit real handicap UI.
- F02/F06 must cover team formation, active fighter and any between-round
  choice. F08 needs character/system-specific resource schemas and visible
  secondary meters where applicable. A uniform guessed maximum is inadequate.
- F05, F11 and F14–F17 need mode/text audit. F09/F10 can use the two-combatant
  interface once its camera and actor states are proved.

### Z3U — Street Fighter Alpha 3 Upper

- ROM indices 6/7/16/17: `z3u`, `a3u`, `z3uu`, `a3uu`.
- F03 adapts to Ism choice. F04 has no assumed CVS2 ratio equivalent;
  audit any additional native mode/level choices. F08 must distinguish
  Ism-dependent resource behavior and audit other visible HUD gauges.
- Trace both initializer branches and their revision/mode discriminator.
  A working ordinary path does not establish alternate-path support.
- F05/F11/F14–F17 require per-variant evidence; F09/F10 remain independently
  verified two-combatant position/stance outputs. Do not import CPS-2 Alpha 3
  addresses into Upper based solely on the franchise name.

### MJUS — Project Justice

- ROM indices 12/13: `mjus`, `pjus`.
- F02/F06 adapt to three-member teams, order and active actor; distinguish
  assist actors from persistent player slots. CVS2 grooves/ratio allocation
  do not transfer. Audit native team-related choices and level controls.
- F08 requires the title's visible power/resource rules. F09/F10 need camera
  and 3D actor semantics, not CVS2's fixed stage geometry.
- F14–F18 must include story progression and any visible choices as well as
  quotes, endings, credits and prompts that the screen audit finds.
- F05/F11 are conditional on the actual mode and displayed stage/clock UI.

### STG2 — Plasma Sword: Nightmare of Bilstein

- ROM indices 14/15: `stg2`, `pswd`.
- F02/F06 apply to character and match transitions; no CVS2 team/ratio/groove
  UI is presumed. Audit extra native choices rather than assigning those names.
- F08 adapts to its visible plasma resource and special-state presentation.
  F09/F10 require actual camera projection and movement/stance semantics.
- F14–F18 include the cutscene/story path, plus each discovered quote, ending,
  credit and prompt source. F05/F11 depend on actual stage/clock screens.
- The initializer's different arena placement makes it a useful check against
  assumptions accidentally retained in the shared adapter infrastructure.

### PWS1 — Power Stone

- ROM indices 8/9: `pws1`, `pws1u`.
- Character/arena choices and match introductions replace irrelevant groove,
  ratio and team selection. Audit F05 and any genuine additional choices.
- F08 adapts to visible stone ownership and transformation power/state.
  F09 must locate fighters in a 3D arena. Screen X alone does not communicate
  depth; the audio design needs a listening-tested additional spatial cue.
  F10 must not confuse arena depth with jump height.
- Locate visible stones/items and environmental information in the screen
  audit. Their accessibility is a title-specific extension required for
  useful arena navigation, not an already-existing CVS2 feature. Implement
  bounded, distinguishable cues without flooding speech during combat.
- Audit actual F11 clock behavior and F14–F18 text paths. Transformation,
  camera motion and arena transitions are mandatory reset/identity cases.

### PWS2 — Power Stone 2

- ROM indices 10/11: `pws2`, `pws2u`.
- Inherits PWS1's adaptations, but F02/F06–F10/F12/F13/F19 must support up to
  four active players. Team-slot capacity alone does not solve this.
- Require distinct P1–P4 identity, correct local ownership, inactive/eliminated
  slots, join/leave, HUD layout, concurrent damage/resources, and co-located
  fighters. Fixed P1-left/P2-right gauge routing cannot describe four owners.
- F09/F10 must handle changing arenas/cameras and distinguish ground-plane
  movement from height. Prototype density and masking with four real voices
  before treating the audio design as settled.
- Audit mode/arena selection, visible hazards/items, text/dialogue/results,
  continue/credit/ending paths and clock semantics independently. Neither
  PWS1 offsets nor its phase graph prove PWS2 support.

## Completion accounting

Track each F-number per game, ROM variant and relevant mode as: unresolved,
code-traced, captured, implemented, offline-tested, live-validated, or not
applicable with evidence. A partial release can list individual completed
features; a title is complete only when every applicable feature is validated
and every exclusion has a reason. Language limitations must remain explicit.
