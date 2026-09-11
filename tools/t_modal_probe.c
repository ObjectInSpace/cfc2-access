/* Does the SHIPPING modal path produce the line, given the real prompt text?
 * Exercises gt_latest_text -> menu_speak_is_modal_prompt -> flatten, i.e. the
 * chain menu_speak_check_modal() runs, with the resolver stubbed because there
 * is no GMD table here. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "../src/gui_text.h"
#include "../src/menu_speak.h"
#include "../src/log.h"

#define P "If you exit the application, all unsaved\r\nprogress will be lost. Continue?"

static BOOL res(int idx, char *out, unsigned n)
{
    if (idx != 384 || !out || !n) return FALSE;
    _snprintf(out, n, "%s", P); out[n-1] = 0; return TRUE;
}
int main(void)
{
    int bad = 0;
    log_init();          /* menu_speak_line() reports through the log */
    /* 1. the text reaches gt_latest_text with NO cursor movement at all */
    gt_test_reset();
    gt_test_focus_enable();
    static ULONGLONG o[GT_CLASS_SIZE/8];
    memset(o,0,sizeof o); o[0] = GT_VTABLE_ADDR;
    gt_test_settext_body(o, P, NULL, 0);

    /* ⛔⛔ AND THE STRINGS THE DIALOG SETS *AFTER* THE PROMPT.
     *
     * The first version of this probe planted ONLY the prompt and PASSED, while
     * the shipping code did nothing in the game every single time. The real
     * dialog sets the prompt, then "Yes", then "No" -- all prose -- so a reader
     * that takes "the newest prose" gets "No" and never looks at the prompt.
     *
     * ⭐ A fixture that omits the screen's own neighbours omits the very input
     * that breaks the code. These two lines are the difference between a test
     * that proves something and one that agrees with me. */
    static ULONGLONG oy[GT_CLASS_SIZE/8], on[GT_CLASS_SIZE/8];
    memset(oy, 0, sizeof oy); oy[0] = GT_VTABLE_ADDR;
    memset(on, 0, sizeof on); on[0] = GT_VTABLE_ADDR;
    gt_test_settext_body(oy, "Yes", NULL, 0);
    gt_test_settext_body(on, "No",  NULL, 0);

    /* ⛔⛔ ADVANCE THE MOVEMENT SEQUENCE PAST THE RECORD.
     *
     * Without this the test is worthless: in a fresh process the record's stamp
     * happens to equal the current sequence, so a gt_latest_text() re-gated on
     * the stamp STILL PASSES -- verified by sabotage, which is how this hole was
     * found. The quit dialog's real situation is that the text was set and the
     * cursor has since moved elsewhere (or never moved at all), so the stamp
     * CANNOT match. Reproduce that, or the stamp-free lookup is untested. */
    gt_note_focus_move();
    gt_note_focus_move();

    /* The SHIPPING scan: walk the prose newest-first until one MATCHES. */
    char got[512];
    int i, found = 0;
    for (i = 0; gt_prose_at(i, got, sizeof got); i++)
        if (menu_speak_is_modal_prompt(got, res)) { found = 1; break; }

    if (!found) {
        printf("FAIL  the prompt was never found by the scan\n"); bad++;
    } else if (i == 0) {
        /* ⛔ If it is at index 0 the fixture is not reproducing the dialog, and
         * a "newest prose only" reader would pass -- which is the bug. */
        printf("FAIL  found at index 0 -- fixture not reproducing the dialog\n");
        bad++;
    } else {
        printf("ok    prompt found at index %d, past \"No\" and \"Yes\"\n", i);
    }

    /* 3. and flattens to what the user hears */
    char flat[512];
    menu_speak_flatten_prompt(got, flat, sizeof flat);
    const char *want = "If you exit the application, all unsaved progress "
                       "will be lost. Continue?";
    if (strcmp(flat, want) != 0) {
        printf("FAIL  flattened wrongly: \"%s\"\n", flat); bad++;
    } else printf("ok    prompt flattens correctly\n");

    /* 4. ⛔⛔ CALL THE SHIPPING FUNCTION AND READ WHAT IT TRIED TO SAY.
     *
     * My first version of this check REIMPLEMENTED the option lookup here --
     * and passed with the real one disabled, because the harness was supplying
     * the mechanism under test ([[harness-supplies-the-mechanism]]). Third time
     * on this project, second time today.
     *
     * menu_speak_line() logs the exact string it hands the backend (which
     * refuses, there being no speech here -- and it logs that too). So the ONE
     * honest assertion is: run menu_speak_check_modal() and read the line out
     * of the log it wrote. Nothing here decides what the answer should be. */
    {
        FILE *lf;
        char buf[1024];
        int said = 0;
        const char *WANT = "If you exit the application, all unsaved progress "
                           "will be lost. Continue? Yes or No";

        menu_speak_test_set_modal_resolver(res);   /* no GMD table here */
        menu_speak_check_modal();                  /* the real thing */
        menu_speak_test_set_modal_resolver(NULL);

        lf = fopen("CFC2Access.log", "rb");
        if (!lf) {
            printf("FAIL  no log written -- cannot tell what was said\n");
            bad++;
        } else {
            while (fgets(buf, sizeof buf, lf))
                if (strstr(buf, WANT)) said = 1;
            fclose(lf);
            if (!said) {
                printf("FAIL  menu_speak_check_modal() did not produce:\n"
                       "        \"%s\"\n", WANT);
                bad++;
            } else {
                printf("ok    the SHIPPING path said: \"%s\"\n", WANT);
            }
        }
    }

    printf("\n%s\n", bad ? "FAILED" : "ALL PASSED");
    return bad ? 1 : 0;
}
