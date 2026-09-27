# Collection port research — 2026-09-26

This is the technical evidence behind the [staged port plan](collection-port-plan.md).
It audits the current working tree, including the uncommitted CVS2 text work.
No new game adapter has been implemented or live-validated in this investigation.

Follow-up, 2026-09-27: the [bounded lifecycle trace](collection-lifecycle-trace.md)
extends all nine paths through root publication and update callbacks, maps all
18 ROM entries, and records cleanup edges. It corrects CFJ's context slot below.
Root lifetime and live transition checks remain open; no port is implemented.

## Reproduction and evidence boundaries

Run `python tools/collection_port_audit.py`. It reads the existing unpacked
image through `analyze_dump.py`, checks the reviewed dispatcher instruction
bytes, decodes the eight-entry jump table, and records each branch and a
bounded 0x300-byte excerpt at its initializer targets. Output is
`build/collection-port-audit.json`, including section hashes, instruction
bytes, resolved RIP-relative references, and excerpt hashes. No game launch,
process attachment, game-memory writes, or archive downloads are required.

The run completed for all eight games and nine initializer targets. This is
instruction evidence, not runtime verification. Excerpts can include adjacent
functions and do not constitute complete control-flow graphs. Follow branches
and actual function boundaries before interpreting later instructions.

The primary code section is `sec_00001000_code.bin`, 16,625,664 bytes, SHA-256:
`9c340b8ce40a0c15a3eccd3c47207b02a59e38365bf99ae7605afc26bb84f95b`.
All addresses below belong to this dump. Disabled ASLR does not make them
valid across executable updates. The report hashes every supplied section;
mutable data-section hashes identify this capture, not universal build IDs.

## A concrete starting point for every title

At `0x14004C540`, the shell reads canonical game identity from session `+8`.
At `0x14004C575`, it indexes eight image-relative branch addresses from
`0x14004C674`. The branches dispatch as follows:

- CVS1, game 0: branch `0x14004C582`, initializer `0x1405A5A40`.
- CVS2, game 1: branch `0x14004C59A`, initializer `0x1406A97D0`.
- CFJ, game 2: branch `0x14004C5B2`, initializer `0x1407CACA0`.
- Z3U, game 3: branch `0x14004C5CA`; session `+0x2B4 == 0` selects
  `0x14086BC20`, otherwise it calls `0x140DCB360`. The latter also sets a
  shell object flag. The follow-up traces zero to `z3u`/`a3u` and nonzero to
  `z3uu`/`a3uu`. The visible option label and hardware interpretation remain
  unresolved; see the lifecycle trace for the actual routing instructions.
- PWS1, game 4: branch `0x14004C604`, initializer `0x140972E50`.
- PWS2, game 5: branch `0x14004C61C`, initializer `0x140A46190`.
- MJUS, game 6: branch `0x14004C634`, initializer `0x140B926F0`.
- STG2, game 7: branch `0x14004C64C`, initializer `0x140CCB1C0`.

These reproduce the known CVS2 target as a positive control. The game names
come from the existing explicit mapping in `src/emu_ram.c`, not a new guess
about alphabetical order or `rom_index / 2`.

## Shared initialization structure, with meaningful exceptions

Let S be the session reached through `0x141C14F98`, and A its first pointer.
The initializers load A and store a context pointer at `0x142C79B40`, except
CFJ, which uses `0x142CF9D48` (corrected by the 2026-09-27 follow-up).

- CVS1, CVS2, CFJ, ordinary Z3U, PWS1, PWS2, and MJUS place that context
  at A+0x08000000. Their context `+8` is assigned A+0x08400000.
- STG2 and alternate Z3U instead place it at A+0x08B00000, with context
  `+8` assigned A+0x08F00000. Their context `+0x18` also differs:
  A+0x08D00000 rather than A+0x08200000.
- The context `+0x10` assignment depends on the value returned by
  `0x14003AE70`. PWS2 and MJUS use a multiply/shift/remainder-style sequence
  where CVS2 uses `and 0x3F`. Reusing CVS2's formula is unjustified.
- CFJ initializes the context differently at entry. Shared pointer slots
  do not establish identical state structures or original arcade hardware.

The cheap route is to follow each initializer into its state setup and update
functions. Candidate callees in analogous positions are:

- CVS1: `0x1405A6440`, then `0x1405A65F0`.
- CVS2 control: `0x1406AA320`, then known frame routine `0x1406AA7A0`.
- CFJ: `0x1407CB660`, `0x1407CB8B0`. Follow-up separates the adjacent
  `0x1407CC090` and `0x1407D3200` calls: they belong to the cleanup callback
  at `0x1407CB000`, not the initializer or update wrapper.
- Ordinary Z3U: `0x14086CFC0`, then `0x14086D160`.
- PWS1: `0x140973B10`, then `0x140974100`.
- PWS2: `0x140A46EB0`, then `0x140A47290`.
- MJUS: `0x140B930D0`, then `0x140B93500`.
- STG2: `0x140CCC940`, then `0x140CCCB60`.
- Alternate Z3U: the original excerpt reaches repeated calls to `0x140DCB250`.
  Follow-up reaches setup `0x140DCC280` and update `0x140DCC3F0` through the
  published update wrapper `0x140DCB970`.

The original audit established these as leads by call placement. The follow-up
now proves the setup calls, root publications and update-wrapper calls for all
nine paths. They are still not implemented state readers or safe hook sites.
Next resolve lifetime, complete argument ABI, phase dispatch and feature owners.

Two additional setup excerpts were checked with `disasm.py`: ordinary Z3U's
`0x14086CFC0` copies context `+0x10` into native slot `0x1422FB960`, clears
0x12F98 bytes and publishes the root/size in session `+0x1B0/+0x1B8`.
STG2's `0x140CCC940` similarly publishes native slot `0x142BE6370` and size
0x511B8. These are offline root-publication traces, not validated player
schemas. Reproduce with `python tools/disasm.py ADDRESS --before 0 --count 24`.
They strengthen the lead for a shared session publication interface while
showing why the CVS2 state size cannot be reused.

## Existing foundations and porting hazards

- `emu_ram.c` distinguishes absent, unreadable, and unverified sessions and
  maps all 18 ROM records. CVS1 index zero is valid. Z3U also occupies indices
  16 and 17. `rom_table_index_for()` still documents a provisional two-per-game
  formula; do not use it to route runtime adapters.
- `cvs2_select_tick()` combines identity checking, snapshots, selection,
  gauges, transitions, story, and text. Its non-CVS2/reset behavior must be
  moved behind active-adapter ownership before another game drives audio.
  Otherwise an inactive CVS2 poll could silence the active game's cues.
- `text_channel` already supplies bounded capture, conversion, FIFO,
  text/key deduplication, settling, backend acceptance, and the read-current
  registry. Twelve source slots fit eight game sources plus the shell.
  Register one aggregate source per game; do not register every text subreader.
- Registry order alone is insufficient to establish screen ownership.
  Explicitly test stale/empty game sources and the shell fallback, including
  pause overlays. An old accepted shell line must not masquerade as current
  game text. The current registry continues after empty/stale/refused results.
- `audio.c` and `audio.h` support two owners and two gauge types. Generalizing
  player count, HUD ownership, meter meaning and camera projection is required
  before PWS2 can have feature parity. Preserve P1/P2 sound identity.
- Read implementation before repeating historical comments: current audio
  uses eight 256-sample buffers (about 46 ms queued at 44.1 kHz), not the
  four-buffer figure retained in a header. Current knockdown handling exists
  in `cvs2_frame.c`, despite an older header saying it is not driven.
- The CVS2 pause heuristic watches position/timer/animation changes over 30
  callbacks. This is not a universal pause signal, especially for frozen
  training scenes, variable frame rates, loading or rollback.
- CVS2's fixed stage width and midpoint camera approximation are local
  assumptions. A 3D camera can rotate, zoom, and change perspective.
- `cvs2_hit` and `emu_cursor` are research probes, not accessibility features
  to enable in every game. The current snapshot tool retains historical
  field labels that conflict with the later health/position findings; correct
  labels before using it as the template for new capture tools.
- Do not revive the disproved claims that all collection text is sprite-only,
  that the emulator arena is guest RAM, or that guest addresses directly
  identify CFC2's native fields. The [prior-art audit](cvs2-prior-art-audit.md)
  documents these distinctions and the corrected NAOMI RAM size.

## External checks

Capcom's [collection FAQ](https://www.capcom-support.com/hc/en-us/articles/19966008291228-Capcom-Fighting-Collection-2-Frequently-Asked-Questions-All-Platforms)
confirms the eight titles and that Japanese and English ROMs can differ in
title and character names. Shell language and arcade ROM selection therefore
need separate test dimensions.

Capcom's [game spotlights](https://news.capcomusa.com/2025/05/09/capcom-fighting-collection-2-game-spotlights/)
establish relevant design differences: CVS1 supports teams of up to four;
Alpha 3 has selectable Isms; Fighting Evolution mixes character combat systems;
Project Justice uses three-member teams; Plasma Sword has story cutscenes;
Power Stone revolves around collecting stones and transforming; Power Stone 2
supports four-player arena battles with changing environments. These facts
motivate adaptations, not memory addresses or exact meter encodings.

The main collection site returned 403. The official support article's manual
link to `https://manual.capcom.com/cfc2/` returned 404. No unseen manual details
are treated as evidence. Exact meter names/ranges, optional modes and text
coverage should be confirmed from installed move lists/help, native code and
labelled captures. Existing Flycast/MAME research is a semantic comparison
resource; it is not a substitute for tracing this executable.

Follow-up hardware check: MAME registers original `sfa3` in its
[CPS-2 driver](https://github.com/mamedev/mame/blob/master/src/mame/capcom/cps2.cpp),
and `sfz3ugd` (Zero 3 Upper) with `naomigd` in its
[NAOMI driver](https://github.com/mamedev/mame/blob/master/src/mame/sega/naomi.cpp).
Its [System 246 driver](https://github.com/mamedev/mame/blob/master/src/mame/namco/namcops2.cpp)
registers `fghtjam`; the [ZN driver](https://github.com/mamedev/mame/blob/master/src/mame/sony/zn.cpp)
registers Plasma Sword/Star Gladiator 2 on `coh3002c`. Hardware provenance
guides research but does not identify CFC2's alternate Alpha 3 path or explain
the collection's native layouts. Every enabled path still requires tracing.
