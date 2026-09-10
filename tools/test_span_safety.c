/* ⭐⭐ WHAT THE FAULT GUARD DOES AND DOES NOT COVER.
 *
 * This exists because I wrote a "fast path" that skipped mem_readable_run()
 * before a guarded memcpy, reasoning that the range had already been validated
 * when the string index was built. It CRASHED the process (0xC0000005) the
 * first time it met an unreadable page.
 *
 * The three cases below isolate why, and the answer is a rule for the whole
 * project, not a detail of one function:
 *
 *     A. guarded single-byte read of a PAGE_NOACCESS page  -> CAUGHT
 *     B. guarded byte LOOP across the boundary             -> CAUGHT
 *     C. guarded memcpy() across the boundary              -> ⛔ NOT CAUGHT
 *
 * MinGW's optimised memcpy uses vector instructions, and a fault inside one is
 * not recoverable through this VEH + longjmp guard the way a scalar access is.
 *
 * ⛔⛔ THEREFORE: THE GUARD IS A NET FOR A RACE, NOT THE PRIMARY CHECK.
 * Every guarded memcpy in this codebase (mem_copy, read_cstr, pad_watch) calls
 * mem_readable_run FIRST; the guard covers only the window between that query
 * and the copy. Any new guarded bulk read MUST do the same. If a future change
 * "optimises away" the pre-check, case C fails here instead of killing the
 * user's game.
 *
 * ⚠ Case C is asserted as a KNOWN LIMITATION, not as desired behaviour. It runs
 * in a child process so this suite reports it instead of dying of it.
 *
 * Build/run: tools/run_span_test.ps1
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "memsafe.h"

static int failures;

static void check(const char *what, int ok, const char *detail)
{
    printf("  %s  %s%s%s\n", ok ? "PASS" : "FAIL", what,
           detail && *detail ? " -- " : "", detail ? detail : "");
    if (!ok) failures++;
}

/* The child mode: perform one guarded read form and exit 0 if it was caught.
 * A crash shows up to the parent as a non-zero (0xC0000005) exit code. */
static int child(const char *mode)
{
    if (!mem_guard_install()) return 2;
    BYTE *p = (BYTE *)VirtualAlloc(NULL, 8192, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_READWRITE);
    if (!p) return 2;
    memset(p, 'A', 8192);
    DWORD old;
    if (!VirtualProtect(p + 4096, 4096, PAGE_NOACCESS, &old)) return 2;

    BOOL faulted = FALSE;
    char buf[256];

    if (strcmp(mode, "byte") == 0) {
        volatile char sink = 0;
        MEM_GUARDED(faulted, sink = *(volatile char *)(p + 4096));
        (void)sink;
    } else if (strcmp(mode, "loop") == 0) {
        MEM_GUARDED(faulted, {
            for (int i = 0; i < 255; i++)
                buf[i] = ((volatile char *)(p + 4000))[i];
        });
    } else if (strcmp(mode, "memcpy") == 0) {
        SIZE_T n = 255;
        MEM_GUARDED(faulted, memcpy(buf, p + 4000, n));
    } else {
        return 2;
    }
    return faulted ? 0 : 1;      /* 0 = caught, 1 = no fault seen */
}

static int run_child(const char *exe, const char *mode, DWORD *code)
{
    char cmd[1024];
    _snprintf(cmd, sizeof(cmd), "\"%s\" --child %s", exe, mode);
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    /* Suppress the crash dialog in the child. */
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return 0;
    WaitForSingleObject(pi.hProcess, 20000);
    GetExitCodeProcess(pi.hProcess, code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "--child") == 0) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        return child(argv[2]);
    }

    printf("fault guard coverage\n");
    printf("--------------------\n");

    DWORD code;

    if (run_child(argv[0], "byte", &code)) {
        char d[64]; _snprintf(d, sizeof(d), "child exit=0x%lx", code);
        check("A. a guarded single-byte read of a bad page is CAUGHT",
              code == 0, d);
    }

    if (run_child(argv[0], "loop", &code)) {
        char d[64]; _snprintf(d, sizeof(d), "child exit=0x%lx", code);
        check("B. a guarded byte LOOP across the boundary is CAUGHT",
              code == 0, d);
    }

    /* ⛔⛔ THE UNRELIABLE CASE -- and "unreliable" is the finding.
     *
     * A guarded memcpy over a bad page does NOT behave consistently. Observed,
     * same machine, same toolchain, same day:
     *   - inline in a -O2 test binary : CRASHED (0xC0000005) inside memcpy;
     *   - in this child process       : returned with NO fault reported.
     *
     * Either outcome is unacceptable for a bulk read of foreign memory: one
     * kills the game, the other silently returns whatever bytes it managed.
     * What is NOT observed is the good outcome -- caught and reported, the way
     * cases A and B are.
     *
     * ⚠ So this asserts the SAFETY PROPERTY, not a symptom: a guarded memcpy
     * must not be relied on to survive a bad page. Anything other than "caught"
     * proves the pre-check is mandatory, and that is what this pins. If a
     * future toolchain makes it genuinely recoverable (exit 0), this FAILS and
     * the rule above should be re-derived rather than quietly relaxed. */
    if (run_child(argv[0], "memcpy", &code)) {
        char d[160];
        _snprintf(d, sizeof(d),
                  "child exit=0x%lx -- %s. Either way the guard did NOT catch "
                  "and report it, so mem_readable_run() FIRST is mandatory",
                  code,
                  code == 0xC0000005 ? "crashed inside memcpy"
                                     : "returned without reporting a fault");
        check("C. a guarded memcpy across a bad page is NOT reliably caught",
              code != 0, d);
    }

    printf("\n%s (%d failure(s))\n", failures ? "FAILED" : "ALL PASSED",
           failures);
    return failures ? 1 : 0;
}
