/* The mixer's fighter gauges (audio_gauge_set_fighter_owned), rendered
 * offline: all four players sound, each takes the pan it is given, and each
 * player's articulation is measurably its own -- 0 steady, 1 flutter
 * (tremolo), 2 warble (vibrato), 3 growl (fast AM) -- while the HUD setter
 * keeps players 0/1, its fixed pans and the steady tone. Run from the
 * repository root. */
#include "../src/audio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define BLOCKS 18
static int16_t buf[BLOCKS * AUDIO_BLOCK * 2];

/* Render the hold that follows one change, into buf. */
static void sound(uint64_t e, unsigned player, float pan, int hud)
{
    static int value = 1000;
    int16_t b[AUDIO_BLOCK * 2];
    for (int k = 0; k < 40; k++) audio_render_block_for_test(b);    /* silence */
    value -= 37;
    if (hud) audio_gauge_set_owned(e, AUDIO_GAUGE_HEALTH, player, value, 1000);
    else audio_gauge_set_fighter_owned(e, AUDIO_GAUGE_HEALTH, player, value, 1000, pan);
    for (int k = 0; k < BLOCKS; k++) audio_render_block_for_test(buf + k * AUDIO_BLOCK * 2);
}

static void energy(double *l, double *r)
{
    *l = *r = 0;
    for (int i = 0; i < BLOCKS * AUDIO_BLOCK; i++) {
        *l += (double)buf[2 * i] * buf[2 * i];
        *r += (double)buf[2 * i + 1] * buf[2 * i + 1];
    }
}

/* Variation of the loudness envelope (128-sample RMS windows, mono), as a
 * coefficient of variation, over the steady middle of the hold. */
static double envelope_cv(void)
{
    double sum = 0, sq = 0;
    int n = 0;
    for (int w = 2 * AUDIO_BLOCK; w + 128 <= (BLOCKS - 3) * AUDIO_BLOCK; w += 128) {
        double e = 0;
        for (int i = w; i < w + 128; i++) {
            double m = (double)buf[2 * i] + buf[2 * i + 1];
            e += m * m;
        }
        e = sqrt(e / 128);
        sum += e; sq += e * e; n++;
    }
    double mean = sum / n;
    return sqrt(sq / n - mean * mean) / mean;
}

/* Spread of the waveform's period (samples between rising zero crossings). */
static double period_sd(void)
{
    double sum = 0, sq = 0;
    int n = 0, last = -1, prev = 0;
    for (int i = 2 * AUDIO_BLOCK; i < (BLOCKS - 3) * AUDIO_BLOCK; i++) {
        int s = (int)buf[2 * i] + buf[2 * i + 1];
        if (prev < 0 && s >= 0) {
            if (last >= 0) { double p = i - last; sum += p; sq += p * p; n++; }
            last = i;
        }
        if (s) prev = s;
    }
    assert(n > 20);
    double mean = sum / n;
    return sqrt(sq / n - mean * mean);
}

int main(void)
{
    double l, r, cv[4], sd[4];
    uint64_t e = (1u << 4) | 5u;
    int16_t b[AUDIO_BLOCK * 2];

    assert(audio_owner_begin(e));
    /* First sighting seeds silently, as every gauge does. */
    audio_gauge_set_fighter_owned(e, AUDIO_GAUGE_HEALTH, 3, 1000, 1000, 0.0f);
    audio_render_block_for_test(b);
    for (int i = 0; i < AUDIO_BLOCK * 2; i++) assert(b[i] == 0);
    for (unsigned p = 0; p < 3; p++)
        audio_gauge_set_fighter_owned(e, AUDIO_GAUGE_HEALTH, p, 1000, 1000, 0.0f);

    /* Every player sounds and takes its pan. */
    for (unsigned p = 0; p < 4; p++) {
        sound(e, p, -1.0f, 0);
        energy(&l, &r);
        assert(l > 0 && l > 10 * r);
        sound(e, p, 1.0f, 0);
        energy(&l, &r);
        assert(r > 10 * l);
        sound(e, p, 0.0f, 0);
        cv[p] = envelope_cv();
        sd[p] = period_sd();
        printf("player %u: envelope variation %.3f, period spread %.2f samples\n", p, cv[p], sd[p]);
    }
    /* Steady; flutter and growl move the loudness; warble moves the pitch. */
    assert(cv[1] > 3 * cv[0] && cv[3] > 3 * cv[0]);
    assert(sd[2] > 3 * sd[0] && sd[2] > 3 * sd[1] && sd[2] > 3 * sd[3]);
    assert(cv[1] > 2 * cv[2] && cv[3] > 2 * cv[2]);

    /* The HUD setter: players 0 and 1 only, fixed sides, the steady tone --
     * even on a voice a fighter gauge used before. */
    sound(e, 1, 0.0f, 1);
    energy(&l, &r);
    assert(r > 3 * l);
    assert(envelope_cv() < 2 * cv[0] + 0.01);
    sound(e, 2, 0.0f, 1);
    energy(&l, &r);
    assert(l == 0 && r == 0);

    /* Bad input is ignored. */
    audio_gauge_set_fighter_owned(e, AUDIO_GAUGE_HEALTH, 4, 10, 1000, 0.0f);
    audio_gauge_set_fighter_owned(e, AUDIO_GAUGE_HEALTH, 0, 10, 1000, NAN);
    audio_gauge_set_fighter_owned(e, AUDIO_GAUGE_HEALTH, 0, 10, 0, 0.0f);
    for (int k = 0; k < 40; k++) {
        audio_render_block_for_test(b);
    }
    for (int i = 0; i < AUDIO_BLOCK * 2; i++) assert(b[i] == 0);
    /* Another owner's writes are dropped. */
    audio_gauge_set_fighter_owned(e + 16, AUDIO_GAUGE_HEALTH, 0, 5, 1000, 0.0f);
    for (int k = 0; k < 4; k++) audio_render_block_for_test(b);
    for (int i = 0; i < AUDIO_BLOCK * 2; i++) assert(b[i] == 0);
    audio_owner_stop(e);

    printf("PASS: fighter gauges -- four players, panned where given, steady/flutter/warble/growl "
           "distinct, HUD setter unchanged, bad input and stale owners ignored\n");
    return 0;
}
