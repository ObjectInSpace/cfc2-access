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
