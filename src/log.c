#include "log.h"

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static CRITICAL_SECTION g_lock;
static BOOL g_lock_ready;
static wchar_t g_path[MAX_PATH];

/* ⭐⭐⭐ THE FILE STAYS OPEN. It used to be _wfopen/fclose PER LINE, under this
 * lock, and that was USER-REPORTED LAG: "a lot of lag between me pressing an
 * arrow key and then hearing the change echoed back."
 *
 * ⛔⛔ WHY IT WAS THE KEYPRESS PATH AND NOT JUST LOG VOLUME. Opening and
 * closing a file on Windows costs on the order of a millisecond, and the
 * cursor hook logs on the GAME'S OWN THREAD -- with gt_snapshot() logging once
 * PER ROW before menu_speak() ever runs. So on the first movement of every
 * screen the user waited for a dozen open/close cycles before hearing
 * anything. Measured in the 02:38 run: the mod's own decision path is 0 ms
 * median, so the cost was never the naming logic; it was the I/O around it.
 *
 * ⚠ STILL FLUSHED EVERY LINE. The log's whole value here is that it survives a
 * crash and a hang -- this project has diagnosed both from it -- and fflush is
 * a memcpy into the OS cache, not a disk seek. What is removed is the open and
 * the close, not the durability. */
static FILE *g_fp;

static void resolve_path(void)
{
    DWORD n = GetModuleFileNameW(NULL, g_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        g_path[0] = L'\0';
        return;
    }
    wchar_t *slash = wcsrchr(g_path, L'\\');
    if (!slash) {
        g_path[0] = L'\0';
        return;
    }
    slash[1] = L'\0';
    if (wcslen(g_path) + 16 >= MAX_PATH) {
        g_path[0] = L'\0';
        return;
    }
    lstrcatW(g_path, L"CFC2Access.log");
}

void log_init(void)
{
    if (!g_lock_ready) {
        InitializeCriticalSection(&g_lock);
        g_lock_ready = TRUE;
    }
    resolve_path();

    /* Truncate on start so each run's log stands alone — a stale line from a
     * previous run reading as current evidence has burned us before.
     *
     * ⭐ "w" truncates AND leaves the handle open for the whole run, so the
     * per-line open/close is gone without changing what the file contains. */
    if (g_path[0]) {
        EnterCriticalSection(&g_lock);
        if (g_fp) {
            fclose(g_fp);
            g_fp = NULL;
        }
        g_fp = _wfopen(g_path, L"w");
        LeaveCriticalSection(&g_lock);
    }
}

void log_shutdown(void)
{
    if (g_lock_ready) {
        EnterCriticalSection(&g_lock);
        if (g_fp) {
            fclose(g_fp);
            g_fp = NULL;
        }
        LeaveCriticalSection(&g_lock);
        DeleteCriticalSection(&g_lock);
        g_lock_ready = FALSE;
    }
}

void log_line(const char *fmt, ...)
{
    if (!g_path[0] || !g_lock_ready)
        return;

    EnterCriticalSection(&g_lock);
    /* ⚠ Reopen if the handle was lost (log_init not run, or a failed open).
     * "a" so nothing already written is discarded -- a truncate here would
     * silently destroy the evidence of whatever went wrong. */
    if (!g_fp)
        g_fp = _wfopen(g_path, L"a");
    if (g_fp) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(g_fp, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute,
                st.wSecond, st.wMilliseconds);

        va_list ap;
        va_start(ap, fmt);
        vfprintf(g_fp, fmt, ap);
        va_end(ap);

        fputc('\n', g_fp);
        /* ⚠ Flush, never close: the log must survive a crash or a hang, which
         * is how this project has diagnosed both. fflush hands the bytes to the
         * OS cache; it does not wait for the disk. */
        fflush(g_fp);
    }
    LeaveCriticalSection(&g_lock);
}
