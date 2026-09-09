#include "memsafe.h"
#include "log.h"

#include <string.h>

/* ⛔⛔ THREAD-LOCAL, AND THAT IS NOT A DETAIL.
 *
 * These were ordinary globals, which was safe only while a single worker
 * thread used the guard. The menu-cursor hooks broke that: a detour runs on the
 * GAME's thread while gmd_find()/gmd_text_by_index() run on ours. With one
 * shared jmp_buf, whichever thread called setjmp last owns it -- so a fault on
 * the other thread longjmps onto a stack frame belonging to a DIFFERENT THREAD.
 * That is an immediate, unexplainable crash, and it is far likelier during a
 * language change, which triggers a GMD rescan while the user is navigating.
 *
 * The user hit exactly that: the game died on a language change and named
 * dinput8. Making the state __thread gives every thread its own buffer and its
 * own armed flag, which is what the design assumed all along. */
__thread jmp_buf g_mem_guard;
__thread int     g_mem_guard_armed;

static void         *g_veh;
static volatile LONG g_faults;
static volatile LONG g_installed;

SIZE_T mem_readable_run(const void *p, SIZE_T want)
{
    if (!p || want == 0)
        return 0;

    const BYTE *b = (const BYTE *)p;
    SIZE_T ok = 0;
    while (ok < want) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(b + ok, &mbi, sizeof(mbi)))
            break;
        if (mbi.State != MEM_COMMIT)
            break;
        /* ⚠ PAGE_GUARD is 0x100, outside the low byte. Test it on the
         * UNMASKED value; masking to 0xFF (to drop modifier bits such as
         * PAGE_WRITECOMBINE before comparing access type) silently clears it
         * and turns the guard-page check into dead code. */
        if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
            break;
        DWORD access = mbi.Protect & 0xFF;
        if (!(access & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                        PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                        PAGE_EXECUTE_WRITECOPY)))
            break;

        SIZE_T avail =
            (SIZE_T)((const BYTE *)mbi.BaseAddress + mbi.RegionSize - (b + ok));
        if (avail == 0)
            break;
        ok += avail;
    }
    return ok < want ? ok : want;
}

BOOL mem_readable(const void *p, SIZE_T len)
{
    return mem_readable_run(p, len) == len;
}

static LONG CALLBACK guard_veh(EXCEPTION_POINTERS *ep)
{
    /* ⚠ Per-thread: the VEH runs on whichever thread faulted, so this reads
     * THAT thread's flag. A thread with no armed guard falls through to the
     * normal handler chain, exactly as before. */
    if (!g_mem_guard_armed)
        return EXCEPTION_CONTINUE_SEARCH;

    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_PRIV_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_DATATYPE_MISALIGNMENT)
        return EXCEPTION_CONTINUE_SEARCH;

    /* Deliberately NOT catching EXCEPTION_STACK_OVERFLOW or
     * STATUS_GUARD_PAGE_VIOLATION (0x80000001).
     *
     * Both concern the stack's guard page, which exists so the kernel can grow
     * the stack on demand. Catching either and longjmp-ing out leaves the
     * guard unreset and the thread in a broken state -- papering over the
     * symptom while corrupting the process.
     *
     * The correct fix is for the CALLER not to read stack memory at all;
     * gmd_find() excludes its own stack for exactly this reason. If one of
     * these reaches here, the caller has a bug that must be fixed at the
     * source, so let it propagate loudly. */

    InterlockedIncrement(&g_faults);   /* still global: a count, not state */
    g_mem_guard_armed = 0;
    longjmp(g_mem_guard, (int)code);
    return EXCEPTION_CONTINUE_SEARCH;   /* not reached */
}

BOOL mem_guard_install(void)
{
    if (InterlockedCompareExchange(&g_installed, 0, 0))
        return TRUE;                    /* idempotent */

    g_veh = AddVectoredExceptionHandler(1, guard_veh);
    if (!g_veh) {
        log_line("memsafe: AddVectoredExceptionHandler FAILED -- "
                 "unguarded reads of foreign memory are not safe");
        return FALSE;
    }

    /* Self-test: deliberately fault and require the guard to catch it. An
     * untested fault handler is indistinguishable from one that was never
     * needed until the moment it takes the process down. */
    volatile BOOL faulted = FALSE;
    static volatile unsigned *bad;
    bad = (volatile unsigned *)(uintptr_t)0x10;
    volatile unsigned sink = 0;
    MEM_GUARDED(faulted, sink = *bad);
    (void)sink;

    if (!faulted) {
        log_line("memsafe: guard self-test did NOT fault -- handler is not "
                 "working; refusing to arm it");
        RemoveVectoredExceptionHandler(g_veh);
        g_veh = NULL;
        return FALSE;
    }

    /* The self-test's own fault should not count against real scans. */
    InterlockedExchange(&g_faults, 0);
    InterlockedExchange(&g_installed, 1);
    log_line("memsafe: fault guard armed (self-test passed)");
    return TRUE;
}

void mem_guard_remove(void)
{
    if (InterlockedCompareExchange(&g_installed, 0, 0) && g_veh) {
        RemoveVectoredExceptionHandler(g_veh);
        g_veh = NULL;
        InterlockedExchange(&g_installed, 0);
    }
}

BOOL mem_copy(void *dst, const void *src, SIZE_T n)
{
    if (!mem_readable(src, n))
        return FALSE;
    volatile BOOL faulted;
    MEM_GUARDED(faulted, memcpy(dst, src, n));
    return !faulted;
}

LONG mem_guard_fault_count(void)
{
    return InterlockedCompareExchange(&g_faults, 0, 0);
}
