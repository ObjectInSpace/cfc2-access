/*
 * Safe reading of foreign memory in a live process.
 *
 * Two guards, both paid for by real failures in this project:
 *
 *  - readable_run(): Enigma decommits its own unpacking scaffolding, so PE
 *    section headers describe memory that is GONE. Ask the memory manager,
 *    never trust a header or a pointer.
 *
 *  - the fault guard: a detour here once read past the end of a caller's
 *    buffer and killed the game within a second. VirtualQuery alone cannot
 *    prevent that -- it is inherently TOCTOU, since another thread can free a
 *    region between the query and the read. Catching the fault is the only
 *    real protection.
 *
 * MinGW has no __try/__except, so the guard is a vectored exception handler
 * plus longjmp.
 */
#ifndef CFC2_MEMSAFE_H
#define CFC2_MEMSAFE_H

#include <windows.h>
#include <setjmp.h>

/* How many of the `want` bytes starting at p are committed and readable?
 * Returns 0 if p itself is not readable. Never faults. */
SIZE_T mem_readable_run(const void *p, SIZE_T want);

/* Convenience: is the whole range readable right now? */
BOOL mem_readable(const void *p, SIZE_T len);

/* --- Fault guard ---------------------------------------------------------
 * Arm once per thread of use, then wrap risky reads in MEM_GUARDED.
 * mem_guard_install() is idempotent and self-tests by deliberately faulting:
 * an untested fault handler looks exactly like one that was never needed,
 * right up until it takes the process down. Returns FALSE if the self-test
 * did not fault, in which case the caller must NOT proceed. */
BOOL mem_guard_install(void);
void mem_guard_remove(void);

/* Internal, used by the macro. */
extern jmp_buf       g_mem_guard;
extern volatile LONG g_mem_guard_armed;

/* Run `stmt` under the guard. `faulted` is set TRUE if it faulted.
 * A macro because setjmp's buffer must belong to a frame that is still live
 * when longjmp fires. */
#define MEM_GUARDED(faulted, stmt)                                            \
    do {                                                                      \
        (faulted) = FALSE;                                                    \
        if (setjmp(g_mem_guard) == 0) {                                       \
            InterlockedExchange(&g_mem_guard_armed, 1);                       \
            { stmt; }                                                         \
            InterlockedExchange(&g_mem_guard_armed, 0);                       \
        } else {                                                              \
            (faulted) = TRUE;                                                 \
        }                                                                     \
    } while (0)

/* Guarded copy. Returns FALSE on fault or if the range is not readable. */
BOOL mem_copy(void *dst, const void *src, SIZE_T n);

/* How many faults the guard has absorbed since install. A scan that reports
 * "found nothing" while this is nonzero was a PARTIAL scan, and must say so. */
LONG mem_guard_fault_count(void);

#endif /* CFC2_MEMSAFE_H */
