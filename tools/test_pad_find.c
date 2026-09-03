/*
 * Offline self-test for the pad-record finder (route 5).
 *
 * ⭐⭐ A trap must be PROVEN to fail when the thing it guards is sabotaged, or
 * it is not a test. Every case here was verified that way; the notes say which
 * sabotage each one catches. This project has repeatedly paid for tests that
 * stayed green while the mechanism under test was broken.
 *
 * The false positive this route must not repeat: route 4 found a monotonic
 * allocation counter that satisfied every filter it had -- stepped by a
 * constant, reversed, quiet in the control -- and represented nothing. The
 * defence is that the pad is identified by an INVARIANT the engine maintains,
 * not by correlation with the player's input. So the case that matters is
 * case 3: a decoy changing on exactly the same frames as the pad must still be
 * rejected.
 *
 * Build+run:  see tools/run_pad_test.ps1
 */

#include <stdio.h>
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
    if (g_nsaid < 64)
        WideCharToMultiByte(CP_UTF8, 0, s, -1, g_said[g_nsaid++], 256,
                            NULL, NULL);
}

static int g_fail;
static void check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) g_fail++;
}
static int said(const char *needle)
{
    for (int i = 0; i < g_nsaid; i++)
        if (strstr(g_said[i], needle)) return 1;
    return 0;
}
static int logged(const char *needle)
{
    for (int i = 0; i < g_nlog; i++)
        if (strstr(g_log[i], needle)) return 1;
    return 0;
}
static void clear(void) { g_nlog = 0; g_nsaid = 0; }

/* --- a simulated pad ------------------------------------------------------
 * Maintains the engine's own invariant, so the planted struct is a faithful
 * target rather than something shaped to match whatever the scanner looks for.
 * Field order is the real one: On Old Trg Rel Chg Rep, then the analog axes. */
typedef struct {
    unsigned on, old, trg, rel, chg, rep;
    unsigned rx, ry, rz, lx, ly, lz;
} sim_pad;

static void pad_step(volatile sim_pad *p, unsigned cur)
{
    unsigned prev = p->on;
    p->old = prev;
    p->on  = cur;
    p->trg = cur & ~prev;
    p->rel = prev & ~cur;
    p->chg = cur ^ prev;
    p->rep = cur;
}

/* Seed the way the game does: a candidate only exists while a button is DOWN
 * (the all-zero window is rejected), so put the pad in a pressed state first.
 * In the game pad_scan_poll() re-seeds until this happens by itself. */
static void seed_while_pressed(volatile sim_pad *pad)
{
    pad_step(pad, PAD_RDOWN);
    pad_scan_start();
}

/* Drive a sequence of transitions, sampling after each. */
static void drive(volatile sim_pad *pad, const unsigned *seq, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        pad_step(pad, seq[i]);
        pad_scan_tick();
    }
}

int main(void)
{
    printf("== pad_find self-test ==\n");

    volatile sim_pad *pad = (sim_pad *)VirtualAlloc(
        NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    volatile unsigned *decoy = (unsigned *)VirtualAlloc(
        NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pad || !decoy) { printf("alloc failed\n"); return 1; }
    memset((void *)pad, 0, sizeof *pad);
    memset((void *)decoy, 0, 12 * sizeof(unsigned));

    /* Several DIFFERENT transitions -- that is what collapses the shifted
     * -window coincidences to a single offset. */
    static const unsigned seq[] = {
        PAD_RDOWN, 0, PAD_LDOWN, PAD_LDOWN | PAD_RLEFT, PAD_RRIGHT, 0,
        PAD_RUP, PAD_LLEFT, 0, PAD_RDOWN, 0,
    };
    const unsigned nseq = sizeof seq / sizeof seq[0];

    /* ------------------------------------------------------------------
     * Case 1: the sweep runs and reports its COVERAGE.
     * Catches: a scan that silently covers nothing (the "0.0 MB" failure,
     * where a full candidate table looked like success).
     * ------------------------------------------------------------------ */
    clear();
    pad_scan_start();
    check(g_nlog > 0, "case 1: sweep runs and logs");
    check(logged("MB in"),
          "case 1: coverage is reported, not just a candidate count");
    /* ⛔ The cap must NOT bind. When it does, the sweep covered only the first
     * few megabytes and any later "not found" is meaningless -- and every
     * other assertion here can still pass, so without this check a scanner
     * that accepts all-zero windows (tens of millions of matches) looks
     * completely healthy. Verified: removing the all-zero rejection fails
     * ONLY this case. */
    check(!logged("cap reached"),
          "case 1: candidate cap does NOT bind (coverage is complete)");

    /* ------------------------------------------------------------------
     * Case 2: the planted pad is IDENTIFIED, at the right address.
     *
     * The positive control. Asserting the ADDRESS is what makes it one --
     * "something was identified" would pass while the scanner locked onto
     * unrelated memory.
     *
     * ⚠ Specifically +0, not merely overlapping: the six-word window and the
     * multi-transition requirement exist to eliminate shifted windows, so
     * accepting +4 here would hide exactly the failure they guard against.
     * ------------------------------------------------------------------ */
    clear();
    seed_while_pressed(pad);
    drive(pad, seq, nseq);
    check(pad_record() == (uintptr_t)pad,
          "case 2: the identified record IS the planted pad, at +0");

    /* ------------------------------------------------------------------
     * Case 3: ⭐⭐ THE DECOY THAT MOVES WITH THE PLAYER.
     *
     * Changes on exactly the same frames as the pad -- perfectly correlated
     * with input -- but does not satisfy the invariant. A filter based on
     * "changes when the user presses something" accepts it; only the algebra
     * rejects it. This is the route-4 trap.
     *
     * Sabotage: forcing consistent() to return TRUE fails this case.
     * ------------------------------------------------------------------ */
    clear();
    seed_while_pressed(pad);
    for (unsigned i = 0; i < nseq; i++) {
        pad_step(pad, seq[i]);
        decoy[0] = i + 1; decoy[1] = i * 3; decoy[2] = i + 7;
        decoy[3] = i ^ 5; decoy[4] = i * 2;
        pad_scan_tick();
    }
    int decoy_hit = 0;
    for (int off = 0; off <= 16; off += 4)
        if (pad_record() == (uintptr_t)decoy + off) decoy_hit = 1;
    check(!decoy_hit,
          "case 3: input-correlated DECOY is rejected (the route-4 trap)");
    check(pad_record() == (uintptr_t)pad,
          "case 3: the real pad is still identified");

    /* ------------------------------------------------------------------
     * Case 4: ⚠ quiet frames are not evidence.
     *
     * With nothing pressed every word is zero and the invariant holds
     * trivially, and this process holds millions of zero dwords. If quiet
     * samples counted, the scanner would "identify" arbitrary memory.
     *
     * Sabotage: removing the `w[W_ON] != s->last_on` guard fails this case.
     * ------------------------------------------------------------------ */
    clear();
    memset((void *)pad, 0, sizeof *pad);
    pad_scan_start();
    for (int i = 0; i < 60; i++)      /* many frames, nothing pressed */
        pad_scan_tick();
    check(pad_record() == 0,
          "case 4: quiet frames alone never identify a record");
    clear();
    pad_scan_report();
    check(logged("no candidate has seen a transition"),
          "case 4: report SAYS why, so it cannot read as a clean negative");

    /* ------------------------------------------------------------------
     * Case 5: ONE transition is not enough.
     *
     * Measured: after a single press, offsets +0, +4 and +16 all satisfy the
     * invariant. Identifying on one event would pick among them arbitrarily.
     *
     * Sabotage: setting PAD_CONFIRM_EVENTS to 1 fails this case.
     * ------------------------------------------------------------------ */
    clear();
    memset((void *)pad, 0, sizeof *pad);
    /* ⚠ Seed while pressed, then tick ONCE with no further change, so exactly
     * ONE transition has been witnessed.
     *
     * Getting this count right took two attempts. Seeding does NOT count as a
     * transition (last_on is initialised from the seed sample), and driving
     * the full sequence gives many -- either way PAD_CONFIRM_EVENTS=1 sabotage
     * still passed and the case tested nothing. Exactly ONE transition after
     * seeding is the boundary that matters. */
    seed_while_pressed(pad);
    pad_step(pad, 0);            /* ONE real transition after seeding */
    pad_scan_tick();
    check(pad_record() == 0,
          "case 5: a single transition does NOT identify (shift ambiguity)");
    drive(pad, seq, nseq);
    check(pad_record() == (uintptr_t)pad,
          "case 5: further transitions then resolve it to the pad");

    /* ------------------------------------------------------------------
     * Case 6: all four directions decode, including the settings axis.
     *
     * Menu navigation is up/down and settings adjustment is left/right, read
     * from the same word. A decoder handling only the vertical axis would
     * leave the mod silent on every options screen.
     * ------------------------------------------------------------------ */
    static const struct { unsigned bits; const char *word; } cases[] = {
        { PAD_RDOWN,  "down"  },
        { PAD_RUP,    "up"    },
        { PAD_RLEFT,  "left"  },
        { PAD_LRIGHT, "right" },   /* via the STICK bit, not the d-pad */
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        g_nsaid = 0;
        pad_step(pad, 0);
        pad_scan_tick();
        pad_step(pad, cases[i].bits);
        pad_scan_tick();
        pad_say_state();
        char what[80];
        snprintf(what, sizeof what, "case 6: %s is decoded and spoken",
                 cases[i].word);
        check(said(cases[i].word), what);
    }

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "OK",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
