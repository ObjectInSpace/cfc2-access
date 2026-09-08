/*
 * Runtime IAT probe -- see iat_probe.h for why this exists and what each
 * possible answer implies for the next step.
 */

#include "iat_probe.h"
#include "log.h"
#include "image.h"
#include "memsafe.h"

#include <stdio.h>
#include <string.h>

/* --- The candidates -------------------------------------------------------
 * Grouped by FAMILY, because the family is the finding: it decides which hook
 * is even applicable. Names are exact export names, so a miss here means the
 * game genuinely does not import that function, not that we spelled it oddly.
 *
 * ⚠ The list deliberately includes functions we do NOT expect (GetMessageW,
 * XInputGetState). A probe that only looks for what it predicts cannot tell
 * "the prediction was right" from "the probe only knew one answer" -- and this
 * project has already been burned by a filter that searched for exactly the
 * value it then reported. */
typedef enum {
    FAM_POLLED = 0,
    FAM_RAW,
    FAM_MSG,
    FAM_XINPUT,
    FAM_DINPUT,
    FAM_OTHER
} api_family;

typedef struct {
    const char *dll;
    const char *name;
    api_family  family;
} api_candidate;

static const api_candidate CANDIDATES[] = {
    /* Polled keyboard: the shell would read these every frame. */
    { "user32.dll",  "GetAsyncKeyState",          FAM_POLLED },
    { "user32.dll",  "GetKeyState",               FAM_POLLED },
    { "user32.dll",  "GetKeyboardState",          FAM_POLLED },

    /* Raw input. */
    { "user32.dll",  "GetRawInputData",           FAM_RAW },
    { "user32.dll",  "GetRawInputBuffer",         FAM_RAW },
    { "user32.dll",  "RegisterRawInputDevices",   FAM_RAW },

    /* Window messages. */
    { "user32.dll",  "PeekMessageW",              FAM_MSG },
    { "user32.dll",  "PeekMessageA",              FAM_MSG },
    { "user32.dll",  "GetMessageW",               FAM_MSG },
    { "user32.dll",  "GetMessageA",               FAM_MSG },
    { "user32.dll",  "TranslateMessage",          FAM_MSG },
    { "user32.dll",  "DispatchMessageW",          FAM_MSG },
    { "user32.dll",  "DispatchMessageA",          FAM_MSG },
    { "user32.dll",  "SetWindowLongPtrW",         FAM_MSG },
    { "user32.dll",  "CallWindowProcW",           FAM_MSG },

    /* Gamepad paths -- if the shell only imports these, the keyboard is being
     * handled somewhere we have not looked at all, which is itself a finding. */
    { "xinput1_4.dll",   "XInputGetState",        FAM_XINPUT },
    { "xinput1_3.dll",   "XInputGetState",        FAM_XINPUT },
    { "xinput9_1_0.dll", "XInputGetState",        FAM_XINPUT },
    { "dinput8.dll",     "DirectInput8Create",    FAM_DINPUT },
};

#define N_CANDIDATES ((int)(sizeof(CANDIDATES) / sizeof(CANDIDATES[0])))

/* Test-only override of the block bounds; NULL in every shipping run. Declared
 * in the header so the self-test can drive the REAL function over a synthetic
 * table rather than reimplementing it. */
void *g_iat_test_lo;
void *g_iat_test_hi;

static const char *family_name(api_family f)
{
    switch (f) {
    case FAM_POLLED: return "polled-keyboard";
    case FAM_RAW:    return "raw-input";
    case FAM_MSG:    return "window-message";
    case FAM_XINPUT: return "xinput";
    case FAM_DINPUT: return "directinput";
    default:         return "other";
    }
}

/* Resolved address of each candidate, or NULL if the DLL/export is absent.
 * ⚠ Resolved WITHOUT LoadLibrary: we only want functions the game has already
 * loaded. Loading xinput1_3 ourselves just to ask about it would change the
 * process we are measuring, and could resolve to an address the game's IAT was
 * never going to hold. GetModuleHandle only -- absent means absent. */
static void *resolve_candidate(const api_candidate *c)
{
    HMODULE m = GetModuleHandleA(c->dll);
    if (!m)
        return NULL;
    return (void *)GetProcAddress(m, c->name);
}

/* Which loaded module contains `p`? Fills `name` with the base file name.
 * Uses the address-to-module query rather than walking the module list, so a
 * module loaded after we started is still identified. */
static BOOL module_of(void *p, char *name, size_t cap)
{
    HMODULE m = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)p, &m) || !m)
        return FALSE;

    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(m, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return FALSE;

    const char *leaf = strrchr(path, '\\');
    leaf = leaf ? leaf + 1 : path;
    strncpy(name, leaf, cap - 1);
    name[cap - 1] = '\0';
    return TRUE;
}

BOOL iat_probe_run(iat_probe_result *out)
{
    iat_probe_result r;
    memset(&r, 0, sizeof(r));

    /* ⚠ Publish the (zeroed) result on EVERY path, including the aborts below.
     * An early version returned FALSE without writing `out`, so a caller that
     * inspected the struct anyway read stack garbage -- and in the self-test
     * that garbage satisfied an assertion, which is the precise way a broken
     * detector passes its own test. Zeroed counters are unambiguous: 0
     * examined means it never looked. */
    if (out)
        *out = r;

    /* ⚠⚠ The guard is INERT unless installed, and an inert guard is worse than
     * none -- it makes unprotected reads merely LOOK protected. This cost a
     * 0xC0000005 in pad_find.c. Install self-tests by faulting deliberately;
     * refuse to scan if it did not work. */
    if (!mem_guard_install()) {
        log_line("iat: ABORT -- the fault guard failed its self-test, so a bad "
                 "read here would take the game down. Nothing probed.");
        return FALSE;
    }

    /* ⭐ The self-test substitutes a synthetic block here. Everything below is
     * the SAME code the game runs -- a test that exercised a parallel
     * implementation would prove nothing about the shipping one. */
    void *iat_lo, *iat_hi;
    if (g_iat_test_lo) {
        iat_lo = g_iat_test_lo;
        iat_hi = g_iat_test_hi;
    } else {
        BYTE *base = image_base();
        if (!base) {
            log_line("iat: ABORT -- no module base.");
            return FALSE;
        }
        iat_lo = base + CFC2_IAT_RVA_LO;
        iat_hi = base + CFC2_IAT_RVA_HI;
    }
    SIZE_T span   = (SIZE_T)((BYTE *)iat_hi - (BYTE *)iat_lo);

    log_line("iat: probing the import block at %p-%p (%llu bytes, %llu slots). "
             "Block located OFFLINE from the riprefs index: 272 slots are "
             "reached by `call qword ptr [rip+..]` and they occupy this one run.",
             iat_lo, iat_hi, (unsigned long long)span,
             (unsigned long long)(span / 8));

    /* Resolve the candidates first, and SAY how many were available. If
     * user32 somehow is not loaded, every subsequent "not found" would be
     * meaningless, and that must be visible rather than inferred. */
    void *addr[N_CANDIDATES];
    for (int i = 0; i < N_CANDIDATES; i++) {
        addr[i] = resolve_candidate(&CANDIDATES[i]);
        r.apis_sought++;
        if (addr[i])
            r.apis_available++;
        else
            log_line("iat:   candidate NOT AVAILABLE: %s!%s (module not loaded "
                     "or export absent) -- it cannot be matched below",
                     CANDIDATES[i].dll, CANDIDATES[i].name);
    }
    log_line("iat: %d of %d candidate APIs resolved in already-loaded modules "
             "(no LoadLibrary was performed)", r.apis_available, r.apis_sought);

    if (r.apis_available == 0) {
        log_line("iat: ABORT -- not one candidate resolved. Any 'no match' "
                 "below would say nothing about the game.");
        return FALSE;
    }

    /* Walk the block. */
    for (BYTE *p = (BYTE *)iat_lo; p < (BYTE *)iat_hi; p += 8) {
        r.slots_examined++;

        ULONGLONG v = 0;
        if (!mem_copy(&v, p, sizeof(v)))
            continue;              /* not committed -- counted, not concluded */
        r.slots_readable++;

        if (v == 0)
            continue;

        char mod[64];
        if (!module_of((void *)(UINT_PTR)v, mod, sizeof(mod)))
            continue;              /* not inside any loaded module */
        r.slots_resolved++;

        for (int i = 0; i < N_CANDIDATES; i++) {
            if (!addr[i] || (void *)(UINT_PTR)v != addr[i])
                continue;

            r.matches++;
            log_line("iat:   MATCH slot %p -> %s!%s  [%s]",
                     (void *)p, CANDIDATES[i].dll, CANDIDATES[i].name,
                     family_name(CANDIDATES[i].family));

            switch (CANDIDATES[i].family) {
            case FAM_POLLED: r.polled_keyboard = TRUE; break;
            case FAM_RAW:    r.raw_input       = TRUE; break;
            case FAM_MSG:    r.window_messages = TRUE; break;
            case FAM_XINPUT: r.xinput          = TRUE; break;
            case FAM_DINPUT: r.directinput     = TRUE; break;
            default: break;
            }
            break;
        }
    }

    /* --- The report ------------------------------------------------------
     * ⭐ Counts of what was EXAMINED come first, so a null result can be told
     * apart from a probe that never looked. "0 failed" is not "0 checked". */
    log_line("iat: examined %d slots; %d readable; %d resolved into a loaded "
             "module; %d matched a named candidate",
             r.slots_examined, r.slots_readable, r.slots_resolved, r.matches);

    LONG faults = mem_guard_fault_count();
    if (faults > 0)
        log_line("iat: ⚠ the guard absorbed %ld faults -- this was a PARTIAL "
                 "read of the block, so a negative below is not final", faults);

    /* ⭐⭐ THE INCONCLUSIVE CASE MUST BE A RETURN VALUE, NOT A PRINTED WARNING.
     *
     * If no slot resolved into a loaded module, the probe did not measure the
     * game -- the bounds are wrong for this run, or the table is not populated
     * yet. The family flags are then all FALSE for want of evidence, which
     * reads exactly like a real negative ("the shell imports no input API"),
     * and that is the conclusion the next session would build on.
     *
     * This project has already shipped a tool that printed "coverage is
     * INCOMPLETE" while every assertion stayed green, because nothing FAILED
     * on the warning. So the distinction lives in the return value: callers
     * that only check the boolean cannot accidentally treat "never looked" as
     * "looked and found none". */
    if (r.slots_resolved == 0) {
        log_line("iat: ⛔ INCONCLUSIVE -- NO slot resolved into a loaded "
                 "module (examined %d, readable %d). The block bounds are "
                 "wrong for this run, or the table is not populated yet. "
                 "Conclude NOTHING about which API is used; the family flags "
                 "below are absent evidence, not evidence of absence.",
                 r.slots_examined, r.slots_readable);
        if (out)
            *out = r;
        return FALSE;
    } else {
        log_line("iat: RESULT -- polled_keyboard=%s raw_input=%s "
                 "window_messages=%s xinput=%s directinput=%s",
                 r.polled_keyboard ? "YES" : "no",
                 r.raw_input       ? "YES" : "no",
                 r.window_messages ? "YES" : "no",
                 r.xinput          ? "YES" : "no",
                 r.directinput     ? "YES" : "no");

        /* Say what each answer MEANS for the next step, in the log, while the
         * evidence is next to it. The next session should not have to
         * reconstruct the interpretation from the raw booleans. */
        if (r.polled_keyboard)
            log_line("iat:   -> polled keyboard is imported: hook "
                     "GetAsyncKeyState/GetKeyboardState and record the RETURN "
                     "ADDRESS. The caller is the shell's input reader, and the "
                     "selection state is written near it.");
        if (r.raw_input)
            log_line("iat:   -> raw input is imported: the decoded state lives "
                     "in the game's own buffer; hook GetRawInputData and follow "
                     "the output pointer to it.");
        if (r.window_messages)
            log_line("iat:   -> the message pump is imported (expected for any "
                     "windowed app). On its own this is NOT evidence the menus "
                     "read keys this way -- every Win32 app imports it.");
        if (!r.polled_keyboard && !r.raw_input)
            log_line("iat:   -> ⛔ neither polling nor raw input is imported. "
                     "Since sPad is inert too, the shell reads the keyboard "
                     "through the message pump or through a DLL we have not "
                     "considered. Check the dinput8 proxy's own call log next: "
                     "we ARE that DLL, so every DirectInput call is already "
                     "observable for free.");
    }

    /* Unmatched-but-resolved slots are the remaining unknown; naming their
     * modules costs nothing and bounds what else could be carrying input. */
    if (r.slots_resolved > r.matches) {
        log_line("iat: %d resolved slots did not match a candidate. Their "
                 "modules follow, as a bound on where else input could come "
                 "from:", r.slots_resolved - r.matches);

        /* Tally distinct module names. Small fixed table: the import list is
         * a few hundred entries across a handful of system DLLs. */
        char  names[24][64];
        int   counts[24];
        int   nmods = 0;
        memset(counts, 0, sizeof(counts));

        for (BYTE *p = (BYTE *)iat_lo; p < (BYTE *)iat_hi; p += 8) {
            ULONGLONG v = 0;
            if (!mem_copy(&v, p, sizeof(v)) || v == 0)
                continue;
            char mod[64];
            if (!module_of((void *)(UINT_PTR)v, mod, sizeof(mod)))
                continue;
            int j;
            for (j = 0; j < nmods; j++)
                if (_stricmp(names[j], mod) == 0)
                    break;
            if (j == nmods) {
                if (nmods >= 24)
                    continue;
                /* mod is already NUL-terminated within 64 bytes by
                 * module_of(); a plain copy is exact, not truncating. */
                memcpy(names[nmods], mod, sizeof(names[nmods]));
                nmods++;
            }
            counts[j]++;
        }
        for (int j = 0; j < nmods; j++)
            log_line("iat:   %-24s %d slots", names[j], counts[j]);
    }

    if (out)
        *out = r;
    return TRUE;
}
