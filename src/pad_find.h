/*
 * Find MTFramework's live pad state -- route 5.
 *
 * ⭐ WHY THIS IS NOT A FIFTH VARIATION ON THE FOUR CLOSED ROUTES.
 *
 * Routes 1-3 each anchored on something derived from shipped FILE content, and
 * file content does not vary. Route 4 inverted that and scanned for variance,
 * which worked -- it found real cursor-reactive memory -- but variance alone
 * could not say what a field MEANT, and it produced a monotonic-counter false
 * positive that passed every filter it had. The eventual finding was that the
 * GUI is animation-driven: selection is "which node holds the SELECT state",
 * so there is no flat 0..N-1 index anywhere to scan for.
 *
 * This route reads the answer from the ENGINE'S OWN DATA MODEL instead, which
 * was recovered from the unpacked image:
 *
 *   sPad          class descriptor, size 0x3c4, name hash 0x7db4fe86
 *   sPad::Pad     per-pad state record, size 0xb8
 *   sPad::Map     button mapping, size 0x50
 *
 * and the reflected field names of the pad record, in declaration order:
 *
 *   Old  Trg  Rel  Chg  Rep   -- the five button words
 *   RepeatStartTime RepeatTime -- menu auto-repeat timing
 *
 * with the button BIT layout (16 names on a fixed stride, then the analog
 * axes) giving:
 *
 *   bit 0 Rup   bit 1 Rright  bit 2 Rdown   bit 3 Rleft     (D-pad)
 *   bit 4 Lup   bit 5 Lright  bit 6 Ldown   bit 7 Lleft     (left stick)
 *
 * So DOWN = 0x44, UP = 0x11, LEFT = 0x88, RIGHT = 0x22 -- and both menu
 * navigation (up/down) and settings adjustment (left/right) are the same four
 * bits in the same word. One reader covers both.
 *
 * ⚠⚠ THE POINT: THIS TARGET IS SELF-VALIDATING.
 *
 * Every earlier route had to argue that a field it found was the field it
 * wanted. Here the five button words stand in an arithmetic relationship that
 * the engine maintains every frame:
 *
 *      Trg == Cur & ~Old        (newly pressed this frame)
 *      Rel == Old & ~Cur        (newly released)
 *      Chg == Cur ^ Old         (changed either way)
 *
 * A timer, an animation phase, an allocation counter or a frame counter cannot
 * satisfy those simultaneously by chance. That is a POSITIVE identification
 * test, not a correlation -- which is exactly what routes 3 and 4 lacked, and
 * exactly what let a monotonic counter masquerade as state for a whole session.
 *
 * ⚠ A found candidate is still only "a struct shaped like sPad::Pad". It
 * becomes the pad when its bits move WITH the keys the player presses, which
 * the verifier reports rather than assumes. "The pattern matched" and "this is
 * the pad" are different claims and this project has conflated them before.
 *
 * Read-only: VirtualQuery + guarded page reads, no hooks, no calls into game
 * code. Hooking crashed this game once already, and nothing here needs it.
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
#define PAD_VERT    (PAD_UP   | PAD_DOWN)       /* 0x55 */
#define PAD_HORZ    (PAD_LEFT | PAD_RIGHT)      /* 0xaa */

/* Size of the per-pad record, from the sPad::Pad class descriptor. */
#define PAD_RECORD_SIZE 0xb8

/* Arm the search: record the first sample of every candidate site.
 *
 * Called on the hotkey rather than at a fixed beat, because the pad struct is
 * only meaningful once the game is actually reading input, and a scan run at a
 * fixed startup beat has twice in this project produced a confident report
 * about memory that did not exist yet. */
void pad_scan_arm(void);

/* Sample armed candidates and apply the Trg/Rel/Chg consistency test.
 *
 * Runs from the worker loop at input rate. Cheap by construction: it reads
 * whole pages under one guard and indexes candidates out of the local copy --
 * a per-candidate VirtualQuery is never affordable at scale, a lesson this
 * project has now paid for three times (hd_capture, gui_tree, hd_span_tick). */
void pad_scan_tick(void);

/* Report what survived, best first, and say whether anything is confirmed. */
void pad_scan_report(void);

/* Speak the direction the confirmed pad is currently reporting. This is the
 * end goal in miniature: if Down speaks "down" and Left speaks "left", the
 * engine's own input state is readable and the mod can narrate menus. */
void pad_say_state(void);

/* Forget everything and start over. */
void pad_scan_reset(void);

/* --- for the self-test ----------------------------------------------------
 * How many sites are currently confirmed, and is this exact address one of
 * them? The harness needs to assert MEMBERSHIP rather than read it out of the
 * log: the report prints only the first handful of survivors, so a sabotage
 * that floods the survivor set with thousands of false positives can leave a
 * log-scraping assertion passing while the mechanism is entirely broken.
 * That is the "assert the result, not the header" failure this project has
 * already paid for once. */
unsigned pad_confirmed_count(void);
BOOL     pad_is_confirmed(uintptr_t addr);

/* Pin the site that pad_say_state() reads, so a test can verify the DECODER
 * against a known struct instead of against whichever mirror the ranking
 * happened to pick. In the game the ranking chooses; here the harness must be
 * able to ask "given THIS pad, do you say the right direction?" -- otherwise
 * the case is testing the ranking and the decoder at once and is flaky. */
void pad_debug_pin(uintptr_t addr);

#endif /* CFC2_PAD_FIND_H */
