/* Does the SHIPPING modal path produce the line, given the real prompt text?
 *
 * Drives the real chain end to end: the setText hook body sees the strings the
 * dialog sets, fills the modal slot, and menu_speak_check_modal() announces --
 * with only the GMD resolver stubbed, because there is no GMD table here.
 *
 * ⛔⛔ USER (2026-10-04): "sometimes the confirm quit yes/no prompts are not
 * reading", then: "we shouldn't be relying on a buffer that can fill up to
 * determine what's currently being spoken." The announcer used to SEARCH the
 * 48-entry focus record; it filled and went blind, and it kept closed dialogs'
 * prompts. It now reads one slot the hook fills when a prompt is set. Cases
 * 6-11 are the ways the old design failed plus the new design's own edges. */
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

static ULONGLONG o[GT_CLASS_SIZE/8], oy[GT_CLASS_SIZE/8], on[GT_CLASS_SIZE/8];
static unsigned char parent[0x400];

/* The dialog opening: prompt, then the TOP option, then the bottom one --
 * the order measured in every captured run. */
static void open_dialog(void)
{
    gt_test_settext_body(o,  P,     NULL, 0);
    gt_test_settext_body(oy, "Yes", NULL, 0);
    gt_test_settext_body(on, "No",  NULL, 0);
}

int main(void)
{
    int bad = 0;
    const char *PROMPT_LINE = "will be lost. Continue?";
    log_init();          /* menu_speak_line() reports through the log */
    gt_test_reset();
    gt_test_focus_enable();

    memset(o, 0, sizeof o);   o[0]  = GT_VTABLE_ADDR;
    memset(oy, 0, sizeof oy); oy[0] = GT_VTABLE_ADDR;
    memset(on, 0, sizeof on); on[0] = GT_VTABLE_ADDR;
    /* ⭐ The real dialog's shape: both options share a parent at +0x088, and
     * the selected index lives at parent+0x38c (0 = the TOP option = "Yes"). */
    memset(parent, 0, sizeof parent);
    { void *pp = parent;
      memcpy((char *)oy + 0x88, &pp, sizeof pp);
      memcpy((char *)on + 0x88, &pp, sizeof pp); }

    /* The worker publishes the prompt texts before any dialog opens -- in the
     * game, within a second of the language resolving. Nothing open yet, so
     * nothing may be said. */
    run_modal();
    if (count_log(PROMPT_LINE) != 0) {
        printf("FAIL  0. spoke with no dialog open\n"); bad++;
    } else printf("ok    0. prompts published; silent with no dialog\n");

    /* ⛔ The cursor has moved since (or never moved at all) -- the dialog's
     * text is never stamped with the current movement, so a movement-keyed
     * reader would miss it. Reproduce that. */
    gt_note_focus_move();

    /* 1. ⛔⛔ CALL THE SHIPPING FUNCTION AND READ WHAT IT TRIED TO SAY. An
     * earlier version of this probe reimplemented the lookup and passed with
     * the real one disabled ([[harness-supplies-the-mechanism]]). */
    *(unsigned *)(parent + 0x38c) = 1;      /* "No" is selected */
    open_dialog();
    gt_note_focus_move();
    run_modal();
    {
        const char *WANT = "If you exit the application, all unsaved progress "
                           "will be lost. Continue? No";
        if (count_log(WANT) != 1) {
            printf("FAIL  1. menu_speak_check_modal() did not produce:\n"
                   "        \"%s\"\n", WANT);
            bad++;
        } else printf("ok    1. the SHIPPING path said: \"%s\"\n", WANT);
    }

    /* 2. ⛔⛔ AND THE OTHER INDEX, alone. One direction passes with the
     * mapping INVERTED. And only the option is spoken -- USER: "it repeats the
     * whole dialog, it should just say yes or no" -- so the line must BE
     * "Yes", not contain it. (Matched on the quoted line: with no backend a
     * test logs "NOT SPOKEN", not "spoke".) */
    *(unsigned *)(parent + 0x38c) = 0;
    run_modal();
    if (count_log(": \"Yes\"") != 1) {
        printf("FAIL  2. index 0 did not speak the TOP option (\"Yes\") alone\n");
        bad++;
    } else printf("ok    2. index 0 speaks \"Yes\", index 1 speaks \"No\"\n");

    /* 6. THE SAME DIALOG OPENED AGAIN, same option highlighted. The 22:28 run
     * did exactly this ("Exit the game?" twice at move 12), and the old
     * history search matched the first opening and said nothing. */
    {
        int before = count_log(PROMPT_LINE);
        open_dialog();
        run_modal();                                /* index still 0 */
        int after = count_log(PROMPT_LINE);
        if (after != before + 1) {
            printf("FAIL  6. a RE-OPENED dialog was not announced "
                   "(prompt lines %d -> %d)\n", before, after);
            bad++;
        } else printf("ok    6. the same dialog opened again is announced again\n");
    }

    /* 7. AND A CLOSED DIALOG STAYS QUIET. The slot keeps its last opening;
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
        } else printf("ok    7. a closed dialog's last opening says nothing\n");
    }

    /* 8. A LONG SESSION. Far more strings than any buffer would hold, then the
     * dialog: it must still be heard. The old focus record dropped everything
     * after 48. */
    {
        static ULONGLONG ox[GT_CLASS_SIZE/8];
        memset(ox, 0, sizeof ox); ox[0] = GT_VTABLE_ADDR;
        for (int k = 0; k < 500; k++) {
            char s[48];
            _snprintf(s, sizeof s, "Some menu caption %d.", k);
            gt_test_settext_body(ox, s, NULL, 0);
        }
        *(unsigned *)(parent + 0x38c) = 1;
        int before = count_log(PROMPT_LINE);
        open_dialog();
        run_modal();
        int after = count_log(PROMPT_LINE);
        if (after != before + 1) {
            printf("FAIL  8. a dialog after 500 other strings was not announced "
                   "(prompt lines %d -> %d)\n", before, after);
            bad++;
        } else printf("ok    8. a dialog is still heard after 500 other strings\n");
    }

    /* 9. ONLY A PUBLISHED PROMPT OPENS A DIALOG. The pause menu's own caption
     * "Exit the game." sits right next to the real prompt "Exit the game?" in
     * the captures; prose that is not a prompt must never be announced as one,
     * however it is followed. */
    {
        static ULONGLONG oc[GT_CLASS_SIZE/8];
        memset(oc, 0, sizeof oc); oc[0] = GT_VTABLE_ADDR;
        int before = count_log("Exit the game.");
        gt_test_settext_body(oc, "Exit the game.", NULL, 0);
        gt_test_settext_body(oy, "Yes", NULL, 0);
        gt_test_settext_body(on, "No",  NULL, 0);
        run_modal();
        run_modal();
        if (count_log("Exit the game.") != before) {
            printf("FAIL  9. a non-prompt caption was announced as a dialog\n");
            bad++;
        } else printf("ok    9. a caption that is not a prompt opens nothing\n");
    }

    /* 10. A POLL BETWEEN THE PROMPT AND ITS OPTIONS waits, then speaks the
     * whole thing once -- not the prompt alone, then nothing. */
    {
        *(unsigned *)(parent + 0x38c) = 1;
        int before = count_log(PROMPT_LINE);
        int before_full = count_log("Continue? No\"");
        gt_test_settext_body(o, P, NULL, 0);        /* options not yet set */
        run_modal();
        int mid = count_log(PROMPT_LINE);
        gt_test_settext_body(oy, "Yes", NULL, 0);
        gt_test_settext_body(on, "No",  NULL, 0);
        run_modal();
        int after = count_log(PROMPT_LINE);
        int after_full = count_log("Continue? No\"");
        if (mid != before || after != before + 1 || after_full != before_full + 1) {
            printf("FAIL  10. options arriving after a poll: prompt lines "
                   "%d -> %d -> %d, full lines %d -> %d\n",
                   before, mid, after, before_full, after_full);
            bad++;
        } else printf("ok    10. a poll mid-opening waits for the options\n");
    }

    /* 11. AND A DIALOG THAT NEVER SENDS OPTIONS IS STILL SPOKEN -- the wait is
     * bounded, so a prompt alone is never silenced by it. */
    {
        int before = count_log(PROMPT_LINE);
        gt_test_settext_body(on, "No", NULL, 0);    /* so the prompt is a change */
        gt_test_settext_body(o, P, NULL, 0);
        run_modal();                                /* starts the wait */
        Sleep(350);
        run_modal();                                /* past it: speak */
        int after = count_log(PROMPT_LINE);
        if (after != before + 1) {
            printf("FAIL  11. a prompt with no options was not spoken "
                   "(prompt lines %d -> %d)\n", before, after);
            bad++;
        } else printf("ok    11. a prompt with no options is spoken after the wait\n");
    }

    /* 12. A RE-SET IS NOT AN OPENING. The game re-sets text it already shows;
     * only a CHANGE is an arrival. If every set counted, an open dialog would
     * re-announce its prompt on each redraw and talk over the user.
     * (Measured: the 22:28 run's "Yes" object was set exactly twice across two
     * openings -- but the guard must not depend on the game being that tidy.) */
    {
        int before = count_log(PROMPT_LINE);
        gt_test_settext_body(o, P, NULL, 0);        /* same text as the last set */
        gt_test_settext_body(o, P, NULL, 0);
        run_modal();
        Sleep(350);
        run_modal();
        int after = count_log(PROMPT_LINE);
        if (after != before) {
            printf("FAIL  12. re-setting the same prompt re-announced it "
                   "(prompt lines %d -> %d)\n", before, after);
            bad++;
        } else printf("ok    12. re-setting the same text is not a new opening\n");
    }

    printf("\n%s\n", bad ? "FAILED" : "ALL PASSED");
    return bad ? 1 : 0;
}
