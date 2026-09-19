CVS2 state-access trace — 2026-09-18

Status: CVS2 pointer chain verified live; character highlight traced to its
writer and checked against user-identified Ryu and Yun captures. These addresses apply to the existing
unpacked executable dump, not automatically to other builds.

**Prior-art boundary**

After the source review, a targeted read of session
`71559caa-11f0-4af8-8ce5-5f81511fd6cf.jsonl` recovered the tool-result text
containing `Port Files Changes Notes.txt`, `Header Data.txt`, and the short
GBIX/PVRT mapping note from the missing Z3U WIP archive. These concern Alpha 3
Upper, not CVS2. The final forum page again failed retrieval. No claim is made
that missing material proves anything about CVS2. The following findings are
independently traced from CFC2's CVS2 branch.

**Game identity and entry point**

`0x14004C540` reads session field `+8`, bounds it to 0..7, and dispatches
through eight image-relative entries at `0x14004C674`. Entry 1 goes to
`0x14004C59A`, which tail-jumps at `0x14004C5AD` to `0x1406A97D0`.
The existing descriptor/name mapping identifies canonical game 1 as CVS2;
ROM indices 2/3 are CVS2/CVS2U. Keep those two index spaces separate.

**Concrete pointer relationships**

Let `S = *(u64*)0x141C14F98`, `A = *(u64*)S`.
The CVS2 initializer at `0x1406A97D0` establishes:

- `C = *(u64*)0x142C79B40 = A + 0x08000000`.
- `C+0` contains `A`, the archive base.
- `C+8` contains `A + 0x08400000`; initialization fills 32 MiB there.
- `C+0x18` contains `A + 0x08200000`.
- `C+0x10` contains `A + ((0xC0 + (r & 0x3F)) << 20)`.
  Here `r` is returned by `0x14003AE70`, which tail-calls the shift/XOR
  state generator at `0x1400BD7D0`. Thus this pointer is not a fixed arena
  offset. The initializer fills the arena's last 64 MiB separately.

The initializer calls `0x1406AA320`. That routine copies `C+0x10` to
`*(u64*)0x1422DEBD0`, clears `0x216B0` bytes there, and publishes that
pointer and size in session fields `+0x1B0`/`+0x1B8`.
It also creates another block immediately following, of size `0x216B8`,
published in session fields `+0x1C0`/`+0x1C8`; its purpose is not yet proven.

Call the first block `R`. The routine sets
`*(u64*)0x1422DEC30 = R + 0x78`. These are actual pointer stores, not
candidate values selected because they look like health or cursor coordinates.

**Guest-address translation exists for some data**

`0x1406B0060` converts an incoming address into a host pointer using
`*(u64*)(C+8) + address - 0x0C000000` before dereferencing it.
`0x1406ADF10` uses the same relationship for resource records.
The queue-processing code at `0x1406AA8F1..0x1406AA91D` similarly subtracts
`0x0C000000`, adds `C+8`, and copies archive data to that destination.

This establishes the translation for those data accesses. It does not prove
that original NAOMI gameplay addresses retain their meanings in that region.
The separate `R` block must be investigated on its own terms.

**Actual state updates and dispatch**

The CVS2 update function at `0x1406AA7A0` increments a counter at
`*(u64*)0x1422DEC30 + 0x18`, then updates two input records in `R`, stride
`0x14`: current bits at `R+0x26`, previous at `+0x28`, pressed at `+0x2A`,
released at `+0x2C`, with a separate repeat path. This is useful for following
the game's own consumers. The accessibility reader must not count these edges.

The update calls `0x1406B1CA0`. That function reads byte zero of the structure
pointed to by `0x1422DEC30` and calls through the table at `0x141282A20`.
The first seven entries point to:

- 0: `0x1406B1FF0`.
- 1: `0x1406B9BA0`.
- 2: `0x1406BA870`.
- 3: `0x1406B2260`.
- 4: `0x1406B2010`.
- 5: `0x1406B22D0`.
- 6: `0x1406B20D0`.

Several handlers dispatch again using following bytes. This establishes a
state-machine structure, but screen names for those numeric states remain
unverified. Do not call a byte 'character select' merely from its value.

**Prepared live check**

`tools/cvs2_state_snapshot.py` uses OpenProcess with read/query rights and
ReadProcessMemory. It checks an instruction signature, CVS2 game/ROM identity,
and the pointer relationships above before reading the bounded `R` block.
It writes raw bytes and pointer metadata to new workspace files. It neither
injects code nor writes game memory, and it does not announce guessed names.
Reads are not atomic; it rechecks the root pointer to detect some transitions.

Python compilation and help invocation passed. Live captures now exist under
`build/cvs2-state/`, including a no-cursor baseline, two Ryu readings, and
`moved-03`, subsequently identified by the user as Yun.

**Live evidence and its limits**

- `no-cursor-01`: PID 16656, R `0x13E31000`, phase `0102030404030100`.
- `ryu-01` and `ryu-idle-02`: PID 24788, R `0x16571000`, phase
  `0201010100000000`. Both have P1 character ID 0, logical cell (1,5),
  cursor coordinates (96,224).
- `moved-03`: PID 24788, R `0x14471000`, same phase. P1 ID 44, logical
  cell (3,5), coordinates (140,224). User subsequently confirmed Yun.

The user reported restarting and returning to selection after a timeout.
These are separate visits, NOT a verified single Ryu-to-Yun movement. Metadata
has the same PID for the latter captures; do not infer an OS process restart
between those two files. The state block nevertheless relocated, and the
pointer chain resolved it successfully. Ryu's fields were unchanged over the
idle comparison. The name order in the previously reviewed Flycast script
also places Ryu at zero and Yun at 44; that is corroboration, not a substitute
for the live labels. No other character name has been live-verified here.

**Character-selection dispatch and exact highlight write**

Let `P = R+0x78`. In the observed captures, phase bytes 2,1,1,1 dispatch:

- `0x1406B1CA0` -> table `0x141282A20`, index P[0] = 2 -> `0x1406BA870`.
- That reads P[1] = 1 -> `0x1406BABA0`, which reads P[2] = 1 -> `0x1406BBE70`.
- P[0x128] = 0 selects `0x1406FA760`; P[3] = 1 selects `0x1406FDF00`
  through `0x141294888`.
- `0x1406FDF00` checks P[0x104] against R[0x1FFEC], then uses
  R[0x1FFD0] = 7 in table `0x1412948A0`, reaching `0x1406FB870`.
- With R[0x1FFD8] <= 2, its loop at `0x1406FB9C5` passes player structures
  `B = R+0x1EBA8+player*0x678` (player 0 or 1). It dispatches on
  R[0x1FFD4+player] through `0x141294920`. Value 1 selects `0x1406FBFB0`.

`0x1406FBFB0` skips when R[0x1FFE4+player] is nonzero. It calls
`0x1406FA780` at `0x1406FC009`. The callee reads the player's current input
record through B+0x588 and updates signed dword cursor coordinates:

- X: R+0x20098+player*8.
- Y: R+0x2009C+player*8.

It resolves coordinates through the game's own cell/type data. The caller
validates the result and updates logical column/row bytes at
R+0x1FFDE+player*2 and R+0x1FFDF+player*2. The final normal-cell lookup is
the byte at `R+0x1FFF0+row*16+column`. At **0x1406FC1E8** the routine writes
that resolved ID to **B+1**. Thus P1's highlight byte is **R+0x1EBA9**,
and the corresponding P2 byte is **R+0x1F221**. The P2 address is code-derived;
active P2 selection has not been live-validated.

Special cells 0x30..0x33 take additional paths, including another B+1 write
at `0x1406FC330`. Do not announce those as ordinary character IDs or assume
the grid ID always equals the displayed random preview. Team-confirmation
fields around B+0x5C0 are separate from this highlight field.

**Diagnostic reader and remaining work**

Additional row check: the user reported moving one up from Ryu.
`up-from-ryu-04` and `up-from-ryu-idle-05` (PID 27160, R `0x147E1000`)
both decode successfully, with stored and grid character IDs both 9,
logical cell (2,4), and coordinates (113,272). This differs from the earlier
Ryu cell (1,5), consistent with crossing a diamond-grid row boundary.
The prior Flycast CHARACTER list maps zero-based ID 9 to Sagat; the user has
not yet independently named this highlight. There is no same-session Ryu
baseline for this process, so a return to Ryu remains the next check.
The two new captures have identical decoded results, including the active
selection guards. No new character-name mapping was added to the reader.

Return attempt `return-ryu-06`: after the user reported returning, the capture
retained PID 27160 and R `0x147E1000`, but phase bytes were
`0201020102000200`. The decoder correctly returned `unsupported_or_inactive`.
This does not validate a return to Ryu: the observed character-highlight
dispatch path was no longer active when read. Whether a timer or another
transition caused the change is not established by this capture.

User-labeled Sagat capture `sagat-07`: PID 27160, relocated R `0x161E1000`,
phase `0201010100000000`. Both highlight and grid ID are 9, logical cell
(2,4), coordinates (105,264). The active-selection checks pass. This verifies
the Sagat name mapping independently of the earlier directional description;
the diagnostic now names IDs 0, 9 and 44 as Ryu, Sagat and Yun. Different
coordinates within the same logical cell still resolve to Sagat. The user
also confirmed that the preceding return attempt had left character select.

Successful return `ryu-return-08`: same PID 27160 and R `0x161E1000` as
`sagat-07`, with active phase `0201010100000000`. The user identified Ryu;
both stored and grid IDs are 0, logical cell (1,5), coordinates (105,228).
The diagnostic reports Ryu with all guards passing. This completes the
same-session Sagat-to-Ryu row-return check. The intermediate motion itself
was not continuously recorded; these are validated endpoint snapshots.

`tools/cvs2_selection_decode.py` decodes a saved snapshot without any input
history. It restricts output to the observed dispatch path, checks the active
player bit and player index, bounds the logical grid, rejects special cells,
and requires the grid ID to match B+1. Names currently cover only the three
user-verified IDs. Inactive P2 and the no-cursor capture are rejected.

The Python tool remains a research diagnostic. Reads are not
atomic. Other selection modes, P2, random cells, confirmation transitions,
groove, ratio and stage still need their own tracing and validation. The next
character checks now include another row and a return to a known character;
confirmation transitions still need validation beyond rejecting an inactive capture.
Do not infer that a character-select success proves every menu works.

**Speech integration**

`src/cvs2_select.c` now implements the guarded reader in the mod. The existing
20 ms speech-worker loop calls it; it uses `speech_say` and the existing
UniversalSpeech backend. It does not add a hook or listen for input. Two
consecutive valid readings of the same ID are required before announcement;
accepted announcements are deduplicated until selection changes or ends.
A changed state-block pointer resets that history. Failed speech calls remain
eligible for retry. Inconsistent readings and random/special cells are silent.

The 48 normal-character names follow the reviewed Flycast ID list, with
unambiguous Claw/Boxer/Dictator regional labels. Only Ryu, Sagat and Yun have
been checked against user-named live highlights. Other select modes and active
P2 still need live checks; the reader restricts itself to the traced substate.

`tools/run_cvs2_test.ps1` compiles the shipping module against a mock speech
backend and replays local captures. It passes ID/name checks, idle suppression,
exit/reentry, transient mismatch, special-cell rejection, failed-speech retry,
and pointer-relocation cases. It requires the captures in `build/cvs2-state`.
The full DLL build passes with existing warnings in other modules. Audible
in-game validation remains pending after restarting the mod.

Installed the built DLL after the user closed CFC2. Source and installed
SHA-256 match:
`64315996d74c67aacf06dcb0cb96662539436c25d88c5c6abd1373c52bebbc75`.
The previous DLL is backed up at
`build/dinput8-before-cvs2-20260918-165614.dll`.

**Three-member teams and P2 correction**

The user reported that speech worked for the first team pick and stopped after
confirmation. The original guard required R[0x1FFD4+player] == 1. This was
too narrow: dispatch table `0x141294920` has the identical highlight handler
`0x1406FBFB0` at indices **1, 4 and 7**, corresponding to the three picks.
Indices 2/5/8 run `0x1406FBB00`; 3/6/9 run `0x1406FCAC0`.

The confirmation path at `0x1406FC57A` increments the player's substate and
at `0x1406FC5AD` sets the per-player busy flag R[0x1FFE4+player] to 1.
`0x1406FBB00` increments the selected-count byte at R+0x1FFD6+player,
advances the substate after the delay (or goes to 10 after three picks), and
sets busy to 2. `0x1406FCAC0` completes another delay, increments the substate
again, and clears busy. Thus 1 -> 2 -> 3 -> 4 and 4 -> 5 -> 6 -> 7 resume
the SAME cursor handler. Completed teams and animation states remain excluded.

The dispatcher loop `0x1406FB9C5..0x1406FBA39` passes both player structures,
advancing by 0x678 and stopping at 0xCF0. The handler indexes cursor state,
busy flags, and substates using each structure's signed player index at
B+0x5EC. P2 therefore uses the same decoding logic with its own fields;
no separate cursor search or assumed movement ordering is needed.

The C and Python readers now accept only substates 1/4/7. Speech resets its
per-player deduplication when the team slot changes, including when polling
misses the intervening animation. P2 announcements identify the player even
when P1 has finished. Simultaneous changes use one combined utterance, avoiding
one announcement interrupting the other or queuing stale P2 positions.

Expanded offline tests reproduce the old guard failure before the fix and pass
afterward. Instruction-derived fixtures cover all three slots for both players,
reject substates 0/2/3/5/6/8/9/10/11/12, verify independent busy guards, exercise
P2-only and simultaneous speech, and verify repeated highlights in a new slot.
These are synthetic variants of captured state, not new live P2/team captures.
No additional user run was required for this diagnosis; in-game validation of
the corrected later picks and active P2 remains unperformed.

**Groove selection**

User-labeled C Groove capture `c-groove-09`: PID 13904, R `0x15CA1000`,
phase bytes `0201010100000000`, R[0x1FFD0]=2, P1 substate 1 and P2
substate 3. The active-player mask is 1. P1's B+0x5D0 is zero.

The existing phase dispatch reaches `0x1406FDF00`, whose selection substate
2 calls `0x1406FAAC0`. That function loops over both player structures at
stride 0x678 and uses table `0x141294900`. Per-player substate 1 selects
`0x1406FBD80`. It reads B+0x5D0 at `0x1406FBD90`, applies the player's
input-repeat/pressed bits, and writes the resulting selection at
`0x1406FBDB4`. Normal mode wraps at six; the flag at P+0x139 enables an
eight-entry path. The extra two entries remain unnamed and silent.

On confirmation or timer expiry, `0x1406FBE8D` increments the player's
substate, exiting the input handler. Therefore the reader requires global
selection substate 2, player substate 1, the active-player bit and correct
player index. It does NOT reuse character select's busy-flag guard, since the
groove handler does not use that guard. The hover fields are:

- P1: R+0x1F178.
- P2: R+0x1F7F0.

The reviewed Flycast list supplies the normal order C/A/P/S/N/K; the live
C capture independently verifies ID 0. `cvs2_groove_decode` and the existing
speech observer now announce normal groove changes through `speech_say`.
Tests use the real C capture plus explicitly synthetic changes for the other
values, both players, confirmation, and custom-value rejection. These changes
are prepared for the combined build, not yet installed or audibly verified.

**Ratio selection**

The first user-reported ratio capture (`ratio-10`) landed in a character
confirmation transition. The user said the screen had only appeared briefly
and returned to it. `ratio-12` is the later baseline: PID 13904,
R `0x13DA1000`, phase `0201010100000000`, global selection substate 10,
R[0x1FFD2]=1, P1 substate 2, P2 substate 4, P1 team size 3 and choice 0.
P1's team bytes B+0x5C0..2 are 0,1,44 (Ryu, Ken, Yun).

Substate 10 dispatches to `0x1406FD200`, which calls `0x140735950` until
R[0x1FFE8] reaches 0x30. For R[0x1FFD2]=1, the latter loops over both
player structures and dispatches through `0x1412A0230`. Per-player substate 2
selects `0x140735CE0`. It changes **R[0x1FFE6+player]** in response to
left/right and normalizes it modulo three at `0x140735DFE..0x140735E17`.
Confirm or zero countdown increments the player's substate to 3.

The next handler, `0x140735B30`, commits three ratio bytes to B+0x5CC..0x5CE.
It indexes the executable table at `0x1412A0210` with
`3 * (3 * (team_size - 1) + choice)`, where team_size is B+0x5D1.
The 27 table bytes group as:

- One member: (3,255,255) for all three choices (input selection skipped).
- Two members: (1,1,255), (2,0,255), (0,2,255).
- Three members: (1,0,0), (0,1,0), (0,0,1).

Ratios are zero-based; the reviewed Flycast display also adds one. Thus the
three-member choices display 2/1/1, 1/2/1, 1/1/2; two-member choices display
2/2, 3/1, 1/3. The capture is the Ryu 2 / Ken 1 / Yun 1 option. The user has
identified the screen, not independently confirmed these individual numbers.

`cvs2_ratio_decode` requires this dispatch path, active player, valid player
index, nonzero countdown, valid team size/IDs and choice 0..2. Speech names
each team member and its prospective ratio, reading the current team and
choice rather than counting inputs. Offline tests cover the real baseline,
synthetic two/three-member choices for both players, and confirmation rejection.
Groove, ratio and corrected character/team readers build together; deployment
and audible verification of these additions are still pending. Stage selection
has not yet been located; do not confuse stage assignment with a hover cursor.

**Stage-selection trace (subsequent finding)**

The user clarified that Versus/Training are launched from the collection shell,
but their selection screens are in the arcade game. The native CVS2 alternate
selection route agrees: `0x1406BBE70` selects `0x1406F38D0` when P+0x128 is
nonzero. P[3]=1 dispatches via `0x141294480`; R[0x1FFD0]=2 selects
`0x1406F4AC0`. That routine dispatches participating players through a table
selected by R[0x1FFEF], using the per-player R[0x1FFD4+player] substate.

The pointer table at `0x141294400` identifies stage handler `0x1406F33F0`
at player substate 0 for selector values 1 and 3. Do not assign those two
selector values to named modes without a live mode capture. Its inner table
at `0x1406F379C` makes B[4]=3 the interactive path. It reads repeat/pressed
input and updates **R+0x64C**, wrapping through 12 entries and skipping locked
ones. Writes occur at `0x1406F36AC` / `0x1406F36FC`; confirmation stores the
choice to the shell-facing state +0x68C at `0x1406F355B`, advances B[4], and
sets the player's busy flag. Choice 11 is explicitly randomized modulo 11 at
`0x1406F35B7..0x1406F35D7`, so it must be spoken as Random while highlighted.

This is a SHARED stage choice, not separate P1/P2 stage values. B+0x420 points
to the other player structure in all checked captures. The handler waits or
stops accepting input when the other player's B[4] has advanced. The reader
requires the alternate selection route, selector 1/3, active participant,
player substate 0, B[4]=3, peer B[4]<=3, nonbusy flag, and nonzero selection
timer. It announces the shared choice once even if both players participate.

The reviewed Flycast stage list supplies IDs 0..10: Aomori, Shanghai, Nairobi,
Kinderdijk, London, Barentsburg, New York, Osaka, Osaka Tower Rooftop,
Osaka In Flames, Training. ID 11 is Random by native-code evidence. Stage
names and activation have NOT been live-validated; tests use instruction-derived
fixtures for both participants, boundaries, mode/confirmation guards and shared
speech. The custom route also has separate character-selection handlers; this
build does not claim that the Arcade character/groove/ratio reader covers them.

The combined build contains corrected Arcade character/team/P2 selection,
normal grooves, ratio allocations, and the traced alternate stage path. All
offline regressions pass. Installation and the combined audible test remain
pending; stage confidence is code-derived, unlike the user-labeled character,
C Groove, and ratio-screen captures.

Combined build installed after detecting CFC2 was closed. Installed/source
SHA-256 match:
`05844c591bb9638d09ecaeb5471e00a8ae66eb325b4d7e2f1f4dfaa65ca2e482`.
Previous installed DLL retained as
`build/dinput8-before-combined-20260918-225716.dll`.
The next live run can test the additions together; no audible result is
claimed from the offline tests or installation verification.

**Alternate character/groove coverage and shorter speech**

User feedback after that build: stage speech works, and Arcade selection
speech works; Versus/Training groove and character screens remain silent.
The latter were explicitly excluded by P+0x128 != 0 in the Arcade decoder.
The user also requested removal of player-number prefixes to save time.

The six per-mode tables referenced by `0x141294400` select groove handler
`0x1406F27B0` at substates 0/1/0/1/absent/1 and character handler
`0x1406F2AB0` at 1/2/1/2/0/2 respectively. Both handlers use B[4]=1 for
interaction, clear their busy flag on entry and advance B[4] on confirmation.
Groove calls `0x1406F6190` from `0x1406F2A9C`, which navigates allowed grooves
and updates B+0x5D0. The same ordinary C/A/P/S/N/K IDs apply.

Character calls `0x1406F55D0`, whose final lookup at `0x1406F60C6` uses a
12-column grid, distinct from Arcade's 16-column lookup. It publishes the cell
ID to R+0x1FFD6+player at `0x1406F60F5`, and the normal ID to B+1 at
`0x1406F6143`. Random previews are handled separately by `0x1406F3B80`.
The new alternate decoder requires matching ordinary IDs in both fields;
it does not reuse the Arcade grid offset or speak random previews.

Activation requires the alternate parent route, global substate 2, the
table-derived handler substate, participant bit at R+0x1FFEC, B[4]=1,
correct player index, nonbusy flag and nonzero timer. Team-slot deduplication
also includes B+0x5D1 because this route repeats the character handler for
successive picks. All spoken Player 1/2 prefixes are removed, including ratio
announcements. Internal per-player tracking and diagnostics remain separate.

Expanded offline tests pass for both players, alternate groove-to-character
transitions, repeat team picks, confirmation/busy guards, mismatched cell IDs,
and prefix-free speech, alongside the previous Arcade/ratio/stage regressions.
Alternate-mode fixes still require audible verification after installation.

Installed with matching source/destination SHA-256
`2a74373f1839b2af869c438b2eb069d240bd0847c1aa484ec3e11901c40a20f1`.
Backup: `build/dinput8-before-alternate-20260918-235245.dll`.

Dump fingerprints (SHA-256):

- `sec_00001000_code.bin`:
  `9c340b8ce40a0c15a3eccd3c47207b02a59e38365bf99ae7605afc26bb84f95b`.
- `sec_011d6000_data.bin`:
  `a12f6a815677755c8ef7762f30d93a1c50dac792e48176c3dead68945d0720d5`.

**In-match gauges: super meter traced, health NOT located (2026-09-19)**

Five live captures in one CVS2 Arcade session (PID 30500, R `0x15BA1000`,
same pointer chain): `hp-full` (match start), `hp-hurt`, `hp-low`, `hp-zero`
(user at zero health), `hp-ken` (P1 second team member, damaged). Dump
fingerprints match the audited build, so traced addresses apply.

The match runs under phase bytes `02 01 02 02 ...`; byte 4 distinguishes
substates (3 = intro, 4 = in-round, 5 = KO/round-end). `hp-zero` is substate 5
and is NOT comparable with in-round captures: P1's meter reads `0xFFFF` there,
a sentinel, not a value. Only same-substate captures may be diffed.

Match setup is `0x1406B9F40`, reached from phase handler `0x1406B9BA0` through
subtable `0x14128CCD8`. It initializes the SAME player structures the selection
reader uses: `B = R+0x1EBA8 + player*0x678`, loop `add rsi,0x678` / `cmp edi,2`
at `0x1406BA079`. It cross-links them at `R+0x1EFC8` and `R+0x1F640`, and
carries the chosen groove into `B+0x5D0`. So in-match state shares the selection
block; no new pointer chain is needed.

SUPER METER (traced to its writers, then confirmed behaviourally):

- `B+0x16C` (u16) -- live meter. Gain at `0x1406C26A0`, `imul edx,eax,0x64`,
  i.e. fixed units of 100, clamped to the per-character cap at `B+0x5BC`
  (`cmovg` at `0x1406C26E9`). Spend at `0x1406C4241`.
- `R+0x20F20 + player*2` (u16) -- banked stocks, clamped to `0x7FFF`.
  Also zeroed by the hit handler at `0x1406C0881`.
- `R+0x20F1C + player*2` (u16) -- per-level cost, added to stocks on spend.

Across the five captures P1's meter read 11520 / 4260 / 52 / --- / 6098 while
P2's held at 8216 through the stretch where only P1 was acting. That
independence is the discriminator; it is what every health candidate failed.

HEALTH IS NOT IN `R`, and this is an enumeration, not a failed search:

- Every `sub` into a player-struct offset (`0 < disp < 0x678`) was enumerated by
  linear decode over `0x1406A0000..0x140740000`. 26 offsets total. `B+0x16C` is
  the meter; the rest are position, velocity and timers. No health.
- Every `add`/`sub` on R-global offsets `0x1F000..0x216B0` was enumerated.
  Five offsets, all small counters. `R+0x21060` (63 sites) reads 0/0/0/769 --
  a counter, not vitality.
- `B+0x246` was REJECTED after surviving two diffs (182->125->110, and P2
  tracking it). The game compares it against 32/44/46/48/52/54/56/58/66/70
  across 32 sites -- a dense cluster of small thresholds is an animation or
  state counter, not vitality, which is tested against zero and percentages.
  It nearly shipped as the answer on shape alone.
- The Flycast NAOMI addresses are a clean negative here. `0x8C2419A8`
  translates (see below) to memory reading `cd cd cd cd`: allocated, never
  written. Those addresses do not apply to CFC2's layout.
- Guest RAM was captured twice (32 MiB, fully readable) across a damaging
  exchange: **612 bytes differed**. Live gameplay state is not there.

GUEST TRANSLATION IS REAL AND LIVE ON MATCH DATA. The per-player init
`0x1406AFD30` does `add r9,-0xC000000` against `C+8` (`0x1406AFD5D`), using the
guest pointer at `B+0x5F0`. Live values are `0x0C700000` (P1) and `0x0C880000`
(P2), resolving to valid per-character data; `B+0xB8` holds the resolved host
table. This corrects the earlier note that the translation was demonstrated
only for asset/resource loading. It is character data, not live health.

Where health can still be: a region neither captured nor enumerated -- the
arena outside both `R` and the 32 MiB mapped window, or a host-side structure
reached through a pointer rather than a fixed `R` offset. The enumerations
above bound the search; they do not close it. Do not adopt a health candidate
on numeric shape alone -- `B+0x246` shows how far a wrong field survives that.

`build/cvs2-state/hp-*.bin` and `guest-{hurt,low}.bin` are retained as the
verification set for any future candidate. `tools/grab.ps1` captures one
labelled snapshot, resolving the PID itself.

**Why the health search failed: a coverage gap, not an absence (2026-09-19)**

Pulling on the pointer route produced a negative that is worth more than the
searches themselves. Layout, computed from `hp-ken.json`:

- arena   = `0x7BA1000`, 256 MiB (`0x10000000`).
- context = arena + `0x8000000`.
- scratch = arena + `0x8200000`.
- mapped  = arena + `0x8400000`, the 32 MiB guest window.
- **R       = arena + `0xE000000`**, i.e. 224 MiB into the arena, size `0x216B0`.

Captured so far: `R` (136,880 B) and the mapped window (32 MiB). That is about
32.1 MiB of a 256 MiB arena. **Roughly 190 MiB was never read.** Every "health
is not in X" result above is therefore correct and bounded: it is not in `R`,
and it is not in the guest window. It says nothing about the rest of the arena.

Additional traces from this pass, all negative for health but recorded so they
are not repeated:

- Hit handler is `0x1406C0060` (entry), reached from the meter caller at
  `0x1406C015A`. Its body writes hit-reaction state (`B+0x168`, `+0x176`,
  `+0x1D2`, `+0x294`, ...) and zeroes meter stocks at `0x1406C0881`. It contains
  no health subtraction.
- `B+0x3F0` is a POINTER read by the hit handler (`mov rsi,[rdx+0x3F0]` at
  `0x1406C007C`). It is NULL in all five captures: it is populated only during
  an active hit, which no snapshot caught. This is the most promising remaining
  lead and needs a hook, not a snapshot.
- Enumerated every variable (non-immediate) memory subtract in
  `0x1406A0000..0x140740000` paired with a nearby cmov/zero-store: ONE hit,
  `R+0x216A8`, which is a `1 - x` blink toggle with a countdown at `R+0x216A9`.
  Not health.
- Pointer-relative 16-bit subtracts with small displacements are all
  `sub ..., 1` frame decrements. Damage is variable, so none of them qualify.
- `R` contains 889 qwords pointing back into `R` (252 stable across captures),
  i.e. a live object graph, many entries referencing `R+0x1F220`. Blind
  traversal of it did not converge and is not recommended without a hook.

The next step is NOT another snapshot diff. Either hook `0x1406C0060` and read
`B+0x3F0` while a hit is being applied, or capture the arena outside the two
known windows. A snapshot cannot see a pointer that only exists mid-hit.

**Arena sweep: the search space is now fully bounded (2026-09-19)**

`tools/arena_snap.py` captures the committed arena outside the two known
windows: ONE `VirtualQuery` per region (the whole 256 MiB arena is a single
committed region, so exactly one syscall), then 1 MiB block reads, with a JSON
index of (addr, offset, len) runs so a later diff knows what each byte is.

Two captures in one live round, same characters, same phase byte 4 = `04`,
223.0 MiB each. Damage was confirmed independently in the paired `R` snapshots:
P1 meter 6098 -> 2406 and stocks 1635 -> 852, while P2 held exactly at 9816/656.

Result: **16,233 bytes of 233,832,448 differ (0.0069%)**, and every one of them
falls in a 3 MiB span at `arena+0x8000000 .. +0x8300000` -- the context block
`C` and the scratch region. Per-MiB histogram: MiB 128 = 530 B, MiB 129 =
13,155 B, MiB 130 = 2,548 B, and **zero elsewhere in 223 MiB**.

The health test applied to that diff (value drops, partner at a plausible
stride holds, similar magnitudes, no collapse) produced 548 paired candidates,
all rejected on inspection: they are ascending runs at regular 0x20 strides
(43,44,45,46... / 87,88,89,90...) clustered at `0x0FBD1xxx`, i.e. an index or
sprite list being walked, not two independent fighter gauges.

So the live-changing memory in the entire 256 MiB arena is exactly:

- `R` at arena+`0xE000000` (136,880 B) -- captured, and every `sub` into its
  player structs and globals enumerated. Health is not there.
- 3 MiB at arena+`0x8000000` (C/scratch) -- diffed here, no health pair.
- the 32 MiB mapped guest window -- 612 bytes changed over a damaging exchange.

Health is therefore not discoverable by snapshot diffing of the arena. This is
a bounded negative, not a failed search: the remaining candidate is memory
reached through a pointer that is only valid mid-hit, which a snapshot cannot
observe by construction. `B+0x3F0` (read at `0x1406C007C`, NULL in all six
captures) is exactly such a pointer.

Next step is the hook on `0x1406C0060`, reading `B+0x3F0` while a hit resolves.
The 223 MiB captures were deleted after diffing; `tools/arena_snap.py` and this
record reproduce them in one command if needed.

**The B+0x5F0 guest-pointer lead is closed (2026-09-19)**

Tested offline against `guest-hurt`/`guest-low`, a validly paired capture:
same characters, same guest pointers, P1 meter 4260 -> 52 while P2 held at
8216, i.e. a real damaging exchange.

`B+0x5F0` is `0x0C700000` (P1) and `0x0C880000` (P2), both inside the captured
32 MiB window. Over that exchange, **0 bytes changed in 128 KiB at either
fighter's region**. The pointer targets static per-character data (move/frame
tables), not live state.

All 612 changed bytes in the whole 32 MiB window fall in one 64 KiB block at
guest `0x0C470000`, and their content is nibble-packed 4bpp pixel data
(`aa`/`cc`/`dc`/`ed` runs) -- sprite graphics being animated, not state.

The window itself is not guest RAM: its first MiB is entirely `0xCD`
(uninitialized heap fill) and it is 96.9% nonzero static data. That matches the
initializer FILLING `C+8` with 32 MiB of archive content, exactly as the
pointer trace says. Do not re-interpret this window as NAOMI main RAM; the
`host = C[8] + guest - 0x0C000000` translation resolves ASSET addresses, which
is all it was ever shown to do.

**The hit-handler hook was also a wrong target.** `0x1406C0060` was reached
backwards, from the meter-gain function's caller, on the assumption that
"calls meter gain" means "applies damage". Verified live during a real match:
the detour IS installed (entry bytes read `e9 ae 0e 93 ff`, a jmp, in the
running process) and `calls=0`. Its sole caller `0x1406C0D90` is reached from
5 sites at `0x1406BFC94..0x1406BFE3A` and is a conditional sub-path, not the
main hit flow. One fact worth keeping: at `0x1406C0DD2` that caller does
`mov rax,[rbx+0x3F0]` then `movzx ecx,[rax+0x10]` with NO null check, so on
that path the pointer is reliably valid -- the slot is real, the question is
only when it is populated.

⚠ `hit_detour()` calls `mem_readable()` twice per invocation, i.e. two
`VirtualQuery` syscalls on the render thread. It has never fired so this has
cost nothing, but it must be replaced with a bounded read before the probe is
ever pointed at a hot function. COUNT THE SYSCALLS BEFORE HOOKING A HOT PATH.

**Method note, since three leads in a row failed the same way.** The hit
handler, the bar-ratio division search, and this pointer were all generated by
pattern-matching the code for things that LOOK like damage/health/state. That
is the memory-scanning failure mode one level up, and this project's record is
explicit: the wins (sPad, sKeyboard, the meter) all came from starting at a
known-live value and following the instructions that touch it. The division
search did produce two real fields -- `R+0x203D8` (a fade timer, `dec` then
scale to 255 at `0x1406BCE02`) and a misread of the meter stocks -- but neither
is health.

Health remains unlocated. What is now excluded, with evidence: all of `R`
(every sub enumerated), the whole 256 MiB arena by snapshot diff, the 32 MiB
asset window, and the per-character data at `B+0x5F0`.

**⛔⛔⛔ CORRECTION: B+0x16C IS HEALTH, NOT THE SUPER METER (2026-09-19)**

Everything above that calls `B+0x16C` the super meter is WRONG, including the
"meter solved" claim and the "health is a bounded negative" conclusion that
followed from it. Health was in the player struct the whole time, filed under
the wrong name.

The user settled it in TRAINING MODE, where the variables are controlled:

- spending a super -> the value did not move;
- the meter refilling -> the value did not move;
- hitting the opponent -> THEIR value dropped.

The third observation alone is decisive. Nothing about a super meter makes it
fall when the other player is hit.

Corrected fields, same validated state block:

    health      B+0x16C   (B = R + 0x1EBA8 + player*0x678)
    max health  B+0x170   per-character: 11520 Ryu, 11200 for P2's fighter

⭐⭐ WHY THE EARLIER "CONFIRMATION" WAS WORTHLESS, because this is the reusable
part. Two independent lines of evidence were cited and NEITHER discriminated:

1. "P1 moved while P2 held at 8216 across 7 captures." That is equally true of
   one player taking damage while the other is untouched. The check separated
   a real per-player field from a shared timer -- NOT a meter from health.
2. "`imul edx,eax,0x64` at 0x1406C26A0 adds in units of 100; 0x1406C4241
   subtracts." Damage and recovery have exactly that shape too.

And a third signal pointed the right way and was reasoned past: B+0x170 is a
steady PER-CHARACTER ceiling (11520 vs 11200), which fits CVS2's per-fighter
vitality far better than a meter cap. It was noted early as looking like
vitality and then dismissed.

⚠ THE RULE: a behavioural test that cannot tell two hypotheses apart is not
evidence for either one. Before calling a field identified, ask what the
observation would look like if the OTHER answer were true. Here: identical.

⚠ Consequences for the rest of this document:

- The "bounded negative" for health is RETRACTED. The enumeration itself was
  sound -- B+0x16C did appear in the list of subtracted player-struct fields --
  but it had already been mislabelled, so the search walked past its own
  answer. An enumeration is only as good as the labels on its entries.
- THE SUPER METER IS NOW UNLOCATED. Do not re-derive it from 0x1406C26A0 or
  0x1406C4241; those are damage and recovery. The signature to look for is a
  field that DROPS when a super is spent and CLIMBS as it is built -- both
  directly controllable in training mode, which is how this one was settled.
- Stun, and the game-specific meters, remain unlocated.

Shipping state: `src/cvs2_select.c` feeds AUDIO_GAUGE_HEALTH from these
fields; the tone is the triangle at 180-540 Hz per the audio design. Test
case 7b in `tools/test_audio.c` pins health and super as audibly distinct
(366 Hz vs 646 Hz at equal fill) so the two can never ship on one timbre.

## 2026-09-19: four-entry handicap screen

The user reported a missing screen after character selection in Versus/Training,
with four horizontal entries and the second selected initially. They identify
it as handicap/health/damage rather than ratio. Do not treat its visible label
as established merely from the data structure used by the game.

Live capture `build/cvs2-state/handicap-confirmed-02.bin` (PID 19600, ROM 3,
R=0x16421000) was taken after the user explicitly parked on that screen:
P bytes 02 01 01 01, P+0x128=1, R+0x1FFD0=2, mode R+0x1FFEF=3,
participant mask=1, player step=3, B+4=1, busy=0, choice R+0x1FFE6=1.
`handicap-right-03.bin`, after the user's one-step-right movement, has choice=2
and the same active dispatch. This identifies the actual current selection;
no input-history counting is used by the reader.

The mode-3 table dispatches step 3 to 0x1406F3200:
- 0x1406F32A5 initializes choice R+0x1FFE6+p to 1.
- 0x1406F3385 bounds right at 3; 0x1406F33A8 increments choice.
- 0x1406F33C2 bounds left at 0; 0x1406F33E3 decrements choice.
- Confirmation at 0x1406F333B leaves inner step 1 and marks busy.
- 0x1406F324B commits to B+0x5CC, the same per-character storage that
  the team-ratio handler uses. This supports the internal ratio interpretation
  but does not independently establish the heading shown to the user.
- This handler does not auto-confirm based on the selection timer. Do not
  inherit the timer guard used by the other alternate handlers.

Speech is deliberately `Level 1` through `Level 4`, without player prefixes
or an unverified health/damage percentage. It reads both player structures,
requires the traced active dispatch and two matching samples, and stops after
confirmation. Modes 0/1/5 also use the previously traced team-ratio handler
0x1406F2F60; the decoder now accepts that alternate dispatch with its timer,
team-size, and bypass guards, sharing the existing team-ratio speech.

Regression tests replay both untouched live handicap captures for Level 2 to
Level 3. Code-derived fixtures cover all four choices, both players, wrong
modes, busy/confirmed states, and alternate team-ratio selection. Those
synthetic cases are not additional live validation. Tests and build pass;
existing compiler warnings remain in unrelated modules. A desktop screenshot
attempt returned a black image, so it supplies no evidence about the heading.

Deployed after the user closed CFC2. Installed/built SHA256 match:
`9E07BA29AFDB5CB521EE348E3B59A498813444FAC81B5EDCA9F35B2CF40FDE25`.
Backup: `build/dinput8-before-handicap-20260919-123416.dll`.
End-to-end speech in the relaunched game remains for the user to confirm;
actual selection state and the offline speech path were validated above.
