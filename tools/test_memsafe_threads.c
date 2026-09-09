/*
 * Does the fault guard survive being used from SEVERAL THREADS AT ONCE?
 *
 * ⛔⛔ THE BUG THIS EXISTS FOR, which shipped and crashed the user's game.
 * g_mem_guard was a single global jmp_buf with a single global armed flag. That
 * was safe while only the worker thread used it -- and then the menu-cursor
 * hooks started running MEM_GUARDED on the GAME's threads, while gmd_find() and
 * gmd_text_by_index() ran it on ours.
 *
 * With one shared buffer, whichever thread called setjmp last owns it. A fault
 * on any other thread then longjmps onto a stack frame belonging to a DIFFERENT
 * THREAD, which is an immediate and completely unexplainable crash. The user hit
 * it changing the game's language -- an action that triggers a GMD rescan on our
 * thread while they are navigating menus on the game's.
 *
 * ⚠ Reasoning about this was not enough: the code looked "guarded" and the
 * per-thread requirement is invisible at every call site. So this test faults
 * deliberately, hard, from many threads at once. With __thread it passes; with
 * plain globals it crashes or hangs -- verified both ways.
 */

#include "../src/memsafe.h"
#include "../src/log.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

#define NTHREADS   8
#define NITER      3000

static volatile LONG g_ok;       /* guarded faults correctly absorbed */
static volatile LONG g_leaked;   /* reads that did NOT fault when they must */

/* Each thread hammers the guard: a deliberate fault, then a legitimate read,
 * over and over. Interleaving is the point -- the failure needs two threads
 * inside MEM_GUARDED simultaneously. */
static DWORD WINAPI worker(LPVOID param)
{
    (void)param;

    /* A page we own and can legitimately read, to mix good reads with bad. */
    volatile int good = 0x5eeded;

    for (int i = 0; i < NITER; i++) {
        /* 1. A read that MUST fault: address 0x10 is never mapped. */
        volatile BOOL faulted = FALSE;
        volatile int sink = 0;
        MEM_GUARDED(faulted, sink = *(volatile int *)(uintptr_t)0x10);
        (void)sink;
        if (faulted)
            InterlockedIncrement(&g_ok);
        else
            InterlockedIncrement(&g_leaked);

        /* 2. A read that must NOT fault, and must return the right value.
         *    If a longjmp from another thread corrupted our frame, this is
         *    where a wrong value or a crash shows up. */
        volatile BOOL f2 = FALSE;
        volatile int v = 0;
        MEM_GUARDED(f2, v = good);
        if (f2 || v != 0x5eeded)
            InterlockedIncrement(&g_leaked);
    }
    return 0;
}

int main(void)
{
    if (!mem_guard_install()) {
        printf("FATAL: guard self-test failed\n");
        return 1;
    }

    HANDLE th[NTHREADS];
    for (int i = 0; i < NTHREADS; i++) {
        th[i] = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (!th[i]) { printf("FATAL: CreateThread failed\n"); return 1; }
    }

    /* ⚠ A bounded wait. The shared-jmp_buf bug can also HANG (a thread
     * longjmps into a frame that never returns), and a hang must be a failure
     * rather than something to sit through. */
    DWORD w = WaitForMultipleObjects(NTHREADS, th, TRUE, 60000);
    if (w == WAIT_TIMEOUT) {
        printf("  FAIL  concurrent guard use HUNG (60 s) -- almost certainly a "
               "longjmp onto another thread's stack\n");
        return 1;
    }
    for (int i = 0; i < NTHREADS; i++)
        CloseHandle(th[i]);

    LONG ok = InterlockedCompareExchange(&g_ok, 0, 0);
    LONG leaked = InterlockedCompareExchange(&g_leaked, 0, 0);
    LONG expect = (LONG)NTHREADS * NITER;

    printf("  guarded faults absorbed: %ld of %ld expected; anomalies: %ld\n",
           ok, expect, leaked);

    if (ok != expect || leaked != 0) {
        printf("  FAIL  the guard did not behave per-thread\n");
        return 1;
    }
    printf("  PASS  %d threads x %d iterations, every fault absorbed on its "
           "own thread\n", NTHREADS, NITER);
    printf("\nall cases passed\n");
    return 0;
}
