#include "log.h"

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static CRITICAL_SECTION g_lock;
static BOOL g_lock_ready;
static wchar_t g_path[MAX_PATH];

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
     * previous run reading as current evidence has burned us before. */
    if (g_path[0]) {
        FILE *f = _wfopen(g_path, L"w");
        if (f)
            fclose(f);
    }
}

void log_shutdown(void)
{
    if (g_lock_ready) {
        DeleteCriticalSection(&g_lock);
        g_lock_ready = FALSE;
    }
}

void log_line(const char *fmt, ...)
{
    if (!g_path[0] || !g_lock_ready)
        return;

    EnterCriticalSection(&g_lock);
    FILE *f = _wfopen(g_path, L"a");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond,
                st.wMilliseconds);

        va_list ap;
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);

        fputc('\n', f);
        fclose(f);
    }
    LeaveCriticalSection(&g_lock);
}
