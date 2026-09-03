/*
 * Watch the sPad singleton and report which bytes track the keyboard.
 *
 * ⭐ WHY THIS IS NOT ANOTHER SCANNER.
 *
 * The previous approach searched 1.4 GB for a struct it could only recognise
 * by behaviour, and produced three false positives in a row -- a shadow-copy
 * pair, an alternating boolean, and a site frozen at one value. Each time the
 * fix was another filter, and each time a different look-alike survived it.
 * There is an endless supply of memory that resembles a pad record.
 *
 * This does not search. It follows ONE pointer, established offline:
 *
 *     [image + 0x22c82c8]  ->  the sPad singleton
 *
 * That address is a fact about the binary, not a guess. It was found by
 * anchoring on the reflection metadata: every top-level singleton class has a
 * null, heavily-referenced slot 0x30 bytes after its name pointer, and nested
 * types (sPad::Pad, sPad::Map, sCamera::Viewport ...) have none. The pattern
 * holds for sUnit, sCamera, sGUI, sPad and sArea, and the RIP arithmetic at
 * the 403 use sites resolves to exactly this address. ASLR is off and the exe
 * has no relocations, so it is valid on every launch.
 *
 * ⚠ WHAT IS NOT KNOWN: where the pad records sit INSIDE that object. The
 * class-size field in the descriptor decodes inconsistently -- three of five
 * classes are dereferenced far beyond their claimed size -- so the layout is
 * genuinely unresolved rather than merely unmeasured. Do not trust 0xb8 here;
 * that grid fit 6 of 14 observed offsets, which is noise.
 *
 * So this reports rather than concludes. It snapshots a window of the object
 * while a direction is held and again while nothing is, and names the bytes
 * that differ. If a byte holds 0x04 or 0x40 with Down pressed and 0 released,
 * that is the record -- read off the machine instead of inferred.
 *
 * ⚠ It deliberately makes no claim about which byte is "the" pad. Naming a
 * winner is what went wrong three times; the user and the log can see the
 * candidates and decide.
 *
 * Read-only: guarded reads, no hooks, no calls into game code.
 */
#ifndef CFC2_PAD_WATCH_H
#define CFC2_PAD_WATCH_H

#include <windows.h>

/* Image-relative address of the sPad singleton pointer. */
#define PAD_SINGLETON_RVA 0x22c82c8

/* How far into the object to watch. The layout is unknown, so this is
 * deliberately generous: the observed dereferences off this pointer run past
 * 0x34000, so a small window would very likely miss the records entirely. */
#define PAD_WATCH_SPAN 0x36000

/* Sample the object. `dirs` is what the player is holding (PAD_* bits, 0 for
 * none) -- the two states are compared against each other, so both are needed.
 * Call from the worker loop; cheap, and it stops once it has both. */
void pad_watch_tick(unsigned dirs);

/* Log the bytes that differed between held and released, with their values. */
void pad_watch_report(void);

/* ⛔⛔ THESE OFFSETS ARE NOT THE PAD -- DISPROVED 2026-09-03. KEPT AS A
 * WARNING, NOT AS A RESULT.
 *
 * A first run with LEFT held reported exactly two bytes carrying Left bits
 * (0x80 stick, 0x08 D-pad) and I called it confirmation. It was not:
 *
 * 1. ⛔ THE FILTER SEARCHED FOR THOSE VALUES. It accepted only held bytes
 *    equal to 0x08/0x80/0x88, then finding two was reported as independent
 *    evidence. Circular. With ~80 bytes dropping to zero on release, two
 *    landing on those values is roughly what chance predicts.
 *
 * 2. ⛔⛔ THERE WAS NO SAME-STATE CONTROL. The two captures were taken EIGHT
 *    SECONDS apart, so the diff is dominated by elapsed time -- timers, frame
 *    counters, animation phase -- not by the key. A second run holding DOWN
 *    produced the IDENTICAL count of 109 differing bytes, and +0x34bd8 again
 *    read 0x80. A byte that says 0x80 whether Left or Down is held is a
 *    constant flag, not input state.
 *
 * ⭐ This project had already paid for lesson 2 in the heapdiff route: without
 * a control, everything that merely ticks looks like a signal. A two-state
 * diff means nothing until a same-state diff shows what changes anyway. I
 * built the differential tool and left out the part that makes it valid.
 *
 * What IS still solid: the singleton address below. It resolved in-game to a
 * real object (0x2881a8f0) on every run. Where the pad data lives inside or
 * behind it remains unknown. */
#define PAD_STICK_OFFSET 0x34bd8   /* DISPROVED -- reads 0x80 regardless */
#define PAD_DPAD_OFFSET  0x34c00   /* DISPROVED */

/* Speak whichever direction the confirmed bytes currently report. */
void pad_watch_say(void);

/* Button bits, from the reflected names in the unpacked image:
 * bit 0 Rup, 1 Rright, 2 Rdown, 3 Rleft, 4 Lup, 5 Lright, 6 Ldown, 7 Lleft. */
#define PAD_UP     0x11u
#define PAD_RIGHT  0x22u
#define PAD_DOWN   0x44u
#define PAD_LEFT   0x88u

#endif /* CFC2_PAD_WATCH_H */
