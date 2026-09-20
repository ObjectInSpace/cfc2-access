/*
 * Offline test for the gauge mixer. Drives audio.c through its render seam,
 * so the DSP is checked with no sound card and no game.
 *
 * Every case asserts on SAMPLES, not on a flag: "the code took the silent
 * branch" is not evidence that the output was silent. Each mechanism below was
 * sabotaged and seen to fail the case that tests it (noted per case).
 *
 * ⛔⛔ CASES LEAK INTO EACH OTHER THROUGH THE FADE, and it cost four rounds
 * on the volume cases (15-19). Three ways it bites, all measured here:
 *
 *  1. A voice muted mid-tone FADES over ~7 blocks rather than cutting (by
 *     design -- a cut clicks). The next case's first blocks therefore contain
 *     the previous case's tail.
 *  2. A TICK decays over ~40 ms (7 blocks) independently of every gauge, so a
 *     case following a tick measures the tick.
 *  3. ⭐⭐ peak_over() returns the MAXIMUM over its window, so it answers
 *     "was anything loud during this window", NOT "did it end silent". An
 *     assertion that a fade completes must let the fade RUN and then measure,
 *     or it fails on the very tail it just proved should exist.
 *
 * ⭐ THE RULE: a case must explicitly silence every cue it does not own, then
 * drain, then assert that the drain WORKED before measuring anything. Waiting
 * alone is not draining -- it leaves the case at the mercy of whatever the
 * previous one happened to leave running.
 *
 * ⭐⭐ And the sharpest one: case 17b PASSED under the exact sabotage it
 * exists to catch, because a neighbouring gauge's fade satisfied its `l > 0`.
 * A positive assertion can be satisfied by the WRONG SOURCE, which makes it
 * look like evidence while testing nothing. Both directions of a separation
 * claim need asserting, and each needs a clean floor first.
 */
#include "../src/audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", (m)); fails++; } \
                         else printf("ok: %s\n", (m)); } while (0)

static int16_t blk[AUDIO_BLOCK * 2];

/* Peak absolute amplitude per channel over one rendered block. */
static void peaks(int *pl, int *pr)
{
    audio_render_block_for_test(blk);
    int l = 0, r = 0;
    for (int i = 0; i < AUDIO_BLOCK; i++) {
        int a = abs(blk[i * 2]), b = abs(blk[i * 2 + 1]);
        if (a > l) l = a;
        if (b > r) r = b;
    }
    *pl = l; *pr = r;
}

/* Peaks after letting the gain smoother settle.
 *
 * ⚠ The mixer ramps gain (fast attack, slow release) and only sounds for
 * HOLD_BLOCKS after a value CHANGE. So measuring one block right after a
 * change reads a tone still climbing out of silence, and a pan assertion
 * would fail for a reason that has nothing to do with pan. Render a few
 * blocks, keeping the gauge fed so the hold never lapses, then measure the
 * settled block. */
static void pan_peaks(int *pl, int *pr)
{
    for (int i = 0; i < 6; i++) {
        int l, r;
        peaks(&l, &r);
    }
    peaks(pl, pr);
}

/* Count envelope ONSETS over n blocks -- the beacon's pulse rate.
 *
 * ⚠ Zero-crossing pitch is the wrong instrument for a pluck: it measures the
 * inharmonic partials, not how often the pluck repeats. The beacons are told
 * apart by RATE, so that is what a test of them must measure. Counts rising
 * edges where the envelope crosses a fraction of its own peak. */
static int pulse_count(int nblocks)
{
    int count = 0, above = 0, peak = 1;
    /* First pass: find the peak so the threshold is relative, not absolute. */
    for (int b = 0; b < nblocks; b++) {
        audio_render_block_for_test(blk);
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            int a = abs(blk[i * 2]) + abs(blk[i * 2 + 1]);
            if (a > peak) peak = a;
        }
    }
    for (int b = 0; b < nblocks; b++) {
        audio_render_block_for_test(blk);
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            int a = abs(blk[i * 2]) + abs(blk[i * 2 + 1]);
            if (!above && a > peak / 2) { above = 1; count++; }
            else if (above && a < peak / 8) above = 0;
        }
    }
    return count;
}

/* Dominant frequency of a voice's TONE, by counting zero crossings over a
 * long window and dividing by elapsed time.
 *
 * ⛔⛔ THIS EXISTS BECAUSE EVERY OTHER MEASURE MISSED A SILENT BEACON.
 * The first pluck drove its partials from the PULSE ramp, putting them at
 * 9-49 Hz -- below hearing. Amplitude was correct, pulse count was correct,
 * pan was correct, 29 checks passed, and the user heard nothing. Nothing
 * asserted that the output was in the AUDIBLE BAND at all. */
static double tone_hz(int nblocks)
{
    int cross = 0;
    long samples = 0;
    int prev = 0, have_prev = 0;
    for (int b = 0; b < nblocks; b++) {
        audio_render_block_for_test(blk);
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            int v = blk[i * 2] + blk[i * 2 + 1];   /* mid: pan-independent */
            if (have_prev && ((prev < 0) != (v < 0))) cross++;
            prev = v; have_prev = 1;
            samples++;
        }
    }
    if (samples == 0) return 0.0;
    return (double)cross * (double)AUDIO_SR / (2.0 * (double)samples);
}

/* Render n blocks, returning the largest peak seen on either channel. */
static int peak_over(int n)
{
    int best = 0;
    for (int i = 0; i < n; i++) {
        int l, r; peaks(&l, &r);
        if (l > best) best = l;
        if (r > best) best = r;
    }
    return best;
}

/* Dominant frequency of the left channel, by zero-crossing count. Crude but
 * sufficient to tell "pitch went up" from "pitch went down". */
static double left_freq(void)
{
    audio_render_block_for_test(blk);
    int cross = 0;
    for (int i = 1; i < AUDIO_BLOCK; i++)
        if ((blk[(i - 1) * 2] < 0) != (blk[i * 2] < 0)) cross++;
    return (double)cross * (double)AUDIO_SR / (2.0 * (double)AUDIO_BLOCK);
}

int main(void)
{
    /* No audio_init(): we never open a device. The render seam must work
     * regardless, which is the point of having it. */

    /* --- 1. Silence by default ------------------------------------------
     * Sabotage: make update_voices() always set gain -> this fails. */
    CHECK(peak_over(8) == 0, "1. silent with no gauge activity");

    /* --- 2. First sighting is SILENT ------------------------------------
     * A value arriving with no history is not a change. Without the seed
     * rule every gauge would sound at round start.
     * Sabotage: drop the `seeded` branch -> this fails. */
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 5000, 10000);
    CHECK(peak_over(8) == 0, "2. first value seeds silently (no round-start blast)");

    /* --- 3. A change makes sound ----------------------------------------
     * Sabotage: never re-arm hold -> this fails. */
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 6000, 10000);
    CHECK(peak_over(4) > 0, "3. a changed value produces audio");

    /* --- 4. A settled gauge goes quiet again ----------------------------
     * Feed the SAME value repeatedly; hold must expire and the voice fade.
     * Sabotage: remove the hold countdown -> this never silences. */
    for (int i = 0; i < 40; i++) {
        audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 6000, 10000);
        int l, r; peaks(&l, &r);
    }
    CHECK(peak_over(10) == 0, "4. a static gauge falls silent");

    /* --- 5. Owner panning: P1 left, P2 right ----------------------------
     * The design fixes gauge pan to the HUD side, not to the fighter.
     * Sabotage: make pan 0 for both -> both these fail. */
    audio_gauge_reset_all();
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 1000, 10000);
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 2000, 10000);
    int l1 = 0, r1 = 0;
    for (int i = 0; i < 4; i++) { int l, r; peaks(&l, &r); if (l > l1) l1 = l; if (r > r1) r1 = r; }
    CHECK(l1 > r1 * 2, "5a. P1 gauge is dominantly LEFT");

    audio_gauge_reset_all();
    for (int i = 0; i < 30; i++) { int l, r; peaks(&l, &r); }   /* drain */
    audio_gauge_set(AUDIO_GAUGE_SUPER, 1, 1000, 10000);
    audio_gauge_set(AUDIO_GAUGE_SUPER, 1, 2000, 10000);
    int l2 = 0, r2 = 0;
    for (int i = 0; i < 4; i++) { int l, r; peaks(&l, &r); if (l > l2) l2 = l; if (r > r2) r2 = r; }
    CHECK(r2 > l2 * 2, "5b. P2 gauge is dominantly RIGHT");

    /* --- 6. Pitch rises with fill ---------------------------------------
     * Sabotage: invert the freq mapping -> this fails.
     * Both readings are taken while the voice is sounding, and the frequency
     * smoother is allowed to settle first. */
    audio_gauge_reset_all();
    for (int i = 0; i < 30; i++) { int l, r; peaks(&l, &r); }
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 500, 10000);
    for (int i = 0; i < 12; i++) { audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 500 + i, 10000); left_freq(); }
    double f_low = left_freq();

    for (int i = 0; i < 12; i++) { audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 9000 + i, 10000); left_freq(); }
    double f_high = left_freq();
    printf("   (low fill %.0f Hz, high fill %.0f Hz)\n", f_low, f_high);
    CHECK(f_high > f_low * 1.3, "6. pitch RISES as the gauge fills");

    /* --- 6b. HEALTH pitch FALLS as it depletes ---------------------------
     * ⭐ Case 6 only ever exercised SUPER, so nothing pinned the direction of
     * the gauge the user actually tracks under pressure. A user report that
     * "P1 health sounds like it rises as it depletes" could not be answered by
     * the suite -- it had to be re-derived by hand. Pin it.
     *
     * ⚠ Uses a REAL max (11520, Ryu at ratio 1) and walks health DOWN the way
     * damage does. Max health depends on character AND ratio, so this asserts
     * the DIRECTION, never an absolute frequency.
     *
     * Sabotage: invert the mapping to (f_hi - range*level) -> this fails while
     * case 6 still passes, because the two gauges move opposite ways. */
    audio_gauge_reset_all();
    for (int i = 0; i < 30; i++) { int l, r; peaks(&l, &r); }
    audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 11520, 11520);
    for (int i = 0; i < 12; i++) { audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 11520 - i, 11520); left_freq(); }
    double h_full = left_freq();

    for (int i = 0; i < 12; i++) { audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 600 - i, 11520); left_freq(); }
    double h_low = left_freq();
    printf("   (health full %.0f Hz, nearly dead %.0f Hz)\n", h_full, h_low);
    CHECK(h_full > h_low * 1.3, "6b. health pitch FALLS as health depletes");

    /* --- 7. Two gauges mix simultaneously -------------------------------
     * The reference implementation could not do this; it is the reason for a
     * voice table.
     *
     * ⛔ THE FIRST VERSION OF THIS CASE WAS A PROXY AND SURVIVED ITS OWN
     * SABOTAGE. It asserted `bl > 0 && br > 0`, but equal-power panning at
     * -0.85 still puts 0.118 of P1 into the RIGHT channel, so rendering P1
     * alone satisfied both halves. Rendering only voice 0 left it green.
     * The fix is to measure against a ONE-VOICE baseline: two voices must
     * make the quiet channel materially louder than one voice does. */
    audio_gauge_reset_all();
    for (int i = 0; i < 30; i++) { int l, r; peaks(&l, &r); }
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 1000, 10000);
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 2000, 10000);
    int solo_r = 0;
    for (int i = 0; i < 4; i++) { int l, r; peaks(&l, &r); (void)l; if (r > solo_r) solo_r = r; }

    audio_gauge_reset_all();
    for (int i = 0; i < 30; i++) { int l, r; peaks(&l, &r); }
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 1000, 10000);
    audio_gauge_set(AUDIO_GAUGE_SUPER, 1, 1000, 10000);
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 2000, 10000);
    audio_gauge_set(AUDIO_GAUGE_SUPER, 1, 2000, 10000);
    int bl = 0, br = 0;
    for (int i = 0; i < 4; i++) { int l, r; peaks(&l, &r); if (l > bl) bl = l; if (r > br) br = r; }
    printf("   (P1 alone right=%d, both right=%d)\n", solo_r, br);
    CHECK(bl > 0 && br > solo_r * 2,
          "7. both players sound at once (vs one-voice baseline)");

    /* --- 7b. Gauge TYPES are audibly distinct ---------------------------
     * Timbre identifies the gauge, so HEALTH must not sound like SUPER at the
     * same fill. health 150-420 Hz triangle, super 600-1500 Hz soft square.
     *
     * ⛔⛔ THE OLD THRESHOLD WAS 1.4x AND IT PASSED A PAIRING THE USER COULD
     * NOT TELL APART. The previous tones (health 180-540, super 330-990) sit
     * at a CONSTANT 1.83x at equal fill -- over the 1.4 bar, green test, and
     * confusable in practice, because 1.83 is near a perfect fifth so the two
     * fuse into one sound, and because the RANGES OVERLAPPED (330-540 shared):
     * full health and empty super were the same pitch.
     *
     * ⭐ So assert the property that actually matters -- the registers must not
     * overlap AT ALL -- not just a ratio at one convenient fill. This is the
     * "a test that passes the broken thing is not a test" lesson again: the
     * old case measured equal-fill ratio, which is the one configuration where
     * an overlapping pair still looks fine.
     *
     * Sabotage: give both the same f_lo/f_hi -> both assertions fail. */
    audio_gauge_reset_all();
    for (int i = 0; i < 30; i++) { int l, r; peaks(&l, &r); }
    audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 5000, 10000);
    for (int i = 0; i < 12; i++) { audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 5000 + i, 10000); left_freq(); }
    double f_health = left_freq();

    audio_gauge_reset_all();
    for (int i = 0; i < 30; i++) { int l, r; peaks(&l, &r); }
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 5000, 10000);
    for (int i = 0; i < 12; i++) { audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 5000 + i, 10000); left_freq(); }
    double f_super = left_freq();
    printf("   (health %.0f Hz, super %.0f Hz at equal fill)\n", f_health, f_super);
    CHECK(f_super > f_health * 2.5, "7b. health and super differ clearly at equal fill");

    /* ⭐ THE STRONGER CHECK: the worst case is not equal fill, it is health
     * FULL against super EMPTY -- the closest the two ranges ever come. If the
     * registers overlap, those two are the same pitch and pitch stops meaning
     * fill. Measure that corner directly. */
    audio_gauge_reset_all();
    for (int i = 0; i < 30; i++) { int l, r; peaks(&l, &r); }
    audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 9999, 10000);
    for (int i = 0; i < 12; i++) { audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 9988 + i, 10000); left_freq(); }
    double f_health_full = left_freq();

    audio_gauge_reset_all();
    for (int i = 0; i < 30; i++) { int l, r; peaks(&l, &r); }
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 1, 10000);
    for (int i = 0; i < 12; i++) { audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 1 + i, 10000); left_freq(); }
    double f_super_empty = left_freq();
    printf("   (worst case: health FULL %.0f Hz vs super EMPTY %.0f Hz)\n",
           f_health_full, f_super_empty);
    CHECK(f_super_empty > f_health_full * 1.15,
          "7b2. the gauge registers never overlap, even at their closest");

    /* --- 7c. A frame-rate feed stays SMOOTH ------------------------------
     * ⛔⛔ THE FIRST VERSION OF THIS CASE MEASURED ITS OWN QUANTISATION.
     * It asserted on left_freq(), which counts zero crossings over a 256-frame
     * block: resolution SR/(2*BLOCK) = 86.1 Hz. It reported "86.1 Hz jumps"
     * and I read that as real pitch stepping. It was ONE crossing -- the
     * smallest value the metric can express. A measurement cannot detect an
     * effect finer than its own resolution, and reporting it as a finding is
     * how I spent a round "fixing" a smoother that was never involved.
     *
     * What is worth asserting is that a value fed at frame rate produces a
     * CONTINUOUS waveform: no sample-to-sample discontinuities, which is what
     * a listener hears as crackle. Measure the largest single-sample jump in
     * the output and require it to stay below what the oscillator itself can
     * produce at the top of the health range.
     * Sabotage: reset v->phase on each feed -> this fails. */
    audio_gauge_reset_all();
    for (int i = 0; i < 40; i++) { int l, r; peaks(&l, &r); }
    audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 14400, 14400);
    {
        int worst = 0, hp = 14400, prev = 0, first = 1;
        for (int f = 0; f < 30; f++) {
            hp -= 300;
            audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, hp, 14400);
            for (int k = 0; k < 3; k++) {
                audio_render_block_for_test(blk);
                for (int i = 0; i < AUDIO_BLOCK; i++) {
                    int v = blk[i * 2];
                    if (!first) {
                        int d = v - prev; if (d < 0) d = -d;
                        if (d > worst) worst = d;
                    }
                    prev = v; first = 0;
                }
            }
        }
        /* ⛔⛔ AN ABSOLUTE JUMP THRESHOLD IS WAVEFORM-SPECIFIC, and that broke
         * this case when health became a SQUARE. The old limit (3000) was
         * derived for a triangle's gentle slope; a square jumps the full peak
         * -to-peak at every edge BY DESIGN -- predicted 9633, measured 9723.
         * That is the waveform, not a click, and "fixing" it would mean
         * removing the edges that make it a square.
         *
         * ⭐ Compare against the SAME waveform held STATIC instead. The
         * property under test is that FEEDING A VALUE adds no discontinuity;
         * whatever steps the oscillator makes on its own appear in both, so
         * they cancel. This is a control, not a tuned constant -- it stays
         * correct if the waveform changes again. */
        int base_worst = 0;
        {
            int bprev = 0, bfirst = 1;
            audio_gauge_reset_all();
            for (int i = 0; i < 40; i++) { int l, r; peaks(&l, &r); }
            /* Two feeds to start it sounding, then hold the value steady. */
            audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 7000, 14400);
            audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 6900, 14400);
            for (int f = 0; f < 20; f++) {
                audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 6900, 14400);
                audio_render_block_for_test(blk);
                for (int i = 0; i < AUDIO_BLOCK; i++) {
                    int v = blk[i * 2];
                    if (!bfirst) {
                        int d = v - bprev; if (d < 0) d = -d;
                        if (d > base_worst) base_worst = d;
                    }
                    bprev = v; bfirst = 0;
                }
            }
        }
        printf("   (worst jump: changing %d, static %d)\n", worst, base_worst);
        /* ⚠ HEADROOM SET BY SABOTAGE, not by taste. Correct code measures
         * EXACTLY the control (9723 vs 9723) because the feed adds nothing.
         * Resetting the phase on each feed -- a real click -- gives 12964,
         * i.e. 1.33x. A 1.5x allowance let that sabotage PASS, so the bound
         * is 1.15x: loose enough for sampling jitter, tight enough to catch
         * the discontinuity this case exists for. */
        CHECK(base_worst > 0 && worst < base_worst * 115 / 100,
              "7c. frame-rate feed adds no discontinuity beyond the waveform");
    }

    /* --- 7d. Clock ticks are one-shot and can OVERLAP --------------------
     * The user asked for onsets at the right moment and said overlap is fine.
     * A tick must therefore sound on its own, and a second tick must not be
     * stolen by the first.
     * Sabotage: make audio_tick() reuse one slot -> the overlap check fails. */
    audio_gauge_reset_all();
    for (int i = 0; i < 40; i++) { int l, r; peaks(&l, &r); }
    CHECK(peak_over(4) == 0, "7d-a. silent before any tick");

    audio_tick();
    CHECK(peak_over(2) > 0, "7d-b. a tick makes sound");

    /* It must DECAY, not sustain.
     * ⚠ peak_over(n) returns the MAXIMUM across n blocks, so asserting it is
     * zero right after a tick measures the ATTACK, not the decay -- the first
     * version of this case did exactly that and failed against correct code.
     * Let the tick ring out, THEN assert silence. */
    for (int i = 0; i < 12; i++) { int l, r; peaks(&l, &r); }   /* ~70 ms */
    CHECK(peak_over(8) == 0, "7d-c. a tick decays to silence");

    /* Two ticks close together: the second must still be audible after the
     * first has been running, i.e. they do not share one slot. */
    {
        audio_tick();
        for (int i = 0; i < 2; i++) { int l, r; peaks(&l, &r); }
        audio_tick();                       /* while the first still rings */
        int both = peak_over(2);
        for (int i = 0; i < 30; i++) { int l, r; peaks(&l, &r); }  /* drain */
        audio_tick();
        int single = peak_over(2);
        printf("   (one tick %d, overlapping two %d)\n", single, both);
        CHECK(both > single, "7d-d. overlapping ticks sum rather than steal");
    }
    for (int i = 0; i < 40; i++) { int l, r; peaks(&l, &r); }

    /* --- 8. Master toggle silences everything ---------------------------
     * Sabotage: ignore g_enabled in render() -> this fails. */
    audio_gauge_reset_all();
    for (int i = 0; i < 30; i++) { int l, r; peaks(&l, &r); }
    audio_toggle_enabled();              /* -> disabled */
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 1000, 10000);
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 5000, 10000);
    CHECK(peak_over(30) == 0, "8. master toggle silences cues");
    audio_toggle_enabled();              /* back on */

    /* --- 9. Reset does not sound ----------------------------------------
     * Round/character change must not read as a drain.
     * Sabotage: make reset keep `seeded` -> the next value counts as a change
     * and this fails. */
    audio_gauge_reset_all();
    for (int i = 0; i < 30; i++) { int l, r; peaks(&l, &r); }
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 9999, 10000);
    CHECK(peak_over(8) == 0, "9. reset then a new value is silent (no false drain)");

    /* --- 10. Bad input is ignored, not fatal ---------------------------- */
    audio_gauge_set(AUDIO_GAUGE_SUPER, 7, 1, 10);      /* bad player */
    audio_gauge_set((audio_gauge)99, 0, 1, 10);        /* bad gauge  */
    audio_gauge_set(AUDIO_GAUGE_SUPER, 0, 1, 0);       /* maxv <= 0  */
    CHECK(1, "10. out-of-range input did not crash");

    /* --- 11. THE BEACON IS PERSISTENT -----------------------------------
     * ⭐⭐ The property that makes it a beacon rather than a cue: it sounds
     * CONTINUOUSLY, with no value change and no re-feeding. This is the case
     * whose absence the user reported -- position was wired to gauge pan,
     * which only sounds while a gauge MOVES, so walking made no sound.
     *
     * Sabotage: give the beacon a hold timer like a gauge, or drive it from
     * the gauge path, and 11a fails.
     *
     * ⛔ Asserted on SAMPLES. A setter that stores pan while the mixer
     * ignores it would pass any check of the variable. */
    audio_gauge_reset_all();
    audio_beacons_off();
    for (int i = 0; i < 20; i++) { int l, r; peaks(&l, &r); }
    CHECK(peak_over(4) == 0, "11a-pre. silent outside a match");

    audio_beacon_set(0, 0.0f, AUDIO_STANCE_STAND);
    audio_beacon_set(1, 0.0f, AUDIO_STANCE_STAND);
    {
        int first, later;
        first = peak_over(8);
        /* Render a long stretch with NO further calls at all. A gauge would
         * have decayed to silence long ago; a beacon must not. */
        for (int i = 0; i < 120; i++) { int l, r; peaks(&l, &r); }
        later = peak_over(8);
        CHECK(first > 0 && later > 0 && later > first / 2,
              "11a. beacon still sounding after 120 blocks with no new input");
    }

    /* --- 11b/c. Beacon pan places it, and both directions are asserted --
     * ⚠ Both directions matter: a stuck-left implementation passes a
     * left-only check. That asymmetry caught a real sabotage here before. */
    {
        int l1, r1, l2, r2;
        audio_beacon_set(0, -1.0f, AUDIO_STANCE_STAND);
        audio_beacon_set(1, -1.0f, AUDIO_STANCE_STAND);
        pan_peaks(&l1, &r1);
        CHECK(l1 > r1 * 3, "11b. beacons at pan -1 sound on the LEFT");

        audio_beacon_set(0, +1.0f, AUDIO_STANCE_STAND);
        audio_beacon_set(1, +1.0f, AUDIO_STANCE_STAND);
        pan_peaks(&l2, &r2);
        CHECK(r2 > l2 * 3, "11c. the SAME beacons at pan +1 move to the RIGHT");
    }

    /* --- 12. Centre, clamping, and leaving a match ---------------------- */
    {
        int l, r, lc, rc;
        audio_beacon_set(0, 0.0f, AUDIO_STANCE_STAND);
        audio_beacon_set(1, 0.0f, AUDIO_STANCE_STAND);
        pan_peaks(&l, &r);
        CHECK(l > 0 && r > 0 && abs(l - r) < (l + r) / 8,
              "12a. pan 0 is centred (both channels, near-equal)");

        /* Out of range must clamp to the edge, not wrap or go silent. */
        audio_beacon_set(0, -9.0f, AUDIO_STANCE_STAND);
        audio_beacon_set(1, -9.0f, AUDIO_STANCE_STAND);
        pan_peaks(&lc, &rc);
        CHECK(lc > rc * 3, "12b. pan below -1 clamps to hard left, not silence");

        audio_beacons_off();
        for (int i = 0; i < 40; i++) { int q1, q2; peaks(&q1, &q2); }
        CHECK(peak_over(4) == 0, "12c. beacons stop when the match ends");
    }

    /* --- 12d. The two beacons are DISTINGUISHABLE ----------------------
     * ⭐ User requirement: pan alone is ambiguous when both fighters are near
     * centre -- exactly when precision matters -- so "which beacon is me"
     * must be carried by the sound itself.
     *
     * ⚠ The beacons are PLUCKS, told apart by PULSE RATE, so a pitch measure
     * is the wrong instrument -- counting zero crossings of a decaying
     * inharmonic pluck measures its partials, not its repeat rate. Count
     * ENVELOPE PEAKS instead: that is the property the ear uses here.
     *
     * Sabotage: give both players the same rate and this fails. */
    {
        int n1, n2;
        audio_beacons_off();
        audio_beacon_set(0, -1.0f, AUDIO_STANCE_STAND);
        for (int i = 0; i < 8; i++) { int l, r; peaks(&l, &r); }
        n1 = pulse_count(200);

        audio_beacons_off();
        audio_beacon_set(1, -1.0f, AUDIO_STANCE_STAND);
        for (int i = 0; i < 8; i++) { int l, r; peaks(&l, &r); }
        n2 = pulse_count(200);

        CHECK(n1 > 0 && n2 > 0 && n1 > n2,
              "12d. P1 and P2 beacons pulse at audibly different rates");
    }

    /* --- 12e. Stance changes the beacon, low to high -------------------
     * ⭐ USER REQUIREMENT (2026-09-19): "the pitch needs to raise or lower
     * when the player jumps or crouches". Stance scales the pulse rate, so
     * airborne is faster/brighter and knocked down is slower/duller.
     *
     * ⚠ The game does not drive this yet (see the trace: no airborne capture
     * and no stance byte located). The MECHANISM is tested here so that
     * wiring it later is not building on an untested path. */
    {
        int lo, hi;
        audio_beacons_off();
        audio_beacon_set(0, -1.0f, AUDIO_STANCE_DOWN);
        for (int i = 0; i < 8; i++) { int l, r; peaks(&l, &r); }
        lo = pulse_count(200);

        audio_beacons_off();
        audio_beacon_set(0, -1.0f, AUDIO_STANCE_AIR);
        for (int i = 0; i < 8; i++) { int l, r; peaks(&l, &r); }
        hi = pulse_count(200);

        CHECK(lo > 0 && hi > lo,
              "12e. airborne stance is clearly brighter/faster than knocked down");
    }

    /* --- 12h. The stance ladder is ORDERED, low to high -----------------
     * ⭐ The design requires one ordered axis carrying the vertical state:
     * knocked down < crouching < standing < airborne. Assert the ORDER,
     * which is what a listener actually uses, rather than any one value.
     *
     * ⚠ MEASURED AS TONE, NOT PULSE COUNT. Stance scales both, but the pulse
     * counter is unreliable at the top of the range: faster plucks overlap
     * and the envelope detector merges them, so it reported stand 142 > air
     * 132 for a ladder that is correctly ordered. An instrument that
     * saturates is not evidence of the thing it saturates on.
     *
     * Sabotage: make crouch and stand share a multiplier and this fails. */
    {
        double down, crouch, stand, air, top;
        audio_gauge_reset_all();

        audio_beacons_off();
        audio_beacon_set(0, 0.0f, AUDIO_STANCE_DOWN);
        for (int q = 0; q < 8; q++) { int a, b; peaks(&a, &b); }
        down = tone_hz(60);

        audio_beacons_off();
        audio_beacon_set(0, 0.0f, AUDIO_STANCE_CROUCH);
        for (int q = 0; q < 8; q++) { int a, b; peaks(&a, &b); }
        crouch = tone_hz(60);

        audio_beacons_off();
        audio_beacon_set(0, 0.0f, AUDIO_STANCE_STAND);
        for (int q = 0; q < 8; q++) { int a, b; peaks(&a, &b); }
        stand = tone_hz(60);

        audio_beacons_off();
        audio_beacon_set(0, 0.0f, AUDIO_STANCE_AIR);
        for (int q = 0; q < 8; q++) { int a, b; peaks(&a, &b); }
        air = tone_hz(60);

        audio_beacons_off();
        audio_beacon_set(0, 0.0f, AUDIO_STANCE_AIR_TOP);
        for (int q = 0; q < 8; q++) { int a, b; peaks(&a, &b); }
        top = tone_hz(60);

        printf("   (stance tone Hz: down %.0f, crouch %.0f, stand %.0f, "
               "air %.0f, top %.0f)\n", down, crouch, stand, air, top);
        CHECK(down < crouch && crouch < stand && stand < air && air < top,
              "12h. stance ladder is strictly ordered down<crouch<stand<air<top");
    }

    /* --- 12g. ⛔⛔ THE BEACON MUST BE IN THE AUDIBLE BAND ---------------
     * THE CASE THAT WOULD HAVE CAUGHT A SILENT SHIP. The first pluck built
     * its partials from the PULSE ramp, so every component sat at 9-49 Hz --
     * below hearing on any normal speaker. Amplitude, pulse rate and pan
     * were all correct, 29 checks passed, and the user heard NOTHING.
     *
     * ⭐ The lesson: "samples are non-zero" is not "a human can hear it".
     * Assert the FREQUENCY BAND, not just the presence of energy.
     *
     * Sabotage: drive the beacon tone from the pulse rate (set
     * v->freq = g_beacon_rate[p]) and this fails while every other case
     * still passes -- which is exactly what shipped. */
    {
        double t1, t2;
        audio_gauge_reset_all();
        audio_beacons_off();
        for (int i = 0; i < 60; i++) { int a, b; peaks(&a, &b); }

        audio_beacon_set(0, 0.0f, AUDIO_STANCE_STAND);
        for (int i = 0; i < 8; i++) { int a, b; peaks(&a, &b); }
        t1 = tone_hz(80);

        audio_beacons_off();
        for (int i = 0; i < 60; i++) { int a, b; peaks(&a, &b); }
        audio_beacon_set(1, 0.0f, AUDIO_STANCE_STAND);
        for (int i = 0; i < 8; i++) { int a, b; peaks(&a, &b); }
        t2 = tone_hz(80);

        printf("   (beacon tone: P1 %.0f Hz, P2 %.0f Hz)\n", t1, t2);
        /* Comfortably inside the band a small speaker reproduces. */
        CHECK(t1 > 120.0 && t1 < 5000.0 && t2 > 120.0 && t2 < 5000.0,
              "12g. both beacons sound in the AUDIBLE band, not sub-bass");
    }

    /* --- 12f. THE PAN LAW IS PERCEPTUALLY EVEN -------------------------
     * ⭐ USER REQUIREMENT (2026-09-19): "the panning needs to average out so
     * that each step is equally weighted in terms of the degree of pan".
     *
     * The old sine/cosine law gave ~1.7 dB per step near centre and ~100 dB
     * at the edge, so a step into the corner moved the image far more than a
     * step across the middle. Assert the inter-channel DIFFERENCE (what the
     * ear localises by) changes by a near-constant amount per equal step.
     *
     * Sabotage: restore cos/sin in pan_gains() and this fails. */
    {
        double d[9];
        int ok = 1;
        /* ⛔ ISOLATE THE BEACON FIRST. A gauge tone left sounding by an
         * earlier case lands in the peak and corrupts the reading -- it made
         * the first pan measure 11863 instead of 2750 and reported a 15x
         * unevenness for a law that is actually uniform to 1.00x. Reset the
         * gauges and let them fade fully before measuring.
         *
         * ⚠ The failure looked exactly like the bug under test. Measuring a
         * SUM of voices tells you nothing about one of them. */
        audio_gauge_reset_all();
        audio_beacons_off();
        for (int i = 0; i < 60; i++) { int a, b; peaks(&a, &b); }
        audio_beacon_set(0, -1.0f, AUDIO_STANCE_STAND);
        for (int i = 0; i < 9; i++) {
            int L = 0, R = 0;
            float pan = -1.0f + (float)i * 0.25f;
            /* ⚠ Do NOT call audio_beacons_off() between settings here. That
             * drops the voice gain to zero and the smoother ramps it back up,
             * so peaks measured during the ramp read low and unevenly -- a
             * measurement artifact that looks exactly like an uneven pan law.
             * Just move the existing, already-sounding beacon. */
            audio_beacon_set(0, pan, AUDIO_STANCE_STAND);
            /* ⚠ Measure the peak over MANY blocks. The beacon is a pulse, so
             * a single block catches the envelope at an arbitrary point and
             * the reading swings wildly for reasons unrelated to pan. */
            for (int k = 0; k < 200; k++) {
                int a, b;
                peaks(&a, &b);
                if (a > L) L = a;
                if (b > R) R = b;
            }
            /* dB difference; both channels must stay audible at every pan. */
            if (L <= 0 || R <= 0) ok = 0;
            d[i] = 20.0 * log10((double)(L ? L : 1) / (double)(R ? R : 1));
        }
        {
            double mn = 1e9, mx = -1e9;
            for (int i = 1; i < 9; i++) {
                double s = d[i - 1] - d[i];
                if (s < mn) mn = s;
                if (s > mx) mx = s;
            }
            printf("   (pan step dB: min %.2f, max %.2f, ratio %.2f)\n",
                   mn, mx, mn > 0.0 ? mx / mn : -1.0);
            /* Every step within 2x of every other -- the old law was ~58x. */
            CHECK(ok && mn > 0.0 && mx < mn * 2.0,
                  "12f. pan steps are evenly weighted across the whole field");
        }
    }

    /* --- 13. The pan MODEL: world X -> screen pan -----------------------
     * The frame hook derives pan from the two fighters' world X. The model
     * is reproduced here so its edge behaviour is pinned by a test rather
     * than living only in a comment:
     *
     *   cam  = clamp(midpoint, +/-(STAGE_HALF - SCREEN_HALF))
     *   pan  = clamp((x - cam) / SCREEN_HALF, -1, +1)
     *
     * ⭐ The values below are the real measured ones from the captures, so
     * this is a regression on evidence, not on invented numbers. */
    {
        const float STAGE_HALF = 352.0f, SCREEN_HALF = 192.0f;
        const float lim = STAGE_HALF - SCREEN_HALF;
        struct { float x0, x1, want0, want1; } cases[] = {
            /* match start: symmetric about centre                        */
            { -100.0f,  100.0f, -100.0f/192.0f,  100.0f/192.0f },
            /* hp-ken / state-a: P1 pinned in the LEFT corner -> hard left */
            { -352.0f, -253.14f, -1.0f, (-253.14f + lim) / 192.0f },
            /* pos-right-01: P2 pinned in the RIGHT corner -> hard right   */
            {  312.45f, 352.0f, (312.45f - lim) / 192.0f, 1.0f },
        };
        int ok = 1;
        for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            float mid = (cases[i].x0 + cases[i].x1) * 0.5f;
            float cam = mid < -lim ? -lim : (mid > lim ? lim : mid);
            float p0 = (cases[i].x0 - cam) / SCREEN_HALF;
            float p1 = (cases[i].x1 - cam) / SCREEN_HALF;
            if (p0 < -1.0f) p0 = -1.0f;
            if (p0 >  1.0f) p0 =  1.0f;
            if (p1 < -1.0f) p1 = -1.0f;
            if (p1 >  1.0f) p1 =  1.0f;
            if (fabs(p0 - cases[i].want0) > 0.001 ||
                fabs(p1 - cases[i].want1) > 0.001) ok = 0;
            /* The invariant that matters: nobody is ever off screen. */
            if (p0 < -1.0f || p0 > 1.0f || p1 < -1.0f || p1 > 1.0f) ok = 0;
        }
        CHECK(ok, "13. pan model reproduces the measured captures, corners at +/-1");
    }

    /* --- 14. GAUGE pan stays fixed to the HUD --------------------------
     * ⛔⛔ THE TWO LAYERS MUST NOT MERGE. Gauge pan is fixed by owner (P1
     * bars left, P2 bars right), mirroring the on-screen HUD; only the
     * BEACON follows the fighter. I previously wired position into the gauge
     * pan, which broke this and produced no audible beacon at all.
     *
     * Sabotage: drive gauge pan from the beacon and this fails -- a beacon
     * placed hard right would drag P1's health tone to the right with it. */
    {
        int l, r;
        audio_beacons_off();
        audio_beacon_set(0, +1.0f, AUDIO_STANCE_STAND);  /* P1 hard RIGHT */
        audio_gauge_reset_all();
        audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 900, 1000);
        audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 800, 1000);
        /* Measure with the beacon off so only the gauge is sounding. */
        audio_beacons_off();
        pan_peaks(&l, &r);
        CHECK(l > r * 2,
              "14. P1's gauge stays LEFT even with P1's beacon hard right");
    }

    /* --- 15. Per-cue volume: the WRAP -----------------------------------
     * The user's rule: starts at 100, one press up wraps to 0 so muting costs
     * a single key. Asserted as the full cycle, because an add-and-clamp
     * implementation passes any single-step check and then STOPS at the ends.
     *
     * Sabotage: replace the ring arithmetic with add-and-clamp -> 15a fails at
     * the wrap and 15b fails at 0. */
    {
        int ok = 1;
        audio_cue_set_volume(AUDIO_CUE_HEALTH, 100);
        /* up from 100 must be 0, not stay at 100 */
        ok &= audio_cue_step_volume(AUDIO_CUE_HEALTH, TRUE) == 0;
        ok &= audio_cue_step_volume(AUDIO_CUE_HEALTH, TRUE) == 25;
        ok &= audio_cue_step_volume(AUDIO_CUE_HEALTH, TRUE) == 50;
        ok &= audio_cue_step_volume(AUDIO_CUE_HEALTH, TRUE) == 75;
        ok &= audio_cue_step_volume(AUDIO_CUE_HEALTH, TRUE) == 100;
        CHECK(ok, "15a. volume steps up and WRAPS 100 -> 0 in one press");

        ok = 1;
        audio_cue_set_volume(AUDIO_CUE_HEALTH, 0);
        /* down from 0 must wrap to 100: a key that no-ops at an end is
         * indistinguishable from a broken key to a user who cannot see it */
        ok &= audio_cue_step_volume(AUDIO_CUE_HEALTH, FALSE) == 100;
        ok &= audio_cue_step_volume(AUDIO_CUE_HEALTH, FALSE) == 75;
        CHECK(ok, "15b. shift steps down and wraps 0 -> 100");
    }

    /* --- 15c. The levels are INDEPENDENT --------------------------------
     * Sabotage: index the array with a constant -> this fails. A shared level
     * would be invisible until two cues were adjusted in one session. */
    {
        audio_cue_set_volume(AUDIO_CUE_HEALTH, 25);
        audio_cue_set_volume(AUDIO_CUE_SUPER, 75);
        audio_cue_set_volume(AUDIO_CUE_BEACON, 0);
        audio_cue_set_volume(AUDIO_CUE_TICK, 100);
        CHECK(audio_cue_volume(AUDIO_CUE_HEALTH) == 25 &&
              audio_cue_volume(AUDIO_CUE_SUPER)  == 75 &&
              audio_cue_volume(AUDIO_CUE_BEACON) == 0  &&
              audio_cue_volume(AUDIO_CUE_TICK)   == 100,
              "15c. each cue keeps its own level");
    }

    /* --- 16. A muted cue is actually SILENT ------------------------------
     * ⭐ Asserts on SAMPLES, not on the stored percent. "The setter stored 0"
     * is not evidence that the mixer honoured it -- that gap is exactly where
     * a volume control can appear to work while changing nothing.
     *
     * Sabotage: drop the cue_gain() factor from update_voices() -> this fails
     * while 15a-c all still pass. */
    {
        audio_cue_set_volume(AUDIO_CUE_HEALTH, 0);
        audio_cue_set_volume(AUDIO_CUE_SUPER, 0);
        audio_beacons_off();
        audio_gauge_reset_all();
        /* ⚠ SETTLE FIRST. Muting a cue that is already sounding FADES it over
         * ~7 blocks (measured) rather than cutting, which is deliberate -- a
         * hard cut clicks. Without this drain the case measures the previous
         * case's tail and fails for a reason that has nothing to do with the
         * mute. That is what the first version of this case did. */
        (void)peak_over(12);
        audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 1000, 1000);
        audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 900, 1000);
        CHECK(peak_over(10) == 0, "16a. a muted gauge produces no samples");

        /* And the same cue at 100 DOES sound -- so 16a is not passing merely
         * because the gauge never fired. A silence assertion alone cannot
         * tell "correctly muted" from "never produced anything". */
        audio_cue_set_volume(AUDIO_CUE_HEALTH, 100);
        audio_gauge_reset_all();
        audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 1000, 1000);
        audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 900, 1000);
        CHECK(peak_over(4) > 0, "16b. the same gauge at 100%% still sounds");
    }

    /* --- 16c. A level change SCALES rather than just gating --------------
     * 50% must be quieter than 100% but not silent. Sabotage: treat any
     * nonzero level as full volume -> this fails while 16a-b still pass. */
    {
        int loud = 0, quiet = 0, l, r;
        audio_cue_set_volume(AUDIO_CUE_HEALTH, 100);
        audio_gauge_reset_all();
        audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 1000, 1000);
        for (int i = 0; i < 8; i++) {
            audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 1000 - i * 10, 1000);
            peaks(&l, &r);
            if (l > loud) loud = l;
        }
        audio_cue_set_volume(AUDIO_CUE_HEALTH, 50);
        audio_gauge_reset_all();
        /* Drain the 100%% tone before measuring the 50%% one -- see 16a. */
        (void)peak_over(12);
        audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 1000, 1000);
        for (int i = 0; i < 8; i++) {
            audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 1000 - i * 10, 1000);
            peaks(&l, &r);
            if (l > quiet) quiet = l;
        }
        CHECK(quiet > 0 && quiet < loud * 3 / 4,
              "16c. 50%% is audibly quieter than 100%%, not silent");
        audio_cue_set_volume(AUDIO_CUE_HEALTH, 100);
    }

    /* --- 17. Muting one cue leaves the others alone ----------------------
     * The beacons muted must not silence a gauge. Sabotage: apply one cue's
     * level to every voice -> this fails. */
    {
        int l, r;
        audio_cue_set_volume(AUDIO_CUE_BEACON, 0);
        audio_cue_set_volume(AUDIO_CUE_HEALTH, 100);
        audio_beacon_set(0, -1.0f, AUDIO_STANCE_STAND);
        audio_gauge_reset_all();
        audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 1000, 1000);
        audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 900, 1000);
        pan_peaks(&l, &r);
        CHECK(l > 0, "17a. muting the beacons leaves the health gauge audible");

        /* ⭐ AND THE CONVERSE, which is what actually catches a SHARED level.
         * 17a alone passes under a sabotage that routes every cue through the
         * health level, because health is the one left at 100. Mute health
         * instead and require the BEACON to survive: only genuinely separate
         * levels pass both halves.
         *
         * Sabotage: make cue_gain() ignore its argument -> 17b fails. */
        /* ⚠ DRAIN 17a's GAUGE FIRST, with the beacon still off. Without this
         * the health tone is mid-fade during the measurement and `l > 0` is
         * satisfied by that tail rather than by the beacon -- 17b then passes
         * under the very sabotage it exists to catch. Verified: with the drain
         * the sabotage fails 17b; without it, it passes. */
        audio_cue_set_volume(AUDIO_CUE_HEALTH, 0);
        audio_beacons_off();
        audio_gauge_reset_all();
        (void)peak_over(16);
        CHECK(peak_over(4) == 0, "17b-pre. everything drained before measuring");

        audio_cue_set_volume(AUDIO_CUE_BEACON, 100);
        audio_beacon_set(0, -1.0f, AUDIO_STANCE_STAND);
        pan_peaks(&l, &r);
        CHECK(l > 0, "17b. muting health leaves the beacons audible");

        audio_beacons_off();
        audio_cue_set_volume(AUDIO_CUE_BEACON, 100);
        audio_cue_set_volume(AUDIO_CUE_HEALTH, 100);
    }

    /* --- 18. The TICK has its own level ----------------------------------
     * ⚠ The tick latches its level when ARMED, so the mute must precede the
     * tick -- that is the designed behaviour (no smoother, so a mid-decay
     * change would step the envelope), and the test states it rather than
     * working around it.
     *
     * Sabotage: drop t->vol from the tick mix -> 18a fails. */
    {
        audio_cue_set_volume(AUDIO_CUE_TICK, 0);
        audio_beacons_off();
        audio_gauge_reset_all();
        (void)peak_over(12);                 /* drain case 17 -- see 16a */
        audio_tick();
        CHECK(peak_over(4) == 0, "18a. a muted clock tick is silent");

        audio_cue_set_volume(AUDIO_CUE_TICK, 100);
        audio_tick();
        CHECK(peak_over(2) > 0, "18b. the tick at 100%% sounds again");
    }

    /* --- 19. Muting mid-tone FADES, it does not cut ----------------------
     * ⭐ Written because the first version of cases 16 and 18 failed on this
     * tail and the tail is CORRECT: a hard cut on a sounding voice clicks,
     * and the user adjusts volume while cues are playing, which is exactly
     * when a click would land. Pinning it stops a future "optimisation" from
     * turning the fade into a cut and calling the tests green.
     *
     * Sabotage: zero sm_gain the instant gain hits 0 -> 19a fails (silent
     * immediately) while every other case still passes. */
    {
        int l, r;
        audio_cue_set_volume(AUDIO_CUE_HEALTH, 100);
        audio_beacons_off();
        audio_gauge_reset_all();
        /* ⚠ Case 18b left a TICK sounding, and a tick decays over ~40 ms (7
         * blocks) with no relation to this case's gauge. Mute it so the fade
         * being measured is the gauge's own. Every cue this case does not own
         * has to be silenced explicitly -- a "drain" that only waits is at the
         * mercy of whatever the previous case happened to leave running. */
        audio_cue_set_volume(AUDIO_CUE_TICK, 0);
        (void)peak_over(12);
        audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 900, 1000);
        audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 800, 1000);
        for (int i = 0; i < 6; i++) peaks(&l, &r);   /* tone established */

        audio_cue_set_volume(AUDIO_CUE_HEALTH, 0);
        peaks(&l, &r);
        int first = (l > r ? l : r);
        CHECK(first > 0, "19a. muting a sounding tone fades rather than cuts");
        /* ...and the fade does REACH silence, so a mute is a real mute. */
        /* ⚠ peak_over() returns the MAXIMUM across its window, so it answers
         * "was anything loud during the fade", not "did the fade end". The
         * first version asserted peak_over(12) == 0 and failed on the fade's
         * own second block (2314) -- the very tail 19a just proved should be
         * there. Let the fade run, THEN measure. */
        (void)peak_over(12);                 /* let the fade complete */
        CHECK(peak_over(4) == 0, "19b. the fade completes to true silence");
        audio_cue_set_volume(AUDIO_CUE_HEALTH, 100);
        audio_cue_set_volume(AUDIO_CUE_TICK, 100);
    }

    printf(fails ? "\nFAILURES: %d\n" : "\nall %d checks passed\n",
           fails ? fails : 44);
    return fails ? 1 : 0;
}
