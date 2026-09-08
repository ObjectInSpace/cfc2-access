/*
 * Self-test for the DERIVED key edges.
 *
 * ⭐⭐ WHY THIS EXISTS -- it encodes a measured in-game failure
 * The first in-game run showed the engine's own `Trg` bank empty for roughly
 * five of every six arrow presses, while the `cur` bank plainly showed the key
 * going down. `Trg` is set for one frame (~16 ms) and the mod polls at 100 ms,
 * so it is simply missed most of the time. Anything built on Trg would silently
 * drop most keypresses -- and a screen reader that misses five presses in six
 * is worse than none, because the user cannot tell it apart from the menu not
 * moving.
 *
 * So edges are derived by differencing successive `cur` reads, which is exact
 * at any poll rate. This test drives that derivation through a known sequence,
 * including the sparse-sampling case that broke Trg: a key that goes down AND
 * COMES BACK UP between two polls is genuinely unobservable, and the code must
 * not pretend otherwise.
 *
 * ⚠ Each case was verified to FAIL under the sabotage noted next to it.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../src/kbd_state.h"
#include "../src/log.h"

static int failures;

static void check(int cond, const char *what)
{
    if (cond) printf("  ok    %s\n", what);
    else { printf("  FAIL  %s\n", what); failures++; }
}

/* Drive one poll with a synthetic `cur` containing exactly these codes. */
static kbd_edges poll_with(const int *codes, int n)
{
    static DWORD cur[KBD_BANK_WORDS];
    memset(cur, 0, sizeof(cur));
    for (int i = 0; i < n; i++)
        cur[codes[i] >> 5] |= 1u << (codes[i] & 31);
    g_kbd_test_cur = cur;
    kbd_edges e;
    memset(&e, 0, sizeof(e));
    kbd_poll_edges(&e);
    return e;
}

int main(void)
{
    printf("derived key-edge self-test\n\n");

    const int none[1]  = { 0 };
    const int down[1]  = { KBDC_DOWN };
    const int up[1]    = { KBDC_UP };
    const int both[2]  = { KBDC_UP, KBDC_DOWN };

    /* --- Case 1: the first poll reports NO presses ----------------------
     * There is no previous sample to difference against, so every held key
     * would look newly pressed. Acting on those would fire a menu action the
     * user never requested.
     * SABOTAGE: drop the `first` clearing -> this fails. */
    kbd_reset_edges();
    kbd_edges e = poll_with(down, 1);
    check(e.first, "case 1: first poll is flagged as seeded");
    check(!kbd_is_set(e.pressed, KBDC_DOWN),
          "case 1: a key already held on the first poll is NOT a press");
    check(kbd_is_set(e.down, KBDC_DOWN),
          "case 1: but it IS reported as currently down");

    /* --- Case 2: a genuine press is detected ---------------------------
     * ⭐ THE CASE THE ENGINE'S Trg FAILED IN-GAME. Trg would be empty here
     * unless we happened to sample within one frame of the transition;
     * differencing cur catches it regardless.
     * SABOTAGE: `pressed = cur & prev` instead of `cur & ~prev` -> fails. */
    kbd_reset_edges();
    poll_with(none, 0);
    e = poll_with(down, 1);
    check(!e.first, "case 2: second poll is not flagged as seeded");
    check(kbd_is_set(e.pressed, KBDC_DOWN),
          "case 2: a key that went down between polls IS a press");
    check(!kbd_is_set(e.released, KBDC_DOWN),
          "case 2: and is not simultaneously a release");

    /* --- Case 3: holding is NOT a repeated press -----------------------
     * The user holding a key must produce one press, not one per poll -- a
     * flood of phantom presses would be read as rapid navigation.
     * SABOTAGE: `pressed = cur` -> fails. */
    e = poll_with(down, 1);
    check(!kbd_is_set(e.pressed, KBDC_DOWN),
          "case 3: a key still held is NOT pressed again");
    check(kbd_is_set(e.down, KBDC_DOWN),
          "case 3: it is still reported as down");

    /* --- Case 4: release is detected ------------------------------------
     * SABOTAGE: `released = prev & cur` -> fails. */
    e = poll_with(none, 0);
    check(kbd_is_set(e.released, KBDC_DOWN),
          "case 4: a key that came up between polls IS a release");
    check(!kbd_is_set(e.down, KBDC_DOWN),
          "case 4: and is no longer down");

    /* --- Case 5: keys are independent ------------------------------------
     * A press of one key must not report the other. Without this a single
     * shared-state bug would pass every case above.
     * SABOTAGE: OR all words together -> fails. */
    kbd_reset_edges();
    poll_with(none, 0);
    e = poll_with(up, 1);
    check(kbd_is_set(e.pressed, KBDC_UP), "case 5: UP press detected");
    check(!kbd_is_set(e.pressed, KBDC_DOWN),
          "case 5: DOWN is not falsely reported pressed");

    /* --- Case 6: two keys at once ---------------------------------------
     * Both must appear; menus allow diagonal input and a press during a hold.
     * SABOTAGE: keep only the first set bit -> fails. */
    e = poll_with(both, 2);
    check(kbd_is_set(e.pressed, KBDC_DOWN),
          "case 6: the newly added key is a press");
    check(!kbd_is_set(e.pressed, KBDC_UP),
          "case 6: the already-held key is not re-pressed");
    check(kbd_is_set(e.down, KBDC_UP) && kbd_is_set(e.down, KBDC_DOWN),
          "case 6: both keys are reported down");

    /* --- Case 7: a press+release BETWEEN two polls is invisible ---------
     * ⭐⭐ THE HONEST LIMIT, asserted rather than left implicit. If a key goes
     * down and back up entirely between two 100 ms polls, no differencing
     * scheme can see it -- the state is identical either side. This is the
     * SAME sampling limit that broke Trg, only much rarer (a keypress lasts
     * ~150 ms, a frame ~16 ms). The test states it so nobody later reads a
     * missed press as a bug in the derivation.
     *
     * ⚠ NOT sabotage-provable in the usual way: no change to the derivation
     * makes this observable, which is exactly the point. It documents a
     * property of sampling, not of the code. */
    kbd_reset_edges();
    poll_with(none, 0);
    /* the press happened here, entirely between the two calls */
    e = poll_with(none, 0);
    check(!kbd_is_set(e.pressed, KBDC_DOWN),
          "case 7: a press+release entirely between polls is invisible "
          "(a documented sampling limit, not a defect)");

    g_kbd_test_cur = NULL;
    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
