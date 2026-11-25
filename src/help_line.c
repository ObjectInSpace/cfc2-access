/* help_line.c -- the recorded state (see help_line.h). The hook itself is in
 * help_line_hook.c so every test runner can link this file without MinHook. */
#include "help_line.h"

volatile LONG g_help_msg = -1;
volatile LONG g_help_seq;
BOOL          g_help_installed;
static BOOL   g_test_mode;

BOOL help_line_installed(void) { return g_help_installed || g_test_mode; }

BOOL help_line_latest(int *msg, LONG *seq)
{
    LONG s1 = InterlockedCompareExchange(&g_help_seq, 0, 0);
    LONG m  = InterlockedCompareExchange(&g_help_msg, 0, 0);
    if (s1 == 0 || m < 0)
        return FALSE;
    if (msg) *msg = (int)m;
    if (seq) *seq = s1;
    return TRUE;
}

void help_line_test_set(int msg)
{
    g_test_mode = TRUE;
    InterlockedExchange(&g_help_msg, (LONG)msg);
    InterlockedIncrement(&g_help_seq);
}

void help_line_test_reset(void)
{
    g_test_mode = FALSE;
    InterlockedExchange(&g_help_msg, -1);
    InterlockedExchange(&g_help_seq, 0);
}

/* ---- dialogs (see help_line.h) ---- */
void * volatile g_dlg_obj;
volatile LONG   g_dlg_seq;
BOOL            g_dlg_installed;
static BOOL     g_dlg_test;

BOOL dialog_installed(void) { return g_dlg_installed || g_dlg_test; }

BOOL dialog_latest(void **dlg, LONG *seq)
{
    LONG s1 = InterlockedCompareExchange(&g_dlg_seq, 0, 0);
    void *o = InterlockedCompareExchangePointer((void * volatile *)&g_dlg_obj,
                                                NULL, NULL);
    if (s1 == 0 || !o)
        return FALSE;
    if (dlg) *dlg = o;
    if (seq) *seq = s1;
    return TRUE;
}

void dialog_test_open(void *dlg)
{
    g_dlg_test = TRUE;
    InterlockedExchangePointer((void * volatile *)&g_dlg_obj, dlg);
    InterlockedIncrement(&g_dlg_seq);
}

void dialog_test_reset(void)
{
    g_dlg_test = FALSE;
    InterlockedExchangePointer((void * volatile *)&g_dlg_obj, NULL);
    InterlockedExchange(&g_dlg_seq, 0);
}
