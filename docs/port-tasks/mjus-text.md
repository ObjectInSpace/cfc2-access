# Project Justice text sources

Written 2026-10-19. What is read, and why the rest is not yet.

## Read: the immediate ASCII draw

- `0x140B96130` draws a string at once: ecx x, edx y, xmm2 scale, r9d
  colour, the string on the stack. Bytes above 0x10 draw glyph table
  0x141792D20[byte]; others only move the pen.
- Seven callers: the credit task (routine 1, 0x140BA0D00) with INSERT %d MORE
  COIN(S), CREDIT(S) and a coin fraction; the copyright line; ROUND BONUS and
  its score (0x140BC9439).
- `src/mjus_text.c` hooks it, collects each update's strings and publishes
  them after the update returns, through the shared reader
  (`src/game_text.c`). The credit display is never narrated, as for every
  game; the shared filter now also knows "CREDIT(S) 3" and "1/2". The `` ` ``
  key reads it all.
- Test: `.\tools\run_mjus_text_test.ps1`.

## Not readable as text: story dialogue, names and menus

- The only strings Project Justice's native code references are those above,
  plus EDIT SELECT and NO DATA (0x140C34B2E, 0x140C34DE7, another draw path,
  not hooked yet).
- The ROM blob (`bin/mjus` in game_60.arc) holds no English as ASCII or
  UTF-16; its AFS entries are compressed.
- Guest main RAM (`*(*0x142C79B40 + 8)`, guest 0x0C000000), saved every 4 s
  through the story scenes by `tools/guest_ram_snap.py`: no English text, and
  no character name under any constant byte or 16-bit offset.
- So the dialogue is drawn as pictures. Story scenes are bytecode
  (R+0x2230 points into scripts such as 0x1417B35D0), run by task routine 7
  (0x140BA16D0) through 0x140BCB930, 0x140BCBAD0 and 0x140BA5440.

## Next step for story text

Find the scene opcode that shows a dialogue picture and the texture it
loads, decode those textures, and transcribe them, as Alpha 3's Japanese
story sheets were (`tools/z3u_story_sheets.py`). Each version has its own
pictures, so the Japanese ROM needs its own pass.
