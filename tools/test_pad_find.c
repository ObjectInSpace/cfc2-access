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

#define DIRS (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)

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

/* Hold one direction for several samples, agreeing with the keyboard each
 * time -- how a real record earns its identification. */
static void hold(volatile sim_pad *pad, unsigned dir, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        pad_step(pad, dir);
        pad_scan_tick(dir & DIRS);
    }
}

/* Drive a sequence of presses, sampling after each.
 *
 * The second argument to pad_scan_tick is GROUND TRUTH -- what the player is
 * holding. Here the simulated pad and the simulated player agree by
 * construction, which is exactly what a real pad does and what a decoy
 * cannot. */
static void drive(volatile sim_pad *pad, const unsigned *seq, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        pad_step(pad, seq[i]);
        pad_scan_tick(seq[i] & DIRS);
    }
}

int main(void)
{
    printf("== pad_find self-test ==\n");

    /* ⚠⚠ PLANT REALISTIC NOISE, or the seed filters cannot be tested.
     *
     * This harness's own memory is small and mostly zeros, so the seed-time
     * invariant rejects almost nothing here: measured 5-13 candidates with it
     * and 8 without. A threshold on that count would be calibrated against
     * nothing. The game seeds ~1800 out of 1.4 GB, and the filter is what
     * keeps that from being millions.
     *
     * So plant a megabyte of plausible-looking non-pad data -- small integers
     * and counters, the shapes plausible() admits -- and require the invariant
     * to reject the overwhelming majority of it. */
    {
        unsigned *noise = (unsigned *)VirtualAlloc(
            NULL, 1u << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (noise) {
            /* ⚠ Purely RANDOM data yields ZERO consistent windows -- measured
             * -- so it cannot calibrate anything. The invariant is much
             * stronger than the earlier "45 million matches" figure suggested;
             * that number came from ZERO-FILLED memory, not realistic data.
             *
             * What actually survives is the degenerate family: a value and its
             * shadow copy followed by padding (On == Old forces
             * Trg = Rel = Chg = 0). Plant those densely, since they are what
             * the seed filters must contend with in the game. */
            unsigned x = 12345u;
            for (unsigned i = 0; i + 8 < (1u << 20) / 4u; i += 8) {
                x = x * 1103515245u + 12345u;
                unsigned v = (x >> 16) & 0x0FFFu;   /* small, top byte clear */
                noise[i + 0] = v;                   /* On                    */
                noise[i + 1] = v;                   /* Old == On             */
                noise[i + 2] = 0;                   /* Trg                   */
                noise[i + 3] = 0;                   /* Rel                   */
                noise[i + 4] = 0;                   /* Chg                   */
                noise[i + 5] = noise[i + 6] = noise[i + 7] = 0;
            }
        }
    }

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
    /* ⚠ Assert the candidate COUNT, not just that the sweep ran.
     *
     * The seed filters exist solely to control this number and their effect
     * appears in no pass/fail outcome -- sabotaging the seed-time invariant
     * left the entire suite green while the candidate set grew without bound.
     * In the game the filters take 1.4 GB down to ~1800 candidates; a harness
     * that never looks at the count cannot distinguish that from millions.
     * This process is far smaller, so the bar is correspondingly low. */
    /* ⚠ NO threshold on the candidate count, deliberately.
     *
     * I tried to assert one and it was wrong twice over. Random data yields
     * ZERO consistent windows (measured), so it calibrates nothing; and the
     * shadow-copy family planted above yields ~6300 from 2.3 MB, which the
     * SEED filters do not reduce at all -- only the ground-truth comparison
     * eliminates those. So "the seed filters keep the set small" is simply
     * not a property this code has, and asserting it would have been a
     * threshold tuned until it passed.
     *
     * What the seed filters actually do is reject all-zero windows, which
     * case 1's cap check already covers. The candidate count is reported in
     * the log for a human to notice, not asserted. */
    check(pad_seed_count() > 0,
          "case 1: the sweep produced candidates to work with");

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
        pad_scan_tick(seq[i] & DIRS);
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
        pad_scan_tick(0);
    /* ⚠ With the shadow-copy noise planted, thousands of candidates exist and
     * agree with "nothing held" trivially. The claim that matters is that
     * nothing is IDENTIFIED without real input, which needs agreements across
     * held directions too. */
    check(pad_record() == 0,
          "case 4: quiet frames alone never identify a record");
    clear();
    pad_scan_report();
    check(logged("no candidate has agreed with the keyboard"),
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
    /* Exactly ONE agreement while a direction is held. With thousands of
     * shadow candidates present, identifying on one would pick among them
     * arbitrarily -- so one must not be enough. */
    pad_step(pad, PAD_RDOWN);
    pad_scan_tick(PAD_DOWN);
    check(pad_record() == 0,
          "case 5: ONE held agreement does not identify a record");
    drive(pad, seq, nseq);
    check(pad_record() == (uintptr_t)pad,
          "case 5: further transitions then resolve it to the pad");

    /* ------------------------------------------------------------------
     * Case 7: ⛔⛔ THE SHADOW-COPY DECOY -- the one that reached the user.
     *
     * The three equations are trivially satisfied whenever On == Old, because
     * that forces Trg = Rel = Chg = 0. So ANY pair of equal words followed by
     * three zeros passes: a value plus its shadow copy, then padding. That
     * shape is everywhere in a real process.
     *
     * In-game this matched a site whose value was 0xFF and the readout said
     * "up down left right" -- all four directions at once, which no physical
     * d-pad can report. The whole test suite was green at the time; nothing
     * here modelled the degenerate family.
     *
     * Two defences, and this case requires BOTH:
     *   - opposite directions cannot be co-held (rejects the value outright)
     *   - a transition must set Trg or Rel (a shadow pair never does)
     *
     * Sabotage: removing sane_dirs() from plausible(), OR removing the
     * Trg/Rel event requirement, fails this case.
     * ------------------------------------------------------------------ */
    clear();
    memset((void *)pad, 0, sizeof *pad);
    /* ⚠⚠ THE DECOY MUST EXIST BEFORE SEEDING. Candidates are only created by
     * the sweep, so anything written afterwards is never a candidate at all --
     * the first three versions of this case wrote the decoy after
     * seed_while_pressed() and therefore tested NOTHING, passing happily with
     * the defence removed. Verified by tracing: only one site was ever
     * evaluated. */
    decoy[0] = decoy[1] = 0x1000u;      /* shadow pair, sane, nonzero */
    decoy[2] = decoy[3] = decoy[4] = 0;
    seed_while_pressed(pad);

    /* Head start: both sites otherwise reach the threshold on the same tick,
     * where the tie is broken by allocation order -- so the pad could win by
     * luck rather than by the decoy being rejected. */
    {
        static const unsigned lb0[] = { 0x00, 0x04, 0x00, 0x40 };
        for (unsigned k = 0; k < 12; k++) {
            decoy[0] = decoy[1] = 0x1000u | lb0[k % 4];
            decoy[2] = decoy[3] = decoy[4] = 0;
            /* ⚠ The player holds DOWN throughout, as a real one would. The
             * decoy's value wanders independently, so it agrees only by
             * accident and is killed the first time it does not.
             *
             * An earlier version fed the decoy its OWN value as ground truth,
             * letting it "agree" every tick. That models a player whose key
             * presses mirror an internal counter -- impossible, and it made
             * the case fail for a reason that could never occur in the game. */
            pad_step(pad, PAD_RDOWN);
            pad_scan_tick(PAD_DOWN);
        }
    }
    for (unsigned i = 0; i < 40; i++) {
        pad_step(pad, (i & 1) ? 0u : PAD_RDOWN);
        static const unsigned lowbytes[] = { 0x00, 0x04, 0x00, 0x40,
                                             0x00, 0x02, 0x00, 0x08 };
        unsigned shadow = 0x1000u | lowbytes[i % 8];
        decoy[0] = decoy[1] = shadow;
        decoy[2] = decoy[3] = decoy[4] = 0;
        /* mostly held, with occasional release -- only held samples count
         * toward identification, so the pad needs enough of them */
        pad_scan_tick(((i % 4 < 3) ? PAD_DOWN : 0u));
    }
    int shadow_hit = 0;
    for (int off = 0; off <= 16; off += 4)
        if (pad_record() == (uintptr_t)decoy + off) shadow_hit = 1;
    check(!shadow_hit,
          "case 7: SHADOW-COPY decoy (On==Old, value 0xFF) is rejected");
    check(pad_record() == (uintptr_t)pad,
          "case 7: the real pad is identified instead");
    /* ⚠ Leave no shadow pair behind: later cases seed fresh, and a decoy that
     * survives into them would dominate and mask what THEY test. Sabotage of
     * the transition guard and of the kill-on-violation rule stopped being
     * caught when this case was added without the cleanup. */
    memset((void *)decoy, 0, 12 * sizeof(unsigned));

    /* ------------------------------------------------------------------
     * Case 9: a site that VIOLATES the invariant during a real event is
     * killed, not merely skipped.
     *
     * The Trg/Rel guard means most junk never gets evaluated, so the kill rule
     * needs a decoy built to reach evaluation and then fail: it sets Trg (so
     * it counts as a real event) but its other words do not satisfy the
     * equations. Without the kill it survives, keeps accumulating, and can
     * out-score the pad.
     *
     * Sabotage: `if (!consistent(w))` -> `if (0)` fails this case. It is the
     * only case that does, now that the Trg/Rel guard filters the rest.
     * ------------------------------------------------------------------ */
    clear();
    memset((void *)pad, 0, sizeof *pad);
    memset((void *)decoy, 0, 12 * sizeof(unsigned));
    /* seeded consistent-and-nonzero, so it becomes a candidate */
    decoy[0] = 0x04; decoy[1] = 0x00; decoy[2] = 0x04;
    decoy[3] = 0x00; decoy[4] = 0x04;
    seed_while_pressed(pad);
    for (unsigned i = 0; i < 40; i++) {
        pad_step(pad, (i & 1) ? 0u : PAD_RDOWN);
        /* a real "event" (Trg set) whose algebra is WRONG */
        decoy[0] = 0x04 + (i & 3); decoy[1] = 0x40;
        decoy[2] = 0x04;                    /* Trg nonzero -> evaluated */
        decoy[3] = 0x11; decoy[4] = 0x77;   /* inconsistent with On/Old */
        pad_scan_tick(((i & 1) ? 0u : PAD_RDOWN) & DIRS);
    }
    int viol_hit = 0;
    for (int off = 0; off <= 16; off += 4)
        if (pad_record() == (uintptr_t)decoy + off) viol_hit = 1;
    check(!viol_hit,
          "case 9: a site violating the invariant on a real event is KILLED");
    memset((void *)decoy, 0, 12 * sizeof(unsigned));

    /* ------------------------------------------------------------------
     * Case 10: ⭐⭐ THE ALTERNATING-BOOLEAN DECOY -- the second one that
     * reached the user, and the case that justifies ground truth at all.
     *
     * A state machine keeping `flag, prev, set, cleared, changed` satisfies
     * the invariant perfectly, is direction-sane (bit 0 only), and sets Trg or
     * Rel on every change. In-game 480 such sites qualified at once, 300 ms
     * after startup, and the readout said "no direction" because half the
     * samples land on zero. NO predicate over the five words can reject it --
     * as data it is indistinguishable from a pad.
     *
     * What it cannot do is agree with the KEYBOARD. Here the player holds Down
     * throughout while the decoy toggles on its own schedule, so it disagrees
     * almost immediately.
     *
     * Sabotage: removing the dir_set comparison in pad_scan_tick fails this
     * case. It is the only case that catches it -- every shape-based check
     * passes this decoy by design.
     * ------------------------------------------------------------------ */
    clear();
    memset((void *)pad, 0, sizeof *pad);
    memset((void *)decoy, 0, 12 * sizeof(unsigned));
    /* a perfectly-formed alternating boolean quintet, seeded nonzero */
    decoy[0] = 1; decoy[1] = 0; decoy[2] = 1; decoy[3] = 0; decoy[4] = 1;
    seed_while_pressed(pad);
    for (unsigned i = 0; i < 40; i++) {
        unsigned on = i & 1, prev = !on;
        decoy[0] = on;                 /* flag */
        decoy[1] = prev;               /* previous */
        decoy[2] = on & ~prev;         /* "set this tick"   */
        decoy[3] = prev & ~on;         /* "cleared"         */
        decoy[4] = on ^ prev;          /* "changed"         */
        /* ⚠ The player presses AND RELEASES, as a real one does. Holding a
         * direction forever means the pad never demonstrates a release, and
         * identification now requires both -- that is what stops a site frozen
         * at one value from qualifying. */
        unsigned held = (i % 4 < 2) ? PAD_RDOWN : 0u;
        pad_step(pad, held);
        pad_scan_tick(held ? PAD_DOWN : 0u);
    }
    int boolean_hit = 0;
    for (int off = 0; off <= 16; off += 4)
        if (pad_record() == (uintptr_t)decoy + off) boolean_hit = 1;
    check(!boolean_hit,
          "case 10: ALTERNATING-BOOLEAN decoy is rejected (needs ground truth)");
    check(pad_record() == (uintptr_t)pad,
          "case 10: the real pad is identified instead");
    memset((void *)decoy, 0, 12 * sizeof(unsigned));

    /* ------------------------------------------------------------------
     * Case 11: ⭐⭐ THE FROZEN SITE -- the third false positive to reach the
     * user ("it says down no matter what").
     *
     * A site permanently holding the Down value agrees with every sample taken
     * WHILE Down is held. An earlier version skipped the release samples on
     * the grounds that quiet agreement is trivial, which meant such a site was
     * never once asked "and now that the key is up?" -- the single observation
     * that disproves it.
     *
     * Release is evidence. This case holds Down, releases, and requires the
     * frozen decoy to be rejected.
     *
     * Sabotage: restoring `if (!player_dirs) return;` at the top of
     * pad_scan_tick fails this case.
     * ------------------------------------------------------------------ */
    clear();
    memset((void *)pad, 0, sizeof *pad);
    memset((void *)decoy, 0, 12 * sizeof(unsigned));
    /* frozen at a valid, self-consistent "Down is held" state */
    decoy[0] = PAD_RDOWN; decoy[1] = 0; decoy[2] = PAD_RDOWN;
    decoy[3] = 0;         decoy[4] = PAD_RDOWN;
    seed_while_pressed(pad);
    for (unsigned i = 0; i < 40; i++) {
        unsigned held = (i % 4 < 3) ? PAD_RDOWN : 0u;   /* mostly held */
        pad_step(pad, held);
        /* the decoy never changes, whatever the player does */
        decoy[0] = PAD_RDOWN; decoy[1] = 0; decoy[2] = PAD_RDOWN;
        decoy[3] = 0;         decoy[4] = PAD_RDOWN;
        pad_scan_tick(held ? PAD_DOWN : 0u);
    }
    /* ⚠ Assert the decoy is DEAD, not merely that it lost. It can lose a tie
     * on allocation order while still alive, which made this case blind to
     * the release-sample sabotage it exists to catch. */
    int frozen_alive = 0;
    for (int off = 0; off <= 16; off += 4)
        if (pad_is_alive((uintptr_t)decoy + off)) {
            frozen_alive = 1;
        }
    check(!frozen_alive,
          "case 11: a site FROZEN at the Down value is KILLED by the release");
    check(pad_record() == (uintptr_t)pad,
          "case 11: the real pad is identified instead");
    memset((void *)decoy, 0, 12 * sizeof(unsigned));

    /* ------------------------------------------------------------------
     * Case 8: a readout must never report opposing directions.
     *
     * The user-visible symptom of the bug above. Even if some site were
     * identified, speaking "up down left right" is self-evidently wrong and
     * must be impossible by construction.
     * ------------------------------------------------------------------ */
    {
        unsigned bad[] = { 0xFFu, 0x77u, 0x0Fu, 0x55u, 0xAAu };
        int any_opposed = 0;
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            unsigned v = bad[i];
            int up = (v & PAD_UP) != 0, dn = (v & PAD_DOWN) != 0;
            int lf = (v & PAD_LEFT) != 0, rt = (v & PAD_RIGHT) != 0;
            /* these values are exactly what plausible() must refuse */
            if ((up && dn) || (lf && rt)) {
                unsigned w[PAD_WORDS] = { v, v, 0, 0, 0 };
                /* a window carrying this value must not be seedable */
                if (pad_window_acceptable(w)) any_opposed = 1;
            }
        }
        check(!any_opposed,
              "case 8: windows holding opposing directions are never accepted");
    }

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
        pad_scan_tick(0);
        pad_step(pad, cases[i].bits);
        pad_scan_tick(cases[i].bits & DIRS);
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
