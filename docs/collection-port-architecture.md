# Shared architecture for the collection ports

Design requirement, 2026-09-26: system changes must reach every game unless
there is a documented game-specific reason for different behavior. This is a
proposed implementation architecture; the existing runtime has not yet been
migrated. See the [delivery stages](collection-port-plan.md).

## Boundaries

The data flow is: CFC2 session identity → runtime access → game decoder →
normalized observations → shared accessibility systems → speech/audio.
The shell is another observation source and an overlay owner. Neither the
speech backend nor the mixer should know a game's native offsets or CPU type.

Keep three different identities:

- Original hardware and reference ROM revision: useful for choosing research
  tools, endian rules and semantic references.
- CFC2 executable/build, archive/ROM identity, and selected runtime branch:
  determines which host access method is valid.
- Game rules and visible content: determines how decoded values become
  meaningful choices, meters, teams, positions and text.

Original Alpha 3 is CPS-2, whereas Zero 3 Upper is NAOMI. CVS1 Pro, CVS2,
Power Stone, Power Stone 2 and Project Justice are in MAME's NAOMI driver.
Fighting Evolution/Jam is System 246; Plasma Sword is ZN-2. These distinctions
invalidate the old blanket “CPS-2/NAOMI collection” description. They do not
prove CFC2 runs the same CPUs or exposes original RAM layouts. Primary
references: [CPS-2](https://github.com/mamedev/mame/blob/master/src/mame/capcom/cps2.cpp),
[NAOMI](https://github.com/mamedev/mame/blob/master/src/mame/sega/naomi.cpp),
[System 246](https://github.com/mamedev/mame/blob/master/src/mame/namco/namcops2.cpp),
[ZN](https://github.com/mamedev/mame/blob/master/src/mame/sony/zn.cpp).
The [lifecycle trace](collection-lifecycle-trace.md) now associates CFC2's
zero/nonzero discriminator with `z3u`/`a3u` and `z3uu`/`a3uu`, respectively.
This does not establish their hardware implementation or guest memory layout.

## Common systems: one implementation

- Session/router: owns active game/ROM/build identity, activation, hook
  readiness, monotonic session epochs, departure/reset and source ownership.
  A game switch invalidates old captures and silence/reset events before a new
  adapter can publish. A root address alone is not an epoch: allocators reuse
  addresses and quick-load can replace content without replacing the root.
- Capture transport: bounded immutable snapshots, queues and frame publication;
  fault guards and explicit unreadable/unsupported/stale statuses. No retained
  raw game pointers in worker events. Render hooks must pass the full original
  ABI, avoid blocking/logging/speech/allocation, and publish only copied data.
- Text/speech: evolve `text_channel` as the single delivery mechanism. Own
  encoding helpers, retry-on-acceptance, bounded queues, freshness, reading
  order, re-entry, arbitration and read-current outcomes. Dedup strategies
  remain selectable shared policies: latest choice, blinking text, ordered
  story, settled quote. Games select/configure policies; they do not reimplement
  suppression, queues or backend acceptance.
- Selection/transition presentation: a common presenter formats player,
  choice, team, style, ratio/level and match-transition observations. Game
  decoders supply names and semantics; localization and “You” ownership are
  resolved centrally. Unsupported ownership uses truthful player labels.
- Audio semantics: one controller turns normalized health/resource/position/
  stance/clock observations into cues, tracks edges, applies reset/pause
  policy and honors settings. The existing mixer remains the sole DSP/device
  implementation. No separate mixer or volume persistence in each port.
- Controls/settings: one hotkey dispatcher and one persisted schema, preserving
  F5–F8 and read-current-text. Defaults apply collection-wide. Optional per-game
  overrides must explicitly fall back to global settings; avoid seven diverging
  copies of the INI logic. New resource families need clear spoken labels.
- Diagnostics: common capability/status reporting, bounded counters and
  worker-side logs. Diagnostic failure is distinct from “no game running.”
- Contract/replay tests: every adapter feeds the same presentation and lifecycle
  tests. A shared fix must run against every registered adapter's fixtures.

Example: changing speech retries changes `text_channel` once; every game's
story, quote and selection producer uses it. Changing beacon smoothing changes
the shared audio controller/mixer once; each game continues supplying positions.
Changing CVS1's native character-ID map stays in its decoder.

## Runtime backends and game adapters

Use a small C interface and static descriptor registry in the existing build;
no new dynamic plugin loader is needed. Proposed modules are `game_session`,
`game_adapter`, `game_observation`, `game_present` and `game_audio`, alongside
the existing `text_channel`, `audio`, `speech` and shell modules.

A runtime backend is responsible for validated memory access, address spaces,
pointer lifetime and capture sites. A native CFC2 backend may share proven
session/context access. If a variant genuinely exposes CPS-2 guest RAM or
another guest address space, its backend supplies bounded typed reads with
explicit endianness and translation. Implement such a backend only after
tracing proves it is needed. Do not introduce a pretend universal RAM base.

A game adapter declares:

- Supported exact ROM identities and build/signature evidence; original
  hardware as metadata, independently of runtime backend selection.
- Lifecycle operations: probe/validate, activate, capture/poll, decode,
  deactivate; explicit error results. Install hooks only after unpacking and
  signature/ABI validation. Keep capture disabled for inactive identities.
- Capabilities: selection kinds, team capacity, active-player capacity,
  visible meter types, spatial dimensions, text categories and timer semantics.
  Separate support from availability in the current mode/frame.
- Native phase and player mappings, character names, resource range/stock
  rules, camera transforms, visible-text completion/encoding and glyph maps.
- Evidence-backed policy parameters or named extensions, with a reason and
  fixtures for each departure from the defaults.

Adapter code must not call `speech_say`, persist settings, synthesize sound,
poll hotkeys or reset another adapter's output. It submits copied observations
to the shared systems. Keep any bypass needed during CVS2 migration temporary,
listed, and removed before declaring the extraction complete.

## Normalized observation contract

Every observation carries adapter/ROM identity, session epoch, capture sequence,
capture time and validity. Frame-consistent values travel together; do not
combine current health with a previous frame's player identity or camera.

- Phase: title/select/intro/combat/round-end/result/story/credits plus
  independently known pause and shell-overlay states. Unknown remains unknown.
  An adapter may retain native substate internally without exposing magic bytes.
- Participants: up to four active player slots; separate team-member capacity,
  local-owner mapping, team identity, active actor and slot presence. An assist
  actor is not automatically another player. Inactive slots are explicit.
- Selection: owner, kind, selection-step/visit identity, highlighted semantic
  value and display label, confirmed/cancelled state where known. A highlight
  change replaces an obsolete pending highlight; story FIFO never does.
- Gauges: owner, semantic resource kind, current/max, units or discrete stocks,
  validity, visibility and resource generation. Unknown max is unavailable,
  not zero. Character/transformation/mode changes reset comparison history.
- Spatial state: normalized screen position, optional ground-plane relation,
  height, facing and evidenced stance, each with validity. Adapter projection
  uses the current camera. The common audio system owns how those values sound.
- Clock: visible remaining value, units/count direction, running/disabled state
  and round identity. Adapter decodes; common code detects warning crossings
  according to a documented policy. Never infer a timer from an arbitrary counter.
- Text: source category, screen/page/line identity, copied content and encoding
  or decoded Unicode, order/position, visibility/completion and delivery policy.
  Stock text is not current text without an observed display event.

Use bounded arrays and explicit overflow/error status. A malformed owner, NaN
position, invalid resource range, truncated text or stale epoch cannot silently
become a valid zero. Partial valid observations may still reach independent
features; one unverified meter must not disable character-select speech.

## Ownership, concurrency and failure

Only the router grants the active output generation. Producers from an old
generation cannot publish audible state. Publication uses nonblocking bounded
capture and a validated snapshot/queue handoff; worker work drains within a
budget. Invalidation includes queued story text, settled quotes, selection
latches, clock edges and all active audio voices.

Shell menus and pause overlays must explicitly own their visible text; when
gameplay owns the screen, stale shell history cannot answer read-current-text.
The registry remains the common user action, with at most one aggregate source
per game plus the shell. An active-but-empty source is not the same as inactive.
Determine arbitration before registering the seven additional sources.

Text readers operate if audio is disabled/unavailable. A failed optional hook
disables and diagnoses its capability; it cannot leave half-initialized state
that looks valid. A failed identity/build check disables the affected adapter,
preserves shared shell access and clears stale output. Settings and diagnostic
hotkeys remain responsive. Rejected speech does not advance semantic state.

For quick-load/rollback, identify discontinuities without treating normal
render repeats as new sessions. Real-time audio follows the accepted current
snapshot; discrete speech needs event/visit identity to avoid duplicate replay.
Backend speech already accepted may require interruption on departure; clearing
an internal queue alone does not clear the screen reader's queue. Test this.

The [shared snapshot trace](collection-snapshot-trace.md) now proves that
in-place restoration can bypass the per-game restore routines and invoke the
same native update repeatedly. Keep owner generation, state revision and
eligible publication sequence separate. S+0x770 brackets replay and also
marks intermediate shell updates; polling it is not synchronization. A
revision change invalidates old observations without automatically re-arming
accepted speech. The outer completion and thread contract remain unproved.

## Rules for game-specific exceptions

An exception must name the differing visible behavior or technical contract,
the affected feature/ROM/mode, evidence, override location and regression case.
Prefer descriptor data, then a narrowly typed strategy callback. Keep common
behavior out of callbacks. No generic presenter branch on `game == CVS1` when
a resource/style descriptor expresses the same distinction.

Legitimate examples: native byte decoding, sprite glyph maps, Ism-dependent
resource caps, title-specific camera projection, or an actual dialogue-choice
screen. Different retry logic, volume file handling, session invalidation or
copy-pasted beacon synthesis are not justified by a different ROM architecture.

## Migration without losing CVS2 behavior

Extract incrementally and replay CVS2 after each boundary move. First centralize
ownership/session handling, then normalized observations and presentation,
then audio feeds. Keep addresses/phase decoders in CVS2. Preserve output order,
cue identity, timing policy and controls; document deliberate bug fixes
separately. Add CVS1 as the second consumer to demonstrate that the interfaces
are actually reusable before filling out all seven adapters.

Prove propagation with one shared retry test exercised through both adapters,
one shared setting affecting both, and game-switch tests that reject stale
CVS2 events while CVS1 owns audio. Later run the same contracts for every title,
including four-player synthetic observations before PWS2 runtime integration.
