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

/* ⛔⛔ THE RETURN VALUE IS NOT OPTIONAL.
 *
 * This used to discard it, which made a FAILING speechSay indistinguishable
 * from a working one -- and the log then showed a tidy `mcspeak: "..."` line
 * for every movement while the user heard nothing at all. I read those lines as
 * evidence that speech worked. They were evidence that the function was
 * CALLED.
 *
 * ⭐ Same rule this project keeps relearning: "could not do it" must be
 * observable, not inferred. speech_say() now reports whether the backend
 * accepted the text, and logs the FIRST failure loudly so a silent mod is
 * diagnosable from the log instead of from the user's ear. */
BOOL speech_say(const wchar_t *text, BOOL interrupt)
{
    if (!g_say) {
        static BOOL warned;
        if (!warned) {
            warned = TRUE;
            log_line("speech: ⛔ speechSay is NOT available -- nothing this mod "
                     "says will be heard. Every later 'spoke' line is a call, "
                     "not audio.");
        }
        return FALSE;
    }
    if (!text || !*text)
        return FALSE;

    /* ⭐⭐⭐ TIME THE BACKEND CALL. This runs on the GAME'S OWN THREAD inside
     * the cursor hook, so if speechSay blocks, the game and the readout both
     * wait -- and that is indistinguishable by ear from a slow mod.
     *
     * ⛔ USER-REPORTED: "a lot of lag between me pressing an arrow key and then
     * hearing the change echoed back." Everything the mod controls was measured
     * and is fast (0 ms median from movement to speech decision; 14-85 ms from
     * keypress to the hook). This is the one link that could not be measured
     * from outside the process, so it is now measured from inside.
     *
     * ⚠ Reports the WORST and the mean, not every call: a per-call line would
     * itself be log I/O on the keypress path -- the very thing just removed. */
    LARGE_INTEGER t0, t1, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    int rc = g_say(text, interrupt ? 1 : 0);
    QueryPerformanceCounter(&t1);
    {
        static double worst, total;
        static long   calls;
        static double next_report = 50.0;   /* report once past 50 ms total */
        double el = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 /
                    (double)freq.QuadPart;
        calls++;
        total += el;
        if (el > worst) worst = el;
        /* ⭐ Report on a THRESHOLD, not a schedule: a run that ends early must
         * still have said something, and a fast backend should stay silent
         * rather than printing "all is well" forever. */
        if (worst >= 5.0 && total >= next_report) {
            next_report = total * 2.0;
            log_line("speech: ⚠ speechSay is SLOW -- %ld call(s), worst %.1f ms, "
                     "mean %.1f ms. This runs on the game's thread, so this "
                     "time is added to every keypress before you hear it.",
                     calls, worst, total / (double)calls);
        }
    }

    /* UniversalSpeech returns non-zero on success; 0 means no backend took it. */
    if (rc == 0) {
        static BOOL warned;
        if (!warned) {
            warned = TRUE;
            log_line("speech: ⛔ speechSay REFUSED the text (returned 0). The "
                     "backend is loaded but is not speaking -- check that NVDA "
                     "is running and that nvdaControllerClient.dll sits next to "
                     "the exe.");
        }
        return FALSE;
    }
    return TRUE;
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
