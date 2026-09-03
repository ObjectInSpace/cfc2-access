/*
 * Find MTFramework's live pad record -- route 5.
 *
 * Routes 1-4 are closed with evidence (see CLAUDE.md). This one does not infer
 * which item is selected from memory that happens to change; it reads the
 * engine's own input record, recovered from the reflection metadata in the
 * unpacked image:
 *
 *   sPad::Pad   size 0xb8, name hash 0x15d8846f   (read from its descriptor)
 *
 * whose fields, in declaration order, are:
 *
 *   ... Press_free  On Old Trg Rel Chg Rep  Rx Ry Rz/RT  Lx Ly Lz/LT ...
 *
 * SIX button words, then the analog axes. `On` is the CURRENT button state and
 * `Old` is the previous frame's -- exactly what the names say.
 *
 * The button BIT layout comes from the 16 button names on a fixed stride:
 *
 *   bit 0 Rup  bit 1 Rright  bit 2 Rdown  bit 3 Rleft   (D-pad)
 *   bit 4 Lup  bit 5 Lright  bit 6 Ldown  bit 7 Lleft   (left stick)
 *
 * so DOWN = 0x44, UP = 0x11, LEFT = 0x88, RIGHT = 0x22. Menu navigation
 * (up/down) and settings adjustment (left/right) are the same four bits in the
 * same word, so one reader covers both.
 *
 * ⚠ THE OFFSETS ARE NOT STATICALLY RECOVERABLE, so this searches.
 *
 * Three independent attempts to get a fixed address failed, all negative:
 * nothing anywhere references the DTI descriptors; the descriptors carry no
 * instance pointer and no property table (+24/+32 are sibling class links,
 * +40 is `size | hash`); and nothing references the field-name strings as u64
 * or u32 RVA. Same fact that closed route 3 -- this engine walks its metadata
 * sequentially rather than addressing it. So the struct is certain but its
 * address is not, and a search is unavoidable.
 *
 * ⚠⚠ Do NOT try to read offsets out of the name blob. The names are spaced 4
 * bytes apart through the whole button run, which looks exactly like a u32
 * field-offset table and is really just string lengths ("Old\0", "Trg\0").
 * It carries no offset information.
 *
 * ⭐⭐ WHAT MAKES THE SEARCH SOUND: an invariant, not a correlation.
 *
 * Every earlier route had to argue that what it found was what it wanted, and
 * route 4 produced a monotonic counter that passed every filter it had. Here
 * the engine maintains, every frame:
 *
 *      Trg == On & ~Old       (newly pressed)
 *      Rel == Old & ~On       (newly released)
 *      Chg == On ^ Old        (changed either way)
 *
 * THREE equations over five observed words, with nothing derived. Random data
 * satisfies that with probability ~2^-96 per eventful sample. That is a
 * positive identification test rather than a correlation, which is precisely
 * what routes 3 and 4 lacked.
 *
 * ⚠ A single press makes several words equal (0x44, 0, 0x44, 0, 0x44 ...), so
 * shifted windows satisfy the equations too -- measured: offsets +0, +4 and
 * +16 all match after one transition. That is a property of sparse data, not
 * of the struct: TWO DISTINCT TRANSITIONS collapse it to exactly one offset.
 * The scan therefore requires several differing events before it reports, and
 * needs no tie-breaking heuristics to do it.
 *
 * ⚠ "The pattern matched" and "this is the pad" are still different claims.
 * The report says how many sites survived and does not assert uniqueness.
 *
 * Read-only: VirtualQuery + guarded page reads, no hooks, no calls into game
 * code. Hooking crashed this game once and nothing here needs it.
 */
#ifndef CFC2_PAD_FIND_H
#define CFC2_PAD_FIND_H

#include <windows.h>

/* Button bits, from the reflected names in the unpacked image. */
#define PAD_RUP     0x01u
#define PAD_RRIGHT  0x02u
#define PAD_RDOWN   0x04u
#define PAD_RLEFT   0x08u
#define PAD_LUP     0x10u
#define PAD_LRIGHT  0x20u
#define PAD_LDOWN   0x40u
#define PAD_LLEFT   0x80u

/* Either input device, per direction: D-pad bit | stick bit. */
#define PAD_UP      (PAD_RUP    | PAD_LUP)      /* 0x11 */
#define PAD_RIGHT   (PAD_RRIGHT | PAD_LRIGHT)   /* 0x22 */
#define PAD_DOWN    (PAD_RDOWN  | PAD_LDOWN)    /* 0x44 */
#define PAD_LEFT    (PAD_RLEFT  | PAD_LLEFT)    /* 0x88 */

/* The five consecutive words the invariant is checked over, and the record
 * size from the sPad::Pad descriptor. */
#define PAD_WORDS        5      /* On Old Trg Rel Chg */
#define PAD_RECORD_SIZE  0xb8

/* Start (or restart) the search. Every 4-aligned position in writable memory
 * whose five words satisfy the invariant AND are not all zero becomes a
 * candidate; later transitions eliminate the coincidental matches.
 *
 * ⛔ The all-zero rejection is load-bearing: zeros satisfy the invariant
 * trivially and a process is mostly zeros (measured ~57k matches per 1.8 MB,
 * i.e. tens of millions across this game), so without it the candidate table
 * fills in the first few megabytes and never reaches the pad.
 *
 * Consequently a sweep only finds anything while a button is DOWN. That is
 * handled by pad_scan_poll() re-seeding, not by asking the player to hold a
 * key while pressing a hotkey -- a measurement that needs the user to remember
 * something is the wrong measurement. */
void pad_scan_start(void);

/* Re-seed while the search has no candidates. Call from the worker loop; it
 * rate-limits itself and does nothing once the record is identified. */
void pad_scan_poll(void);

/* Sample every surviving candidate once. Call from the worker loop at input
 * rate; it reads whole pages under one guard and indexes candidates out of the
 * local copy, because a per-candidate VirtualQuery is never affordable at
 * scale -- a lesson this project has paid for three times. */
void pad_scan_tick(void);

/* Log what survived and whether the record is identified. */
void pad_scan_report(void);

/* Speak the direction currently held, from the identified record. */
void pad_say_state(void);

/* Address of the identified record's `On` word, or 0 while unresolved. */
uintptr_t pad_record(void);

/* Would this five-word window be accepted as a candidate?
 *
 * Exposed because the seed predicate is where a whole class of false positives
 * is rejected, and that deserves direct assertions rather than being tested
 * only through a full scan. A window holding opposing directions (up+down or
 * left+right) must never be accepted -- a physical pad cannot report them, and
 * a site that did reached the user speaking "up down left right". */
BOOL pad_window_acceptable(const unsigned w[PAD_WORDS]);

#endif /* CFC2_PAD_FIND_H */
