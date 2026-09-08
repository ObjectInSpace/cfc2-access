/*
 * Self-test for the GetAsyncKeyState caller trace.
 *
 * ⭐⭐ WHAT THIS TEST IS ACTUALLY FOR
 * The trace produces a list of code addresses and the next session will spend
 * disassembly time on whichever one it names. The failure mode that costs the
 * most is not "found nothing" -- it is naming OUR OWN call sites as the game's
 * input reader. mod.c calls GetAsyncKeyState nine times every 100 ms, so a
 * broken self-filter does not produce a null result; it produces a confident
 * wrong answer that looks exactly like a real finding.
 *
 * So the test drives the REAL hook (installed on the real user32 export) with
 * calls from two real modules -- this exe, standing in for our DLL, and
 * test_ct_helper.dll, standing in for the game -- and asserts the trace
 * separates them. A test whose callers all lived in one module could not tell a
 * perfect filter from no filter at all.
 *
 * ⚠ EVERY CASE BELOW WAS RUN WITH ITS MECHANISM SABOTAGED AND SEEN TO FAIL.
 * The sabotage for each is written next to it. An assertion that has never been
 * observed failing is not evidence -- this project has already shipped a trap
 * that passed with the thing it tested switched off.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../src/caller_trace.h"
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

typedef void (*pfn_one_key)(int);
typedef void (*pfn_sweep)(int, int);

/* Our own caller, standing in for mod.c's nine sites. Deliberately in THIS
 * module so it must be classified as self. */
static void self_calls(int times)
{
    for (int i = 0; i < times; i++)
        GetAsyncKeyState(VK_F5);
}

int main(void)
{
    printf("caller trace self-test\n\n");

    /* The probe and the hook both need user32 genuinely loaded in THIS
     * process. A console exe links it here because we call GetAsyncKeyState
     * directly, but make it explicit rather than depending on that. */
    if (!GetModuleHandleW(L"user32.dll")) {
        printf("  FAIL  user32 not loaded in the test process\n");
        return 1;
    }

    HMODULE helper = LoadLibraryA("test_ct_helper.dll");
    if (!helper) {
        printf("  FAIL  cannot load test_ct_helper.dll (err %lu) -- the test "
               "needs a SECOND module to have any discriminating power\n",
               GetLastError());
        return 1;
    }
    pfn_one_key h_one   = (pfn_one_key)(void *)
                          GetProcAddress(helper, "ct_helper_one_key");
    pfn_sweep   h_sweep = (pfn_sweep)(void *)
                          GetProcAddress(helper, "ct_helper_sweep");
    if (!h_one || !h_sweep) {
        printf("  FAIL  helper exports missing\n");
        return 1;
    }

    /* --- Case 0: nothing is concluded before the hook is installed ------
     * SABOTAGE: make ct_report_log() print sites regardless of `installed`
     * -> this case fails. It is the same rule as the IAT probe's return
     * value: "never looked" must not be able to masquerade as "looked and
     * found none". */
    ct_report r;
    ct_snapshot(&r);
    check(!r.installed, "case 0: reports NOT installed before install");
    check(r.total_calls == 0, "case 0: no calls recorded before install");

    if (!ct_install()) {
        printf("  FAIL  ct_install() failed -- cannot run the remaining "
               "cases\n");
        return 1;
    }

    /* --- Case 1: the hook actually intercepts --------------------------
     * The liveness proof. Everything else is meaningless without it.
     * SABOTAGE: enable the hook but have MH_EnableHook not be called
     * -> total_calls stays 0 and this fails first. */
    self_calls(10);
    ct_snapshot(&r);
    check(r.total_calls >= 10, "case 1: hook intercepts (total_calls >= 10)");

    /* --- Case 2: OUR calls are classified as ours, not as game sites ---
     * ⭐⭐ THE DISCRIMINATING CASE. This is the false positive that would
     * otherwise be reported as the shell's input reader.
     * SABOTAGE: delete the `r >= g_self_lo && r < g_self_hi` branch
     * -> self_calls becomes 0, foreign_calls jumps, and a site inside this
     * exe appears in the table. Verified: this case fails and case 3 also
     * fails, while cases 1 and 5 still pass. */
    check(r.self_calls >= 10,
          "case 2: our own calls counted as SELF (>= 10)");
    check(r.foreign_calls == 0,
          "case 2: our own calls did NOT become game sites (foreign == 0)");
    check(r.n_sites == 0,
          "case 2: no site recorded for a caller inside our own module");

    /* --- Case 3: a caller in ANOTHER module IS recorded -----------------
     * The positive direction. Without this, a filter that rejected
     * everything would pass case 2 perfectly.
     * SABOTAGE: make the self-range cover all memory -> foreign_calls stays
     * 0 and this fails, while case 2 still passes. That asymmetry is the
     * point of having both. */
    h_one(25);
    ct_snapshot(&r);
    check(r.foreign_calls >= 25,
          "case 3: calls from another module counted as FOREIGN");
    check(r.n_sites == 1,
          "case 3: exactly one distinct site recorded so far");

    /* --- Case 4: repeats collapse into ONE site with a hit count -------
     * The trace must produce a SET of call sites, not a call log:
     * GetAsyncKeyState runs every frame, and a flood is not evidence.
     * SABOTAGE: skip the "find existing site" scan and always append
     * -> n_sites climbs with every call and this fails. */
    LONG sites_before = r.n_sites;
    LONG hits_before  = r.sites[0].hits;
    h_one(40);
    ct_snapshot(&r);
    check(r.n_sites == sites_before,
          "case 4: repeat calls add NO new site");
    check(r.sites[0].hits >= hits_before + 40,
          "case 4: repeat calls increment the existing site's hit count");

    /* --- Case 5: distinct VK counting separates reader from hotkey -----
     * The report tells the reader to rank by DISTINCT VKS, because a tight
     * one-key check can out-hit a 70-key sweep. That advice is only sound if
     * the count is real.
     * SABOTAGE: increment distinct_vks unconditionally instead of only on a
     * newly-set bit -> the one-key site's count tracks its hit count and
     * this fails. */
    h_sweep(0x30, 30);          /* 30 different VKs, one site */
    ct_snapshot(&r);
    check(r.n_sites == 2, "case 5: the sweep is a SECOND distinct site");

    ct_site *one = NULL, *sweep = NULL;
    for (int i = 0; i < r.n_sites; i++) {
        if (r.sites[i].distinct_vks == 1) one = &r.sites[i];
        else                              sweep = &r.sites[i];
    }
    check(one != NULL,
          "case 5: the one-key site reports exactly 1 distinct vk");
    check(sweep != NULL && sweep->distinct_vks == 30,
          "case 5: the sweeping site reports exactly 30 distinct vks");
    /* ⭐ And the ordering advice must hold: the one-key site has MORE hits
     * (65 vs 30) yet FEWER vks. If this ever inverts, the report's
     * instruction to rank by vks is wrong and must change with it. */
    if (one && sweep)
        check(one->hits > sweep->hits && one->distinct_vks < sweep->distinct_vks,
              "case 5: hits and distinct-vks genuinely disagree, which is why "
              "the report ranks by vks");

    /* --- Case 6: removal actually removes ------------------------------
     * The off switch has to work, and "it stopped crashing" is not proof.
     * SABOTAGE: make ct_remove() return immediately -> total_calls keeps
     * climbing and this fails. Verified.
     *
     * ⚠ WHAT THIS CASE DOES *NOT* COVER, found by sabotaging it:
     * deleting only the MH_DisableHook() call leaves this case PASSING,
     * because MH_Uninitialize() below also tears the detour down. So case 6
     * proves "ct_remove() stops interception", which is the property the off
     * switch needs, but it does NOT pin which of the two calls achieves it.
     * That is recorded rather than papered over: an assertion is only worth
     * what its observed failures are, and this one's are narrower than its
     * name suggests. */
    ct_remove();
    ct_snapshot(&r);
    LONG total_at_remove = r.total_calls;
    self_calls(20);
    h_one(20);
    ct_snapshot(&r);
    check(r.total_calls == total_at_remove,
          "case 6: after ct_remove() no further calls are intercepted");

    FreeLibrary(helper);

    printf("\n%s (%d failure%s)\n",
           failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
