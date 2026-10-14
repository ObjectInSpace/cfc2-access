# In-game text for CVS1 and CFJ, 2026-10-14

The user's requirement: any text displayed in a game must be read. This file
records what is built, what is covered and what is not yet.

## Built: the positioned text-record queues

Both games format their system text into a 128-record queue. The update
resets it, fills it and draws it in the same frame. Each game's existing frame
hook copies the queue right after the update returns, and publishes it through
the shared text channel. Lines with letters are spoken automatically, using
CVS2's 2-second re-arm so a blinking prompt is not repeated. The `` ` `` key
reads the whole current frame, digits included.

- **CVS1** (`src/cvs1_text.c`):
  - Count `0x1422D0624`, records at `0x1422D0630`, stride 0x40.
  - Each record holds x, y and style bytes, then the text at +3.
  - Producer `0x1405AF6B0` (22 call sites), reset `0x1405AF7C0`, drain `0x1405AF730`.
  - Producer literals include FREE PLAY, PRESS START BUTTON, INSERT COIN(S), INSERT MORE COIN(S), GAME OVER, WIN / WINS %d, the copyright lines, Direct Connect Check and Press Start to Return. Six sites print a `%s` of something not yet identified.
  - Renderer `0x1405C8DB0` gives style 7 to the shared renderer, which has no escapes. Other styles take `^` plus a byte as a special glyph, omitted. Byte 0x5C, the copyright lines' first glyph, is treated as decoration.
- **CFJ** (`src/cfj_text.c`):
  - Count `0x1422F1844`, records at `0x1422F1860`, stride 0x40.
  - Each record holds an x word, a y word and a style dword, then the text at +8.
  - Producer `0x1407D1320` (14 callers), reset `0x1407D1440`, drain `0x1407D13C0` into renderer `0x1407DB720`.
  - Literals include the coin, credit and start prompts, CONTINUE %1d, GAME OVER, NO CHALLENGER, SURVIVAL MODE and the copyright line.
  - The renderer skips style 0 entirely, so those rows are never spoken. Its named escapes are `^Z` for a backslash, `^Y` for a caret, `^M` for a double quote and `^N` for a backtick. Any other caret pair is a special glyph, omitted. Bytes from 0x90 take another glyph path and refuse the row.
  - An immediate-print path, `0x1407D1290` (one caller), draws without the queue and is not captured.

- **Alpha 3** (`src/z3u_text.c`, added 2026-10-14, ordinary branch):
  - Queue R+0x9D70 inside the Alpha 3 block, 0x60 records of 0x18 bytes: +0 in use, +1 font, +2 colour, +4 x, +6 y, +8 a POINTER to the string. The text is not copied into the record, so the capture follows the pointer, but only into the game image or the Alpha 3 block, with a fault-guarded copy.
  - Print routine `0x140879770` (75 call sites). Every screen handler draws the queue through `0x14087E640` → `0x14087F5B0` inside the update, so the frame hook copies it after the update returns.
  - Literals include GAME OVER, CONTINUE?, GIVE UP?, SAVE, EXIT, QUIT GAME with YES and NO, SURVIVAL MODE, TEAM VS, NEXT, WINS, SCORE, CLEAR TIME, CONGRATULATIONS!!, THANK YOU FOR PLAYING, PRESENTED BY, INSERT COIN(S) and the copyright lines. The credit line is formatted at R+5 and the score at R+0x9D58.
  - The drawer gives each byte above 0x20 glyph (byte - 0x21) from the record's font. Fonts 1 to 3 print the game's own ASCII literals and are read as ASCII; '@' leads the copyright line and is omitted.
  - ⚠ Font 4 draws picture words from tile codes (table `0x1412DA840`, about 23 words). Their meaning is only in the image, so they are refused, never spelled out. The two seen live: 0x91 to 0x95 blinking on the right side during select and play, and "UVWXY" on the continue screen beside the countdown digit.
  - Live evidence: the select and match captures' traces hold the queue. They show the font 4 words, the score digits and the continue digit; the ASCII screens above were not reached in them.
  - ⚠ Fixed 2026-10-14 after the first listening run, where nothing was heard:
    - The opening screens (task modes 1 and 2) print through the attract walker `0x1408797D0`, which draws and CLEARS each record inside the update. A capture after the update always saw an empty queue. The walker is now hooked, and its records are collected before it runs. The title screen prints "STREET FIGHTER ALPHA3 UPPER", "2001 01 11" and "USA" in font 0 (table `0x1412DA5D0`; Japanese at `0x1412DA590`), which is now read as ASCII.
    - The story screen before each fight (game mode 2, screen 1, 107 to 123 s in the select capture) types its text through message objects at R+0x9C68, +0x9CC0 and +0x9D18 (stride 0x58, allocator `0x1408761E0`). +0 marks the object in use and +0x38 points to the entry in the game's data. For the US version the entry is `0x1408A92E8`: [context+8] + 0xA398B4 + line*0x45; the Japanese version uses +0xA390B4 and stride 0x4A. An entry is line NUL indent line NUL ... until an indent of 0. Each entry is read whole as soon as it starts typing.
    - The trace shows four entries, one at a time, about 3.8 s each. The format is checked against Ryu's entries in the shipped `bin/z3u` (file offset 0x4C3C8F6): "Ryu is a man following the path to become a true martial artist."

Rows a decoder refuses are logged once each, at most 32 per run, as
`cvs1-text: refused row` or `cfj-text: refused row`, with their bytes. That is
how unmapped glyphs are found.

Tests: `run_cvs1_text_test.ps1` and `run_cfj_text_test.ps1`. They use the
producer literals and each renderer's rules, and the sabotages are recorded in
the commit. ⚠ No live capture of either queue yet: the first run's log is the
live evidence.

## Not covered yet

- Text drawn outside these queues, possibly including win quotes, story and ending text, and VS-screen names. Both games draw much of their HUD as sprites, and other glyph paths exist: CVS1 calls the shared glyph draw `0x140F36A70` from seven functions. Find which screens show text that is not spoken, then trace that screen's draw path.
- Special glyphs such as © and button icons: their atlas positions are not mapped.
