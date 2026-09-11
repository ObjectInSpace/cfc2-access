/* Does gt_modal_probe() actually find the parent and report a change?
 *
 * ⛔ Built because three tests today passed while the feature did nothing. This
 * one plants two "Yes"/"No" objects sharing a +0x088 parent, changes ONE dword
 * in that parent, and requires the probe to report THAT OFFSET. Nothing here
 * re-derives the answer: the assertion reads the probe's own log line. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "../src/gui_text.h"
#include "../src/log.h"

static unsigned char g_parent[0x400];

int main(void)
{
    int bad = 0;
    log_init();

    /* The switch must exist or the probe is inert by design. */
    HANDLE h = CreateFileA("cfc2_modal.on", GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

    gt_test_reset();
    gt_test_focus_enable();

    static ULONGLONG oy[GT_CLASS_SIZE/8], on[GT_CLASS_SIZE/8];
    memset(oy, 0, sizeof oy); oy[0] = GT_VTABLE_ADDR;
    memset(on, 0, sizeof on); on[0] = GT_VTABLE_ADDR;
    memset(g_parent, 0, sizeof g_parent);
    /* Both options point at the same parent -- the real dialog's shape. */
    void *p = g_parent;
    memcpy((char *)oy + 0x88, &p, sizeof p);
    memcpy((char *)on + 0x88, &p, sizeof p);
    gt_test_settext_body(oy, "Yes", NULL, 0);
    gt_test_settext_body(on, "No",  NULL, 0);

    gt_modal_probe();                 /* baseline */
    *(unsigned *)(g_parent + 0x1c) = 1;   /* "the selection moved" */
    gt_modal_probe();                 /* should report +0x01c */

    FILE *f = fopen("CFC2Access.log", "rb");
    char buf[1024]; int watched = 0, saw = 0;
    if (!f) { printf("FAIL  no log\n"); return 1; }
    while (fgets(buf, sizeof buf, f)) {
        if (strstr(buf, "modal: watching parent")) watched = 1;
        if (strstr(buf, "+0x01c:00000000->00000001")) saw = 1;
    }
    fclose(f);
    DeleteFileA("cfc2_modal.on");

    if (!watched) { printf("FAIL  probe never found the parent\n"); bad++; }
    else printf("ok    probe found the shared parent and took a baseline\n");
    if (!saw) { printf("FAIL  the changed dword was NOT reported\n"); bad++; }
    else printf("ok    reported the change at +0x01c\n");

    printf("\n%s\n", bad ? "FAILED" : "ALL PASSED");
    return bad ? 1 : 0;
}
