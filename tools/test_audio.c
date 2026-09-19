/*
 * Offline test for the gauge mixer. Drives audio.c through its render seam,
 * so the DSP is checked with no sound card and no game.
 *
 * Every case asserts on SAMPLES, not on a flag: "the code took the silent
 * branch" is not evidence that the output was silent. Each mechanism below was
 * sabotaged and seen to fail the case that tests it (noted per case).
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
     * same fill. They are separate voices with separate frequency ranges
     * (health 180-540 Hz triangle, super 330-990 Hz sine), so at equal fill
     * their pitches must differ clearly.
     * Sabotage: give both the same f_lo/f_hi -> this fails. */
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
    CHECK(f_super > f_health * 1.4, "7b. health and super are audibly distinct");

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

    printf(fails ? "\nFAILURES: %d\n" : "\nall %d checks passed\n",
           fails ? fails : 12);
    return fails ? 1 : 0;
}
