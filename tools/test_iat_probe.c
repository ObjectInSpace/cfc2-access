/*
 * Self-test for the runtime IAT probe.
 *
 * ⭐⭐ WHY THIS EXISTS, AND WHY IT PLANTS DECOYS
 *
 * The probe's whole value is that its NEGATIVE is trustworthy: if it reports
 * "the shell does not import GetAsyncKeyState", the next session will stop
 * looking down that road. A detector that silently finds nothing is
 * indistinguishable from one that is broken -- this project has already spent
 * five game launches on that exact confusion.
 *
 * So the test plants a table whose contents are KNOWN and requires the probe to
 * report exactly them: the functions planted must be found, and the ones
 * deliberately left out must be reported absent. Both directions are asserted,
 * because a probe that answered "yes" to everything would pass a
 * presence-only test.
 *
 * It drives the REAL iat_probe_run() over a synthetic block, not a copy of its
 * logic. A test of a reimplementation proves nothing about the shipping code.
 *
 * ⚠ Each case below was verified to FAIL when the mechanism it tests is
 * sabotaged; see the SABOTAGE notes. An assertion never observed failing is
 * not evidence.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../src/iat_probe.h"
#include "../src/log.h"

static int failures;

static void check(int cond, const char *what)
{
    if (cond) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

/* Build a synthetic import block. `include_polled` decides whether the polled
 * keyboard APIs are planted, which is the discrimination the whole probe rests
 * on: the same table with and without them must give different answers. */
static ULONGLONG *make_table(int *n_out, BOOL include_polled, BOOL include_raw)
{
    static ULONGLONG slots[64];
    int n = 0;

    HMODULE u32 = GetModuleHandleA("user32.dll");
    HMODULE k32 = GetModuleHandleA("kernel32.dll");

    /* Always present: the message pump (every Win32 app has it) plus filler
     * from kernel32, so the table looks like a real IAT rather than a list of
     * exactly the things being searched for. Padding matters: a table
     * containing only the targets would let a probe that matches everything
     * pass. */
    slots[n++] = (ULONGLONG)(UINT_PTR)GetProcAddress(u32, "PeekMessageW");
    slots[n++] = (ULONGLONG)(UINT_PTR)GetProcAddress(u32, "DispatchMessageW");
    slots[n++] = (ULONGLONG)(UINT_PTR)GetProcAddress(k32, "CreateFileW");
    slots[n++] = (ULONGLONG)(UINT_PTR)GetProcAddress(k32, "VirtualAlloc");
    slots[n++] = (ULONGLONG)(UINT_PTR)GetProcAddress(k32, "CloseHandle");
    slots[n++] = 0;                       /* a real IAT has holes */
    slots[n++] = (ULONGLONG)(UINT_PTR)GetProcAddress(k32, "Sleep");

    if (include_polled) {
        slots[n++] = (ULONGLONG)(UINT_PTR)GetProcAddress(u32, "GetAsyncKeyState");
        slots[n++] = (ULONGLONG)(UINT_PTR)GetProcAddress(u32, "GetKeyboardState");
    }
    if (include_raw)
        slots[n++] = (ULONGLONG)(UINT_PTR)GetProcAddress(u32, "GetRawInputData");

    *n_out = n;
    return slots;
}

static BOOL run_over(ULONGLONG *tbl, int n, iat_probe_result *r)
{
    g_iat_test_lo = tbl;
    g_iat_test_hi = tbl + n;
    BOOL ok = iat_probe_run(r);
    g_iat_test_lo = g_iat_test_hi = NULL;
    return ok;
}

int main(void)
{
    /* The probe reports through the log, so the log is where a failure
     * explains itself. Without this the test can only say "it returned
     * FALSE" -- which is exactly the uninformative negative the probe exists
     * to avoid producing. */
    log_init();
    printf("iat_probe self-test (details in CFC2Access.log next to this exe)\n");

    /* ⚠ A plain console exe does not link user32, so GetModuleHandle finds it
     * absent and the probe correctly REFUSES to conclude anything. That refusal
     * is the behaviour we want in-game -- so the fix belongs here, not in the
     * probe: load the DLLs the game already has loaded, making this process
     * resemble the one being measured.
     *
     * The probe itself still never calls LoadLibrary. It must only ever see
     * modules that are genuinely present in its host process. */
    if (!LoadLibraryA("user32.dll")) {
        printf("  FAIL  could not load user32.dll for the test\n");
        return 1;
    }
    LoadLibraryA("xinput1_4.dll");   /* optional: absence is a valid state */

    /* --- Case 1: the APIs are present and must be FOUND ------------------
     * SABOTAGE: make the address comparison always fail
     * (`(void*)(UINT_PTR)v != addr[i]` -> `1`). Result: polled_keyboard=no,
     * matches=0, and this case fails. Verified. */
    {
        int n;
        ULONGLONG *tbl = make_table(&n, TRUE, TRUE);
        iat_probe_result r;
        printf("case 1: polled + raw planted\n");
        check(run_over(tbl, n, &r), "probe ran");
        check(r.polled_keyboard, "polled keyboard FOUND (it was planted)");
        check(r.raw_input,       "raw input FOUND (it was planted)");
        check(r.window_messages, "window messages FOUND (they were planted)");
        check(r.matches >= 4,    "at least 4 slots matched");
        check(r.slots_examined == n, "examined every slot in the block");
    }

    /* --- Case 2: the APIs are ABSENT and must be reported absent ---------
     * This is the case that makes the in-game negative mean something. A probe
     * that reported "polled keyboard" regardless would pass case 1 and fail
     * here.
     * SABOTAGE: make the comparison always succeed (`||` -> `&&` in the skip
     * condition). Result: polled_keyboard=YES on a table that contains no
     * polled API, and this case fails. Verified. */
    {
        int n;
        ULONGLONG *tbl = make_table(&n, FALSE, FALSE);
        iat_probe_result r;
        printf("case 2: NEITHER planted -- the discriminating case\n");
        check(run_over(tbl, n, &r), "probe ran");
        check(!r.polled_keyboard, "polled keyboard correctly reported ABSENT");
        check(!r.raw_input,       "raw input correctly reported ABSENT");
        /* The pump is still there, so this is not a table the probe simply
         * failed to read -- it read it and found the other family. */
        check(r.window_messages,
              "window messages still found -- the table WAS read");
        check(r.slots_resolved > 0,
              "slots resolved -- proves the absence above is a real negative");
    }

    /* --- Case 3: polled present, raw absent ------------------------------
     * The two families must be independent. A probe that set every flag once
     * it matched anything would pass cases 1 and 2 but fail here. */
    {
        int n;
        ULONGLONG *tbl = make_table(&n, TRUE, FALSE);
        iat_probe_result r;
        printf("case 3: polled only -- families must be independent\n");
        check(run_over(tbl, n, &r), "probe ran");
        check(r.polled_keyboard, "polled keyboard FOUND");
        check(!r.raw_input,      "raw input correctly reported ABSENT");
    }

    /* --- Case 4: an unreadable block must ABORT, not report a clean null --
     * ⛔ This is the failure mode that has cost this project the most: a scan
     * that could not read anything printing a tidy "found nothing". The probe
     * must distinguish "looked and found none" from "never looked".
     * SABOTAGE: drop the `slots_resolved == 0` guard. Result: the probe prints
     * RESULT with every family "no", which reads exactly like a real negative.
     * Verified. */
    {
        /* A reserved-but-not-committed page: readable-looking address, no
         * committed memory behind it. */
        void *bad = VirtualAlloc(NULL, 0x1000, MEM_RESERVE, PAGE_NOACCESS);
        iat_probe_result r;
        printf("case 4: unreadable block must not look like a clean negative\n");
        g_iat_test_lo = bad;
        g_iat_test_hi = (BYTE *)bad + 0x200;
        BOOL ok = iat_probe_run(&r);
        g_iat_test_lo = g_iat_test_hi = NULL;

        /* ⭐⭐ THE LOAD-BEARING ASSERTION. Everything else in this case would
         * also hold for a genuine negative; only the return value separates
         * "looked and found none" from "never looked". A probe that returned
         * TRUE here would hand mod.c a struct of all-FALSE flags, which it
         * would then SPEAK as "found: none of them" -- a fabricated finding.
         * SABOTAGE: delete the `return FALSE` in the slots_resolved == 0
         * branch. Result: this one assertion fails and the other three still
         * pass, which is precisely why it has to exist. Verified. */
        check(!ok, "returned FALSE: inconclusive is not reported as a negative");
        check(r.slots_examined > 0, "it counted the slots it tried to read");
        check(r.slots_resolved == 0, "nothing resolved");
        check(!r.polled_keyboard && !r.raw_input && !r.window_messages,
              "no family claimed from an unreadable block");
        VirtualFree(bad, 0, MEM_RELEASE);
    }

    printf("\n%s (%d failures)\n", failures ? "SELF-TEST FAILED" : "all ok",
           failures);
    return failures ? 1 : 0;
}
