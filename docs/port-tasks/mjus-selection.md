# Project Justice select screens: evidence for `src/mjus_select.c`

Written 2026-10-19 from three live captures on ROM 13 `pjus` (English shell)
and the code that uses each field. Both ROMs (12 `mjus`, 13 `pjus`) share
one initializer and one update, so the same code and offsets serve the
Japanese version.

## Captures

- `build/captures/mjus-1`, `mjus-3`, `mjus-4` (not retained; 32 MB and
  165 MB). `tools/make_mjus_fixtures.py` keeps what the decoders read in
  `tools/fixtures/mjus-select-live-20261019.bin` and
  `tools/fixtures/mjus-match-frames-20261019.bin`.
- mjus-3, session 1: Free mode, then free select. Picks Wild Daigo, Edge,
  Batsu. Session 2 (the block was republished): Story mode, school list,
  Taiyo.
- mjus-4: Story mode, Taiyo, two fights, both lost, results, continue.
- ⚠ The tool's keyboard log missed most presses. The game's own input word
  R+0x2008 (JVS order: 0x8000 start, 0x2000 up, 0x1000 down, 0x0800 left,
  0x0400 right, 0x0200/0x0100 buttons) gives the timing.
- ⚠ The F9 labels were numbers only ("screen 1" ...); the trace carries the
  evidence.

## Flow, all relative to the root R = `*0x142996830` (size 0x9E8A8)

- R+0x21C0 is the game phase. The dispatcher at 0x140B9E1F2 bounds it at 8
  and jumps through 0x140B9E410. 1 is menus, select and story scenes
  (sub-state R+0x21C1, 0x140B9E456), 2 fighting, 3 results, 4 the continue
  decision (which consults the collection's own session flags, `*0x141C14F98`
  +0x7C0 and +0x7D0, so the mod does not announce it).
- A 16-slot task list at R+0x93F30, stride 0x18, runs the screens
  (0x140B93B60): +0 state word, +5 routine index into 0x141787260, +7 screen
  state, +8 step. The select screens are routine 6, 0x140BA1230 (slot 5 live).

## The Story/Free menu

- Routine 6, state 1, step 1. Left or right toggles R+0x22F0 (0x140BA145E,
  `xor 1`). On confirm, 0 calls 0x140BC9840 (school list) and 1 calls
  0x140BCA240 (free select). R+0x22F2 becomes 1 on confirm.
- The choice is kept at side record +0x2A (R+0x252A for side 1). When a
  collection setting at `*0x141C14F98` +0x7F8 is set, the menu is skipped and
  +0x2A is forced to 1 (0x140BA12DA).
- Live: session 1 toggled 0, 1, 0, 1 and confirmed 1; session 2 confirmed 0.
- Words: "Story mode", "Free mode" in the mod's 14 languages
  (`MT_STORY_MODE`, `MT_FREE_MODE`), worded as the collection words them
  (ACHIEVEMENT_32_DESC, SOUND_MJUS_40).

## The school list (Story)

- Routine 6, state 2, step 1, with +0x2A = 0. 0x140BC98D0 sets the count
  R+0x22F1 to 6, or 7 when 0x140B9B670 holds (the Secret Characters EX
  setting: STATS_MJUS_44_TIPS says the seventh, Darkside Student Congress,
  needs it). Down steps R+0x22F0 up, wrapping at the count; up steps down.
  Confirm stores it at side record +0x2B (0x140BC9C0E). Only the one human
  side drives it (R+0x2029 - 1).
- Live: count 7; the cursor visited 0, 6, 0, 6, 0 and confirmed 0, which
  filled the team with 1, 2, 3 (Batsu, Hinata, Kyosuke): Taiyo.
- Order. The story table 0x1417B2870 is indexed school*2 + route, then
  0x1417B2790 lists each route's five chapter scripts:
  - 0 branches at chapter 2: Taiyo (confirmed live; the collection says its
    branch is decided by a loss before chapter 2 ends).
  - 1 branches at chapter 3: Gorin (its branch is the chapter 2 team).
  - 2, 4 and 6 have one route: Pacific, Justice, Darkside.
  - 3 and 5 branch at chapter 4: Gedo and Seijyun, both "Rescue Daigo" at
    chapter 4. School 5's teammate choice offered Akira, Zaki and Yurika
    live (mjus-1), Seijyun's members, which fixes Gedo at 3.
- Names: the collection's own (STATS_MJUS_30..40). English in every language
  but Japanese, which has its own: 太陽学園, 五輪高校, パシフィックH.S.,
  外道高校, ジャスティス学園, 聖純女学院, 暗黒生徒会.

## Free character select

- Routine 6, state 2, step 1, with +0x2A = 1. Side records R+0x2500 and
  R+0x2580 (stride 0x80, from `(side + 0x49) << 7`, 0x140B9E4D7).
- +0x17 is the pick slot, 0 to 2 (0x140C342EE bounds it at 3).
- +0x18 + slot holds that slot's highlight (0x140C342FC side 1,
  0x140C34327 side 2). It is the native character ID, the move-list ID space
  of the shell's name table: the cursor starts on 1, Batsu, and the three
  committed picks at R+0x255C..0x255E (0x1C Wild Daigo, 0x0A Edge, 0x01
  Batsu) equal the last highlight of each slot.
- R+0x2029: bit 0 side 1 human, bit 1 side 2 (0x140C342E8, 0x140C34310).
- Versus and Training use the same screen with +0x2A = 0xFF on both side
  records (live, mjus-6, 2026-10-20). Both sides are human; each picks three
  with the same slot and highlight fields, committed to R+0x255C (side 1)
  and R+0x25DC (side 2). The first release accepted only 1, so these modes
  were silent. Fixture `mjus-versus-20261020.bin`.
- Names: `character_name(6, id, version)`, including the shell's remap of
  0x12 to 0 for the US version (Iincyo is "Chairperson" there).

## The story teammate choice

- Run by the story scene (0x140C40E20), not the routine-6 task.
- R+0x225E is the choosing side, 1 or 2; R+0x2260 the count of offered
  characters, and R+0x2261 onward their native IDs.
- The cursor R+0x230C steps over them, wrapping at the count and skipping
  entries whose picked flag R+0x230D + i is set (0x140C40FD0..0x140C4100D).
  0xFE means not started (0x140C40EBC).
- Confirm copies R+0x2261 + cursor into the team at R+0x21CC (0x140C41390).
  R+0x22F2 stays 0 until the choice ends.
- The pick count R+0x230B rises one frame after the cursor's jump to the
  next free entry, so the visit counts the picked flags instead.
- Live, three captures: Taiyo offered Batsu, Hinata, Kyosuke twice
  (committed 1, 3, 2 and 1, 2, 3, each equal to the entries the cursor
  stood on); school 5 offered Akira, Zaki, Yurika.

## Tests

- `.\tools\run_mjus_select_test.ps1` replays 165 live samples: the menu and
  school order, the committed picks, words by language and version,
  shared-channel speech, visit and retry, and every gate.
- `python tools\sabotage_mjus.py`: shifting the human bits, ignoring the slot,
  removing the free/story gate or the step gate, swapping school names and
  ignoring the pick visit each fail it.

## Listening check

Deployed 2026-10-19: `dinput8.dll` SHA-256
`c68b8a652f21515d6190a13f962cb0bf077c27d14156b04ca41f36775da415fe`. The
previous DLL is kept as `build/deployment-backups/dinput8-a890c2db.dll`.

- The first menu speaks "Story mode" and "Free mode" as the cursor moves.
- The school list speaks each school; tell me if Gedo and Seijyun sound
  swapped.
- Free select speaks the highlight; each new pick speaks its first highlight.
- The log has `mjus-select: admitted ROM 13 (pjus)` and `mjus-select:
  accepted` lines.

## Not covered

- A second human player. Side 2 reads its own record by the same code path,
  but no capture has shown it.
