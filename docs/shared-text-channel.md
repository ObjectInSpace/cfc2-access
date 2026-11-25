# The shared text channel

Consolidates the four text readers onto one speech and read-current-text
layer, `src/text_channel.h` / `.c` (plus `src/text_capture.c`, the render-thread
half). Capture methods and user-visible behaviour are unchanged; every reader's
existing offline suite still passes with its original assertions.

## What was duplicated

Four readers each re-implemented the same five mechanics:

- `cvs2_text.c` — positioned records and font runs, published per text frame
- `cvs2_story.c` — story pages and the credit roll, FIFO
- `cvs2_select.c` — win quotes, decoded from the guarded state copy
- `menu_speak.c` — the shell: captions, modal prompts, hints

The five:

1. narrow bytes to UTF-16, refusing anything that does not convert
2. a capture buffer written on a render thread and read on the worker
3. invalidation when the session underneath the capture changes
4. de-duplication, so a redrawn line is not spoken again
5. a drain that advances only when the backend accepted the text

Number 5 is the one worth centralising on its own. Each reader had to learn it
separately, and `speech.c`'s own comment records the cost of getting it wrong: a
log full of tidy "spoke" lines while the user hears nothing. `tc_say()` is now
the single place that couples "said it" to "mark it spoken".

## What was deliberately NOT unified

The de-duplication **policy**. The three CVS2 readers key suppression
differently, each difference is load-bearing and regression-pinned:

- text: keyed on the text, 2000 ms re-arm. "PRESS START" blinks; it must not
  repeat per blink, but it must speak again on a later visit.
- story: keyed on the entry index, 1500 ms re-arm, and FIFO — every page must be
  spoken, in order. Dropping one loses dialogue.
- quote: one shot per session root, and it needs two identical reads to settle,
  because the buffer is read while an animation fills it.

A single policy would have to be the union of those, and the union is wrong for
all three. So the policy stays the caller's, expressed by which primitive it
picks: `tc_textdedup`, `tc_keydedup`, or `tc_settle`. The layer supplies the
mechanism only.

## The read-current-text key

The ``` key now means the same thing everywhere, through a registry rather than a branch
per game. A source registers a name and a read function; the key asks each in
registration order, and the first one that speaks wins. Registration order is
the priority order, kept explicit rather than sorted by anything clever.

Sources today:

- `CVS2` — reads the completed text frame, registered by `cvs2_text_register()` (2026-11-25: its worker half is now the shared `game_text.c`, like CVS1, CFJ and Alpha 3)
- `the menus` — repeats the focused row, registered by
  `menu_speak_register_reader()`, last, so a running game answers first

Every outcome gets its own spoken sentence: nothing registered, nothing active
here, nothing readable on screen, no fresh frame. Collapsing those into silence
or into one message is what makes a hotkey undiagnosable without sight. A
backend refusal is logged and says nothing further, because announcing "no
readable text" over text the backend already rejected would be false.

The shell source repeats what was **actually spoken and accepted**, not the
prose recorder's history. The text-source audit declined a read-all over that
history, and that still holds: it is a history, not a screen. Repeating the
focused row is a claim the code can support.

## Porting another game

Three steps, none of which touches `mod.c` or the hotkey:

1. Hook that game's draw path; call `tc_frame_add()` from the detours and
   `tc_frame_publish()` at the end-of-frame boundary. Nothing else in the layer
   runs on a render thread.
2. Give the worker poll a decode step and a suppressor, and assemble with
   `tc_say()`.
3. `tc_register()` the game's read function.

The decode step stays per-game — CP932 records, a credit glyph map, the shell's
UTF-8 — because that is the part that is genuinely different. `cvs2_text.c` is
the reference shape and is now only its hook addresses, signatures, record
header layout and record syntax.

## Threading

`tc_frame_add`, `tc_frame_publish` and `tc_queue_push` are the only functions
callable from a game thread. They take no lock that can block (`TryAcquire`
only), allocate nothing, log nothing and never speak. Losing a publish race
costs one frame; the next frame republishes. A refused queue push leaves the
producer's cursor unchanged, so the line is offered again rather than lost.

`tc_frame_add` lives in its own translation unit because it is the only part
needing memsafe's per-thread fault guard. A reader that merely queues finished
lines links neither the guard nor a stub for it, which is what keeps
`test_cvs2_story.c`'s own `mem_copy` seam intact.

## Tests

- `.\tools\run_tc_test.ps1` — 11 cases over the shared mechanics
- `python tools\sabotage_tc.py` — applies 9 sabotages and requires the suite to
  fail on each; all 9 caught. Run it after changing the layer, because a passing
  suite that has never been seen to fail is not evidence.

The per-game suites are unchanged in substance and all still pass:
`run_cvs2_text_test.ps1`, `run_cvs2_story_test.ps1`, `run_cvs2_test.ps1`, plus
`run_mc_test`, `run_ms_test`, `run_hint_test`, `run_modal_test` for the shell.

⚠ Seven runners needed `text_channel.c` added to their link lines. That is the
failure mode CLAUDE.md warns about — an un-buildable runner looks exactly like a
passing one — so run all of them after touching this layer, not a chosen few.

Commit validation on 2026-09-26: all 28 `tools/run_*test.ps1` runners exited
successfully and `build.ps1` produced the DLL. The initial sandbox run could
not access the installed WinGet compiler; the full rerun with compiler access
passed. The DLL build retains warnings in existing code. This check did not
deploy the DLL or validate audible behavior in a new game session.
