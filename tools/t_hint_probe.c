/* Does the SHIPPING hint path speak the achievement description?
 *
 * ⛔⛔ WHY THIS IS AN END-TO-END PROBE AND NOT A UNIT CASE. The feature this
 * tests exists because the mod ALREADY HAD every part it needed and still said
 * nothing: gt captured the text, prompt_identify could resolve it, and the user
 * sat in silence for 4 seconds because no code path connected them. Units that
 * pass while the chain does nothing is exactly the failure that produced "you
 * did nothing" on the modal path. So this drives menu_speak_check_hint() itself
 * and reads what it actually spoke out of the log.
 *
 * ⭐ NO RESOLVER STUB HERE, unlike the modal probe: prompt_id.c carries the real
 * generated table, so identification is the shipping identification.
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "../src/gui_text.h"
#include "../src/menu_speak.h"
#include "../src/log.h"

/* ⭐ The exact string the game set in the 02:38 run, captured from the log.
 * ACHIEVEMENT_11_DESC, the hint for "The True(r) Final Battle". */
#define HINT "Encounter Shin Akuma or Ultimate Rugal in Arcade Mode of " \
             "Capcom vs. SNK 2: Mark of the Millennium 2001."

/* An achievement NAME -- the grid's own row text. The movement path speaks
 * this, so the hint path must NOT: announcing it would double every movement. */
#define NAME "The True(r) Final Battle"

/* ⚠ Local to the probe: the shipping log has no size API and does not need one
 * added for a test's convenience. "Did the mod say anything?" is measured as
 * "did the log grow?", which is what menu_speak_line() actually does. */
static long log_size(void)
{
    FILE *lf = fopen("CFC2Access.log", "rb");
    long n = 0;
    if (!lf) return 0;
    fseek(lf, 0, SEEK_END);
    n = ftell(lf);
    fclose(lf);
    return n;
}

static int log_says(const char *want)
{
    /* ⚠ Read the TAIL, not the head. An earlier version read the first 4 KB and
     * would have started reporting false FAILs as soon as the log grew past
     * that -- a test that rots into failure for a reason unrelated to the code. */
    FILE *lf = fopen("CFC2Access.log", "rb");
    char buf[65536];
    int found = 0;
    long end;
    size_t n;
    if (!lf) return 0;
    fseek(lf, 0, SEEK_END);
    end = ftell(lf);
    if (end > (long)sizeof buf - 1)
        fseek(lf, end - ((long)sizeof buf - 1), SEEK_SET);
    else
        fseek(lf, 0, SEEK_SET);
    n = fread(buf, 1, sizeof buf - 1, lf);
    buf[n] = 0;
    fclose(lf);
    found = strstr(buf, want) != NULL;
    return found;
}

static void plant(const char *s)
{
    static ULONGLONG o[GT_CLASS_SIZE / 8];
    memset(o, 0, sizeof o);
    o[0] = GT_VTABLE_ADDR;
    gt_test_settext_body(o, s, NULL, 0);
}

int main(void)
{
    int bad = 0;
    log_init();

    /* --- 1. the hint is SPOKEN, with no cursor movement anywhere ---------- */
    gt_test_reset();
    gt_test_focus_enable();
    plant(HINT);
    menu_speak_check_hint();
    if (!log_says("Encounter Shin Akuma or Ultimate Rugal")) {
        printf("  FAIL  1. the hint view's description is spoken\n");
        bad = 1;
    } else {
        printf("  PASS  1. the hint view's description is spoken\n");
    }

    /* --- 2. holding the same hint does NOT repeat it ---------------------- */
    /* ⚠ The poll runs every 20 ms; without the guard the user would hear the
     * same sentence 50 times a second for as long as the view is open. */
    {
        long before = log_size();
        menu_speak_check_hint();
        menu_speak_check_hint();
        if (log_size() != before) {
            printf("  FAIL  2. the same hint is not repeated on every poll\n");
            bad = 1;
        } else {
            printf("  PASS  2. the same hint is not repeated on every poll\n");
        }
    }

    /* --- 3. an achievement NAME is NOT spoken by this path ---------------- */
    /* ⛔ The grid's row text is already spoken by the movement path. If the
     * family test were widened to ACHIEVEMENT_* this would double it. */
    {
        long before;
        gt_test_reset();
        gt_test_focus_enable();
        plant(NAME);
        before = log_size();
        menu_speak_check_hint();
        if (log_size() != before) {
            printf("  FAIL  3. an achievement NAME is not spoken as a hint\n");
            bad = 1;
        } else {
            printf("  PASS  3. an achievement NAME is not spoken as a hint\n");
        }
    }

    /* --- 4. closing the view re-arms, so the SAME hint speaks again ------- */
    /* ⚠ A one-shot latch would leave the second visit silent, which reads as a
     * broken mod. The modal path clears its guard for the same reason. */
    {
        long before;
        gt_test_reset();          /* the view closed: no prose on screen */
        gt_test_focus_enable();
        menu_speak_check_hint();  /* clears the guard */
        plant(HINT);
        before = log_size();
        menu_speak_check_hint();
        if (log_size() == before) {
            printf("  FAIL  4. re-opening the same hint speaks it again\n");
            bad = 1;
        } else {
            printf("  PASS  4. re-opening the same hint speaks it again\n");
        }
    }

    /* --- 5. an unrelated string is not mistaken for a hint ---------------- */
    {
        long before;
        gt_test_reset();
        gt_test_focus_enable();
        plant("Total Play Sessions: 52");
        before = log_size();
        menu_speak_check_hint();
        if (log_size() != before) {
            printf("  FAIL  5. a non-achievement string is not spoken\n");
            bad = 1;
        } else {
            printf("  PASS  5. a non-achievement string is not spoken\n");
        }
    }

    /* --- 6. ⛔⛔ A DESCRIPTION ALREADY IN THE RECORD DOES NOT RE-FIRE -------
     *
     * USER-HEARD BUG that cases 1-5 all MISSED: on the achievements grid the
     * mod read out an unlock condition on EVERY cursor movement, with the
     * Hints view never opened. gt_prose_at() enumerates a RETAINED HISTORY, so
     * a description captured on an earlier visit sat in the record and
     * re-triggered forever.
     *
     * ⚠ WHY THE EXISTING CASES COULD NOT CATCH IT: each one reset the record
     * (gt_test_reset) and planted exactly one string, so "still in the record
     * from before" was a state the fixture never built. A fixture that starts
     * clean cannot test staleness.
     *
     * Here the description stays in the record while OTHER text arrives after
     * it -- the grid's own row titles, which is exactly what the game does --
     * and the hint path must stay silent. */
    {
        long before;
        gt_test_reset();
        gt_test_focus_enable();
        plant(HINT);
        menu_speak_check_hint();          /* the view opened: speaks once */
        /* The user closes it and sweeps the grid: titles keep arriving. */
        plant(NAME);
        plant("Another Me");
        plant("Photo Finish");
        before = log_size();
        menu_speak_check_hint();
        menu_speak_check_hint();
        menu_speak_check_hint();
        if (log_size() != before) {
            printf("  FAIL  6. a description still in the record does NOT "
                   "re-fire while the user browses\n");
            bad = 1;
        } else {
            printf("  PASS  6. a description still in the record does NOT "
                   "re-fire while the user browses\n");
        }
    }

    printf(bad ? "\nFAILED\n" : "\nall cases passed\n");
    return bad;
}
