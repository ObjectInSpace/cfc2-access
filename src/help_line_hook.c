/* help_line_hook.c -- the detour on the help-line setter (help_line.h). */
#include "help_line.h"
#include "log.h"
#include "memsafe.h"
#include "../libs/minhook/include/MinHook.h"

#include <string.h>

extern volatile LONG g_help_msg;
extern volatile LONG g_help_seq;
extern BOOL          g_help_installed;
extern void * volatile g_dlg_obj;
extern volatile LONG   g_dlg_seq;
extern BOOL            g_dlg_installed;

typedef void (*help_set_fn)(void *mgr, int msg);

static help_set_fn  g_real_set;

/* The detour. Records the index, then calls through. No pointer is read. */
static void hook_set(void *mgr, int msg)
{
    InterlockedExchange(&g_help_msg, (LONG)msg);
    InterlockedIncrement(&g_help_seq);
    g_real_set(mgr, msg);
}

BOOL help_line_install(void)
{
    static const unsigned char expect[10] = {
        0x40, 0x57,                         /* push rdi            */
        0x48, 0x83, 0xec, 0x20,             /* sub rsp, 0x20       */
        0x80, 0x79, 0x30, 0x00              /* cmp byte [rcx+0x30],0 */
    };
    void *target = (void *)(ULONG_PTR)HELP_LINE_SETTER_ADDR;
    unsigned char got[10];
    MEMORY_BASIC_INFORMATION mbi;
    MH_STATUS st;

    if (g_help_installed)
        return TRUE;
    if (VirtualQuery(target, &mbi, sizeof(mbi)) != sizeof(mbi) ||
        mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        log_line("help: 0x%llx is not committed executable memory -- not "
                 "hooking; descriptions fall back to the caption channel.",
                 HELP_LINE_SETTER_ADDR);
        return FALSE;
    }
    if (!mem_copy(got, target, sizeof(got)) || memcmp(got, expect, sizeof(got))) {
        log_line("help: 0x%llx does not carry the expected prologue -- the "
                 "offline model does not match this build. Not hooking; "
                 "descriptions fall back to the caption channel.",
                 HELP_LINE_SETTER_ADDR);
        return FALSE;
    }
    st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        log_line("help: MH_Initialize failed (%d).", (int)st);
        return FALSE;
    }
    st = MH_CreateHook(target, (void *)&hook_set, (void **)&g_real_set);
    if (st != MH_OK) {
        log_line("help: MH_CreateHook failed (%d).", (int)st);
        return FALSE;
    }
    st = MH_EnableHook(target);
    if (st != MH_OK) {
        log_line("help: MH_EnableHook failed (%d); removing.", (int)st);
        MH_RemoveHook(target);
        return FALSE;
    }
    g_help_installed = TRUE;
    log_line("help: hooked the help-line setter at 0x%llx -- each row's "
             "description is now the message the game sets for it.",
             HELP_LINE_SETTER_ADDR);
    return TRUE;
}


/* ---- the dialog opener (help_line.h) ------------------------------------ */
typedef ULONG_PTR (*dlg_open_fn)(void *dlg, int type, int msg, int sel,
                                 ULONG_PTR a5, ULONG_PTR a6, ULONG_PTR a7);
static dlg_open_fn g_real_open;

/* Records the dialog object only; every field is read later, guarded, on the
 * worker. Published AFTER the original returns, so the fields are set. */
static ULONG_PTR hook_open(void *dlg, int type, int msg, int sel,
                           ULONG_PTR a5, ULONG_PTR a6, ULONG_PTR a7)
{
    ULONG_PTR r = g_real_open(dlg, type, msg, sel, a5, a6, a7);
    InterlockedExchangePointer((void * volatile *)&g_dlg_obj, dlg);
    InterlockedIncrement(&g_dlg_seq);
    return r;
}

BOOL dialog_install(void)
{
    static const unsigned char expect[7] = {
        0x8b, 0x44, 0x24, 0x38,             /* mov eax, [rsp+0x38] */
        0x4c, 0x8b, 0xd1                    /* mov r10, rcx        */
    };
    void *target = (void *)(ULONG_PTR)DIALOG_OPEN_ADDR;
    unsigned char got[7];
    MEMORY_BASIC_INFORMATION mbi;
    MH_STATUS st;

    if (g_dlg_installed)
        return TRUE;
    if (VirtualQuery(target, &mbi, sizeof(mbi)) != sizeof(mbi) ||
        mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ||
        !mem_copy(got, target, sizeof(got)) || memcmp(got, expect, sizeof(got))) {
        log_line("dialog: 0x%llx is not the expected dialog opener -- not "
                 "hooking; prompts fall back to the text-matched list.",
                 DIALOG_OPEN_ADDR);
        return FALSE;
    }
    st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED)
        return FALSE;
    if (MH_CreateHook(target, (void *)&hook_open, (void **)&g_real_open) != MH_OK)
        return FALSE;
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        return FALSE;
    }
    g_dlg_installed = TRUE;
    log_line("dialog: hooked the dialog opener at 0x%llx -- every shell prompt "
             "is read from the dialog's own fields.", DIALOG_OPEN_ADDR);
    return TRUE;
}
