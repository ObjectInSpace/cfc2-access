# User-facing quick-save follow-up

2026-10-07. The user reports that saving/loading generally records at the
start of the match, specifically the Vs. screen showing the teams. This is
user evidence about the checkpoint. It is not yet a measured callback timeline,
an exact frame boundary, or a claim covering every game and mode.

## Established UI evidence

The retained pause-menu evidence in src/menu_speak.c identifies Quick Save
and Load Quick Save from their own captions. The English text corpus contains
LOAD = "Load Quick Save", SAVE = "Quick Save", and separate confirmation,
success and failure messages for each. Training's Change Character action
must not be substituted. The shipping menu reader uses the actual drawn row
text; historical row numbers are not a reliable instruction across modes.

The pause cursor call at 0x1400a749d is inside 0x1400a73b0. Its later input
path tests S+0x7b4, +0x828 and +0x80c at 0x1400a75fa..0x1400a7613,
also seen in the native save/restore gates. That overlap is a lead, not proof
that this input branch invokes Quick Save or identifies the copy mechanism.
A bounded search for embedded hashes of POP_LOAD_CHECK, POP_LOAD_END,
POP_SAVE_CHECK and POP_SAVE_END found none in the dumped code sections.
That negative result does not exclude data-driven message lookup.

## Keep the native mechanisms separate

- Per-game secondary-block routines conditionally save or restore native state,
  with game-specific repairs after copying.
- Post-update transfer wrappers copy smaller payloads through S+0x465c.
- Shared callback registration/load/replay operates on registered state blocks;
  the join-match route is a separate established entry into callback mode.

The user's checkpoint observation does not select one of these mechanisms by
itself. In particular, no snapshot-load callback in a Quick Load capture would
not prove that native state was unchanged. The current five-site probe does
not directly observe all three mechanisms.

## Bounded next work

Trace the pause-menu command dispatch and its confirmation result to the
actual save/load request. Determine whether saving persists an earlier
match-start checkpoint rather than capturing the instant the menu opens.
Follow load through replacement and repair to the first eligible observation.
Retain the Vs. screen report as the expected visible checkpoint to compare.

Prepare a targeted live check only after identifying the relevant observer:
label the chosen game/mode, save and load as separate actions, the success
messages, and the screen reached after load. Existing quick-save data must
not be overwritten incidentally; a load-only check can use an existing save
if suitable. Do not request another generic game switch or join-match run
to answer this user-facing save/load question.

The offline pilot may remain limited to evidenced modes, with decoding
disabled in unsupported paths. Exhaustive online replay coverage is not a
prerequisite for that limited delivery. This scope decision does not itself
implement a reliable mode gate or lifetime-safe acquisition.

No new live capture, hook, DLL change or save-file operation was performed.
