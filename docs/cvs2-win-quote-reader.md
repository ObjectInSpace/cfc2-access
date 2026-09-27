CVS2 displayed win quotes — 2026-09-26

The native drawing code keeps the displayed text at `R+0x20F5C` and
`R+0x20FAC`, two 80-byte, zero-terminated buffers. These are inside the
existing `CVS2_STATE_SIZE` snapshot. Reading them requires no additional
process reads, memory scans, winner guesses, or compiled quote lookup.

The code path was followed from the observed phase, not a value search:

- `P=R+0x78`, phase `02 01 02 03 02`.
- `0x1406BA870` dispatches P[1]=1 to `0x1406BABA0`.
- Its table `0x14128CD78`, P[2]=2, selects `0x1406BA970`, which jumps to
  `0x1406B2390`.
- Table `0x141283230`, P[3]=3, selects `0x1406B4310`, which jumps to
  `0x1406D5F80` (post-match substate dispatcher).
- Setup `0x1406D5FA0` calls native RNG `0x1406B1230`, masks the result
  with 7 at `0x1406D6166`, and supports input overrides and special dialogue.
  At `0x1406D6381`/`0x1406D6451` it stores the selected line-offset array
  pointer at R+0x20F48. Its resource base is
  `(*(u64*)(*(u64*)0x142C79B40+8))+0x800000`.
- `0x1406D6D32` reads that pointer; offsets are **u32**, not u16.
  `0x1406D6D69` and `0x1406D6E49` store source line lengths at
  R+0x20F54/56. Animation advances copied lengths at R+0x20F58/5A.
- `0x1406D6CEA`/`0x1406D6CFF` initialize destination pointers to the two
  draw buffers. The animation fills them, and the draw branches copy them
  to the stack before calling `0x1406AD730`, e.g. `0x1406D706A`.
- R+0x20F3E is dialogue mode 0/1/2; mode 0 always draws two lines.
  Modes 1/2 draw a second line when R+0x20F40 is 2.
- R+0x20F50 states 1/2 animate lines, 3 is complete, 4 fast-forwards
  remaining characters. Completion counts still need checking in state 4.

Live evidence, PID 9972: R=0x14601000, resource base=0x10801000,
selected array=0x10806530 (offset 0x5530), first source line offset=0x12AC.
The selected source and completed draw buffer both contain
“You should not have challenged me!” (34 bytes, Sagat quote 1).
The retained 208-byte draw-state slice is
`tools/fixtures/cvs2-quote-complete.bin`; its origin is R+0x20F30.
It was captured with phase `0201020302000000`.

Older real snapshots already contain partial draw text:

- winquote-01: “Practice all you want! You will never” plus “be good enoug”.
- winquote-ken-02: “Maybe next time you should think about” plus “watc”.
- winquote-raiden-03: substate 1, buffers empty; it is too early to speak.

The reader waits for substate 2, completion state 3/4, bounded printable
ASCII lines, matching source/copied lengths, and valid terminators. It joins
the lines and requires identical completed text in two consecutive polls.
It speaks once per screen visit, retries rejected speech, and resets on
departure, setup, or root replacement. It uses the existing speech worker.
Japanese/non-ASCII text is currently rejected rather than mispronounced.

Validation: CVS2 replay suite passes, including the captured complete line,
real partial/empty captures, synthetic completion of the captured two-line
Sagat quote, modes 0/1/2, animation skip, invalid lengths/text/terminators,
phase exclusion, deduplication, root changes, re-entry, and speech failure.
Full DLL build succeeds (existing warnings elsewhere in the project).
Installed after the user closed the game. Built and installed SHA-256 match:
`D935F8BD5AA3672A7910E85B89F878D8B7E0DC7A5319FCA50E4C4EDC4179D832`.
Backup: `build/dinput8-before-quote-reader-20260926-021948.dll`.
In-game automatic speech still needs a new match to validate audible behavior.

`python tools/cvs2_read_quote.py PID --speak` can read a completed live quote
and send it to NVDA without restarting the game. It refuses incomplete or
changing data. A run after the initial capture correctly refused because the
game had already left the quote phase; no live speech is claimed for that run.

Corrections to prior research: failure to find an archive offset as a native
immediate did not rule out native code accessing the resource indirectly.
The chosen index need not persist as a byte: the game retains a pointer and
render buffers. The earlier scan also searched shortened first-line needles,
which can miss a still-animating line. The native phase dispatch provided the
direct route to the reader.
