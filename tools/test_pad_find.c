/*
 * Offline self-test for the pad-state finder (route 5).
 *
 * ⭐⭐ WHY THIS HARNESS IS SHAPED THIS WAY.
 *
 * This project has repeatedly paid for tests that PASS while the mechanism
 * under test is broken. The rule earned there: a trap must be PROVEN to fail
 * when the thing it guards is sabotaged, or it is not a test. So every case
 * below was written and then verified by deliberately breaking the code it
 * covers -- notes record which sabotage each case catches.
 *
 * The specific false-positive this route must not repeat: route 4 found a
 * monotonic allocation counter that satisfied every filter it had (stepped by
 * a constant, reversed, quiet in the control) and represented nothing. The
 * defence here is that the pad struct is identified by an ALGEBRAIC INVARIANT
 * the engine maintains, not by correlation with the user's input. So the
 * hardest case in this file is case 3: a decoy that MOVES WITH THE PLAYER but
 * does not satisfy the algebra must be rejected.
 *
 * Build+run:  see tools/run_pad_test.ps1
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <windows.h>

#include "../src/pad_find.h"

/* --- stubs for the mod's services ---------------------------------------- */
static char g_log[512][512];
static int  g_nlog;

void log_init(void) {}
void log_shutdown(void) {}
void log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (g_nlog < 512) {
        vsnprintf(g_log[g_nlog], sizeof g_log[0], fmt, ap);
        printf("    | %s\n", g_log[g_nlog]);
        g_nlog++;
    }
    va_end(ap);
    fflush(stdout);
}

static char g_said[64][256];
static int  g_nsaid;
BOOL speech_init(void) { return TRUE; }
void speech_shutdown(void) {}
void speech_stop(void) {}
const wchar_t *speech_backend(void) { return L"TEST"; }
void speech_say(const wchar_t *s, BOOL interrupt)
{
    (void)interrupt;
    if (g_nsaid < 64) {
        /* narrow it so the assertions can use plain strstr */
        WideCharToMultiByte(CP_UTF8, 0, s, -1, g_said[g_nsaid], 256,
                            NULL, NULL);
        g_nsaid++;
    }
}

static int g_fail;
static void check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) g_fail++;
}

/* Did any log line contain this substring? */
static int logged(const char *needle)
{
    for (int i = 0; i < g_nlog; i++)
        if (strstr(g_log[i], needle)) return 1;
    return 0;
}
static void clear_log(void) { g_nlog = 0; g_nsaid = 0; }

/* --- a simulated pad ------------------------------------------------------
 * Maintains the engine's own invariant exactly as MTFramework does, so the
 * planted struct is a faithful target rather than something built to match
 * whatever the scanner happens to look for. */
typedef struct {
    unsigned old, trg, rel, chg, rep;
} sim_pad;

static void pad_step(volatile sim_pad *p, unsigned cur)
{
    unsigned old = p->old;
    p->trg = cur & ~old;
    p->rel = old & ~cur;
    p->chg = cur ^ old;
    p->rep = cur;          /* held-or-repeating; menus act on this */
    p->old = cur;
}

int main(void)
{
    printf("== pad_find self-test ==\n");

    /* The planted pad lives in ordinary heap memory, which is where the real
     * one lives too. */
    volatile sim_pad *pad = (sim_pad *)VirtualAlloc(
        NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pad) { printf("alloc failed\n"); return 1; }
    memset((void *)pad, 0, sizeof *pad);
    printf("  planted pad at 0x%llx\n", (unsigned long long)(uintptr_t)pad);

    /* ------------------------------------------------------------------
     * Case 1: the scanner ARMS on a plausible, self-consistent site.
     * Catches: a broken plausible()/consistent() that arms nothing.
     * ------------------------------------------------------------------ */
    clear_log();
    /* ⚠ Arm while a press is LIVE. Arming requires an eventful sample -- see
     * the note in pad_find.c; a scan run on quiet memory arms nothing useful
     * and that is deliberate, not a defect. */
    pad_step(pad, PAD_RDOWN);
    pad_scan_arm();
    check(logged("armed"), "case 1: scan runs and reports what it armed");
    check(!logged("NO candidate sites"), "case 1: found at least one site");

    /* ------------------------------------------------------------------
     * Case 2: a real press sequence CONFIRMS the planted pad.
     *
     * This is the positive control. Without it, a scanner that rejects
     * everything would look identical to one that correctly found nothing.
     * ------------------------------------------------------------------ */
    clear_log();
    unsigned seq[] = { 0, PAD_RDOWN, 0, PAD_RDOWN, 0, PAD_LDOWN, 0,
                       PAD_RUP, 0, PAD_RLEFT, 0, PAD_RRIGHT, 0 };
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        pad_step(pad, seq[i]);
        pad_scan_tick();
    }
    check(logged("CONFIRMED"), "case 2: planted pad is CONFIRMED");

    clear_log();
    pad_scan_report();
    check(logged("confirmed"), "case 2: report states the confirmation");
    /* ⚠ Assert the PLANTED pad is among the survivors, by address.
     *
     * Several sites legitimately confirm -- the engine keeps a record per
     * player plus previous-frame snapshots, and this harness's own locals
     * mirror the same words. So the honest assertion is MEMBERSHIP, not
     * uniqueness. An earlier version demanded the representative BE the
     * planted pad and failed for a reason that was never a bug. */
    /* ⚠⚠ THE WINDOW IS AMBIGUOUS BY UP TO A FEW WORDS, and that is a property
     * of the data, not a bug in the scanner.
     *
     * A single press makes several of the five words equal (pressing Down once
     * gives Old=Trg=Chg=Rep=0x44, Rel=0), and a window starting one word later
     * then satisfies the same two equations. The scanner really does arm the
     * planted pad -- at +4 rather than +0.
     *
     * So the assertion accepts any window overlapping the record. Demanding
     * exactly +0 would fail for a reason that has nothing to do with whether
     * the pad was found, and this project has already burned rounds on tests
     * that failed for reasons that were never bugs. What the in-game reader
     * must therefore NOT do is trust the arming offset; it re-derives the
     * field positions from a multi-direction sequence (see case 5). */
    int found_pad_window = 0;
    for (int off = 0; off <= 16; off += 4)
        if (pad_is_confirmed((uintptr_t)pad + off)) { found_pad_window = 1; break; }
    check(found_pad_window,
          "case 2: a window ON the planted pad is among the confirmed sites");

    /* ------------------------------------------------------------------
     * Case 3: ⭐⭐ THE DECOY THAT MOVES WITH THE PLAYER.
     *
     * This is the case that matters, and the one route 4 would have failed.
     * The decoy changes on exactly the same frames as the pad -- perfectly
     * correlated with the player's input -- but does not satisfy the algebra.
     * A filter based on "changes when the user presses something" accepts it;
     * only the invariant rejects it.
     *
     * Sabotage check: making consistent() return TRUE unconditionally leaves
     * cases 1 and 2 PASSING and fails only this one.
     * ------------------------------------------------------------------ */
    volatile unsigned *decoy = (unsigned *)VirtualAlloc(
        NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!decoy) { printf("alloc failed\n"); return 1; }
    /* Shaped to survive plausible(): small values, five words, and it even
     * starts self-consistent (all zero) so it WILL be armed. */
    memset((void *)decoy, 0, 5 * sizeof(unsigned));

    clear_log();
    pad_scan_reset();
    pad_step(pad, PAD_RDOWN);        /* live press so arming can see it */
    decoy[0] = 1; decoy[1] = 1; decoy[2] = 0; decoy[3] = 1; decoy[4] = 1;
    pad_scan_arm();
    unsigned decoy_addr_found = 0;
    for (unsigned i = 0; i < 24; i++) {
        unsigned cur = (i & 1) ? 0u : PAD_RDOWN;
        pad_step(pad, cur);
        /* the decoy ticks on the very same frames, like a counter driven by
         * input, but its words are unrelated to each other */
        decoy[0] = i; decoy[1] = i * 3; decoy[2] = i + 7;
        decoy[3] = i ^ 0x5; decoy[4] = i;
        pad_scan_tick();
    }
    (void)decoy_addr_found;
    clear_log();
    pad_scan_report();
    /* The decoy must NOT appear among confirmed sites. Assert on the address
     * rather than on a count, so the check cannot be satisfied by luck. */
    /* ⚠⚠ Ask the module directly rather than scraping the log. The report
     * prints only the first handful of survivors, so when consistent() is
     * sabotaged and SIX THOUSAND sites confirm, the decoy is accepted but
     * simply does not appear in the printed lines -- and a log-scraping
     * assertion PASSES while the mechanism is completely broken. Verified:
     * with the algebra disabled this check fails and the log-scraping one
     * did not. */
    int decoy_confirmed = 0;
    for (int off = 0; off <= 16; off += 4)
        if (pad_is_confirmed((uintptr_t)decoy + off)) { decoy_confirmed = 1; break; }
    check(!decoy_confirmed,
          "case 3: input-correlated DECOY is rejected (the route-4 trap)");
    check(logged("confirmed"), "case 3: the real pad is still confirmed");

    /* ------------------------------------------------------------------
     * Case 4: ⚠ an all-zero region must NOT confirm anything.
     *
     * When nothing is pressed every word is zero and the algebra holds
     * trivially, and this process contains millions of zero dwords. If quiet
     * frames counted as evidence, the scanner would "confirm" arbitrary
     * memory. eventful() is what prevents that.
     *
     * Sabotage check: making eventful() return TRUE unconditionally makes this
     * case fail while cases 1-3 still pass.
     * ------------------------------------------------------------------ */
    clear_log();
    pad_scan_reset();
    memset((void *)pad, 0, sizeof *pad);
    memset((void *)decoy, 0, 5 * sizeof(unsigned));
    /* ⚠ Nothing is held here ON PURPOSE: arming on quiet memory must yield
     * nothing to confirm. Note this also exercises the "no candidates" path. */
    pad_scan_arm();
    for (unsigned i = 0; i < 40; i++)   /* many frames, nothing pressed */
        pad_scan_tick();
    /* ⚠⚠ ASSERT ON THE PLANTED PAD, NOT ON THE WHOLE PROCESS.
     *
     * The first version of this case asserted that NOTHING confirms while
     * nothing is pressed. That is not achievable inside a live test harness:
     * this process's own CRT buffers and stale locals hold pad-shaped words
     * that keep changing, and they confirm legitimately. The check failed for
     * a reason that was never a defect in the scanner.
     *
     * What IS verifiable, and is the real claim, is that a QUIET struct earns
     * no evidence: the planted pad is untouched for 40 frames, so no window on
     * it may appear among the confirmed sites. That is exactly the property
     * that stops the all-zero trivial case from manufacturing confirmations.
     *
     * Sabotage check: making eventful() return TRUE unconditionally makes the
     * quiet pad start accumulating agreement and fails this case, while cases
     * 1-3 still pass. */
    clear_log();
    pad_scan_report();
    int quiet_pad_confirmed = 0;
    for (int off = 0; off <= 16; off += 4)
        if (pad_is_confirmed((uintptr_t)pad + off)) { quiet_pad_confirmed = 1; break; }
    check(!quiet_pad_confirmed,
          "case 4: an untouched struct earns NO evidence (all-zero is trivial)");

    /* ------------------------------------------------------------------
     * Case 5: the direction decode -- including LEFT/RIGHT for settings.
     *
     * Menu navigation is up/down and settings adjustment is left/right, and
     * both are the same word. A decoder that only handled the vertical axis
     * would silently make the mod useless on every options screen.
     * ------------------------------------------------------------------ */
    clear_log();
    pad_scan_reset();
    memset((void *)pad, 0, sizeof *pad);
    pad_step(pad, PAD_RDOWN);
    pad_scan_arm();
    for (unsigned i = 0; i < 14; i++) {
        pad_step(pad, (i & 1) ? 0u : PAD_RDOWN);
        pad_scan_tick();
    }
    /* ⚠ Pin the decoder to the PLANTED pad. Without this the case tests the
     * ranking and the decoder at once: the representative may be some mirror
     * in the harness's own memory that never sees these presses, and the case
     * fails intermittently for a reason unrelated to decoding. */
    pad_debug_pin((uintptr_t)pad);

    g_nsaid = 0;
    pad_step(pad, 0);            /* release first: clear the previous dir */
    pad_scan_tick();
    pad_step(pad, PAD_RLEFT);
    pad_scan_tick();
    pad_say_state();
    int said_left = 0;
    for (int i = 0; i < g_nsaid; i++) if (strstr(g_said[i], "left")) said_left = 1;
    check(said_left, "case 5: LEFT is decoded and spoken (settings axis)");

    g_nsaid = 0;
    pad_step(pad, 0);            /* release first: clear the previous dir */
    pad_scan_tick();
    pad_step(pad, PAD_LRIGHT);
    pad_scan_tick();
    pad_say_state();
    int said_right = 0;
    for (int i = 0; i < g_nsaid; i++) if (strstr(g_said[i], "right")) said_right = 1;
    check(said_right, "case 5: RIGHT via the STICK bit is decoded too");

    g_nsaid = 0;
    pad_step(pad, 0);            /* release first: clear the previous dir */
    pad_scan_tick();
    pad_step(pad, PAD_RDOWN);
    pad_scan_tick();
    pad_say_state();
    int said_down = 0;
    for (int i = 0; i < g_nsaid; i++) if (strstr(g_said[i], "down")) said_down = 1;
    check(said_down, "case 5: DOWN is decoded (navigation axis)");

    /* ------------------------------------------------------------------
     * Case 6: ⭐⭐ decoding from a SHIFTED window.
     *
     * The scanner legitimately arms a window that starts a word or two into
     * the record (several button words are equal after a single press, so more
     * than one starting offset satisfies the algebra). A decoder that reads
     * fixed offsets from that base names the wrong word.
     *
     * Case 5 pins at +0, where fixed offsets happen to work, so it CANNOT see
     * this bug -- verified: replacing the union read with `w[OFF_REP] |
     * w[OFF_TRG]` left the whole suite green. This case pins at +4 and fails
     * under exactly that sabotage.
     * ------------------------------------------------------------------ */
    pad_debug_pin((uintptr_t)pad + 4);
    g_nsaid = 0;
    pad_step(pad, 0);
    pad_scan_tick();
    pad_step(pad, PAD_RLEFT);
    pad_scan_tick();
    pad_say_state();
    int shifted_left = 0;
    for (int i = 0; i < g_nsaid; i++)
        if (strstr(g_said[i], "left")) shifted_left = 1;
    check(shifted_left,
          "case 6: direction decodes correctly from a SHIFTED window");

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "OK",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
