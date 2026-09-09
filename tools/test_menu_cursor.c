/*
 * Offline self-test for the menu cursor hook.
 *
 * It drives the SHIPPING detour body (mc_test_body -> detour_body) with
 * synthetic arguments, so what is tested is what ships. No game needed.
 *
 * ⭐⭐ THE PROJECT'S RULE, PAID FOR SEVERAL TIMES: a test that has never been
 * SEEN to fail is not evidence. Every mechanism below was deliberately
 * sabotaged and the suite re-run. Results, verbatim:
 *
 *   report every call (never compare old/new)  -> ONLY case 2 fails
 *   never mark a movement as taken             -> the suite HANGS (case 7)
 *   restore the old value after the original   -> cases 1, 3, 7 fail
 *   never count invocations                    -> ONLY case 8 fails
 *   always report "vertical"                   -> ONLY case 4 fails
 *
 * ⚠⚠ AND ONE HONEST NEGATIVE, recorded rather than hidden:
 * sabotaging the pointer check alone (`safe = TRUE`) leaves the whole suite
 * PASSING. Cases 5 and 6 do not test cursor_is_safe() in isolation -- when it
 * is removed, the fault guard catches the access violation and sets `safe`
 * FALSE by the other route, so the hook still behaves correctly. Removing BOTH
 * layers crashes the test with 0xC0000005.
 * ⭐ So what cases 5 and 6 actually establish is: "a bad cursor pointer does
 * not reach an unguarded dereference", by whichever of the two layers gets
 * there first. That is the property that matters for not killing the game --
 * but it is weaker than "the VirtualQuery check works", and this comment exists
 * so nobody later reads the green run as the stronger claim.
 *
 * ⚠ What this test canNOT check either, stated so a green run is not over-read:
 * whether 0x14007cdc0 and 0x14007c9f0 really are the game's cursor helpers.
 * That rests on the offline disassembly and on the runtime prologue guard.
 * This test checks the hook's BEHAVIOUR given those addresses are right.
 */

#include "../src/menu_cursor.h"
#include "../src/memsafe.h"
#include "../src/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int failures;

static void check(const char *name, int cond, const char *fmt, ...)
{
    if (cond) {
        printf("  PASS  %s\n", name);
        return;
    }
    printf("  FAIL  %s", name);
    if (fmt && *fmt) {
        va_list ap;
        va_start(ap, fmt);
        printf("  ");
        vprintf(fmt, ap);
        va_end(ap);
    }
    printf("\n");
    failures++;
}

/* Drain and count movements, returning the last one seen. */
static int drain(mc_move *last)
{
    mc_move m;
    int n = 0;
    while (mc_take_move(&m)) { *last = m; n++; }
    return n;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!mem_guard_install()) {
        printf("FATAL: the fault guard did not pass its own self-test; "
               "every guarded read below would be unprotected.\n");
        return 1;
    }

    mc_move last;
    mc_stats st;
    int cursor;

    /* --- case 1: a real move is observed, with the right old/new ---------- */
    memset(&last, 0, sizeof(last));
    cursor = 3;
    mc_test_body((void *)0xDEAD0000, &cursor, 0, 7, TRUE, 4, TRUE, +1,
                 (void *)0xCA11515E);
    int n1 = drain(&last);
    check("1. a movement is reported once, with old and new indices",
          n1 == 1 && last.old_index == 3 && last.new_index == 4 &&
          last.min_index == 0 && last.max_index == 7 && last.vertical,
          "n=%d old=%d new=%d min=%d max=%d vert=%d",
          n1, last.old_index, last.new_index,
          last.min_index, last.max_index, (int)last.vertical);

    /* --- case 2: a call that does NOT move the cursor reports nothing -----
     * THE DISCRIMINATING CASE. The helpers run on every frame a menu polls
     * input, not only when something moves. A hook that reported every call
     * would make the mod speak the same item continuously. */
    cursor = 4;
    mc_test_body((void *)0xDEAD0000, &cursor, 0, 7, TRUE, 4, TRUE, +1,
                 (void *)0xCA11515E);
    mc_move ignored;
    check("2. a call that leaves the index unchanged reports NOTHING",
          drain(&ignored) == 0, "");

    /* --- case 3: the cursor variable is actually updated by the original --
     * The hook must not swallow or undo the game's write. */
    cursor = 2;
    mc_test_body((void *)0xDEAD0000, &cursor, 0, 7, TRUE, 5, TRUE, +1,
                 (void *)0xCA11515E);
    check("3. the game's write through the cursor pointer survives the hook",
          cursor == 5, "cursor=%d", cursor);

    /* --- case 4: horizontal is distinguished from vertical ---------------- */
    cursor = 0;
    mc_test_body((void *)0xDEAD0000, &cursor, 0, 3, FALSE, 1, TRUE, +1,
                 (void *)0xCA11515E);
    drain(&last);
    check("4. the horizontal helper is reported as horizontal",
          !last.vertical, "vertical=%d", (int)last.vertical);

    /* --- case 5: a bad cursor pointer is REJECTED, not dereferenced -------
     * ⭐⭐ This is the case that matters most for safety. This project has
     * already killed the game by dereferencing a caller's pointer inside a
     * detour. The address below is reserved-not-committed, so a dereference
     * would fault; the hook must decline instead, count the rejection, and
     * still return the original's value. */
    LONG rej_before, rej_after;
    mc_get_stats(&st); rej_before = st.rejected;
    void *reserved = VirtualAlloc(NULL, 0x1000, MEM_RESERVE, PAGE_NOACCESS);
    if (!reserved) {
        printf("  FAIL  5. could not reserve a test page\n");
        failures++;
    } else {
        mc_test_body((void *)0xDEAD0000, (int *)reserved, 0, 7, TRUE, 1,
                     FALSE, +1, (void *)0xCA11515E);
        mc_get_stats(&st); rej_after = st.rejected;
        check("5. an uncommitted cursor pointer is rejected, not dereferenced",
              rej_after == rej_before + 1 && drain(&ignored) == 0,
              "rejected %ld -> %ld", rej_before, rej_after);
        VirtualFree(reserved, 0, MEM_RELEASE);
    }

    /* --- case 6: a NULL cursor is rejected -------------------------------- */
    mc_get_stats(&st); rej_before = st.rejected;
    mc_test_body((void *)0xDEAD0000, NULL, 0, 7, TRUE, 1, FALSE, +1,
                 (void *)0xCA11515E);
    mc_get_stats(&st);
    check("6. a NULL cursor pointer is rejected",
          st.rejected == rej_before + 1, "rejected %ld -> %ld",
          rej_before, st.rejected);

    /* --- case 7: mc_take_move does not repeat a movement ------------------
     * A stale repeat would make the mod re-speak the previous item every poll,
     * which is indistinguishable from the user actually moving there. */
    cursor = 1;
    mc_test_body((void *)0xDEAD0000, &cursor, 0, 7, TRUE, 2, TRUE, +1,
                 (void *)0xCA11515E);
    check("7. a movement is taken exactly once",
          drain(&last) == 1 && drain(&ignored) == 0, "");

    /* --- case 8: calls are counted even when nothing moves ----------------
     * ⭐ Liveness. "0 moves" is only meaningful beside a large call count; with
     * calls == 0 the hook is not intercepting and nothing can be concluded. */
    mc_get_stats(&st);
    check("8. every invocation is counted, moved or not",
          st.calls >= 7, "calls=%ld moves=%ld rejected=%ld",
          st.calls, st.moves, st.rejected);

    /* --- case 9: a WRAP -- step and index delta disagree ------------------
     * ⭐⭐ THE CASE THAT WOULD HAVE CAUGHT THE REAL BUG, added after the fact.
     * The helper's return value is the DIRECTION PRESSED, not the change in the
     * index; the two differ exactly when the cursor wraps. The original test
     * had a stand-in returning `new - old`, which made them agree by
     * construction and taught me a false fact that only surfaced in the
     * in-game log (0 -> 5 reporting step -1, which looked like a sign bug in
     * the mod and was not).
     *
     * Here: index goes 0 -> 5 (a delta of +5) while the user pressed BACKWARD
     * (step -1). The hook must report the step it was GIVEN, not recompute one.
     * This is the real main-menu behaviour, verbatim from the log. */
    cursor = 0;
    mc_test_body((void *)0xDEAD0000, &cursor, 0, 5, TRUE, 5, TRUE, -1,
                 (void *)0xCA11515E);
    drain(&last);
    check("9. on a wrap, step is the direction pressed, not the index delta",
          last.old_index == 0 && last.new_index == 5 && last.step == -1,
          "old=%d new=%d step=%+d (a stand-in returning new-old gives +5)",
          last.old_index, last.new_index, last.step);

    /* --- case 10: the caller address identifies the screen ---------------
     * ⭐⭐ This is what makes the index USEFUL. The 68 cursor call sites sit in
     * 37 distinct functions -- one per menu -- so the return address says WHICH
     * MENU is being navigated. Without it, "item 3 of 6" cannot be turned into
     * an item NAME, because the label run to index into is per-screen.
     *
     * Two different callers must be reported as different. A hook that recorded
     * a constant, or the helper's own address, would look fine on a single
     * screen and silently conflate every menu in the game. */
    cursor = 0;
    mc_test_body((void *)0xDEAD0000, &cursor, 0, 7, TRUE, 1, TRUE, +1,
                 (void *)0xAAAA1111);
    drain(&last);
    void *c1 = last.caller;
    cursor = 0;
    mc_test_body((void *)0xDEAD0000, &cursor, 0, 7, TRUE, 1, TRUE, +1,
                 (void *)0xBBBB2222);
    drain(&last);
    void *c2 = last.caller;
    check("10. the caller address is recorded and distinguishes call sites",
          c1 == (void *)0xAAAA1111 && c2 == (void *)0xBBBB2222,
          "c1=%p c2=%p", c1, c2);

    /* --- case 11: the ORIGINAL's own arguments reach it intact ------------
     * ⭐⭐ THE CASE THAT WOULD HAVE CAUGHT A BUG THE USER HEARD.
     *
     * The helper takes SIX arguments. The detour declared four, on the
     * reasoning that the stack arguments "stay in the caller's frame and reach
     * the original unchanged" -- true for a TAIL call, false for the fresh call
     * a detour actually makes. The original then read its 5th and 6th arguments
     * out of stack slots our frame never filled.
     *
     * The 5th is the cursor SFX SELECTOR (dispatched at 0x14007cf25), so the
     * menu movement sound audibly changed. The 6th is `wrap`, which decides
     * whether the cursor wraps at the ends of a list -- that one did not
     * visibly break, which was luck rather than design.
     *
     * ⚠ Every other case here asks what the hook REPORTED. This one asks what
     * it PASSED ON. That is a different question, and it is why ten green cases
     * missed a defect the user could hear. */
    cursor = 0;
    mc_test_body((void *)0xDEAD0000, &cursor, 0, 7, TRUE, 3, TRUE, +1,
                 (void *)0xCA11515E);
    int got_sfx = 0, got_wrap = 0;
    mc_test_last_passthrough(&got_sfx, &got_wrap);
    check("11. the original receives its 5th and 6th arguments unchanged",
          got_sfx == 307 && got_wrap == 309,
          "sfx_sel=%d (want 307)  wrap=%d (want 309)", got_sfx, got_wrap);

    printf("\n");
    if (failures) {
        printf("FAILED: %d case(s)\n", failures);
        return 1;
    }
    printf("all cases passed\n");
    return 0;
}
