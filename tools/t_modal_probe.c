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

/* How many log lines contain `needle` -- i.e. how many times the shipping path
 * tried to say it. */
static int count_log(const char *needle)
{
    FILE *f = fopen("CFC2Access.log", "rb");
    char b[1024];
    int n = 0;
    if (!f) return -1;
    while (fgets(b, sizeof b, f))
        if (strstr(b, needle)) n++;
    fclose(f);
    return n;
}

static void run_modal(void)
{
    menu_speak_test_set_modal_resolver(res);
    menu_speak_check_modal();
    menu_speak_test_set_modal_resolver(NULL);
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
    static unsigned char parent[0x400];
    memset(oy, 0, sizeof oy); oy[0] = GT_VTABLE_ADDR;
    memset(on, 0, sizeof on); on[0] = GT_VTABLE_ADDR;
    /* ⭐ The real dialog's shape: both options share a parent at +0x088, and
     * the selected index lives at parent+0x38c (0 = the TOP option = "Yes"). */
    memset(parent, 0, sizeof parent);
    { void *pp = parent;
      memcpy((char *)oy + 0x88, &pp, sizeof pp);
      memcpy((char *)on + 0x88, &pp, sizeof pp); }
    *(unsigned *)(parent + 0x38c) = 1;      /* "No" is selected */
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
        /* ⭐ With index 1 planted, the SELECTED option is "No" -- so that is
         * what must be spoken, not a list of both. */
        const char *WANT = "If you exit the application, all unsaved progress "
                           "will be lost. Continue? No";

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

    /* 5. ⛔⛔ AND THE OTHER INDEX. One direction passes with the mapping
     * INVERTED -- which is exactly the bug caught above, where index 1 spoke
     * "Yes". Asserting both directions is what pins it. */
    {
        FILE *lf2; char b2[1024]; int said2 = 0;
        /* ⭐ SECOND announcement of the SAME dialog: only the option changed,
         * so ONLY the option is spoken. The user reported the old behaviour --
         * "it repeats the whole dialog, it should just say yes or no" -- and a
         * substring test for "Yes" would pass on the full sentence too, so the
         * assertion below requires the line to BE "Yes", not contain it. */
        /* ⚠ Match the QUOTED LINE, not the verb: with no speech backend in a
         * test process menu_speak_line() logs "NOT SPOKEN (backend refused)"
         * rather than "spoke". Keying on the verb made this fail while the code
         * was correct -- the test's expectation was wrong, not the feature. */
        const char *WANT2 = ": \"Yes\"";

        *(unsigned *)(parent + 0x38c) = 0;     /* now the TOP option */
        menu_speak_test_set_modal_resolver(res);
        menu_speak_check_modal();              /* the guard keys on the index */
        menu_speak_test_set_modal_resolver(NULL);

        lf2 = fopen("CFC2Access.log", "rb");
        if (lf2) {
            while (fgets(b2, sizeof b2, lf2))
                if (strstr(b2, WANT2)) said2 = 1;
            fclose(lf2);
        }
        if (!said2) {
            printf("FAIL  index 0 did not speak the TOP option (\"Yes\")\n");
            bad++;
        } else {
            printf("ok    index 0 speaks \"Yes\", index 1 speaks \"No\"\n");
        }
    }

    /* ⛔⛔ USER-REPORTED (2026-10-04): "sometimes the confirm quit yes/no
     * prompts are not reading." Cases 6-8 are the three ways it went silent,
     * each from the record being a HISTORY that nothing empties in shipping
     * play (the only clear was a diagnostics-only report). */
    const char *PROMPT_LINE = "will be lost. Continue?";

    /* 6. THE SAME DIALOG OPENED AGAIN, same option highlighted. The 22:28 run
     * did exactly this ("Exit the game?" twice at move 12). The old guard keyed
     * on the prompt text, and the closed dialog's prompt never left the record,
     * so the second appearance matched the first and said nothing. */
    {
        int before = count_log(PROMPT_LINE);
        gt_test_settext_body(o, P, NULL, 0);        /* re-opened: set afresh */
        gt_test_settext_body(oy, "Yes", NULL, 0);
        gt_test_settext_body(on, "No",  NULL, 0);
        run_modal();                                /* index still 0 */
        int after = count_log(PROMPT_LINE);
        if (after != before + 1) {
            printf("FAIL  6. a RE-OPENED dialog was not announced "
                   "(prompt lines %d -> %d)\n", before, after);
            bad++;
        } else printf("ok    6. the same dialog opened again is announced again\n");
    }

    /* 7. AND A CLOSED DIALOG STAYS QUIET. Its prompt is still in the history;
     * with the selection unreadable (the dialog's objects gone), nothing may
     * be said -- "Yes or No" here would describe a dialog no longer there. */
    {
        int before_p = count_log(PROMPT_LINE);
        int before_o = count_log(" or No\"");
        *(unsigned *)(parent + 0x38c) = 7;          /* not a two-option index */
        run_modal();
        run_modal();
        int after_p = count_log(PROMPT_LINE);
        int after_o = count_log(" or No\"");
        if (after_p != before_p || after_o != before_o) {
            printf("FAIL  7. spoke after the dialog closed (prompt %d -> %d, "
                   "list %d -> %d)\n", before_p, after_p, before_o, after_o);
            bad++;
        } else printf("ok    7. a closed dialog's leftover prompt says nothing\n");
    }

    /* 8. A FULL RECORD. More than 48 distinct strings into a session, with
     * nothing clearing it, the recorder used to DROP every new string -- the
     * prompt never arrived at all. */
    {
        static ULONGLONG ox[GT_CLASS_SIZE/8];
        memset(ox, 0, sizeof ox); ox[0] = GT_VTABLE_ADDR;
        for (int k = 0; k < 70; k++) {
            char s[48];
            _snprintf(s, sizeof s, "Some menu caption %d.", k);
            gt_test_settext_body(ox, s, NULL, 0);
        }
        *(unsigned *)(parent + 0x38c) = 1;
        int before = count_log(PROMPT_LINE);
        gt_test_settext_body(o, P, NULL, 0);
        gt_test_settext_body(oy, "Yes", NULL, 0);
        gt_test_settext_body(on, "No",  NULL, 0);
        run_modal();
        int after = count_log(PROMPT_LINE);
        if (after != before + 1) {
            printf("FAIL  8. a dialog after 70 other strings was not announced "
                   "(prompt lines %d -> %d)\n", before, after);
            bad++;
        } else printf("ok    8. a dialog is still heard once the record is full\n");
    }

    printf("\n%s\n", bad ? "FAILED" : "ALL PASSED");
    return bad ? 1 : 0;
}
