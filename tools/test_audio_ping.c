/* The mixer's positional pings (audio_ping_owned), rendered offline through
 * audio_render_block_for_test: they sound only for the current owner, pan to
 * their side, decay, follow the beacon volume, and each family differs.
 * Run from the repository root. */
#include "../src/audio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void energy(int blocks, double *l, double *r)
{
    int16_t b[AUDIO_BLOCK * 2];
    *l = *r = 0;
    for (int k = 0; k < blocks; k++) {
        audio_render_block_for_test(b);
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            *l += (double)b[2 * i] * b[2 * i];
            *r += (double)b[2 * i + 1] * b[2 * i + 1];
        }
    }
}

/* Zero crossings of the left channel over `blocks`: a pitch proxy. */
static int crossings(int blocks)
{
    int16_t b[AUDIO_BLOCK * 2];
    int n = 0, prev = 0;
    for (int k = 0; k < blocks; k++) {
        audio_render_block_for_test(b);
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            int s = b[2 * i] > 0 ? 1 : b[2 * i] < 0 ? -1 : 0;
            if (s && prev && s != prev) n++;
            if (s) prev = s;
        }
    }
    return n;
}

int main(void)
{
    double l, r, l2, r2;
    uint64_t e1 = (1u << 4) | 4u, e2 = (2u << 4) | 4u;

    energy(4, &l, &r);
    assert(l == 0 && r == 0);
    /* No owner: nothing. */
    audio_ping_owned(e1, AUDIO_PING_STONE, 0, 0.0f, 1.0f);
    energy(20, &l, &r);
    assert(l == 0 && r == 0);

    assert(audio_owner_begin(e1));
    /* Left and right. */
    audio_ping_owned(e1, AUDIO_PING_ITEM, 0, -1.0f, 1.0f);
    energy(40, &l, &r);
    assert(l > 0 && l > 10 * r);
    audio_ping_owned(e1, AUDIO_PING_ITEM, 0, 1.0f, 1.0f);
    energy(40, &l2, &r2);
    assert(r2 > 10 * l2);
    /* It decays to silence on its own. */
    energy(400, &l, &r);
    energy(4, &l, &r);
    assert(l == 0 && r == 0);
    /* Gain is nearness: a far ping is quieter. */
    audio_ping_owned(e1, AUDIO_PING_STONE, 0, 0.0f, 1.0f);
    energy(100, &l, &r);
    audio_ping_owned(e1, AUDIO_PING_STONE, 0, 0.0f, 0.25f);
    energy(100, &l2, &r2);
    assert(l2 < l * 0.1 && l2 > 0);
    /* A type step raises the pitch: 12 steps is an octave. */
    audio_ping_owned(e1, AUDIO_PING_ITEM, 0, 0.0f, 1.0f);
    int c0 = crossings(10);
    energy(400, &l, &r);
    audio_ping_owned(e1, AUDIO_PING_ITEM, 12, 0.0f, 1.0f);
    int c12 = crossings(10);
    energy(400, &l, &r);
    assert(c12 > c0 * 18 / 10 && c12 < c0 * 22 / 10);
    /* Families differ in pitch region and length: the stone rings longest,
     * the prop is lowest. */
    {
        double fam[AUDIO_PING_FAMILIES], tail[AUDIO_PING_FAMILIES];
        int cross[AUDIO_PING_FAMILIES];
        for (int f = 0; f < AUDIO_PING_FAMILIES; f++) {
            audio_ping_owned(e1, (audio_ping_family)f, 0, 0.0f, 1.0f);
            cross[f] = crossings(4);
            energy(30, &fam[f], &r);
            energy(40, &tail[f], &r);      /* 0.2 s to 0.45 s after onset */
            energy(400, &l, &r);
        }
        assert(cross[AUDIO_PING_PROP] < cross[AUDIO_PING_ITEM] && cross[AUDIO_PING_ITEM] < cross[AUDIO_PING_STONE]);
        for (int f = 1; f < AUDIO_PING_FAMILIES; f++) assert(tail[AUDIO_PING_STONE] > tail[f]);
        for (int f = 0; f < AUDIO_PING_FAMILIES; f++) assert(fam[f] > 0);
        /* The prop is the quietest. */
        for (int f = 0; f < AUDIO_PING_PROP; f++) assert(fam[AUDIO_PING_PROP] < fam[f]);
    }
    /* Volume is the beacon level (F7): 0 mutes. */
    audio_cue_set_volume(AUDIO_CUE_BEACON, 0);
    audio_ping_owned(e1, AUDIO_PING_STONE, 0, 0.0f, 1.0f);
    energy(40, &l, &r);
    assert(l == 0 && r == 0);
    audio_cue_set_volume(AUDIO_CUE_BEACON, 100);
    /* A replaced owner's ping, even one already sounding, is dropped. */
    audio_ping_owned(e1, AUDIO_PING_STONE, 0, 0.0f, 1.0f);
    energy(2, &l, &r);
    assert(l > 0);
    assert(audio_owner_begin(e2));
    energy(2, &l, &r);
    assert(l == 0 && r == 0);
    audio_ping_owned(e1, AUDIO_PING_STONE, 0, 0.0f, 1.0f);
    energy(20, &l, &r);
    assert(l == 0 && r == 0);
    /* Out-of-range arguments are ignored. */
    audio_ping_owned(e2, AUDIO_PING_FAMILIES, 0, 0.0f, 1.0f);
    audio_ping_owned(e2, AUDIO_PING_ITEM, -1, 0.0f, 1.0f);
    audio_ping_owned(e2, AUDIO_PING_ITEM, 0, 0.0f, 0.0f);
    energy(20, &l, &r);
    assert(l == 0 && r == 0);
    audio_owner_stop(e2);

    printf("PASS: mixer pings -- owner only, panned, decay, nearness gain, a semitone per "
           "type, distinct families, beacon volume, replaced owner dropped, bad input ignored\n");
    return 0;
}
