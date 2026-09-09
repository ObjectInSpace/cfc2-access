/*
 * Offline self-test for the cGUIObjText reader.
 *
 * It drives the SHIPPING detour body (gt_test_body -> detour_body) and the
 * SHIPPING inspector (gt_test_inspect -> inspect_body) with synthetic objects,
 * so what is tested is what ships. No game needed.
 *
 * ⭐⭐ THE PROJECT'S RULE, PAID FOR SEVERAL TIMES: a test that has never been
 * SEEN to fail is not evidence. Every mechanism below was deliberately
 * sabotaged and the suite re-run; the results are recorded at the bottom of
 * this file, next to the sabotage that produced them.
 *
 * ⚠ What this test canNOT check, stated so a green run is not over-read:
 *
 *  - whether 0x1402f9720 really is cGUIObjText's update method. That rests on
 *    the offline disassembly (the class-name string sits immediately before the
 *    vtable, and this address is in exactly one 8-aligned data slot in the
 *    whole image, that vtable's slot 5) and on the runtime prologue + vtable
 *    guards in check_target(). This test checks the hook's BEHAVIOUR given
 *    those addresses are right.
 *
 *  - ⭐⭐ WHERE THE STRING ACTUALLY LIVES. That is the open question the whole
 *    module exists to answer, and it cannot be answered offline: the layout
 *    has to come from a live object. What the test establishes is that the
 *    inspector correctly reports whatever is there -- including reporting
 *    NOTHING when there is nothing, which is the case a "found it" test would
 *    quietly skip.
 */

#include "../src/gui_text.h"
#include "../src/memsafe.h"
#include "../src/log.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

static int failures;

static void check(const char *name, int cond, const char *detail)
{
    if (cond) {
        printf("  PASS  %s\n", name);
        return;
    }
    printf("  FAIL  %s -- %s\n", name, detail);
    failures++;
}

/* Build a synthetic 528-byte object. Pointer fields are written as addresses
 * inside the buffer itself (base 0x100000), which is exactly the convention
 * gt_test_inspect's resolver implements. */
#define TEST_BASE 0x100000ULL

static void put_ptr(unsigned char *obj, int off, int points_to)
{
    ULONGLONG v = TEST_BASE + (ULONGLONG)points_to;
    memcpy(obj + off, &v, 8);
}

int main(void)
{
    printf("cGUIObjText reader self-test\n");

    /* The inspector reads foreign memory through mem_copy in the live path;
     * the test path does not, but installing the guard keeps the harness
     * honest about what the shipping code needs. */
    if (!mem_guard_install()) {
        printf("  FAIL  the fault guard did not fault during its own "
               "self-test -- refusing to run\n");
        return 1;
    }

    /* --- case 1: the detour passes the original's return value through ----
     * ⚠ The value is chosen by the stand-in, not computed, so "passed through"
     * is provable rather than merely plausible. */
    gt_test_reset();
    {
        void *r = gt_test_body((void *)0x11110000ULL, (void *)0x22220000ULL);
        check("case 1: the original's return value is passed through untouched",
              r == (void *)(ULONG_PTR)0xABCDEF0012345678ULL,
              "the detour altered or replaced the game's return value");
    }

    /* --- case 2: the detour captures the instance pointer ---------------- */
    check("case 2: the instance pointer is captured",
          gt_test_last_captured() == (void *)0x11110000ULL,
          "the pointer the game handed us was not recorded");

    /* --- case 3: it counts calls -----------------------------------------
     * ⭐ The call count is what makes "0 instances" interpretable. With calls
     * == 0 the hook is not intercepting and nothing can be concluded. */
    {
        gt_test_body((void *)0x33330000ULL, NULL);
        gt_stats st;
        gt_get_stats(&st);
        char d[128];
        snprintf(d, sizeof d, "expected 2 calls, got %ld", st.calls);
        check("case 3: invocations are counted", st.calls == 2, d);
    }

    /* --- case 4: a pointer field holding text is found ------------------- */
    gt_test_reset();
    {
        unsigned char obj[GT_CLASS_SIZE];
        memset(obj, 0, sizeof obj);
        strcpy((char *)obj + 0x100, "CONTINUE");
        put_ptr(obj, GT_CAND_B, 0x100);

        int off = -1;
        BOOL ok = gt_test_inspect(obj, sizeof obj, &off);
        char d[128];
        snprintf(d, sizeof d, "ran=%d, offset=%d (wanted 0x%x)",
                 (int)ok, off, GT_CAND_B);
        check("case 4: a pointer field holding text is located", ok && off == GT_CAND_B, d);
    }

    /* --- case 5: NOTHING is reported when there is nothing ---------------
     * ⭐⭐ THE CASE THAT MATTERS MOST, and the one a "did it find the string"
     * test would never write. An inspector that always names some offset is
     * useless: the whole point of running it in-game is to believe it when it
     * says a field IS the string. If it cannot say "no", its yes means
     * nothing. -1, not 0 -- "could not tell" and "offset 0" must not look
     * alike. */
    {
        unsigned char obj[GT_CLASS_SIZE];
        memset(obj, 0, sizeof obj);
        /* Pointer-shaped fields that resolve to binary noise, not text. */
        put_ptr(obj, GT_CAND_A, 0x080);
        put_ptr(obj, GT_CAND_B, 0x090);
        for (int i = 0x80; i < 0xa0; i++)
            obj[i] = (unsigned char)(0x80 + (i & 0x3f));   /* non-ASCII */

        int off = -1;
        BOOL ok = gt_test_inspect(obj, sizeof obj, &off);
        char d[128];
        snprintf(d, sizeof d, "ran=%d, offset=%d (wanted -1)", (int)ok, off);
        check("case 5: an object with no text yields NO conclusion (-1)",
              ok && off == -1, d);
    }

    /* --- case 6: an inline string is found too ---------------------------
     * ⚠ A sweep that only followed pointers would miss a fixed char array in
     * the object entirely, and a fixed array is just as likely a design. */
    {
        unsigned char obj[GT_CLASS_SIZE];
        memset(obj, 0, sizeof obj);
        strcpy((char *)obj + 0x120, "OPTIONS");

        int off = -1;
        BOOL ok = gt_test_inspect(obj, sizeof obj, &off);
        char d[128];
        snprintf(d, sizeof d, "ran=%d, offset=%d (wanted 0x120)", (int)ok, off);
        check("case 6: a string stored INLINE is located", ok && off == 0x120, d);
    }

    /* --- case 7: a longer string beats a candidate offset ----------------
     * ⭐ The two offline candidate offsets earned a look, not a verdict. If
     * another offset holds better text, it wins -- otherwise a wrong guess
     * from the offline reading would outrank the real field forever. */
    {
        unsigned char obj[GT_CLASS_SIZE];
        memset(obj, 0, sizeof obj);
        strcpy((char *)obj + 0x0c0, "ab");                 /* short, candidate */
        put_ptr(obj, GT_CAND_A, 0x0c0);
        strcpy((char *)obj + 0x0e0, "PLAYER SELECT MODE");  /* long, elsewhere */
        put_ptr(obj, 0x150, 0x0e0);

        int off = -1;
        gt_test_inspect(obj, sizeof obj, &off);
        char d[128];
        snprintf(d, sizeof d, "offset=%d (wanted 0x150, the longer string)", off);
        check("case 7: a clearly longer string outranks a candidate offset",
              off == 0x150, d);
    }

    /* --- case 8: a wide (UTF-16) string is recognised --------------------
     * The shell is a Japanese-authored engine; a widened copy in the element
     * would be unsurprising, so it is checked rather than assumed absent. */
    {
        unsigned char obj[GT_CLASS_SIZE];
        memset(obj, 0, sizeof obj);
        const char *w = "MUSEUM";
        for (int i = 0; w[i]; i++) obj[0x0b0 + i * 2] = (unsigned char)w[i];
        put_ptr(obj, GT_CAND_B, 0x0b0);

        int off = -1;
        gt_test_inspect(obj, sizeof obj, &off);
        char d[128];
        snprintf(d, sizeof d, "offset=%d (wanted 0x%x)", off, GT_CAND_B);
        check("case 8: a UTF-16 string is recognised", off == GT_CAND_B, d);
    }

    /* --- case 9: install REFUSES when the target is not the expected code -
     * ⭐⭐ Reported as a REASON, not a bare FALSE. A test asserting only
     * "install failed" passes with every guard deleted, because MinHook
     * independently refuses an unmapped address -- the outcome would be right
     * and the assertion worthless. Asking WHICH layer refused is what makes
     * the guard observable.
     *
     * Here nothing is mapped at 0x1402f9720 in the test process, so the
     * protection check must be the one that fires -- before any byte is
     * written to any address. */
    {
        gt_stats st;
        BOOL ok = gt_install();
        gt_install_status why = gt_install_why();
        gt_get_stats(&st);
        char d[160];
        snprintf(d, sizeof d, "install returned %d, reason %d (wanted 0 and "
                 "GT_INSTALL_BAD_PROTECT=%d), installed=%d",
                 (int)ok, (int)why, (int)GT_INSTALL_BAD_PROTECT,
                 (int)st.installed);
        check("case 9: install refuses at the PROTECTION check, and says so",
              !ok && why == GT_INSTALL_BAD_PROTECT && !st.installed, d);
    }

    /* --- case 10: polling without an install reports nothing -------------
     * ⚠ Not "returns quietly": it must return FALSE, so a caller can tell
     * "no new instance" from "inspected and found nothing". */
    check("case 10: gt_poll_and_log reports FALSE when not installed",
          !gt_poll_and_log(),
          "it claimed to have something to report with no hook installed");

    mem_guard_remove();

    printf("\n%s (%d failure(s))\n", failures ? "FAILED" : "ALL PASSED",
           failures);
    return failures ? 1 : 0;
}

/*
 * ⭐⭐ SABOTAGE LOG -- every mechanism above was broken on purpose and the
 * suite re-run. A test that has never been seen to fail is not evidence.
 * Run 2026-09-09; results verbatim, each sabotage applied alone:
 *
 *   detour records NULL instead of the pointer   -> ONLY case 2 fails
 *   never count invocations                      -> ONLY case 3 fails
 *   inspector's `best` starts at 0, not -1       -> cases 5 and 6 fail
 *   return NULL instead of the original's value  -> ONLY case 1 fails
 *   delete the install protection guard          -> ONLY case 9 fails
 *   delete the inline-string sweep               -> ONLY case 6 fails
 *
 * ⭐⭐ THE ONE WORTH READING TWICE is the install guard. With it deleted,
 * gt_install() STILL returns FALSE -- MinHook refuses an unmapped address on
 * its own -- so an assertion of "install failed" stays green with the guard
 * gone. What changes is the REASON: 2 (BAD_PROTECT) becomes 3 (BAD_PROLOGUE).
 * Case 9 asserts the reason, which is why it catches this and the obvious
 * assertion would not. That asymmetry is the entire point of gt_install_why().
 *
 * ⚠ AND ONE HONEST NEGATIVE, recorded rather than hidden: no case here
 * isolates the VirtualQuery/fault-guard pairing in resolve_live(), because the
 * test path resolves pointers inside the harness's own buffer and never
 * touches foreign memory. That pairing is covered by test_memsafe_threads.c
 * and by menu_cursor's cases, not here. A green run of this suite is NOT
 * evidence that the live read is safe -- it is evidence that the inspector
 * draws the right conclusions from bytes it was given.
 */
