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

/* Button bits, from the reflected names in the unpacked image:
 * bit 0 Rup, 1 Rright, 2 Rdown, 3 Rleft, 4 Lup, 5 Lright, 6 Ldown, 7 Lleft. */
#define PAD_UP     0x11u
#define PAD_RIGHT  0x22u
#define PAD_DOWN   0x44u
#define PAD_LEFT   0x88u

#endif /* CFC2_PAD_WATCH_H */
