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
    shadow bar  B+0x16E   the delayed/drain bar -- LAGS behind +0x16C
    max health  B+0x170   per-fighter AND per-RATIO; read live, never hardcode

⚠ `B+0x16E` sits BETWEEN health and max, and it usually equals current health,
so reading `+0x16C`/`+0x170` as an ADJACENT u16 PAIR silently gives you
(health, shadow) and a fill fraction pinned at 1.00. I did exactly that while
chasing a pitch-direction report and briefly "disproved" max health with it.
They are 4 bytes apart; `state-b` is the capture that separates them
(hp 2406, shadow 3108, max 11520).

⚠ MAX DEPENDS ON CHARACTER AND RATIO (user, 2026-09-19), confirmed in captures:
11520 at ratio 1 in Arcade, 14400 at ratio 2 in Training, same fighter (Ryu).
So it is not a per-character constant either -- it must be read live.

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

**Super-meter search: first attempt did NOT find it (2026-09-19)**

Training-mode pair, same PID and root `0x15541000`, both phase byte 4 = `04`:
`meter-full` (user: both players full health AND full meter) and `meter-spent`
(user: spent down to level 0).

⭐ The baseline independently re-validated health: P1 and P2 both read
14400/14400 at `B+0x16C`/`B+0x170`, exactly as the user described. Note the max
is 14400 in training, not the 11520/11200 seen in Arcade -- max health is
per-character AND per-mode, so do not hardcode it.

⭐ The isolation was clean: health held at 14400 on BOTH sides across the pair,
so nothing in the diff is damage.

Searches run, all negative:

- Player-struct u16s that decreased with P2 unchanged: 3 hits, all animation
  (`B+0x03B`/`0x03C` are one field read at two alignments; `B+0x0D9` moved by 1).
- EVERY P1 struct change listed with P2 context: 47 fields, all animation or
  shared. `B+0x246` again moved IDENTICALLY on both players, re-confirming it
  is a shared counter and not per-player state.
- Whole-block byte scan for a small count landing on exactly 2 (when the user
  first said "level two"): 38 hits, almost all at regular 0x168 strides =
  display list.
- Same, with a partner holding the pre-spend value: 0 hits.
- Whole-block u16 -> 0 with an unchanged partner at 14 plausible strides:
  259 hits, all in `0x00D00..0x01E00` with repeating 32768/16256/5460 at
  0x100/0x800 strides = display list.
- R-globals `0x1F000..0x216B0`: 49 changed bytes, all small values counting UP
  from zero (effect/animation), nothing draining from full.

⛔ WHY THIS PAIR CANNOT SETTLE IT: **10,594 of 136,880 bytes changed (7.7%)**,
and 3,177 u16s anywhere in the block dropped from >=1000 to <=8. A super
animation churns far too much state for a two-capture diff to isolate one
field. The pair is not bad data -- the method is under-powered for this event.

▶ WHAT TO DO INSTEAD. Do not run another spend-diff; it will return thousands
of candidates again. The meter's distinguishing property is that it **refills
gradually while nothing else happens**. Two captures taken during quiet
meter-building, with no attack animation in flight, should differ in very few
bytes -- and the meter must be among them. That inverts the signal-to-noise:
the spend event is the loudest possible moment, the refill is the quietest.

⚠ Also still unchecked: whether the meter lives in the state block at all. The
stock display may be driven from somewhere the 0x216B0 window does not cover,
as health's max does vary by mode. Confirm the field exists here before
spending more captures on locating it.

**Super meter: three-way full/full/empty test, still NOT located (2026-09-19)**

⭐ CORRECTION to the entry above: the user clarified that `meter-full` AND
`meter-spent` were BOTH taken at FULL meter. So that pair contained no meter
change at all, and its 10,594 changed bytes were pure animation noise. The
"spend diff" described above tested nothing. A third capture `meter-empty`
(user: "empty or near empty", same PID, same root `0x15541000`, phase byte
4 = `04`) supplies the real contrast.

⭐⭐ THE THREE-WAY CONSTRAINT is much stronger than any pair: the meter field
must AGREE across the two full captures and DIFFER in the empty one. Animation
noise differs between the two fulls and is therefore excluded automatically.
It cut 3,177 pair-candidates to 84 u16s / 140 bytes.

Still negative. What the survivors are:

- Repeated display constants (5460, 5461, 32768, 16256, 16384) at 0x8 and
  0x100 strides across the whole block -- vertex/display-list data.
- An 84/21 byte pattern at 0x8 stride recurring block-wide, which also passes
  a naive "partner kept its value" filter. It is not a gauge.
- Values clustered at 21500-21900 with tiny deltas, appearing everywhere:
  pointers or coordinates, not a meter.

⛔⛔ THE DECISIVE NEGATIVE: in the 0x678-byte PLAYER STRUCT, exactly **TWO**
bytes are equal in both full captures and different when empty -- `B+0x030`
and `B+0x031`, both `0 -> 255`. That is a flag, not a gauge. **The super meter
is not a field of the player struct.** Health is (`B+0x16C`), so the two are
not stored together, and walking outward from health will not find it.

⭐ Health re-validated again in all three captures: 14400/14400 both players,
matching the user's description exactly. Note max health is 14400 in TRAINING
vs 11520/11200 in Arcade -- per-character AND per-mode. Do not hardcode it.

▶ NEXT, and do not repeat the above: 1,284 bytes block-wide satisfy the
three-way constraint, which is too many because a full-to-empty transition
still spans a lot of game time. The remaining high-value experiment is the
QUIET one: two captures during gradual meter BUILDING with no attack animation
in flight. Very few bytes should differ, and the meter must be among them.
If that also returns nothing plausible, conclude the meter is outside this
0x216B0 window and stop diffing -- max health already varies by mode, so the
block demonstrably does not hold every gauge uniformly.

**⭐⭐ SUPER METER FOUND: B+0x226 (2026-09-19)**

The user corrected the labelling twice; the final set is THREE full-meter
captures (`meter-full`, `meter-spent`, `meter-empty` -- all taken while the
meter was actually full) and ONE genuinely empty (`meter-empty2`). Same PID,
same root `0x15541000`, all phase byte 4 = `04`.

⭐⭐ THE FOUR-WAY CONSTRAINT is what found it, and it is the technique to
reuse: the field must be IDENTICAL across all three full captures and DIFFER
in the empty one. Three captures taken at different times cancel animation
noise that no pair can. 125,002 of 136,880 bytes agree across all three fulls;
1,059 of those differ when empty; and in the 0x678-byte PLAYER STRUCT exactly
**ONE** byte survives:

    super meter   B+0x226   (u16)

    meter-full   P1=168  P2=168
    meter-spent  P1=168  P2=168
    meter-empty  P1=168  P2=168
    meter-empty2 P1=  4  P2=168      <- user spent; P2 never did

P2 holding at 168 across all four while P1 collapses to 4 is the per-player
independence that health's identification originally lacked.

CODE CONFIRMATION, independent of the captures. 57 instructions touch +0x226.
At `0x1406C40EA`: `add word ptr [rbx+0x226], di` followed by `jns` and a store
of 0 -- a variable add that FLOORS AT ZERO (spending). Just above, at
`0x1406C40B0`: `add eax,edi` / `mov [rbx+0x226],ax` / `cmp ax,0x48` / clamp to
`0x48` -- a variable add that CEILINGS (gaining). Accumulate-up-to-a-cap and
drain-to-zero is the meter, and nothing else in the struct has that shape.

⚠ THE CAP IS NOT A SINGLE CONSTANT. The observed full value is 168, but the
only immediates compared against +0x226 are 72 (0x48, x3), 100 (0x64, x2) and
112 (0x70, x1) -- all BELOW 168. So `0x48` is a per-path or per-groove
threshold, not the maximum. That is expected for CVS2, where grooves differ in
meter behaviour. **Do not hardcode 72, 168, or any of these as the max.** The
real maximum must be located (likely a per-groove table) before the gauge can
be scaled to a 0..1 fill fraction for the audio.

⚠ Unlike health, no max-value FIELD has been found beside it. Health has
`B+0x170` adjacent; the meter's ceiling appears to come from code/tables, not
a neighbouring slot. Until that is resolved, a fill fraction is not derivable.

Health re-validated in all four captures: 14400/14400 both players throughout,
matching the user's description each time.

**⭐⭐ THE METER CAP IS PER-GROOVE — table resolved (2026-09-19)**

The observed full value (168) exceeded every immediate compared against
`B+0x226` (72, 100, 112), which meant no single constant could be the max.
Tracing the enclosing function `0x1406C3DE0` resolved it:

    0x1406C3E53  movsx rax, byte ptr [rbx+0x22D]   ; groove index
    0x1406C3E5B  cmp eax, 5
    0x1406C3E70  jump table at 0x1406C4138          ; six cases

Each case loads its own ceiling:

    0 C = 0xA8 (168)   1 A = 0x90 (144)   2 P = 0xC0 (192)
    3 S = 0x64 (100)   4 N = 0xD8 (216)   5 K = 0x48 (72)

⚠ `0x48` is K-Groove's cap ONLY. It is the immediate that appears most often
near this field, and hardcoding it -- or the measured 168 -- would misreport
every other groove. `B+0x22D` is the index, and it agrees with `B+0x5D0` (the
groove field the selection reader already validates) in all four captures.

⭐ THE CHECK THAT MADE IT SHIPPABLE, and the standard to reuse: the table
predicts 100% for all three full-meter captures and 2% for the empty one,
reproducing the user's own description. A cap that could not reproduce the
measured 168 would have been rejected. This is the falsifiable-prediction test
that the earlier health/meter mix-up lacked.

Shipping: `cvs2_gauges_observe()` feeds AUDIO_GAUGE_SUPER from `B+0x226`
scaled by `meter_cap[B+0x22D]`, and stays SILENT for a groove index outside
0..5 rather than guessing a ceiling. Regression in `tools/test_cvs2_select.c`
replays all four captures and forces each of the six groove indices; hardcoding
a single cap fails it immediately (verified by sabotage).

⚠ Test note: the older health assertions counted `ga_sets==2` for "both
players fed". That count is now 4, because each player feeds two gauges, and
the assertion failed for a reason unrelated to health. The suite now records
per-gauge/per-player values instead. A raw call count is a proxy; assert on
the value that matters.

**Round timer: strong candidate R+0x184, needs one live confirmation (2026-09-19)**

Found by diffing same-session capture pairs for a u16 in 0..99 that decreases.
`R+0x184` is the only offset appearing in ALL THREE pairs, and its values are
exactly what a CVS2 round clock looks like:

    hp-full 99   hp-hurt 71   hp-low 55   hp-zero 55
    hp-ken  81   state-a 81   state-b 75
    all four TRAINING captures: 99   (training does not run the clock)

99 is CVS2's default round time; the training captures sitting frozen at 99
while the Arcade ones count down is the discriminator. `hp-ken` resetting to 81
is consistent with a new round starting after the previous one ended.

⚠ NOT yet confirmed against code. The 13 instructions touching `+0x184` are all
BYTE accesses on other structures' `+0x184`, not this u16 at the block base --
the timer is reached through a base pointer I have not identified. So this rests
on behaviour alone, which is exactly the evidence that mislabelled health as the
super meter.

⚠ The wall-clock rate check is INCONCLUSIVE and must not be cited as support:
0.07-0.12 per second across the three pairs, where a real clock is 1/s. The
gaps include long pauses while the session was idle, so elapsed wall time
massively overstates in-match time. Consistency across pairs is mildly
encouraging; the absolute rate proves nothing either way.

▶ THE CONFIRMATION THAT SETTLES IT, and it costs one capture: read the
on-screen timer aloud and capture at that moment. If `R+0x184` equals the
displayed number, it is the timer. This is the falsifiable-prediction standard
that the per-groove meter cap met and the original health/meter claim did not.

Until then do NOT wire timer cues. A wrong timer would fire the 30/15/5-second
warnings at the wrong moments, which is worse than no warning at all.

⛔ **THE OFFLINE ROUTE IS CLOSED — tried 2026-09-19, recorded so it is not
re-attempted.** Two further attempts, both negative:

- `find_field_access.py 0x184` (reads AND writes): 14 hits, every one a BYTE
  access on another structure's `+0x184`, confirming the note above. There is
  no direct `R+0x184` access anywhere in the CVS2 range.
- Since `R` is published at the fixed global `0x1422DEBD0`, I searched for
  RIP-relative references to that global and decoded 40 instructions after
  each, looking for a `+0x184` access on the loaded register. **None.**
  ⚠ That is a WEAK negative, not a result: the global is referenced from
  **3105 sites**, and the root is typically loaded once and passed in a
  register across calls, so the access can be in a different function
  entirely. Do not cite this as evidence the field is not the timer.

So the candidate cannot be promoted or refuted offline, and the one live
capture described above remains the only thing that settles it. It is cheap —
the user reads the on-screen number aloud at capture time — and the
prediction is exact, which is the standard the meter cap met.

**Win quotes: located live, but the DRAW path is not found (2026-09-19)**

Confirmed the quote text exists and is readable at runtime. With the game
running, all five signature phrases resolve inside the arena:

    arena = 0x7B71000  (this session)
    "Stand up and face me again"  arena+0x1FD9F4
    "Shinryuken"                  arena+0x1FDD23
    "Sumo is the greatest"        arena+0x1FE5B0
    "Psycho power"                arena+0x1FEC60
    "Kyokugen style"              arena+0x1FFAFC

So the memory's file offset 0x001FD800 for entry[7] maps directly to
`arena+0x1FD800`. 664 ASCII runs of >=8 chars in the 25,676-byte region.

⭐ STRUCTURE, read off a live dump of the region head:

    +0x0000  u32 0x62CC   -- a table offset
    +0x0004  u32 0x638C   -- a second table offset
    +0x0008  a character REMAP table (font glyph indices, not text)
    +0x0074  a long run of 01/02 bytes -- per-line width/advance data
    +0x01F4  text begins; Ryu's first line. Ken's starts at +0x039C.

⚠ QUOTES ARE MULTI-LINE, each line its own NUL-terminated string:
  +0x01F4 "Stand up and face me again. I have"
  +0x0218 "yet to see your best..."
So "a quote" is several strings, not one, and any reader must join them.

⛔ NEITHER HEADER TABLE IS THE QUOTE INDEX. `+0x62CC` holds u32s (0x5238,
0x5290, 0x52F0, ...) that point PAST the text into another region; `+0x638C`
indexes the 01/02 width data at +0x0074. Do not mistake either for a
per-character quote table -- I did briefly, and the strings they resolve to
are binary garbage.

⛔ THE DRAW ROUTINE IS NOT FOUND, and the static route is closed: a sweep of
0x1406A0000..0x140740000 for the immediates 0x1FD800 / 0x1FD000 / 0x1FE000
returns ZERO instructions. The region is reached through the archive's own
entry table (IBIS/AFS), not by a literal offset, so there is nothing to grep.

▶ WHAT WOULD WORK, in preference order:
1. A WRITE/ACCESS BREAKPOINT on a known quote address while a win screen is on
   screen -- the only method that names the reader directly. Needs a debugger
   or a hardware breakpoint, neither of which this mod currently does.
2. Hook the archive entry resolver (0x1406B0060 / 0x1406ADF10 already known to
   translate guest addresses for resource records) and watch for a request
   that lands in 0x1FD800..0x203CC.
3. Read the quote by index once the per-character table is understood -- but
   only 7 of 48 offsets are confirmed and spacing is explicitly variable, so
   arithmetic would risk speaking the WRONG character's line.

⚠ Do not ship route 3 on its own. Speaking another fighter's victory quote is
indistinguishable from correct behaviour to a blind user, which is the exact
failure class this project has paid for repeatedly.

**⭐⭐ ROUND TIMER CONFIRMED: R+0x184 (2026-09-19)**

The falsifiable check passed. The user reported **99** on the arcade timer;
`timer-99.bin`, captured at that moment, reads **99** at `R+0x184`. Combined
with the earlier evidence -- the only offset decreasing across all three
same-session pairs, frozen at 99 in all four Training captures, resetting to 81
for a new round -- this is settled.

`R+0x186` holds 36..59 across captures: a 0..59 sub-second frame counter. Not
needed for second-boundary cues, which read `+0x184` alone.

⚠ The earlier wall-clock rate check (0.07-0.12/s against an expected 1/s) was
INCONCLUSIVE and is superseded, not confirmed, by this. It failed because the
gaps included long idle pauses, not because the field was wrong.

SHIPPED: `cvs2_frame.c` reads the clock in the per-frame detour, so a tick
lands on the frame the second changes. Warnings at 30 and 15, then every
second for the last five.

⚠ Fires only on a DECREASE. A round reset jumps the value upward past 30 and
15, which would otherwise fire the whole warning set during the reset.

The cue is a one-shot decaying blip (1200 Hz sine, ~40 ms), centre-panned so
it sits apart from the owner-panned gauges, in a 4-slot ring so ticks overlap
rather than steal -- the user said overlap is fine as long as onsets are right.

⚠ audio_tick() takes NO lock. It claims a slot with InterlockedIncrement and
writes only that slot. Taking the mixer's critical section from the frame
thread is what made the health tone crackle.

⛔ TWO TEST DEFECTS WORTH REMEMBERING, both found here:
1. I wrote the decay constant 0.9994 with the comment "~40 ms". It is actually
   **235 ms**. Compute a constant; do not assert it in a comment.
2. The decay assertion used `peak_over(24)`, which returns the MAXIMUM across
   those blocks -- so it measured the tick's ATTACK and failed against
   correct code. Let the sound ring out first, THEN assert silence.
   Measured decay is silent at block 7, 40.6 ms, exactly as intended.

**⛔ Win quotes: the "read it by char_id" shortcut is DEAD (2026-09-19)**

The user: CVS2 displays a RANDOM win quote, and there is no way to determine
in advance which will appear.

That closes the cheap route. Knowing the winner narrows it to that character's
BLOCK, but each block holds several alternatives -- Ryu's alone contains at
least six ("Stand up and face me again...", "A true warrior follows their own
destiny!", "You were your own worst enemy...", "That's it?", "Has your fighting
spirit burned out?!", "Never give into your fear..."). I catalogued those
earlier in this same session and still wrote about "the winner's quote" as if
it were singular.

⚠ So reading by char_id would speak ONE OF SIX at random, and a wrong quote is
indistinguishable from a right one to a blind user -- the exact failure class
this project has paid for repeatedly. Do not ship it, and do not revive
"establish all 48 per-character offsets" as a path to win quotes: the offsets
are not the missing piece, the RUNTIME SELECTION is.

▶ ONLY ONE ROUTE REMAINS: observe the pointer the game hands its text
renderer. Not inference -- observation. The static search is already closed
(zero instructions reference the region offset; it is reached through the
archive entry table), and today's attempt at the archive resolvers found that
0x1406B0060 is allocator/list arithmetic, not a text reader.

That needs machinery this mod does not have: a hardware breakpoint or a
PAGE_GUARD on a known quote address, armed while a win screen is up.

⚠ Worth weighing before spending a session on it: the payoff is flavour text
on a screen that lasts a few seconds, versus position/beacons which change how
the game is PLAYED. Recommend parking this below position.

What is already established and need not be redone:
- the quote region is live at `arena+0x1FD800`, 25,676 bytes, 664 ASCII runs;
- the header is two table offsets (0x62CC, 0x638C -- NEITHER is a quote index),
  a font remap table at +0x0008, per-line width data at +0x0074, text at +0x01F4;
- quotes are MULTI-LINE, each line its own NUL-terminated string.

**⛔⛔ CORRECTION: +0x62CC *IS* THE QUOTE INDEX (2026-09-19)**

The line above ("NEITHER is a quote index") is WRONG, and the reason is worth
more than the correction. `+0x62CC` was read as a table of pointers TO TEXT,
its entries were dereferenced once, they landed on binary, and it was ruled out.

⭐⭐ IT IS A TWO-LEVEL TABLE. The u32s point at PER-CHARACTER SUB-TABLES, which
point at the lines. One dereference lands on the sub-table's own pointer bytes
-- exactly the "binary garbage" that was reported as disproof. The index was
there; the indirection was one level deeper than the read.

⚠ THE REUSABLE RULE: "the pointer resolves to garbage" can mean the pointer is
wrong, OR that it points at more pointers. Before ruling out an index, check
whether the "garbage" is itself a plausible offset array. Here every u32 was a
small ascending in-region value -- the signature of an index, not of text.

⭐ AND THE WHOLE THING IS OFFLINE. The text ships uncompressed in
`bin/cvs2` inside `arc/pc/game_10.arc` (ARC v7, one zlib entry, 96 MB IBIS).
Region file offset `0x1FD840` = live `arena+0x1FD800 + 0x40` (the IBIS header).
All five live-confirmed phrases are present. No launch is needed to work on
this text ever again.

FORMAT: `+0x62CC` = 48 u32, one per character ID -> that character's sub-table
of 8 u32, one per quote -> an array of u16 LINE offsets, terminated by the next
quote's pointer (for quote 7, by the sub-table itself). `+0x638C` = 48 u32 into
the width data at `+0x0074` -- so BOTH header tables are now explained.

**48 characters x exactly 8 quotes = 384**, decoding cleanly.
`tools/cvs2_quotes.py`, controls in the same invocation; sabotaging the
multi-line join is caught by two independent controls and it refuses to report.

⚠ THIS DOES NOT MAKE QUOTES SHIPPABLE. 8 per character, chosen at random at
display time, so the ID gives a 1-in-8 guess. The remaining unknown is the
RUNTIME SELECTION -- a single small integer -- not the text or its layout.

⚠ Why no x86 search will find the reader: a sweep of the dump for `0x62CC`,
`0x638C` and `0x1FD800` returns ZERO occurrences. This region is GUEST data read
by emulated SH-4 code; the host executable never touches these offsets. That is
the structural reason the static route is closed.

**⭐⭐ ENDINGS ARE SOLVED AND SHIPPABLE: one per character (2026-09-19)**

The user confirmed CVS2 endings are NOT random -- one per character. So the
character ID fully determines the text, and the selection reader already
validates that ID. There is no runtime-selection unknown, which is exactly what
separates endings from win quotes.

REGION: file offset `0x204040` in `bin/cvs2`, immediately after the quotes,
same two-level shape:

    +0x0000  u32 0x3EAC -- entry index
    +0x0008  per-line width bytes (01/02/03)
    +0x04B8  text, NUL-terminated lines
    +0x3EAC  256 u32 -- entry -> array of u32 LINE offsets

An entry's array runs until the next entry's begins, so entries are variable
length (1-3 lines). 250 of 256 carry text.

⚠ THE TABLE IS THE WHOLE STORY SCRIPT, not only endings:

    0..47    boss/pre-fight dialogue and the Osaka news interlude
    48..191  THE 48 ENDINGS, exactly 3 entries each
    192..239 pre-final-match lines, one per character
    240..249 the closing ceremony

Only 48..191 is keyed by character ID. Do not speak the rest by ID.

⭐ THE MAPPING IS DERIVED, NOT ASSUMED: endings start at 48, 3 entries per
character, in the SAME roster order `src/cvs2_select.c` already uses. Ryu 48-50,
Ken 51-53, Chun-Li 54-56 ... Rolento 189-191. Sagat lands at 75-77 = ID 9, the
ID the user identified live.

⚠ 12 of 48 endings do not contain the character's roster name, and that is
CORRECT: Claw's says "Vega", Dictator's "Bison", Kyo's is Shingo addressing
"Mr. Kusanagi", Mai's is about Andy, Haohmaru's "the samurai", Cammy's is the
Shadaloo doll monologue. So a name-match RATE alone cannot validate alignment.

⭐⭐ THE CONTROL THAT ACTUALLY DISCRIMINATES: exact phrase ANCHORS on IDs whose
identity is independently live-verified (Ryu 0, Sagat 9, Yun 44) plus the table
endpoint (47). A uniform shift keeps the name-match rate plausible while
breaking every anchor. Verified by sabotage: `ENDINGS_FIRST` 48 -> 51 gives
anchors 0/4, name-match 37 -> 1/48, exit 2, and the tool refuses to report --
where a rate-only control would have shipped Ken's ending as Ryu's.

`tools/cvs2_story.py` (`--char N`, `--raw` for all 256 entries).

⚠ Entries 243-245 hold 0x81-prefixed placeholders where the game substitutes
the winning team's names at runtime. They decode as replacement characters;
that is not corruption.

▶ NOT YET WIRED TO SPEECH. What remains is the runtime trigger: detecting that
an ending is being displayed, and for which character. The text and the ID
mapping are settled; the trigger is not, and it must not be inferred from a
match ending -- a loss and a win both end a match.

**Ending trigger: what the user reports, and what the code settles (2026-09-19)**

User report: endings are shown at the END, after the boss is defeated, DURING
THE CREDIT ROLL. They believe it requires beating Shin Akuma or Ultimate/God
Rugal, but explicitly flagged that as uncertain. Treat the boss condition as
UNVERIFIED -- do not encode it as a guard until a capture shows it.

⛔ THE STATIC ROUTE IS CLOSED FOR ENDINGS TOO, and now for a proven reason.
Searched the dump for the story region's own offsets: `0x3EAC` occurs ONCE, at
`0x140533BE0`, which is OUTSIDE the CVS2 range `0x1406A0000..0x140740000` --
i.e. coincidental. `0x204000` and `0x204040` occur ZERO times. Same result as
the quote region. This is guest data read by emulated SH-4 code; the host x86
executable never touches these offsets, so no amount of x86 disassembly will
find the reader. Do not repeat this sweep.

WHAT IS ESTABLISHED ABOUT THE PHASE MACHINE:

- The top-level table `0x141282A20` has EXACTLY 7 entries (index 7 is null;
  8..11 are the ASCII "CAPCOM VS SNK 2"). Earlier notes listed "the first seven"
  without saying seven is all there are. It is all there are.
- Phase 1 `0x1406B9BA0` (match) subtable `0x14128CCD8`: 10 entries, followed by
  a byte index map `0706050403020100`. Match setup `0x1406B9F40` is index 6.
- Phase 2 `0x1406BA870` (selection) subtable `0x14128CD68`: 10 entries, same
  shape.
- Phase 6 `0x1406B20D0` is a TRANSITION handler: it sets P[0]=1 and zeroes
  P[1]/P[2], i.e. it re-enters the match phase from the top.

⛔⛔ NO CAPTURE OF AN ENDING OR CREDIT ROLL EXISTS. All 28 captures are either
character select (`02 01 01 01 ...`) or in-match (`02 01 02 02 ...`). So which
phase drives the credit roll is UNKNOWN -- not narrowed, unknown. Do not guess
it from the handler list above; three leads in this project have already failed
by pattern-matching code that "looks like" the target.

▶ THE CAPTURE THAT WOULD SETTLE IT, and it is one run: reach an ending and
capture DURING the credit roll with `tools/grab.ps1`. What it answers:

- the phase bytes for the ending/credits state -- which names the handler;
- whether the winning character's ID is still readable at a known offset
  (B+1 and the team bytes B+0x5C0.. are the candidates the selection reader
  already validates), which is what picks WHICH ending to speak;
- whether the boss condition matters at all, by comparing a normal-ending run
  with a true-boss run.

⚠ Until that capture exists, do NOT wire ending speech. Speaking the wrong
character's ending, or speaking one during a plain match end, is
indistinguishable from correct behaviour to a blind user -- the failure class
this project has paid for repeatedly.

⚠ Worth saying plainly: an ending is a one-off narrative payoff at the end of
an arcade run, and the credit roll is long. This is lower value than in-match
position/beacons. It is now CHEAP (the text and mapping are done offline), but
it is not urgent.

**Win-quote timing: the in-match state machine, and a correction (2026-09-19)**

User: win quotes are displayed AFTER A MATCH COMPLETES, before the next match
starts.

⛔ MY FIRST READING WAS WRONG and the user corrected it: I took `hp-zero`
(phase byte 4 = `05`) to be the state preceding the quote. That is ROUND end,
not MATCH end. A match is best-of-three, so the quote comes after a state no
capture has reached. Recorded because the two are easy to conflate and the
captures only cover the round-level ones.

THE STATE MACHINE, traced (this part is solid):

- Phase-1 subtable `0x14128CCD8` index 2 -> `0x1406B9CB0`, a tail-jump to
  `0x1406EF570`.
- `0x1406EF570` dispatches on `P[2]` through the 12-entry table at
  **`0x141293760`** (states 0..11 -> `0x1406F0F60` .. `0x1406F1870`).
- The states ALTERNATE: even = setup, odd = a timed wait that decrements
  `[rdx+8]` and advances `P[2]` when it goes negative.
- Each setup calls `0x1406EF5E0` with an ascending selector: state 6 -> 4,
  state 8 -> 6, state 10 -> 8.
- `0x1406EF5E0` bounds the selector at 0xB and dispatches through the
  rva table at **`0x1406F0ECC`** (base `0x140000000`), 12 entries,
  `0x1406EF608` .. `0x1406F0CC2`.

Captured states: `hp-full` byte4=`03` (intro), `hp-hurt`/`hp-low`/`meter-*`
byte4=`04` (in-round), `hp-zero` byte4=`05` (round end). States 6..11 are
POST-ROUND/POST-MATCH and NO capture reaches them.

⛔ I did NOT identify which selector draws the win quote. Selector 6
(`0x1406F0230`) is a screen-layout setup writing float coordinates, which is
"consistent with" positioning a text panel -- and that is exactly the
pattern-matching that produced this project's last three dead leads (the hit
handler, the bar-ratio division search, the B+0x5F0 pointer). A layout setup
looks identical whether it positions a quote, a WIN banner, or a continue timer.
Do not adopt one of these selectors without a capture.

▶ THE CAPTURE THAT SETTLES IT, one run, with `tools/grab.ps1` DURING the win
quote (after a match ends, before the next begins):

- `P[2]` names the state, and the selector its setup passed names the screen;
- read `B+1` and the team bytes `B+0x5C0..` for the WINNER's character ID --
  that is the block of 8 quotes;
- the remaining unknown is WHICH of the 8, which is a small integer that must
  be somewhere in `R`. With a capture in hand, the four-way-constraint method
  that found the super meter applies directly: several quote captures, the
  field must agree when the same quote shows and differ when it changes.

⚠ Note the asymmetry between the two text bodies. ENDINGS need only the
character ID, which is already readable -- so an ending capture is a trigger
hunt alone. QUOTES additionally need the 1-in-8 selection index, which is a
second unknown. Endings are the nearer win despite being the rarer screen.

**⭐⭐ WIN-QUOTE STATE CAPTURED: phase byte 3 = 0x03 (2026-09-19)**

`build/cvs2-state/winquote-01.bin`, PID 8728, ROM 3, R `0x145F1000`, taken by
the user while a win quote was on screen.

    phase bytes   02 01 02 03 02 00 00 00
                            ^^ byte 3

⭐ THE DISCRIMINATOR IS BYTE 3, and it is new. Every prior in-match capture has
byte 3 = `02` with byte 4 carrying the sub-state (`03` intro, `04` in-round,
`05` round end -- e.g. `timer-99` is `02 01 02 02 03 04 03 00`). This capture is
the first with byte 3 = `03`. So the post-match quote screen is a sibling of
the in-match state, not a sub-state of it.

⚠ ONE capture. Byte 3 = `03` is the value observed WHILE a quote was displayed;
it has not been shown to be absent at other times, and a second capture at a
different moment is what would establish that. Do not treat it as a proven
sufficient guard yet.

WHAT DECODES CLEANLY IN THIS STATE (the same validated offsets, no new chain):

    P1  B+1 = 9  Sagat   team = Ken/Sagat/Ryu    size 3  groove 0 (C)
    P2  B+1 = 40 Todoh   team = Hibiki/Kim/Iori  size 3  groove 2 (P)

So `B+1` -- the field the selection reader already validates -- still holds each
side's ACTIVE fighter here. That is the character ID a quote or ending would be
keyed by, and it survives into the post-match state. This is the half of the
problem that is now answered.

⚠ `B+0x16C`/`B+0x170` read **0/0** for BOTH players in this state. Health is
torn down here, so the gauges must not be fed from it -- and a health-based
"is a match running" test would read this state as a live match at zero HP.
That is a real hazard for the existing audio path, worth checking.

⛔ THE 1-IN-8 QUOTE INDEX IS STILL NOT LOCATED, and one capture cannot find it:
1,622 bytes in each player struct and 372 in the `R` globals `0x1FF00..0x20100`
hold a value in 0..7. A small integer is the least distinctive thing in this
block. This needs the FOUR-WAY CONSTRAINT that found the super meter -- several
quote captures where the user reports the SAME quote (the field must agree) and
one where it differs (the field must differ). Do not pick a candidate on shape.

**⭐ BETWEEN-MATCH PHASES ARE A SAFE TEARDOWN WINDOW -- guard added (2026-09-19)**

⚠ CORRECTION TO THIS ENTRY'S FIRST WRITE-UP. It was headed "THE AUDIO ENGINE WAS
RUNNING OUTSIDE MATCHES -- FIXED" and read as a user-reported defect. It was not.
The user clarified: "There wasn't an issue with these playing during the between
match phases, I'm just telling you it's safe to tear that down and rebuild it
between matches." That is PERMISSION for a teardown/rebuild boundary, not a
symptom report. No wrong cue was ever heard.

⛔ I inferred a live bug from a plausible code path and wrote it up as though the
user had reported one. The timer hazard below is REAL AS CODE -- the guard was
genuinely absent and `R+0x184` genuinely reads 74 in the capture -- but whether
it ever actually fired was never established, and the write-up asserted the
user's experience rather than my own reading. ⭐ Keep the distinction: "this code
path can misfire" and "this misfired for the user" are different claims, and only
one of them was mine to make.

The guard is still worth having on its own merits (below), and the user has now
made the between-match window an explicit teardown point, which is stronger than
a defensive check: state can be rebuilt there rather than merely suppressed.

⚠⚠ THE HEALTH FEED WAS SAFE ONLY BY ACCIDENT. In `winquote-01` both
`B+0x16C` and `B+0x170` read **0/0**, so the existing `mx > 0` gate rejected it.
That gate tests a SIDE EFFECT of the state, not the state, and it holds only as
long as the side effect happens to. Had the post-match state left max health
populated, the health tone would have played over every win quote.

⛔ THE TIMER HAD NO SUCH LUCK. `R+0x184` reads a live-looking **74** in that
capture, and the detour fires `audio_tick()` on any DECREASE landing on
30/15/1..5. Round-clock warnings over a win quote is precisely the "cue at the
wrong moment" failure the timer work warned about -- and no health-based check
could have caught it.

FIX: guard on the phase bytes (`02 01 02 02`) at the top of the detour, reset
the edge trackers on the way out so re-entry cannot fire a stale decrease, and
call `audio_gauge_reset_all()` ONCE per departure so the gauges fall silent
instead of holding their last in-match value.

⚠ `audio_gauge_reset_all()` takes the mixer lock. It runs once per match exit,
not per frame -- the per-frame lock traffic from this thread is what made the
health tone crackle before.

⭐ Case 7e in `tools/test_cvs2_select.c` replays `winquote-01` and asserts no
gauge feed and no speech, plus that `B+1` still reads Sagat(9)/Todoh(40) so a
later change cannot quietly break the field an ending reader needs. Verified by
sabotage: removing the phase guard fails the suite.

Installed after confirming CFC2 was closed. Source/installed SHA-256 match:
`C59EFCD99C416FBD1D126EACE44FB295DCB4BED2114443D2125FA3CCD9374C14`.
Backup: `build/dinput8-before-phaseguard-20260919-152136.dll`.
(An earlier install of this change was `C16E45BE...B2D9287E`; the comment
corrections above changed the binary, so it was reinstalled to keep source and
installed in sync.)

⚠ NOT YET HEARD IN-GAME, and the thing to listen for is the IN-MATCH half. The
offline suite proves the guard rejects the captured win-quote state. It does not
prove the gauges still sound correctly DURING a round on this build -- and since
no between-match misbehaviour was ever reported, over-guarding is the only way
this change can make things worse. If the health or meter tone has gone quiet
mid-round, this commit is the cause.

**⭐ SECOND WIN-QUOTE CAPTURE: the state is confirmed, and a WINNER field
(2026-09-19)**

`winquote-ken-02.bin`, PID 8072, different session and arena from the first,
taken while a win quote was on screen. User: **Ken won**.

⭐ PHASE BYTES ARE IDENTICAL to the first capture: `02 01 02 03 02 00 00 00`.
Two independent sessions agreeing makes byte 3 = `03` a trustworthy
discriminator for the post-match quote screen, not a one-off reading.

⛔⛔ `B+1` IS NOT THE WINNER. It is each side's ACTIVE fighter, and it reads
Ken on **P2** here. The first capture's `B+1` (P1 Sagat) happened to be the
winner, which would have made "read B+1 for the winner" look correct on one
sample. Two captures were needed to see it.

⛔ `B+0x574` WAS REFUTED, and this is the useful part. It reads P1=1 / P2=2 in
both captures and looked exactly like a best-of-three round count -- so it
PREDICTED P2 won both. The user confirmed **Sagat (P1)** won the first match.
⭐ The prediction is what killed it: a field that merely "looks like a round
counter" survives inspection forever, but one that names a winner can be wrong
out loud. Ask what the field PREDICTS, then check it against something the user
knows.

⭐ THE SURVIVING CANDIDATE IS **`B+0x5D3`**: 1 on the winning side, 0 on the
loser, in both captures, matching the user's report each time. It is `0/0` in
EVERY in-match capture (`timer-99`, `hp-full`, `hp-low`, `meter-full`,
`state-b`), so it is set at match end rather than carried through the fight.

Rejected alongside it: `B+0x5CC`/`0x5CD` (the already-traced RATIO bytes --
they differ here because the two matches had different team setups, not because
they mark a winner; they also take the value 2 in-match) and `B+0x60D` (reads
1/0 during `timer-99`, so it is not quote-specific).

⚠ TWO captures only, and `0x5D3` sits inside the per-player config block beside
`0x5D0` (groove) and `0x5D1` (team size). It has made two correct predictions;
it has not been shown to be a winner flag rather than something correlated with
one. ▶ The next quote screen settles it: capture, and say who won BEFORE
looking. A third correct prediction on a P1 win makes it shippable.

⭐⭐ SIMPLER THAN SIDE-ATTRIBUTION (user, 2026-09-19): "There can only be one
winning character quote at a time, which is determined by the winning character
for either side." So the reader does not need to work out WHICH SIDE won and
then index that side's `B+1` -- it needs one question answered, "whose quote is
this", and the answer is a single character ID.

⭐ THE WINNING CHARACTER ID IS IN R DIRECTLY. Searching both captures for a byte
equal to the known winner (9 Sagat, 1 Ken) leaves four global slots, all of
which carry it in both captures:

    R+0x02C64   R+0x05931   R+0x0A98C   R+0x0F6B1

They are not one array (deltas 0x2CCD / 0x505B / 0x4D25) -- four separate
records, consistent with the quote screen being assembled from several
structures (text, portrait, audio).

⭐ `R+0x05931` is the cleanest: it reads **0 in every non-quote capture**
(`timer-99`, `hp-full`, `hp-low`, `meter-full`, `state-b`, `ryu-01`) and holds
the winner's ID only on the quote screen. `R+0x02C64` is NOT usable -- it holds
unrelated values (5, 8, 1) during matches.

⚠ Still two captures. A byte matching a known value twice is weak evidence on
its own; what makes this stronger than the `B+0x574` guess is that the value is
the ID ITSELF rather than a flag needing interpretation, and that it is zero
outside the quote state. One more quote screen confirms it.

**⚠ NARROW CORRECTION (2026-09-25): the sentence above is false as written, but
the FIELD IS FINE.**

"0 in every non-quote capture" was checked against the **6** captures named in
its own parenthesis. Over all **32**, `R+0x05931` is nonzero in nine:

    16  crouch-01, jump-01, knockdown-01, meter-empty, meter-spent, pos-right-01
     1  return-ryu-06, up-from-ryu-04, up-from-ryu-idle-05

⚠ Note `1` is Ken's ID. So the byte is **not SELF-GATING** — it cannot be used
to detect the quote screen. That is all this corrects.

⛔⛔ I FIRST WROTE THIS UP AS A REFUTATION OF THE FIELD and recommended parking
the feature. That was wrong, and the user corrected it in one line: **win quotes
cannot happen during a round.** The nine captures all sit in states the phase
gate excludes, so the failure I described — "it would announce a Ken win quote
mid-round" — is impossible. I invented a scenario the state machine forbids.

⭐⭐ THE LESSON, and it is the more useful half: **check a proposed failure
against the system's own structure before reporting it.** A field that misbehaves
only in states the reader never runs in has not misbehaved. I had the phase
machine documented in this very file and did not apply it.

⭐ THE GATE IS STRONGER THAN THE ORIGINAL CLAIM. Across all 36 captures,
`02 01 02 03` occurs in **exactly the two** taken while a quote was on screen;
every in-match capture is `02 01 02 02`, every selection screen `02 01 01 01`.
Gate on the phase, then read the ID.

**⚠ The `+2` index lead is weaker than recorded, though.** It rests on two
records agreeing on bytes 0..2, described as "the game's own redundancy". Across
the 32 non-quote captures, byte `+2` agrees in 17 and bytes 0..2 agree in full in
10 — so that agreement is common rather than specific to quote time. ⚠ This is a
weaker denominator than it looks (those captures are mostly in-match), but it is
enough to say the agreement is not evidence. `+2` stays a LEAD; the test that
settles it is unchanged and unrun: same character winning twice, does `+2` vary?

**⭐⭐ SHIPPED (2026-09-25): the win-quote reader**

- `tools/gen_quote_table.py` -> `src/quote_table.h`, all 384 quotes compiled in.
  It runs `cvs2_quotes.py`'s own controls (5 live-confirmed phrases, the
  multi-line join, non-printable and distinctness tests) and **refuses to write**
  if they fail, then re-parses the written file to catch C-escaping damage.
  ⭐ The mod therefore never dereferences a guest pointer to produce speech: at
  runtime it reads two bytes and speaks a string it already holds.
- `cvs2_quote_observe()` in `src/cvs2_select.c`: gate on phase, read the winner
  from `R+0x05931`, speak once per visit.
- **DEFAULT: the winner AND the quote** — "Sagat wins. Practice all you want!..."
  Opt out with `cfc2_cvs2_quote_text.off` for the winner's name alone.

⛔⛔ THIS SHIPPED THE WRONG WAY ROUND FIRST, and the correction is the point of
the entry. I defaulted to the winner's NAME only, gated the quote text behind an
opt-in switch, and reported the feature as done. The user: *"I specifically said
it should read the displayed win quote. That's what we have been looking for
this whole session."*

⭐⭐ THE ERROR: I shipped the half I could PROVE in place of the half that was
ASKED FOR, and let an unresolved risk silently redefine the deliverable. The
quote text was already compiled into the binary; the only thing standing between
the user and the feature was a default I chose on their behalf.

⚠ The caution was not baseless — a wrong quote is undetectable to a blind user.
But the right move was to **put the trade to the user**, not to resolve it
quietly and announce success. They took the risk knowingly in one word: the text
is certain, only the choice among eight is not, and every display is logged so
ordinary play settles it.

⭐ THE RULE: when uncertainty makes you want to ship less than was asked for, say
so and let the user decide. A default chosen to protect them from a risk they
were never told about is a decision taken away from them.

Cases 7f/7g in `tools/test_cvs2_select.c`, both sabotage-verified:
- removing the phase gate -> 7g fails (all nine non-quote captures speak);
- `WQ_WINNER` -> `B+1` (the refuted reading) -> 7f fails **on Ken only**, passing
  the Sagat capture. ⭐ Exactly why two captures were needed.
- ignoring `+2` and always speaking quote 0 -> 7f fails on both captures.
⚠ 7f asserts the spoken string is EXACTLY `cvs2_quotes[id][+2]` — that the
plumbing from capture to speech is right. It does NOT assert that `+2` is the
correct index; only live play can answer that.

▶ **THE CHECK THAT SETTLES `+2`, and it needs nothing the user has to see.**
Every display logs `cvs2-quote: winner id N ... quote index M of 8`. Find two
wins by the SAME id in `CFC2Access.log`:
- index VARIES -> `+2` is the random pick, the spoken quote is the real one;
- index CONSTANT -> `+2` is a property of the character, the lead is dead, and
  this must revert to the winner's name alone.

▶ NOT YET HEARD IN-GAME. Win a match in CVS2 and listen for the quote.

⚠ `B+0x5D3` (winner side) remains correct on both captures and is worth keeping
in this record, but it is NOT needed if the character ID is read directly. Do
not build the side-attribution path when the simpler field exists.

**The 1-in-8 quote index: a lead, and the check that must NOT be asked for
(2026-09-19)**

⛔⛔ A METHOD ERROR WORTH RECORDING. I proposed confirming the index by having
the user tell me the first few words of the quote on screen. The user: "I can't
see it." That is the entire premise of this project -- they are blind, and the
quote screen is unreadable to them, which is WHY it needs reading aloud.
The one input I asked for is the thing the mod exists to provide.

⭐ THE RULE: before asking the user to confirm something, check whether the
answer is available to them at all. Anything drawn as sprites by the emulated
game is NOT -- that is the definition of world 2. Speech, audio cues and the
log are observable to them; on-screen text is not.

⚠ This also invalidates the "capture several and name the quote" plan recorded
above for the four-way constraint. Ground truth for in-game TEXT cannot come
from the user. It must come from the data or from the emulated code.

THE LEAD, derived without any user input:

The four global slots holding the winner ID (above) are records, and the bytes
following the ID differ between the two captures:

    winquote-01  (Sagat, ID 9)   R+0x05931: 09 01 06 01 ...
                                 R+0x0F6B1: 09 01 06 00 ...
    winquote-ken-02 (Ken, ID 1)  R+0x05931: 01 00 03 02 ...
                                 R+0x0F6B1: 01 00 03 02 ...

⭐ TWO INDEPENDENT RECORDS AGREE on bytes 0..2 within each capture (`09 01 06`
and `01 00 03`). That agreement is the game's own redundancy, not a byte picked
by eye, and it is the best evidence available here.

Byte **+2** is a valid quote number in both (6 for Sagat, 3 for Ken) and decodes
to real, coherent quotes:
    Sagat[6] "Practice all you want! You will never be good enough to defeat me!"
    Ken[3]   "Maybe next time you should think about watching my hands!!"

⚠⚠ BUT THIS IS NOT CONFIRMED, and plausibility is not evidence: ANY index 0..7
decodes to some plausible quote, which is exactly what a wrong answer looks
like. Speaking the wrong quote is indistinguishable from the right one to this
user -- the failure class this project has paid for repeatedly.

⛔ Byte +1 is NOT the team slot: Sagat's +1=1 matches his slot 1, but Ken's +1=0
while Ken is slot 1 in both teams. Unknown; do not guess it.

⛔⛔ ROUTE 1 (find the writer in host code) IS CLOSED. Searched and negative:

- The records are NOT addressed by a literal displacement: `0x5931`, `0x5933`,
  `0x5930`, `0xF6B1`, `0xF6B3` occur ZERO times as u32 immediates in the CVS2
  range. They are reached through a pointer.
- ⭐⭐ THE DECISIVE ONE: the shift/XOR RNG `0x1400BD7D0` has **79 host callers
  and NOT ONE is inside `0x1406A0000..0x140740000`**. Its wrapper `0x14003AE70`
  has 9 callers, of which the single CVS2-range hit (`0x1406A986E`) is the
  arena initializer already traced -- session setup, not a per-match pick.
  A random quote choice made by host code would have to call one of these.
  It does not. **The pick happens in emulated SH-4**, like the text itself.
- ⚠ Capstone's linear sweep decoded 1,507 instructions and stopped after
  **0.9%** of the range, exactly as CLAUDE.md warns. Any "0 occurrences" from a
  linear sweep here is an artifact; the counts above come from byte-encoding
  searches (`find_callers.py`), which is the method that works.

▶ WHAT REMAINS, and it needs nothing read aloud:
Capture several quote screens for the SAME character. If `+2` VARIES while the
character is constant, it is the per-display random pick and the mapping can be
trusted; if it is CONSTANT for a character, it is a property of that character
and NOT the index. ⭐ This requires only that the same fighter wins more than
once, which happens naturally, and the user need only say WHO won -- something
the mod already speaks.

⚠ If `+2` turns out constant per character, stop: the index is then not in
these records, and with the host-code route closed the remaining option is
observing guest memory around the quote region during display, which is a
larger piece of work than the payoff justifies.

▶ NEXT, in value order:
1. ENDINGS: text and ID mapping are done offline and `B+1` is readable. ⚠ The
   user notes reaching one requires playing through a whole arcade run, so an
   ending capture is EXPENSIVE -- do not plan around getting one casually. The
   trigger is the only missing piece; byte 3 is the thing to test against if a
   run ever happens.
2. A second win-quote capture with the user naming the quote, to start the
   four-way constraint on the 1-in-8 index. Cheap by comparison -- quotes occur
   after every match.

## 2026-09-19: positional projectile audio — deferred, and why

The user asked whether a projectile's sound (Ryu's hadouken was the example)
could follow the fireball across the screen instead of staying at the point of
origin. It can, but not by moving anything that exists today, and the work sits
behind the current build order. Recorded so the constraint is not re-derived.

**⛔ THERE IS NO HOST-SIDE AUDIO SOURCE TO MOVE.** The SFX is a sample the
EMULATED sound hardware mixes -- QSound on CPS-2, AICA on NAOMI. The host plays
the DSP's output; it never sees a per-object voice with a position. What the
user hears as "at Ryu's position" is the arcade board's own panning, which on
these boards is by player side, not by object X. So "reposition the existing
sound" is not a small change -- it is not a change that can be expressed at all.

The only route is: **read the projectile's X from guest RAM and emit our own
beacon.** That fits the settled audio design (memory `cfc2-audio-design-decision`)
as a third beacon alongside the two player beacons -- pan = X, with a timbre
distinct from both, the same separation test case 7b already enforces for health
vs super. The arcade's original sound keeps playing at its static pan; whether
that is acceptable alongside a moving beacon is a listening question, not a
design one, and should not be decided in advance.

**⛔⛔ DEAD RECKONING WAS CONSIDERED AND REJECTED -- by the user, on the
strongest available ground.** The cheap approximation was to detect the launch
and extrapolate from a constant velocity, never finding the object in RAM. It
fails because **projectiles do not share a speed**: the constant would have to
be measured per character, per move, per game. And the table would be silently
wrong exactly where it matters -- EX and level-scaled versions travel at
different speeds than the base move, Blanka balls and Yamazaki snakes are not
constant-velocity at all, and anything reflected or absorbed diverges
immediately. ⭐ A hand-measured table that is wrong on the interesting cases is
worse than an absent feature: to this user a mispositioned beacon is
indistinguishable from a correct one, which is this project's recurring failure
class. Do not revive this as a "first cut".

**▶ WHAT THE REAL WORK IS: the active-object table.** The blocker is one
specific unknown -- where the list of live entities lives in guest RAM, its
stride, and the fields for active / type / owner / X. Everything else is small
once that lands.

⚠ It must be found PER GAME. CVS2's layout implies nothing about CVS1's or
CFJ's; CVS2 is SH-4, the CPS-2 titles are 68k.

⭐ The method is the one that is 4-for-4 on this project and 0-for-4 against:
find the code that WRITES the table and read the offsets out of the
instructions. Do not scan guest RAM for something projectile-shaped -- a moving
X with a plausible range is the most abundant shape in a fighting game's memory,
and the first guest-RAM scan here was already a false positive for exactly that
reason (see the `0x0222` run above).

⚠ Scale honestly: this is comparable to the character-highlight trace, not to
adding a field to an existing reader. It is worth doing -- positional projectile
audio is close to the point of the whole mod -- but it is not a small task, and
nothing above it in the build order has been finished.

## 2026-09-19: player X position — a two-field candidate, derived offline

⛔⛔ **REFUTED THE SAME DAY. DO NOT BUILD ON THIS SECTION.** `B+0x0D8` is a
64-bit POINTER, not a fine-grained X, and the "20:1 law" below is arithmetic on
the low 16 bits of it. See "THE X CANDIDATE IS REFUTED" at the end of this
document. The section is kept because the METHOD error is worth carrying, not
because the offsets mean what it says.

Position is the next piece of the audio design: both player beacons currently
pan by OWNER at a fixed +/-0.85 (`src/audio.c`, "Pan is fixed by OWNER, matching
the HUD"), which is a placeholder. Pan should follow screen X.

⭐ DERIVED WITH ZERO GAME LAUNCHES, from captures already in
`build/cvs2-state/`. No new run was spent to get this far.

**Method.** Diffed the player struct `B = R+0x1EBA8 + player*0x678` across the
seven captures whose phase byte 4 is 4 (in-round). The doc's comparability rule
was respected: `hp-full` (substate 3, intro) and `hp-zero` (substate 5, KO) were
EXCLUDED, not merged. 113 of 828 u16 slots vary for P1, 141 for P2.

**THE CANDIDATE: `B+0x0D2` (u16) is screen X, corroborated by `B+0x0D8`.**

Values run 98..377 for P1/P2 across captures -- a plausible screen range with
sensible fighter separation. ⚠ On its own that is shape, and shape is exactly
what nearly shipped `B+0x246` as health. The evidence is the SECOND field:

    B+0x0D8  ==  20 * B+0x0D2  +  c

⭐⭐ EXACT, zero residual, on all 14 player-captures. `c` is CONSTANT within a
match and changes between matches:
- session `root=0x15ba1000`: P1 c=2480, P2 c=4336; the third capture in that
  session (`hp-ken`) is a different match and has its own pair, P1 c=11680,
  P2 c=19384.
- session `root=0x15541000`: P1 c=2480, P2 c=-2196.

Two independent fields agreeing to the bit is the game's own redundancy -- the
same standard the win-quote bytes were held to -- not a value picked by eye. The
natural reading is that `0x0D2` is a coarse X and `0x0D8` a finer-grained or
camera-relative X sharing one origin per match, `c` being that origin. A single
arbitrary field would not sit in exact 20:1 lockstep with another.

⛔⛔ WHAT THIS DOES NOT ESTABLISH, and must not be skipped:
- **Which field is the SCREEN coordinate and which is world/stage.** The pan
  needs screen-relative X; if `0x0D2` is a world coordinate, a scrolling stage
  makes the beacon wrong exactly when the fighters move, which is when it
  matters. The captures cannot separate these because `c` is constant within
  each match -- i.e. NO CAMERA MOVEMENT IS SAMPLED.
- **The range and the edges.** 98..377 is observed, not bounded. Mapping X to
  pan needs the real screen extent, and guessing it mispans everything.
- **Y.** `0x0D4`/`0x0D6` are zero in every capture -- all sampled fighters are
  grounded. That is an absent signal, not a negative result.
- **Sign/wrap at the left edge.** No capture sits near x=0.
- ⚠ `0x0D0` is a small 1..6 counter in the same neighbourhood; it is NOT part
  of the coordinate and must not be folded into a dword read.

▶ WHAT WOULD SETTLE IT, and it is cheap -- the discriminator is CAMERA MOVEMENT:
capture two in-round snapshots in ONE match with the fighters at opposite ends
of a scrolling stage, having WALKED there rather than teleported.
- If `c` shifts between those two captures while the fighters' on-screen
  placement is comparable, the origin is the camera and `0x0D2` is a WORLD
  coordinate -- screen X is then the derived quantity, not the field.
- If `c` holds and `0x0D2` spans a much wider range, `0x0D2` is screen-relative
  and can drive pan directly.
Either way the same two captures also bound the range, which is the other
missing piece. ⭐ The user need only walk to each corner; nothing is spoken.

⚠ Do NOT wire pan to `0x0D2` before that, however good the 20:1 agreement is.
The agreement proves the two fields describe one quantity; it says nothing about
which SPACE that quantity is in, and a beacon that pans correctly in training
and wrongly on a scrolling stage is the failure class this project has paid for
repeatedly -- a wrong position is indistinguishable from a right one.

⭐ Once settled, the reader is SMALL: the frame hook `0x1406AA7A0`
(`src/cvs2_frame.c`) already runs per frame and publishes u16s from this exact
struct lock-free. Position is two more reads in an existing detour, then pan
replaces the constant in `src/audio.c`. No new pointer chain, no new hook.

**The capture command for that experiment** (`--position`, added 2026-09-19):

    python tools/cvs2_state_snapshot.py PID --out build/cvs2-state/pos-left-01
    python tools/cvs2_state_snapshot.py PID --out build/cvs2-state/pos-right-02

Walk to one corner, capture; walk to the other corner IN THE SAME MATCH,
capture. It prints both players' `x`, `x_fine` and `c`, then states the reading:
`c` shifted -> world space; `c` held -> screen-relative.

⭐ It adds NO new memory reads -- the fields were already inside every capture
this tool has ever written; that is how the candidate was derived offline. What
it adds is a capture-time gate and a capture-time readout.

⛔ It REFUSES a capture whose phase byte 4 is not 4 (writing nothing, so the
label stays free for the retry): an intro or KO capture cannot be compared with
an in-round one, and the refusal names which it got. Verified against the
existing fixtures -- `hp-full` (byte 4 = 3) and `hp-zero` (byte 4 = 5) are
refused, the in-round ones accepted -- and `position_fields()` reproduces the
c values recorded above (2480/4336, 11680/19384, 2480/-2196) from the captures
already in tree.

⚠ Both captures must come from ONE match. `c` changes between matches by
design, so a cross-match pair shows a shifted `c` for a reason that has nothing
to do with the camera -- it would read as "world space" no matter the truth.

## 2026-09-19: ⛔⛔ THE X CANDIDATE IS REFUTED — `B+0x0D8` IS A POINTER

**The `B+0x0D2` / `B+0x0D8` finding recorded earlier today is WRONG and must
not be built on.** Refuted offline, same day, before any pan code was written.

**What the instructions say.** `0x1406B79D7` writes `0x0D2`, and the VERY NEXT
instruction writes `0x0D8`:

    0x1406B79D3  movzx eax, word ptr [rdx + 4]
    0x1406B79D7  mov   word  ptr [rbx + 0xd2], ax
    0x1406B79DE  mov   qword ptr [rbx + 0xd8], rdx     <-- 64-bit POINTER

Every `mov` writing `0x0D8` across the CVS2 range is a **qword store of a
register** (`rax`/`rcx`/`rdx`/`r9`; 18 of them). `0x0D2` is a u16 COPIED from
`[rdx+4]` -- a field of the record `rdx` points at. The pair is
"cached field + the record it came from", not "coarse X + fine X".

Read as the qword it is, `B+0x0D8` holds guest pointers: P1 `0x107F13D8`,
`0x106913C4`, `0x106F1400`; P2 `0x10971898`, `0x108114E0`. The high half is
constant per player per match; only the low bits move.

⭐⭐ **THAT IS EXACTLY WHY THE "LAW" LOOKED PERFECT.** `0xD8 == 20*0xD2 + c`
held with zero residual because I was doing arithmetic on the LOW 16 BITS OF A
POINTER, and `c` -- the "per-match origin" I was going to use as the camera
discriminator -- was just the pointer's base, which is constant within a match
because the allocation is. The proposed experiment (walk to a corner, watch `c`
shift) could NEVER have worked: `c` has nothing to do with the camera.

⚠⚠ **THE LESSON, AND IT IS NOT A NEW ONE HERE.** Two fields agreeing to the bit
was treated as the game's own redundancy. It is not redundancy when the
relationship is an ARTIFACT OF MISREADING THE WIDTH of one of them. A u16 window
onto a qword will correlate beautifully with anything derived from that qword.
⭐ The guard that would have caught it instantly: **before believing a
relationship between two fields, establish each field's WIDTH from the
instructions that write it.** Neither width was checked; both were assumed from
the diff tooling's u16 stride.

⚠ The live probe had already contradicted the reading -- values drifting while
the fighters stood still, and `pos-right-01` (both fighters in the RIGHT corner)
reading P1 x=132 c=2480, indistinguishable from `meter-full` in a different
session and a different stage position. That contradiction is what stopped the
walk-to-the-other-corner capture from being spent. ⭐ Stopping on a measurement
that does not fit, instead of collecting the second half of a designed
experiment, is what kept this cheap.

**Status of player X: UNKNOWN again.** `0x0D2` is a u16 copied from a pointed-to
record and may still be position-adjacent, but nothing here establishes it, and
the 26-offset enumeration cited above ("the rest are position, velocity and
timers") is a SUMMARY, not a mapping -- it names no offset.

▶ NEXT, and it stays offline: `0x0D2`'s source is `[rdx+4]` in a record whose
pointer lands in `0x0D8`. Trace what builds that record. The writers are few --
10 for `0x0D2`, 18 qword `mov`s for `0x0D8` -- and they cluster at
`0x1406A48E3`, `0x1406B77xx`-`0x1406B7Dxx`, `0x14071A9xx`, `0x14073FFxx`.

**The tool: `tools/find_field_access.py`** (new). Finds every instruction
touching a displacement in the CVS2 range, by byte-encoding search plus a
capstone decode CONFIRMING the displacement -- never a linear sweep.
- ⭐ POSITIVE CONTROL, run before trusting any result: `0x16C --writes-only`
  independently finds both documented super-meter sites, `0x1406C26AA` (gain,
  inside the documented `0x1406C26A0` sequence) and `0x1406C4241` (spend, and
  as a `sub`, matching the doc). A scanner that finds nothing and a broken
  scanner are otherwise identical.
- ⚠ It collapses OVERLAPPING decodes: a `66` prefix makes the same bytes decode
  as `word ptr` at one offset and `dword ptr` one byte later, and both carry the
  displacement. Reporting both inflated the meter control from 41 to 72 and
  would make one write look like two independent writers.
- ⚠ Zero hits is NOT automatically a negative: an access through a
  register-held offset carries no displacement and cannot be found this way.
  The tool says so rather than printing a clean-looking zero.

## 2026-09-19: ⭐⭐ FIGHTER POSITION FOUND — `B+0x54` (X) and `B+0x58` (Y)

Derived OFFLINE from the existing dump and the captures already in
`build/cvs2-state/`. **Zero game launches.** This replaces the refuted
`B+0x0D2`/`B+0x0D8` candidate above.

    X        B+0x54  (s32, 16.16 fixed point)  velocity B+0xA8  accel B+0xB0
    Y        B+0x58  (s32, 16.16 fixed point)  velocity B+0xAC  accel B+0xB4

⭐⭐ **THE METHOD, and it is the one this project is 4-for-4 with: ask what
position IS, not what it looks like.** Every failed candidate here
(`B+0x246` health, `B+0x0D2` X) came from diffing captures for plausible
VALUES. A coordinate has a structural definition instead — it INTEGRATES a
velocity that itself integrates an acceleration. That is a query over
instructions, and it has one answer.

`tools/find_integration.py`: every `add/sub [reg+disp], reg` inside the 0x678
player struct whose addend was just loaded from the SAME base register.
`+0x54` has **16 sites, every one fed by `+0xA8`**; `+0x58` has 5, every one
fed by `+0xAC`. Two adjacent coordinates, each with exactly one velocity.
Disassembling the site resolved the whole routine at **`0x140708F50`**:

    0x140708F50  mov edx, [rcx+0xA8]     ; X velocity
    0x140708F56  add [rcx+0x54], edx     ; position += velocity
    0x140708F59  add edx, [rcx+0xB0]     ; velocity += acceleration
    0x140708F5F  mov [rcx+0xA8], edx
    0x140708F65  mov edx, [rcx+0xAC]     ; then the identical block for Y
    0x140708F6B  add [rcx+0x58], edx
    0x140708F6E  add edx, [rcx+0xB4]
    0x140708F74  mov [rcx+0xAC], edx
    0x140708F7A  ret

A field that accumulates a velocity which accumulates an acceleration is a
coordinate. Nothing else in the struct has that three-level shape. A second
copy of the same block exists at `0x140709012` on a different base register.

**Live corroboration, from captures taken for other work:**

- ⭐ **The match-start mirror.** In all three start captures (`hp-full`,
  `meter-full`, `timer-99`) P1 reads exactly `-6553600` and P2 exactly
  `+6553600` — i.e. **-100.00 and +100.00**, summing to zero. Fighters begin
  equidistant from centre, facing each other. An arbitrary field has no reason
  to be mirrored to the bit across three separate sessions.
- Separation across 12 in-round captures runs 39.55 .. 320.06 units — close
  quarters to full screen width, never zero, never absurd.
- `B+0x58` (Y) is **0 in every in-round capture**: all sampled fighters are
  grounded, exactly as expected. ⚠ That is an absent signal, not a proof; no
  capture samples a jump.
- Velocities at `+0xA8` are clean speeds: ±2.50, ±3.00, ±3.06, ±3.50 per
  frame. A walk is a few units per frame.

⭐ **THE FORMAT IS 16.16**, and getting there took one wrong turn worth
recording.

I first tested "are the values exact multiples of the unit", which said 24.8
(24/24 exact) over 16.16 (9/24). ⛔ **That test is monotone in the wrong
direction and can only ever favour the COARSEST candidate**: everything
divisible by 65536 is also divisible by 256, so a coarser unit passes
whenever a finer one does. It cannot distinguish "the real scale" from "any
factor of the real scale".

The test that DISCRIMINATES is the physics, since velocity is integrated once
per frame at 60fps:

    scale    start pos    stage span    walk speed (units/s)
    24.8      25600.00      180352      38400 .. 53760
    20.12      1600.00       11272       2400 ..  3360
    16.16       100.00         704        150 ..   210

Only 16.16 describes a fighting game: fighters start 100 units apart on a
~700-unit stage and walk at 150-210 units/second. ⭐ **Prefer the test whose
answer changes when the hypothesis changes.** "Divides evenly" was true of
several scalings; "implies a believable fighter" is true of exactly one.

⛔⛔ **WHAT THIS DOES NOT ESTABLISH — do not wire pan yet.**

- **World vs. screen space is still OPEN**, and it is the same gap that made
  the last candidate useless. These are WORLD coordinates as far as the
  evidence goes: the integration routine knows nothing about a camera. Screen
  X requires subtracting a camera origin that has NOT been located. On a
  scrolling stage a world-X beacon is wrong exactly when the fighters move,
  which is when it matters.
- **The range is observed, not bounded.** -320..+320 is what 12 captures show.
  Mapping X to pan needs the real stage extent.
- ⚠ Do not read `+0x54` as a u16 or fold `+0x50` into it. It is a signed
  32-bit value and it goes NEGATIVE for the whole left half of the stage — a
  u16 window would wrap to ~65000 there, which is the width-misreading that
  produced the `0x0D8` pointer artifact.
- ⚠ `find_field_access.py 0x54` returns 136 hits, mostly `adc` — those are
  mid-instruction decodes of SSE data, not writers. `0x54` is an ordinary
  displacement in many unrelated structs. The anchored routine is the
  evidence; that raw count is not.

### ⭐⭐ The camera: NOT NEEDED — pan is derivable from the two fighters

The X beacon needs `screen_x`, not world X. The obvious route was to find a
camera origin. **Searching for one failed, and then turned out to be
unnecessary.**

What was tried, and why each is a weak negative (recorded so it is not
re-run):
- Subtractions applied to a value loaded from `+0x54`: only 3 sites, all
  object-relative placement (`[rdi+0x54] - [rbx+0x444] -> [rbx+0x54]`, i.e.
  spawning an effect relative to its owner at `0x14071E1F3`). Not a camera.
- Correlating every 4-byte field in `R` against the fighters' midpoint across
  12 captures: **zero fields** with |r| > 0.97.
- ⚠ Both negatives are weak for the same reason the win-quote pick was: CVS2
  is emulated, so the camera may well be computed in guest SH-4 code, where
  no host instruction performs it at all.

⭐⭐ **THE INSIGHT THAT REMOVES THE BLOCKER: in a 2D fighter the camera is a
FUNCTION of the two fighters.** It tracks their midpoint and clamps at the
stage edges — that is what keeps both fighters on screen. So screen position
is computable from the two world X values alone:

    STAGE = 352        stage half-width, in world units
    SCREEN_HALF = 192  half of CVS2's 384-pixel width
    camera   = clamp(midpoint, -(STAGE - SCREEN_HALF), +(STAGE - SCREEN_HALF))
    screen_x = world_x - camera
    pan      = screen_x / SCREEN_HALF          -> -1.0 .. +1.0

**STAGE = 352 is MEASURED, not assumed.** Across 24 in-round readings the
extremes are -352.00 and +352.50, and **three independent readings from
different sessions and both players sit at exactly ±352.00** — a hard corner
wall. (`hp-ken` P1 and `state-a` P1 at -352.00; `pos-right-01` P2 at +352.00.)

⭐ **THE CHECK THAT MAKES IT SHIPPABLE, and it is a real falsifiable
prediction.** 352 and 192 were derived independently of each other and of the
model. Applied to all 12 in-round captures:

- **0 of 12 captures put a fighter off screen** (|pan| > 1). Nothing forced
  that; the arithmetic could easily have produced out-of-range values.
- Every corner capture lands at **exactly pan = ±1.00** — a fighter in the
  corner is at the screen edge, which is what "corner" means.
- Match start gives a symmetric ±0.52 for fighters 100 units apart.
- Max observed separation is 320.1 against a 384-wide screen, so both
  fighters always fit — exactly the invariant the camera exists to maintain.

⚠ WHAT IS STILL ASSUMED, and how it would show up:
- **SCREEN_HALF = 192** is CVS2's native 384-pixel width, not a measurement
  from this game's rendering. If the emulator's visible window differs, pan
  is scaled slightly — a uniformly too-wide or too-narrow field, not a
  wrong direction.
- **STAGE = 352 may be PER-STAGE.** All captures come from a small number of
  stages. A wider stage would make fighters seem to reach the edge early.
  ⭐ The model degrades gracefully: clamping pan to ±1.0 keeps a fighter at
  the edge rather than panning past it.
- **The camera may lead or lag** rather than sitting exactly on the midpoint
  (many fighters bias toward the active player). That shifts both beacons
  together by a small amount; relative separation stays right.

⛔ None of these can be settled offline, and all are second-order. The
first-order question — does a fighter in the left corner pan hard left — the
model answers correctly on every capture in hand.

### ⛔⛔ FIRST ATTEMPT WAS WIRED TO THE WRONG THING (2026-09-19)

User report: **"I didn't hear a new sound."** Correct, and the log confirmed
the build was current and the frame hook running at 60/s.

I wired fighter position to the **GAUGE pan**. That was wrong twice over:

1. **Gauge pan is FIXED BY OWNER on purpose** — P1 hard left, P2 hard right,
   mirroring the on-screen HUD bars. The design memory is explicit that these
   are two layers: *"gauge pan is FIXED to the HUD (owner), beacon pan MOVES
   with the fighter (position)"*. I collapsed them.
2. **Gauges sound only while a value CHANGES** (~140 ms hold). So a fighter
   walking across the stage moved nothing and produced no sound at all.

⭐⭐ **The actual design was never built.** `cfc2-audio-design-decision` has
always specified *"each player gets their own persistent looping cue"* that
*"NEVER goes silent"* during a match. There was no beacon in the mixer — only
gauge tones. I read the position work as the last missing piece when the
consumer for it did not exist.

⚠ **THE LESSON.** I checked that pan reached the mixer and that the captures
validated the model, and both were true — but never asked *what will the user
actually hear?* A change can be correct at every layer I tested and still
produce nothing, when the thing it feeds is silent by design. Trace a feature
to the ear, not to the last function you changed.

**Now built:** two persistent beacon voices (`audio_beacon_set`), sine, P1 at
440 Hz and P2 at 520 Hz — between the gauge registers (health 150-420
triangle, super 600-1500 square) and a minor third apart so they neither fuse
with each other nor with a gauge. Fed EVERY FRAME, no change rule, no hold.
Gauge pan restored to fixed-by-owner. Stance is a pitch multiplier on the
base, currently pinned to `AUDIO_STANCE_STAND` because Y reads 0 in every
capture and no state byte separating crouch from stand has been found — a
constant is honest, a guessed ladder would mislead.

**Shipped (built, not yet heard).** `src/cvs2_frame.c` reads both fighters'
`B+0x54` as **s32** on the existing frame hook, derives pan with the model
above, and feeds `audio_set_pan()`; `src/audio.c` uses it per voice instead of
the fixed ±0.85. `audio_reset_pan()` restores the HUD-matching default when a
match ends, beside the existing gauge reset.

⚠ `audio_set_pan()` is deliberately **lock-free** — a single clamped float
store. Every other setter in `audio.c` takes the mixer critical section, and
taking it from the frame thread is precisely what made the health tone
crackle. Pan is also fed only when it moves by more than 1/128, so a walking
fighter costs a handful of calls per second rather than 120.

⭐ Tests: `run_audio_test.ps1` grew to 24 checks, asserting on RENDERED
SAMPLES, not on the stored float — a setter that records pan while the mixer
ignores it would pass any check of the variable. Sabotage-verified:
- restoring the fixed `±0.85` in `update_voices` fails 11b and 12a. ⚠ Note
  11a, 12b and 14 still PASS under that sabotage, because the fixed value
  happens to be left-panned; that asymmetry is why both directions are
  asserted.
- setting `SCREEN_HALF` to 240 fails case 13, which pins the model against
  the real measured capture values.

⚠ A test-authoring trap worth keeping: the mixer only sounds for
`HOLD_BLOCKS` after a value CHANGE and ramps gain in, so measuring one block
right after a pan change reads a tone still climbing out of silence. The
first version of these cases failed for that reason and not because of pan.
`pan_peaks()` renders past the ramp before measuring.

Beacon build installed after confirming CFC2 was closed. Built/installed
SHA-256 match:
`BE7724C0D96DF38052B76615FF4E6C3F901F494D5A672A57637C364AF07E9A31`.
(The earlier gauge-pan build was
`2187B5179B3101C3B07A0A4D093E019B607D601B1B6BEAE75C580F9526D62324`.)

### ⭐⭐ Beacon revision after the first listen (2026-09-19)

User feedback on the first audible beacons, three separate issues:

**1. "Not sure they pan exactly with the characters... a little behind or
ahead"; later clarified as POSITION in the stereo field, not timing.** The
pan law was equal-power sine/cosine, whose channel DIFFERENCE -- the thing
the ear localises by -- is badly non-linear in `pan`:

    near centre   1.7 dB per 1/8 step
    near ±0.9     6.1 dB per 1/8 step
    at ±1.0       ~100 dB (one channel reaches exact silence)

So a step into the corner threw the image ~58x harder than a step across the
middle, and the extremes collapsed into one ear ("too wide"). ⭐ Replaced
with a law that is **linear in dB**: `PAN_SPAN` (15) dB of channel difference
per unit of pan, normalised to constant power. Measured: a uniform **3.75 dB
per quarter-step across the whole field**, and both channels stay audible at
full deflection so the image stays in the listener's head.
⚠ `PAN_SPAN` is now the single knob for "too wide"/"too narrow".

**2. Timing must change instantly.** The output queue was 12 buffers = 70 ms,
over four frames at 60 fps. The comment defending it said latency was
"irrelevant -- these are continuous gauge tones, not input feedback", which
was true for gauges and WRONG for a beacon: a beacon is a spatial readout of
something the player is actively controlling. Reduced to 8 buffers (46 ms).
⚠ Queue depth trades dropout tolerance against positional accuracy; only one
side was being weighed. If crackling returns, raise it back first.

**3. "The tones sound a bit too close to the gauges... something very
sonically different."** The beacons were sines sitting between the gauge
registers. ⛔ Separating by pitch and waveform alone is too weak -- sine,
triangle and softened square are all steady pitched harmonic tones, i.e. ONE
FAMILY, so they read as related however far apart their registers are.

⭐ Beacons are now **plucks** (osc timbre 3), differing in KIND on three axes
at once: inharmonic partials (2.76x, 5.40x -- bell ratios, so they cannot
fuse with a pitched tone), a sharp decaying envelope where the gauges are
flat, and a rhythmic pulse. The two players are told apart by PULSE RATE
(P1 9/s, P2 6.2/s -- deliberately not 2:1, which would lock into one rhythm).

**4. "The pitch needs to raise or lower when the player jumps or crouches."**
The mechanism is built and tested (stance scales the pulse rate, so airborne
is faster/brighter and knocked down slower/duller) but is **NOT yet driven by
the game** -- see the stance section below. Everything currently sits at
`AUDIO_STANCE_STAND`.

⚠ TEST-INSTRUMENT NOTES, both of which produced convincing false failures:
- Zero-crossing "pitch" is the wrong instrument for a pluck (it measures the
  partials, not the repeat rate). `pulse_count()` counts envelope onsets.
- A gauge tone left sounding by an earlier case landed in the pan
  measurement and reported **15.3x unevenness for a law that is uniform to
  1.00x**. ⭐⭐ Measuring a SUM of voices says nothing about one of them, and
  the artifact looked exactly like the bug under test. Isolate first.

Sabotage-verified: restoring cos/sin in `pan_gains()` fails 12f alone.

### ⛔⛔⛔ THAT PLUCK BUILD WAS INAUDIBLE — a pluck needs TWO rates

User: **"I don't hear a sound."** The log confirmed the right DLL, the frame
hook at 60/s through a real match with health changes, so the beacons were
being fed. The bug was in the oscillator.

The pluck generated its partials from the **pulse ramp**:

    e   = phase              (advancing at the PULSE rate, 9 Hz)
    s1  = sin(e * 2pi)       -> 9.0 Hz
    s2  = sin(e * 2pi*2.76)  -> 24.8 Hz
    s3  = sin(e * 2pi*5.40)  -> 48.6 Hz

Every component below or barely at the limit of hearing; P2 at 6.2 Hz was
worse (6.2 / 17.1 / 33.5 Hz). On a normal speaker that is silence.

⭐ **A PLUCK HAS TWO INDEPENDENT RATES** and I collapsed them into one ramp:
the TONE pitch inside each pluck (hundreds of Hz) and how often it REPEATS
(a few Hz). The voice now carries `pulse_phase`/`pulse_rate` beside
`phase`/`freq`; `osc()` renders the tone and the render loop applies
`exp(-7 * pulse_phase)` as the envelope. Measured after the fix: **P1 743 Hz,
P2 498 Hz**. P1 is higher AND faster, P2 lower AND slower -- two agreeing
cues, so the players stay distinct if one axis is masked.

⭐⭐⭐ **WHY 29 PASSING CHECKS DID NOT CATCH SILENCE, and this is the
reusable part.** The suite asserted amplitude (non-zero samples), pulse rate
(envelope onsets), pan (channel ratio), persistence, and match gating. All
were CORRECT. Not one asserted that the output lay in the **audible band**.
"Samples are non-zero" is not "a human can hear it" -- a 9 Hz tone satisfies
every amplitude and ratio test ever written and is inaudible.

New case **12g** measures tone frequency and requires 120 Hz .. 5 kHz.
Sabotage-verified by reproducing the shipped bug exactly (tone driven from
the pulse rate): it reports **17 Hz / 6 Hz** and 12g is the ONLY case that
fails. That isolation is the whole point -- it is precisely why the bug
shipped past everything else.

⚠ This is the SECOND silent-beacon ship in a row, both from testing a
property adjacent to the one that matters (first: pan on a voice that never
sounded; second: a voice sounding below hearing). Before shipping audio,
ask what a LISTENER would notice, then assert that.

Installed: `C672E8BB1D0FD6582FE78891B020B2500043C2E679AD8A297C293A6B267F23A6`.

### ⭐ Pause suppression (user request, 2026-09-19)

The beacons kept sounding over the pause menu. A paused game is not
simulating, so the position they report is frozen and conveys nothing.

⚠ **The phase bytes do NOT distinguish pause**: a paused in-round capture
reads `02 01 02 02 04 ...`, identical to an unpaused one. So suppression keys
on the game's OWN frame counter at `*0x1422DEC30 + 0x18` -- the one the
hooked function increments at `0x1406AA7BB`. Our detour still runs while
paused (the caller keeps calling it); that counter stops advancing, which is
exactly what "not simulating" means.

⛔⛔ **THAT VERSION COULD NEVER HAVE WORKED, and the user reported the
beacons still playing on pause.** The hooked function is the one that
INCREMENTS the counter it watched (`0x1406AA7BB`, `inc dword [rax+0x18]`), so
by the time the detour runs the counter has always just advanced --
`ctr == g_last_counter` is essentially never true.

⭐⭐ **A SIGNAL GENERATED BY THE VERY FUNCTION YOU HOOK CANNOT TELL YOU THAT
FUNCTION IS IDLE.** Watch something the SIMULATION advances, not something
the hook itself advances. The trace even says the function increments that
counter, two lines from where I read the address -- I took the address and
skipped the sentence.

**Corrected:** a composite signature of state that only moves while the game
simulates -- both fighters' X, the round timer, and their animation-frame
bytes (`B+0x0D2`/`0x0D3`; position alone is insufficient because two idle
fighters can stand still, while animation keeps ticking). Unchanged for 30
frames (~0.5 s) = paused.

⚠ 30 frames is deliberately generous. A false positive silences the beacons
mid-fight, which is far worse than half a second of beacon into a pause.

⭐ The report now prints a PAUSE COUNT. The first attempt failed silently
because the log could not distinguish "user never paused" from "pause never
detected" -- a liveness counter is what separates those. Installed:
`760A71D3646BC1E6BAA16AB6AA387B9AF4B2EE2E63272EC57661CD089A7BC244`.

### ⭐ Super meter: triangle (user, 2026-09-19)

After hearing square-vs-saw the user kept health's square and asked for a
TRIANGLE on super. Both square and saw are harsh and competed for attention.
Square and triangle share the odd-harmonic series but differ in how much of
it -- `1/n` vs `1/n^2`:

    square    h3 0.333  h5 0.200  h7 0.143   buzzy, hollow
    triangle  h3 0.111  h5 0.040  h7 0.020   soft, near-sine

A much larger brightness gap than square-vs-saw, so the two sit in different
perceptual layers rather than fighting: health cuts through, the super meter
stays easy to ignore -- matching which one the player tracks under pressure.
⚠ The triangle needs no band-limiting; its `1/n^2` rolloff makes aliasing
negligible. The sawtooth path (timbre 4) is retained but unused.

### ⭐⭐ STANCE SOLVED — `B+0x59A`, and height `B+0x58` confirmed (2026-09-19)

Two user-labelled captures settled it, both taken live in one session:

- `crouch-01` — user reported crouching at capture time.
- `jump-01` — ⭐ **the user's idea, and a much better one than mine.** I was
  about to burst-capture 40 snapshots hoping to catch a sub-second jump;
  instead they **paused in mid-air**, freezing the airborne state so a single
  unhurried capture got it. P1 reads y = 81.38 with a small downward velocity
  (just past the peak) while grounded P2 reads 0.

⭐ **This also confirms `B+0x58` IS height.** It had read 0 in all sixteen
earlier captures, which was consistent with "not the height field" as much as
"nobody jumped"; a single airborne sample separates those.

    stance   B+0x59A     0 = grounded/standing   1 = airborne   2 = crouching

Method: bytes CONSTANT across ten standing captures that take a THIRD
distinct value in each of crouch and jump, with grounded P2 unaffected in
both. Three offsets survive -- 0x59A, 0x5A6, 0x5B2 (0xC apart, one value
replicated in a small array); they agree in all 26 player-captures.

⭐ **THE FALSIFIABLE CHECK:** value 1 appears in exactly the captures where
`B+0x58 > 0`, and never otherwise. Two independent fields agreeing across
every capture, which is the standard the meter cap met and the original
health/meter claim did not.

⚠ **`B+0x038` was rejected, and it is worth recording why.** It was the most
enum-looking candidate (standing 4, crouch 3) and would have been an easy
pick from the crouch capture alone. Checking the OTHER player killed it: P2
reads 3 standing and 4 in the crouch capture -- the values SWAP between
players, so it is facing/side, not stance. ⭐ A per-player field must be
tested on the player who did not act.

### ⭐ Knockdown: a DIFFERENT field, `B+0x007` (2026-09-19)

The user knocked P2 down and paused, giving `knockdown-01`.

⛔ **The clean negative first: P2's `B+0x59A` reads 0 while knocked down** --
identical to standing. So the stance byte has no fourth value, and that is
measured, not assumed. Had the ladder been extended by guessing a value
there, it would simply never have fired.

Diffing P2's struct against ten standing captures, requiring the field to be
unchanged for P2 in the crouch and jump captures too (P2 was upright in
both), gives 12 candidates. `B+0x007` is the only small ENUM that changes
value rather than flipping 0->something:

    B+0x007   0 = not in a match   1 = normal   2 = crouching
              3 = knocked down     5 = (seen twice, unexplained)

⚠⚠ **ONLY VALUE 3 IS EVIDENCED, AND FROM ONE CAPTURE.** This is an ACTION
state, not a second stance ladder: value 2 duplicates what 0x59A already
carries, and 5 appears in two captures where nobody was reported knocked
down. ⭐ I checked whether 5 meant "being hit" -- it occurs with damage, but
so does 1, so that reading is NOT supported and was dropped rather than
shipped. A single sample is exactly the evidence class that mislabelled
health as the super meter.

Shipped narrowly: `0x007 == 3` maps to `AUDIO_STANCE_DOWN`; every other
value defers to `0x59A`. That is the narrowest claim the evidence supports.

▶ To go further, labelled captures are needed for: being hit but not downed,
blockstun, and getting up. Until then do not interpret 5.

Installed with pause suppression and the full stance ladder:
`8C30308061A336D02D35043C300026A75746053B480BE177C6CB9B614A4BFA9A`.
Backup: `build/dinput8-before-stance-*.dll`.

### ⭐ Gauge waveforms: square and sawtooth (user, 2026-09-19)

After hearing them in play the user reported the gauge tones still too
similar and proposed the fix: **square for health, sawtooth for super**.

⛔ The old pairing was triangle vs a "softened square" that was only two
harmonics (fundamental + 1/3 third) -- effectively a triangle with slightly
more edge. A difference of DEGREE within one family. The registers were well
separated (150-420 vs 600-1500) and that still was not enough, which is the
same lesson the beacons taught: **separating tones by pitch does not work
when the timbres are close in kind.**

⭐ Square and saw differ in WHICH HARMONICS EXIST. Measured from the shipping
mixer by DFT at harmonic bins:

    health square @420Hz   h1 1.00  h2 0.00  h3 0.33  h4 0.00  h5 0.20  h6 0.00
    super  saw    @1500Hz  h1 1.00  h2 0.49  h3 0.32  h4 0.23  h5 0.18  h6 0.14

Odd-only versus all, exactly as theory predicts. They stay distinguishable
even at the same pitch, which register separation alone never guaranteed.

⚠ Both are BAND-LIMITED via PolyBLEP. A naive square/saw is an infinite
harmonic series, so everything above Nyquist folds back as inharmonic
aliasing -- worst at high pitch, and super reaches 1500 Hz. Proper additive
synthesis would need up to 147 `sinf()` calls PER SAMPLE at the low end, on
the render thread; PolyBLEP rounds each discontinuity over one sample for a
few arithmetic ops. (COUNT THE WORK BEFORE PUTTING IT IN A PER-SAMPLE LOOP.)

⛔⛔ **THIS BROKE THE CLICK TEST, and the fix is the reusable part.** Case 7c
asserted the worst single-sample jump stayed under an ABSOLUTE 3000 -- a
constant derived for a triangle's gentle slope. A square jumps the full
peak-to-peak at every edge BY DESIGN: predicted 9633, measured 9723. The test
was measuring the waveform, and "fixing" it would have meant removing the
edges that make it a square.

⭐ 7c now compares against the SAME waveform held STATIC. Whatever steps the
oscillator makes on its own appear in both and cancel, so the case tests what
it always meant to -- that FEEDING A VALUE adds no discontinuity. Correct
code measures exactly the control (9723 vs 9723).

⚠ And the headroom is set BY SABOTAGE, not taste: resetting the phase on each
feed (a real click) gives 12964, i.e. 1.33x. My first bound of 1.5x let that
sabotage PASS. It is now 1.15x, verified to pass honest code and fail the
click. **A tolerance chosen by eye is a tolerance that has not been tested.**

Installed: `90D9D3B0281A8462BE3A3B603C329C5DD9200D9DEA46A5475C0561AB033CC3CE`.

⭐⭐ **THE TECHNIQUE THAT MADE THIS SESSION CHEAP, and it was the user's
idea: PAUSE IN THE STATE YOU WANT TO CAPTURE.** Crouch, mid-air and knocked
down are all sub-second states, and I was about to burst-capture 40 snapshots
hoping to catch one. Pausing freezes the state so a single unhurried capture
gets it, and the phase bytes are unchanged by pause so the capture is fully
comparable with unpaused ones. Three states, three captures, zero retries.
Use this for any transient state (hitstun, blockstun, getting up, supers).

Shipped: airborne interpolates `AUDIO_STANCE_AIR`..`AIR_TOP` by actual
height, so a jump reads as a rising-then-falling arc rather than a single
step. Test 12h pins the ladder ordering (measured 518 / 633 / 746 / 857 /
1078 Hz). ⚠ That test measures TONE, not pulse count: the envelope detector
merges overlapping fast plucks and reported stand > air for a correctly
ordered ladder. An instrument that saturates is not evidence.

### ▶ (superseded) STANCE: the data is NOT yet located

`B+0x58` ("Y") reads exactly **0 in every in-round capture**, while
`B+0xAC` ("Y velocity") is non-zero and varies (-2.84, -4.62, -7.75). A
fighter with downward velocity and a pinned height is GROUNDED with gravity
applied and clamped -- consistent with no capture having sampled a jump, but
it also means `0x58` has never been seen to move and cannot be trusted as
height on this evidence alone.

⚠ Per the design memory, height is insufficient anyway: crouching and
standing almost certainly SHARE a height, so a state byte is needed to
separate them.

Offline progress: 44 sites store an immediate into `B+0xAC`, and many are
clean 16.16 impulses (+5.00, +9.00, +10.00 units/frame) -- jump velocities,
as expected. Enumerating the byte fields written alongside them did not
produce a state candidate (the scan's operand-size filter is unreliable for
byte stores and needs fixing before it means anything).

⭐ THE CHEAP SETTLEMENT, and it needs one capture rather than more
disassembly: capture while **holding crouch**, and again **at the top of a
jump**, in the same match. Diffing those against a standing capture names
both the height field and the state byte directly, because the only thing
that differs is the stance. Until then a guessed ladder would mislead, so the
beacon stays at standing pitch.

⚠ The FIRST `build.ps1 -Deploy` reported nothing and did NOT land — the
installed hash was unchanged afterwards. A second run copied it correctly.
⭐ Verify a deploy by HASHING BOTH FILES, never by the script's own
"Deployed to ..." line; that message printed on the run that did not copy.

▶ REMAINING: one listening check — walk to each corner and confirm the beacon
reaches full left/right, and that it tracks smoothly in between. That also
tests the two assumptions the captures cannot: `SCREEN_HALF = 192` and
whether `STAGE_HALF` varies per stage.

⚠ Y is located but has no verified use yet: the audio design maps pan to X
only, and no capture samples a jump to confirm `+0x58` moves.

### ⭐⭐ The scanner bug the positive control caught — stale registers

`tools/find_integration.py` runs its control automatically and **exits
non-zero** on failure. It failed twice before this result was trusted, and
both failures were in the TOOL, not the game.

The control asserts the documented routine appears. It reported the X site at
`0x140708F56` found and the Y site at `0x140708F6B` **missing** — 21 bytes
apart, in a routine I had already disassembled and could see was there.

⛔ **My first explanation was wrong.** I assumed a 64-byte window boundary
split the load from its use, and added overlapping windows. Coverage went from
162,639 to 269,241 decoded instructions — a real improvement worth keeping —
**and the control still failed.**

The actual cause: the routine reuses `edx`.

    mov edx, [rcx+0xA8]     ; edx = X velocity
    add [rcx+0x54], edx
    add edx, [rcx+0xB0]     ; edx is now the ACCELERATION step
    mov [rcx+0xA8], edx
    mov edx, [rcx+0xAC]     ; edx = Y velocity
    add [rcx+0x58], edx     ; <-- rejected: edx still mapped to +0xA8

The tracker only APPENDED loads and never invalidated a register when
something else wrote it, so the stale `+0xA8` entry shadowed the fresh
`+0xAC` one. Fixed by keying on the register (width-normalised, so `edx` and
`rdx` are one slot) and clearing it on every write capstone reports.

⭐⭐ **THE FIX MADE THE ANSWER CLEANER, WHICH IS THE REAL CONFIRMATION.**
Before it, `+0x54` appeared to be fed by five different velocities
(`+0xA8`, `+0xC0`, `+0xCF`, `+0x1FA`, `+0x3D0`) and spurious `+0x32`/`+0x56`
candidates existed. After it, `+0x54` is fed by `+0xA8` and nothing else. The
bug was not hiding one site; it was **inventing pairings**.

⚠⚠ **THE LESSON.** Stale register state does not look like an error — it
looks like a plausible finding with extra candidates. Had the control not
pinned a known answer, "position is +0x54, velocity is one of five fields"
would have shipped as the result, and the extra four would have been rationalised.
⭐ And when the first fix does not clear the control, **do not stack a second
guess on it** — instrument the rejection and read why. Printing the actual
operands took one run and named the cause exactly.

## 2026-09-26: the differential probe for the win-quote INDEX

The user's steer: "Astra was able to find the cursor. You can find the quotes
by the same method." Correct — `src/emu_cursor.c` finds guest state by
differential capture, and the quote index is guest state. I had stopped one
step early, having closed only the *text*-side routes.

**What transfers, and what does not.** The cursor probe's load-bearing test is
REVERSIBILITY (`S3 == S1`: press right, press left, return). A win quote cannot
be un-displayed, so that test is unavailable. Replacing it:

- **(a) same-screen control** — two captures of ONE quote screen; the index
  must not change;
- **(b)** the value is in 0..7;
- **(c)** it differs across screens (the pick is random);
- **(d)** it differs between two wins by the SAME character — the constraint
  that separates an index from a per-character property.

`tools/cvs2_quote_index.py` (`capture` / `solve`). Captures are the whole
256 MB arena, compressed ~5x; the solver filters with flat bytearray masks.

### ⛔⛔ THREE MEASUREMENT FAULTS, each of which fakes a result

1. **A paused game destroys the control.** The user paused, advanced a few
   seconds, paused again. Measured: a live pair differs by 130,260 bytes
   (0.0243%), a paused pair by 1,483 (0.0003%) — **80x**. A paused pair tests
   "what changes while frozen", i.e. almost nothing, so almost everything
   survives. ⭐ Captures now record `R+0x186` (the 0..59 sub-second counter) and
   the solver prints WEAK CONTROL when it did not advance. **A control that
   cannot be told from no control is worse than none, because it is trusted.**

2. **⭐⭐ CONSTRAINT (c) GETS WEAKER AS SCREENS ARE ADDED.** It tests "not
   constant across all screens"; "not all equal" over three admits bytes that
   "not equal" over two rejected. Measured: 22,193 at two screens →
   **40,945 at three**. More evidence, weaker test. Do not read a rising count
   as regression. (c) is a floor, not a discriminator.

3. `MemoryError` twice — a set of `(block, byte)` tuples over 512 MB, and
   materializing the survivor list when the correct output was "far too many".

### ▶ THE RESULT SO FAR: (d) cut 25%, not the expected collapse

Four screens (q04 King, q05 Sagat, q07 Sakura, q08 Sagat). With Sagat winning
twice, (d) finally applied: **33,085 → 24,717**.

⭐ WHERE the survivors are is the real finding:

    arena+0x08D00000  14,846  (60%)
    arena+0x08F00000   1,841
    arena+0x08E00000   1,473
    arena+0x08900000     317   <- documented ARGB1555 framebuffer

Sampling between two screens: **38% of bytes differ at +0x08D00000**, 11% at
+0x08900000. State does not churn like that; image data does. The
`+0x08xxxxxx..+0x09xxxxxx` span is GRAPHICS, and two Sagat wins show different
opponents and backgrounds, so those pixels satisfy (d) for reasons unrelated to
the quote.

Excluding that span leaves **4,633**, at `+0x0CAxxxxx` and `+0x104xxxxx`.

⚠ Still far too many to adopt one, and picking the most plausible-looking is
exactly how `R+0x05931` shipped and named the wrong character in play.

▶ WHAT WOULD ACTUALLY FINISH THIS:
- more repeat wins by ONE character, captured LIVE (not paused), which is the
  only constraint that tightens with data;
- excluding the graphics span from the mask up front, so the counts reflect
  state rather than pixels;
- ⚠ and an honest limit: if repeat wins keep cutting ~25%, the index is not a
  plain byte that is stable within a screen, and that conclusion should be
  stated rather than worked around.
