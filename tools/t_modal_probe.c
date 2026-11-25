/* Does the SHIPPING dialog path speak every shell dialog from its own fields?
 *
 * Drives the real chain: dialog_test_open() stands in for the hook on the
 * game's dialog opener (0x14007fc20), and menu_speak_check_modal() reads the
 * dialog object exactly as in the game -- prompt message +0x370, buttons
 * +0x33c / +0x340, their count +0x364, highlighted +0x38c -- with only the
 * message table stubbed, because there is none here. What it tried to say is
 * read back from the log.
 *
 * USER (2026-11-24): "We should cover all prompts." The hand-picked list of 20
 * text-matched prompts is gone; these cases are its replacement's contract. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "../src/gui_text.h"
#include "../src/menu_speak.h"
#include "../src/help_line.h"
#include "../src/log.h"

static BOOL res(int idx, char *out, unsigned n)
{
    const char *s =
        idx == 384 ? "If you exit the application, all unsaved\r\nprogress will be lost. Continue?" :
        idx == 460 ? "Do you want to keep the current settings? (Reverting to previous settings in %d seconds.)" :
        idx == 272 ? "Quick save loaded." :
        idx == 25  ? "Yes" :
        idx == 26  ? "No"  :
        idx == 34  ? "OK"  : NULL;
    if (!s || !out || !n) return FALSE;
    _snprintf(out, n, "%s", s); out[n - 1] = 0; return TRUE;
}

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

/* One poll, then past the 150 ms "let the text arrive" wait, then another. */
static void poll_open(void)
{
    menu_speak_check_modal();
    Sleep(200);
    menu_speak_check_modal();
}

static unsigned char dlg[0x400];
static ULONGLONG fill[GT_CLASS_SIZE / 8];

static void set_dialog(int msg, int nopt, int o0, int o1, int sel)
{
    *(ULONGLONG *)dlg = 0x1410000000ULL;          /* its "vtable" */
    *(int *)(dlg + 0x370) = msg;
    *(int *)(dlg + 0x364) = nopt;
    *(int *)(dlg + 0x33c) = o0;
    *(int *)(dlg + 0x340) = o1;
    *(int *)(dlg + 0x38c) = sel;
}

int main(void)
{
    int bad = 0;
    log_init();
    gt_test_reset();
    gt_test_focus_enable();
    menu_speak_test_set_dialog_resolver(res);
    dialog_test_reset();
    memset(dlg, 0, sizeof dlg);

    /* 0. no dialog has opened: nothing */
    dialog_test_open(NULL);                       /* a NULL object is not one */
    dialog_test_reset();
    menu_speak_check_modal();
    menu_speak_check_modal();
    if (count_log("will be lost") != 0) { printf("FAIL  0. spoke with no dialog\n"); bad++; }
    else printf("ok    0. silent with no dialog open\n");

    /* 1. an opening: the prompt, then the highlighted button */
    set_dialog(384, 2, 25, 26, 1);
    dialog_test_open(dlg);
    poll_open();
    if (count_log("will be lost. Continue? No\"") != 1) {
        printf("FAIL  1. the opening did not say prompt + highlighted button\n"); bad++;
    } else printf("ok    1. opening: \"...Continue? No\"\n");

    /* 2. a change of button: the button ALONE */
    *(int *)(dlg + 0x38c) = 0;
    menu_speak_check_modal();
    if (count_log(": \"Yes\"") != 1) {
        printf("FAIL  2. a change of button did not say \"Yes\" alone\n"); bad++;
    } else printf("ok    2. a change of button says \"Yes\" alone\n");

    /* 3. no change: silence */
    {
        int before = count_log(": \"Yes\"");
        menu_speak_check_modal();
        menu_speak_check_modal();
        if (count_log(": \"Yes\"") != before) { printf("FAIL  3. repeated an unchanged button\n"); bad++; }
        else printf("ok    3. an unchanged button is not repeated\n");
    }

    /* 4. the same dialog opened again is announced again */
    {
        int before = count_log("will be lost");
        dialog_test_open(dlg);
        poll_open();
        if (count_log("will be lost") != before + 1) { printf("FAIL  4. a re-opened dialog was silent\n"); bad++; }
        else printf("ok    4. a re-opened dialog is announced again\n");
    }

    /* 5. a closed dialog (object reused: its vtable changed) says nothing */
    {
        int before = count_log(": \"No\"");
        *(ULONGLONG *)dlg = 0x1420000000ULL;
        *(int *)(dlg + 0x38c) = 1;
        menu_speak_check_modal();
        if (count_log(": \"No\"") != before) { printf("FAIL  5. spoke for a closed dialog\n"); bad++; }
        else printf("ok    5. a closed dialog says nothing\n");
    }

    /* 6. ⭐ a prompt with a COUNTDOWN speaks the dialog's own filled-in text */
    set_dialog(460, 2, 25, 26, 0);
    memset(fill, 0, sizeof fill);
    fill[0] = GT_VTABLE_ADDR;
    { void *pp = dlg; memcpy((char *)fill + 0x88, &pp, sizeof pp); }
    gt_test_settext_body(fill, "Do you want to keep the current settings? (Reverting "
                               "to previous settings in 12 seconds.)", NULL, 0);
    dialog_test_open(dlg);
    poll_open();
    if (count_log("previous settings in 12 seconds.) Yes\"") != 1) {
        printf("FAIL  6. the countdown prompt was not spoken with its number\n"); bad++;
    } else printf("ok    6. countdown prompt spoken as shown: \"...in 12 seconds.) Yes\"\n");

    /* 7. a one-button notice: the message only */
    set_dialog(272, 1, 34, 0, 0);
    dialog_test_open(dlg);
    poll_open();
    if (count_log(": \"Quick save loaded.\"") != 1) {
        printf("FAIL  7. a one-button notice was not spoken alone\n"); bad++;
    } else printf("ok    7. a one-button notice is spoken as its message\n");

    menu_speak_test_set_dialog_resolver(NULL);
    printf("\n%s\n", bad ? "FAILED" : "ALL PASSED");
    return bad ? 1 : 0;
}
