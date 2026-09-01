#include "speech.h"
#include "log.h"

/* UniversalSpeech is loaded dynamically rather than link-time imported: if the
 * DLL is missing we want the mod to keep running (and say so in the log), not
 * to fail process startup and take the game down with it. */
typedef int (*speechSay_t)(const wchar_t *str, int interrupt);
typedef int (*speechStop_t)(void);
typedef int (*getCurrentScreenReader_t)(void);
typedef const wchar_t *(*getScreenReaderNameW_t)(int id);

static HMODULE g_us;
static speechSay_t g_say;
static speechStop_t g_stop;
static getCurrentScreenReader_t g_current;
static getScreenReaderNameW_t g_name;

BOOL speech_init(void)
{
    if (g_us)
        return g_say != NULL;

    /* Load from the game directory (next to our own DLL), not the system path. */
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return FALSE;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash)
        return FALSE;
    slash[1] = L'\0';
    if (wcslen(path) + 21 >= MAX_PATH)
        return FALSE;
    lstrcatW(path, L"UniversalSpeech.dll");

    g_us = LoadLibraryW(path);
    if (!g_us) {
        log_line("speech: LoadLibrary UniversalSpeech.dll failed (err %lu)",
                 GetLastError());
        return FALSE;
    }

    g_say = (speechSay_t)(void *)GetProcAddress(g_us, "speechSay");
    g_stop = (speechStop_t)(void *)GetProcAddress(g_us, "speechStop");
    g_current = (getCurrentScreenReader_t)(void *)GetProcAddress(
        g_us, "getCurrentScreenReader");
    g_name = (getScreenReaderNameW_t)(void *)GetProcAddress(
        g_us, "getScreenReaderNameW");

    if (!g_say) {
        log_line("speech: speechSay export missing");
        return FALSE;
    }

    const wchar_t *backend = speech_backend();
    if (backend)
        log_line("speech: ready, backend=%ls", backend);
    else
        log_line("speech: ready, but NO screen reader detected "
                 "(SAPI fallback may still speak)");

    /* A silent fallback to SAPI5 while a real screen reader is running is a
     * BUG, not a degraded-but-fine state: it ignores the user's configured
     * voice, rate, punctuation level and braille display. It is almost always
     * a missing client DLL (UniversalSpeech loads nvdaControllerClient.dll by
     * that exact name). Make it loud in the log instead of letting it pass. */
    if (backend && !lstrcmpiW(backend, L"SAPI5")) {
        HWND nvda = FindWindowW(L"wxWindowClassNR", L"NVDA");
        if (!nvda)
            nvda = FindWindowW(L"wxWindowClassNR", NULL);
        if (nvda)
            log_line("speech: WARNING -- NVDA appears to be running but the "
                     "backend is SAPI5. nvdaControllerClient.dll is probably "
                     "missing from the game directory.");
    }
    return TRUE;
}

void speech_shutdown(void)
{
    if (g_us) {
        FreeLibrary(g_us);
        g_us = NULL;
        g_say = NULL;
        g_stop = NULL;
        g_current = NULL;
        g_name = NULL;
    }
}

void speech_say(const wchar_t *text, BOOL interrupt)
{
    if (g_say && text && *text)
        g_say(text, interrupt ? 1 : 0);
}

void speech_stop(void)
{
    if (g_stop)
        g_stop();
}

const wchar_t *speech_backend(void)
{
    if (!g_current || !g_name)
        return NULL;
    int id = g_current();
    if (id <= 0)
        return NULL;
    return g_name(id);
}
