/* The positional ping scheduler (src/pickup_cues.c) with the mixer stubbed:
 * when each pickup pings, how distance sets rate and loudness, and what makes
 * it start over. Run from the repository root. */
#include "../src/pickup_cues.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static struct { int family, variant; float pan, gain; uint64_t epoch; } got[4096];
static int ngot;
void audio_ping_owned(uint64_t epoch, audio_ping_family family, int variant, float pan, float gain)
{
    assert(ngot < 4096);
    got[ngot].family = (int)family; got[ngot].variant = variant;
    got[ngot].pan = pan; got[ngot].gain = gain; got[ngot].epoch = epoch;
    ngot++;
}

static pickup_sample one(audio_ping_family f, int variant, float pan, float d)
{
    pickup_sample p;
    memset(&p, 0, sizeof p);
    p.present = p.placed = true;
    p.family = f; p.variant = variant; p.pan = pan; p.distance = d;
    return p;
}

/* Frames at which slot `k` pinged over `frames` frames of `s`. */
static int ping_frames(pickup_cues *c, uint64_t epoch, const pickup_sample *s, int frames, int out[256])
{
    int n = 0;
    for (int f = 0; f < frames; f++) {
        int before = ngot;
        pickup_cues_frame(c, epoch, s);
        if (ngot > before) { assert(n < 256); out[n++] = f; }
    }
    return n;
}

int main(void)
{
    static pickup_sample s[PICKUP_SLOTS];
    static pickup_cues c;
    int at[256], n;

    /* 1. The rule: 0.2 s touching, 1.2 s across the arena; props 1.5x; gain
     *    1 near, falling with distance, never below 0.2. */
    assert(pickup_interval_frames(AUDIO_PING_STONE, 0.0f) == 12);
    assert(pickup_interval_frames(AUDIO_PING_ITEM, 2000.0f) == 72);
    assert(pickup_interval_frames(AUDIO_PING_ITEM, 9999.0f) == 72);
    assert(pickup_interval_frames(AUDIO_PING_PROP, 0.0f) == 18);
    assert(pickup_interval_frames(AUDIO_PING_FOOD, 1000.0f) > pickup_interval_frames(AUDIO_PING_FOOD, 500.0f));
    assert(pickup_gain(0.0f) == 1.0f && fabsf(pickup_gain(500.0f) - 0.5f) < 1e-6f);
    assert(pickup_gain(1e6f) == 0.2f && pickup_gain(-5.0f) == 1.0f);

    /* 2. One stone at slot 0, touching: pings at once, then every 12 frames
     *    plus the frame it pings on. */
    memset(s, 0, sizeof s);
    s[0] = one(AUDIO_PING_STONE, 0, -0.5f, 0.0f);
    ngot = 0;
    n = ping_frames(&c, 16, s, 60, at);
    assert(n == 5 && at[0] == 0 && at[1] == 13 && at[2] == 26);
    assert(got[0].family == AUDIO_PING_STONE && got[0].pan == -0.5f && got[0].gain == 1.0f && got[0].epoch == 16);

    /* 3. Nearer is faster: the same item far away pings less often. */
    {
        static pickup_cues close_by, far_off;
        int a[256], b[256];
        memset(s, 0, sizeof s);
        s[3] = one(AUDIO_PING_ITEM, 4, 0.0f, 100.0f);
        int na = ping_frames(&close_by, 16, s, 600, a);
        s[3].distance = 1800.0f;
        int nb = ping_frames(&far_off, 16, s, 600, b);
        assert(na > 2 * nb && nb >= 8);
    }

    /* 4. A dozen arriving together are staggered by slot over 0.35 s, so
     *    they do not land as one blur. */
    {
        static pickup_cues st;
        int first[PICKUP_SLOTS];
        memset(s, 0, sizeof s);
        for (int k = 0; k < 12; k++) s[k] = one(AUDIO_PING_PROP, 0, 0.0f, 300.0f);
        for (int k = 0; k < PICKUP_SLOTS; k++) first[k] = -1;
        for (int f = 0; f < 30; f++) {
            int before = ngot;
            pickup_cues_frame(&st, 16, s);
            (void)before;
            for (int k = 0; k < 12; k++) if (first[k] < 0 && st.countdown[k] > 0 && st.was_present[k]) {
                /* countdown reset to an interval means it just pinged */
                if (st.countdown[k] == pickup_interval_frames(AUDIO_PING_PROP, 300.0f)) first[k] = f;
            }
        }
        for (int k = 0; k < 12; k++) {
            assert(first[k] == (k % 8) * 3);
        }
    }

    /* 5. Not placeable (behind no camera): waits, pings once it is. */
    {
        static pickup_cues w;
        memset(s, 0, sizeof s);
        s[5] = one(AUDIO_PING_FOOD, 2, 0.3f, 50.0f);
        s[5].placed = false;
        ngot = 0;
        for (int f = 0; f < 40; f++) pickup_cues_frame(&w, 16, s);
        assert(ngot == 0);
        s[5].placed = true;
        pickup_cues_frame(&w, 16, s);
        assert(ngot == 1 && got[0].family == AUDIO_PING_FOOD && got[0].variant == 2);
    }

    /* 6. What starts a pickup over: leaving, a different thing in the slot,
     *    a new owner epoch, a reset; epoch 0 does nothing. */
    {
        static pickup_cues r;
        memset(s, 0, sizeof s);
        s[0] = one(AUDIO_PING_ITEM, 1, 0.0f, 2000.0f);       /* long interval */
        ngot = 0;
        pickup_cues_frame(&r, 16, s);
        assert(ngot == 1);
        pickup_cues_frame(&r, 16, s);
        assert(ngot == 1);                                    /* counting down */
        s[0].variant = 2;                                     /* another type in the slot */
        pickup_cues_frame(&r, 16, s);
        assert(ngot == 2);
        s[0].present = false;
        pickup_cues_frame(&r, 16, s);
        s[0].present = true;                                  /* gone and back */
        pickup_cues_frame(&r, 16, s);
        assert(ngot == 3);
        pickup_cues_frame(&r, 32, s);                         /* new owner */
        assert(ngot == 4 && got[3].epoch == 32);
        pickup_cues_reset(&r);
        pickup_cues_frame(&r, 32, s);
        assert(ngot == 5);
        pickup_cues_frame(&r, 0, s);
        assert(ngot == 5);
    }

    printf("PASS: pickup ping scheduler -- interval and gain by distance, prompt first ping, "
           "nearer faster, staggered arrivals, waits for placement, every restart\n");
    return 0;
}
