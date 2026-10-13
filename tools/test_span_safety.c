/* Optimized fault-guard coverage, isolated in child processes.
 *
 * The original test pinned an observed memcpy failure as a limitation and
 * attributed it to vector instructions. That attribution was not established.
 * In 2026-10-12 the probe's optimized scalar copy exposed elimination of the
 * armed store; volatile TLS plus compiler barriers fixes that control and this
 * memcpy control. Require recovery now, and require a readable copy to work.
 * Keep mem_readable_run prechecks in existing bulk readers: neither this test
 * nor the VEH provides allocation lifetime protection or catches stack guards.
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

    volatile BOOL faulted = FALSE;
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
    } else if (strcmp(mode, "memcpy") == 0 || strcmp(mode, "valid") == 0) {
        SIZE_T n = 255;
        const BYTE *source = p + (strcmp(mode, "valid") == 0 ? 100 : 4000);
        MEM_GUARDED(faulted, memcpy(buf, source, n));
        if (strcmp(mode, "valid") == 0) {
            if (faulted) return 1;
            for (SIZE_T i = 0; i < n; ++i) if (buf[i] != 'A') return 1;
            return 0;
        }
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
    BOOL completed = WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0;
    BOOL read = completed && GetExitCodeProcess(pi.hProcess, code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return read;
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
    } else check("child launched", 0, NULL);

    if (run_child(argv[0], "loop", &code)) {
        char d[64]; _snprintf(d, sizeof(d), "child exit=0x%lx", code);
        check("B. a guarded byte LOOP across the boundary is CAUGHT",
              code == 0, d);
    } else check("child launched", 0, NULL);

    if (run_child(argv[0], "memcpy", &code)) {
        char d[64]; _snprintf(d, sizeof(d), "child exit=0x%lx", code);
        check("C. a guarded memcpy across a bad page is CAUGHT", code == 0, d);
    } else check("C. child launched", 0, NULL);
    if (run_child(argv[0], "valid", &code)) {
        char d[64]; _snprintf(d, sizeof(d), "child exit=0x%lx", code);
        check("D. a readable memcpy succeeds with exact bytes", code == 0, d);
    } else check("D. child launched", 0, NULL);

    printf("\n%s (%d failure(s))\n", failures ? "FAILED" : "ALL PASSED",
           failures);
    return failures ? 1 : 0;
}
