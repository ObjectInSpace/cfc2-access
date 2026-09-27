# Additional text-source audit, 2026-09-26

The audit follows the current CVS2 accessibility work. It also reviews the
collection's existing shell text consumers. It does not establish complete
coverage of all eight arcade titles or text baked into images.

## Sources and coverage

- Collection GMD messages: existing `gmd_find`, `prompt_id`, and `menu_speak`
  readers resolve localized labels and prompts. Static text existence alone
  does not identify the visible menu item.
- Collection live GUI widgets: existing `gui_text` and `menu_speak` consumers
  handle captions, modal prompts, live numbered music tracks, coordinate-based
  rows, achievement hints, and move-list content. The retained prose recorder
  is a history, not a screen snapshot. This audit does not add an unsafe
  read-all operation over that history.
- CVS2 win-quote buffers: existing completed-buffer reader, retained.
- CVS2 story resource: existing page renderer and post-name-substitution
  hooks, retained. Covers boss dialogue, epilogues, and ceremony entries.
- CVS2 credits: existing encoded stream/cursor reader, retained. These are
  individual sprite glyphs and do not pass through the ordinary string hook.
- Newly covered CVS2 positioned text records: start/coin/free-play prompts,
  continue countdown, game over, survival/no-challenger labels, title and
  territory-warning lines, and copyright text. Read from the actual drawing
  records, including the formatted countdown number, rather than predicted
  from state or reconstructed from format strings.
- CVS2 ordinary font draws and HUD name records: now included in the read-current-text
  completed-frame readout. Their existing automatic readers remain owners of
  story/quote and character announcements to avoid duplicate speech.
- Still unhandled: raster-only wording, sprite/icon command sequences with
  unverified glyph maps, and native text adapters for the other arcade titles.
  Finding bytes in an archive does not establish display timing or visibility.

## Native trace

`0x1406B0890(x, y, style, format, ...)` formats a 64-byte positioned record:
signed 16-bit x/y, 32-bit style, then up to 56 bytes of NUL-terminated text.
Two queues are submitted through `0x1406B0930` and `0x1406B09F0` to the common
record renderer at `0x1406B7E00(record, float depth)`.

The record renderer also receives the two actual HUD names from `0x1406C5759`
and `0x1406C5803`; those calls set style 16. Automatic speech skips that style,
but the read-current-text readout includes it.

Ordinary font strings pass through `0x1406AD730(float x, float y, float depth,
char *text)`. Its direct callers are the win-quote renderer, story renderer,
story name substitution, and embedded runs in the record renderer. The new
capture suppresses nested embedded runs so the parent record owns its text.

The native frame routine `0x1406AA7A0` calls the two text submissions at
`0x1406AA8BA` and `0x1406AA8BF`. Return from the second is the publication
boundary. An empty text frame replaces its predecessor. The hook captures
only rendered strings, never walks every string stored in memory.

Useful source references:

- `0x1406B9EA7`: Press Start format at `0x14106E618`.
- `0x1406F9295`: Continue format at `0x14106EDB8`; formatted draw queued at
  `0x1406F92D7` after computing the visible countdown number.
- `0x1406FA25D`: Game Over at `0x14106EDC8`.
- `0x1406FA373`: Survival Mode at `0x1410739B0`.
- `0x1406FA384`: No Challenger at `0x14106EDA8`.
- `0x1406B2010` and `0x1406B22D0`: iterate title and territory-warning records
  at `0x141282A60`, `0x141282BA0`, `0x141282CE0`, and `0x141282F60`.

## User behavior and safeguards

New positioned prose is automatically spoken once while it remains displayed.
A two-second absence allows reappearance to be announced; ordinary blinking
does not continually repeat. Changed continue numbers are new text. Unlabelled
numeric HUD records are available on demand rather than narrated continuously.

The ``` key, while the game has keyboard focus, reads captured CVS2 text ordered by
vertical then horizontal position. It includes current quote/story font lines,
HUD names, and readable positioned records. It does not perform OCR or include
credits that have already scrolled away. A snapshot older than 500 milliseconds
is refused, so pausing rendering for longer can make the readout unavailable.
Session changes require a newly published frame.

Capture is bounded to 160 records and 80 bytes per string (56 for positioned
records). Invalid or unterminated strings are discarded under the existing
fault guard. No speech, logging, allocation, or memory-region queries occur in
the render callbacks. A nonblocking lock publishes the finished frame; speech
and decoding run on the existing worker. The callback only calls the game
original outside its short fault-guarded reads.

Positioned text uses ASCII, caret escapes, and optional embedded font runs.
The reader handles literal caret/backslash escapes and omits unverified
decorative escaped glyphs, including the symbol prefix on copyright lines.
Unmapped sprite/button byte sequences are rejected. Ordinary font runs use
CP932 conversion; non-English behavior has not been live-validated.

## Reproduction and validation

Run `python tools/cvs2_text_audit.py`. The JSON report records 22 direct native
literal references and 27 title/warning records. An archive printable-run scan
finds 617 quote and 472 story line candidates in the known blocks. Another
1,475 printable runs are unverified candidates, many clearly pixel data. Those
numbers are search results, not counts of visible messages or proof of complete
coverage.

`tools/run_cvs2_text_test.ps1` replays render callbacks and tests publication
boundaries, blink suppression, countdown changes, speech retries, read-current-text ordering,
empty/stale-frame rejection, session reset, invalid pointers, and unterminated
buffers. It compiles with warnings treated as errors, including `-Wclobbered`.
Existing CVS2 and story replay suites also pass. These are synthetic callback
tests; they do not prove that every listed screen has been heard live.

Installed DLL SHA256:
`6AB558DF4F178691DDB164FABC6B7C17A2928CEF5AB3C1C7394B728610023253`.
The Steam startup check confirmed all three additional hooks installed at
`14:20:46.259` on 2026-09-26 and the process remained responsive. Screen-by-screen
live validation remains outstanding. Backup:
`build/dinput8-before-text-reader-20260926-142043.dll`.

## Shared-reader direction

A single user-facing reader is worthwhile. Keep source-specific adapters for
the collection's widget trees, native string/record draws, and encoded credits.
Share the speech scheduler, fresh-text snapshot API, duplicate handling, and
read-current action. Visibility and completion must remain adapter contracts:
widget history, in-progress quote buffers, and future credit lines must never
be admitted as current text merely because their bytes are readable.

This change adds the missing native text adapter and ` readout. It does not
claim to have already consolidated all existing menu, story, and credit speech
into one service. That refactor should retain the current source-specific
regression cases and audible behavior.
