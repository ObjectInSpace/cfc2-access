# Character names by native ID, for all eight games

Traced offline 2026-10-13 from the reviewed image dump (code section SHA-256
`9c340b8c...`). No capture or game launch was needed. This resolves the name
blocker in [cvs1-branch-a-names.md](cvs1-branch-a-names.md) and supplies names
for every other title's selection port.

## The evidence chain

1. The pause-menu move list opens at setup `0x14007FF00`:
   - It stores the shell's game index (`[*0x141C14F90 + 0x1AC]`) at screen+0x330.
   - It stores the game-version setting (`[*0x141C14F90 + 0x1B0]`) at screen+0x334, at `0x14007FF39`.
   - It loads that game's roster list from the pointer table at `0x1412048C0`: a count byte, then native IDs in display order.
   - It reads the current fighter's native ID from the shell copy at `+0x578 + 4*player`. Alpha 3 and Project Justice aliases are remapped at `0x14008002E..0x140080077`.
   - It searches the roster list for that ID to place the cursor.
2. The roster redraw `0x140082030` takes the roster entry as the native ID (`movzx r15d, byte [rax+rcx]`). It filters move records by that ID and calls `0x14004B2F0(ctx, game, version, id)` at `0x140082241` for the header name. For Alpha 3, IDs at or above 0x26 are increased by one first, at `0x14008221C`.
3. `0x14004B2F0` switches on the game through the jump table at `0x14004B5A0`:
   - Each case bounds the ID: CVS1 0x23, CVS2 0x30, CFJ 0x2E, Z3U 0x2B, PWS1 0x0B, PWS2 0x10, MJUS 0x42, STG2 0x1D.
   - Each case then reads a C-string pointer. Two-variant tables index `id*2 + version`; MJUS and STG2 have one variant.
   - CVS1 and CVS2 share the table at `0x1411F6640`.
   - CVS1 prefixes "Ex " when the ID's high byte is set.
   - MJUS reads ID 0 for ID 0x12 when the version is not 0.
   - Out-of-range IDs return the empty string.

The version setting picks the Japanese-ROM name (0) or the US-ROM name (1).
Examples are Balrog/Vega/M.Bison and Gouki/Akuma in CVS1 and CVS2, Nash/Charlie in Alpha 3, and Fokker/Falcon in Power Stone.

## Extraction and checks

`tools/extract_roster_names.py` reads the tables only after it has confirmed:
- the function prologue;
- every jump-table target and every case's bytes;
- the shared CVS read;
- the empty-string and "Ex " constants.

It writes `data/character_names.json` and the generated
`src/character_name_table.h`, with a `character_name(game, id, version)`
lookup that mirrors the function. `--check` fails if either output is stale.

`tools/test_extract_roster_names.py` checks:
- **Positive control:** all 48 CVS2 entries agree with the independently sourced, partly live-checked CVS2 names. The only exceptions are pinned: the three deliberately region-neutral labels Claw, Dictator and Boxer, and the spelling "Todo".
- **Coverage:** every move-list roster ID in all eight games has a name.
- **Refusals:** changed case bytes or jump-table entries make the extractor refuse.

## Limits

- The names are the shell's English romanizations. The collection uses the same table in every shell language, but the CVS2 adapter's names have never been checked against a Japanese shell.
- In CVS1, the selection byte does not carry the EX flag, so "Ex " is not spoken. EX selection remains unresolved.
- The version-to-ROM relationship comes from the move list's own use. The CVS1 adapter logs the ROM index next to the version on first admission, so the next run confirms it live.
