/* Replays every distinct frame-decoder input of the 2026-10-20 live Plasma
 * Sword captures through the shipping frame decoder: Arcade (pswd: select,
 * two rounds with a pause, continue) and Training (walking, side steps, jumps
 * over the opponent, a corner), with the shared renderer's view and
 * projection sampled live beside Training. Fixtures:
 * tools/make_stg2_fixtures.py. Run from the repository root. */
#include "../src/stg2_frame.h"
#include "../src/audio.h"
#include "../libs/minhook/include/MinHook.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The hook is never installed here. */
MH_STATUS WINAPI MH_CreateHook(LPVOID t, LPVOID d, LPVOID *o) { (void)t; (void)d; (void)o; return MH_UNKNOWN; }
MH_STATUS WINAPI MH_EnableHook(LPVOID t) { (void)t; return MH_UNKNOWN; }
MH_STATUS WINAPI MH_DisableHook(LPVOID t) { (void)t; return MH_UNKNOWN; }

#define SIZE 0x511B8u
/* Same list as RANGES in tools/make_stg2_fixtures.py. */
static const unsigned ranges[][2] = {
    { 0x0, 2 }, { 0x94, 2 },
    { 0x220, 0xC }, { 0x428, 4 }, { 0x57C, 2 },
    { 0xA40 + 0x220, 0xC }, { 0xA40 + 0x428, 4 }, { 0xA40 + 0x57C, 2 },
    { 0x4D4B4, 0x40 }, { 0x4DF33, 1 }, { 0x4DFF0, 0xA }, { 0x4E598, 1 }, { 0x4E5EC, 2 },
};
#define NRANGES (sizeof ranges / sizeof ranges[0])
#define MAXREC 4096
static unsigned count, intro;   /* intro: the player's first round intro */
static unsigned char *rec[MAXREC];
static double when[MAXREC];
static unsigned tcount;
static unsigned char *trec[MAXREC];
static float cam[32];           /* renderer view, then projection */

static void load(const char *path, unsigned *n, unsigned char **out, double *t)
{
    FILE *f = fopen(path, "rb");
    unsigned char magic[4];
    assert(f && fread(magic, 1, 4, f) == 4 && !memcmp(magic, "STGF", 4));
    assert(fread(n, 4, 1, f) == 1 && *n > 0 && *n <= MAXREC);
    for (unsigned i = 0; i < *n; i++) {
        double ignored;
        out[i] = calloc(1, SIZE);
        assert(out[i]);
        assert(fread(t ? &t[i] : &ignored, 8, 1, f) == 1);
        for (unsigned r = 0; r < NRANGES; r++)
            assert(fread(out[i] + ranges[r][0], 1, ranges[r][1], f) == ranges[r][1]);
    }
    assert(fgetc(f) == EOF);
    fclose(f);
}

static int mode_of(const unsigned char *s) { short v; memcpy(&v, s + 0x4E5EC, 2); return v; }
static void set16(unsigned char *s, unsigned off, short v) { memcpy(s + off, &v, 2); }
static void setf(unsigned char *s, unsigned off, float v) { memcpy(s + off, &v, 4); }

/* Distinct consecutive values of a side's health while it is fed. */
static int health_path(unsigned side, int out[64])
{
    int n = 0;
    for (unsigned i = intro; i < count; i++) {
        match_audio_sample m;
        stg2_frame_decode(rec[i], cam, &m);
        if (!m.owner[side].health.valid) continue;
        if (!n || out[n - 1] != m.owner[side].health.value) {
            assert(n < 64);
            out[n++] = m.owner[side].health.value;
        }
    }
    return n;
}

int main(void)
{
    match_audio_sample m;
    unsigned missing[2] = { 0, 0 };
    unsigned fights = 0, p1_left = 0, on_screen = 0, first_fight = count, airborne = 0, rng_moves = 0;
    uint32_t last_sig = 0;
    bool have_sig = false;
    int last_res[2] = { -1, -1 }, last_lvl[2] = { -1, -1 }, last_clock = 999;
    load("tools/fixtures/stg2-match-frames-20261020.bin", &count, rec, when);
    load("tools/fixtures/stg2-training-frames-20261020.bin", &tcount, trec, NULL);
    {
        FILE *f = fopen("tools/fixtures/stg2-render-20261020.bin", "rb");
        assert(f && fread(cam, 4, 32, f) == 32 && fgetc(f) == EOF);
        fclose(f);
        /* What the renderer held in every live sample: view diag(1,-1,-1),
         * projection cot(39.5/2)/1.3 and cot(39.5/2), w = -z. */
        assert(cam[0] == 1.0f && cam[5] == -1.0f && cam[10] == -1.0f && cam[15] == 1.0f);
        assert(cam[16] > 2.14f && cam[16] < 2.15f && cam[21] > 2.78f && cam[21] < 2.79f);
        assert(cam[27] == -1.0f && cam[31] == 0.0f);
    }
    /* The capture opens on an earlier session's fight (another root, before
     * the player entered Arcade); the player's own match starts at the first
     * round intro (mode 3). */
    for (intro = 0; intro < count && mode_of(rec[intro]) != 3; intro++) {}
    assert(intro < count);
    first_fight = count;

    /* 1. Every live sample: the match gate follows the mode; health only
     *    fights and KOs; beacons only fight; values stay in range. */
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *s = rec[i];
        int mode = mode_of(s);
        bool in = s[0] == 3 && (mode == 3 || mode == 4 || mode == 11);
        stg2_frame_decode(s, cam, &m);
        assert(m.in_match == in);
        if (!in) { assert(!m.owner[0].present && !m.owner[1].present); continue; }
        for (int p = 0; p < 2; p++) {
            const match_audio_owner *o = &m.owner[p];
            assert(o->present);
            assert(o->health.valid == (mode == 4 || mode == 11));
            if (mode != 4) assert(!o->beacon_valid);
            else if (!o->beacon_valid) missing[p]++;
            if (o->health.valid) assert(o->health.value >= 0 && o->health.value <= 200 && o->health.maximum == 200);
            if (mode != 3) {
                /* The plasma gauge: always readable, never past three levels,
                 * and a level-up is never heard as a drop. */
                int lvl = s[0x4DFF8 + p];
                assert(o->resource.valid && o->resource.maximum == 3 * 0x90);
                assert(o->resource.value >= 0 && o->resource.value <= 3 * 0x90);
                if (last_lvl[p] >= 0 && lvl > last_lvl[p]) assert(o->resource.value > last_res[p]);
                last_lvl[p] = lvl;
                last_res[p] = o->resource.value;
            } else {
                assert(!o->resource.valid);
            }
            if (o->beacon_valid) {
                float y; memcpy(&y, s + p * 0xA40 + 0x224, 4);
                assert(o->screen_pan >= -1.0f && o->screen_pan <= 1.0f);
                if (y < 0.0f) {
                    assert(o->stance > AUDIO_STANCE_AIR && o->stance <= AUDIO_STANCE_AIR_TOP);
                    if (p == 0) airborne++;
                } else {
                    assert(o->stance == AUDIO_STANCE_STAND);
                }
            }
        }
        if (mode == 4) {
            if (first_fight == count && i > intro) first_fight = i;
            fights++;
            if (m.owner[0].screen_pan < m.owner[1].screen_pan) p1_left++;
            if (m.owner[0].screen_pan > -1.0f && m.owner[0].screen_pan < 1.0f &&
                m.owner[1].screen_pan > -1.0f && m.owner[1].screen_pan < 1.0f) on_screen++;
            if (have_sig && m.motion_signature != last_sig) rng_moves++;
            /* The clock counts down within a round; a new round restarts it. */
            assert(m.clock_valid && m.clock <= 90);
            if (i > intro && last_clock != 999 && m.clock > last_clock) assert(m.clock >= 88);
            last_clock = m.clock;
        }
        last_sig = m.motion_signature;
        have_sig = true;
    }

    /* 2. Health exactly as the game took it, both rounds, KO overkill as 0. */
    {
        static const int p1[] = { 200, 187, 165, 146, 76, 54, 35, 23, 8, 0,
                                  200, 114, 111, 108, 92, 70, 47, 24, 9, 7, 0 };
        static const int p2[] = { 200, 189, 178, 153, 139, 132, 120, 109,
                                  200, 186, 175, 157, 149, 133 };
        int got[64], n = health_path(0, got);
        assert(n == (int)(sizeof p1 / sizeof p1[0]));
        for (int i = 0; i < n; i++) assert(got[i] == p1[i]);
        n = health_path(1, got);
        assert(n == (int)(sizeof p2 / sizeof p2[0]));
        for (int i = 0; i < n; i++) assert(got[i] == p2[i]);
    }

    /* 3. Round start through the game's projection: P1 is drawn on the RIGHT
     *    (P1 stands at X = -308, which the game's camera maps to the right),
     *    P2 on the left; the clock set to 90 at the intro and already 89. */
    assert(first_fight < count);
    stg2_frame_decode(rec[first_fight], cam, &m);
    assert(m.owner[0].screen_pan > 0.3f && m.owner[1].screen_pan < -0.3f);
    assert(m.clock == 89);
    /* Both on screen in 1501 of 1642 (the 8 below count as on screen: no
     * beacon, pan 0). The rest are short clusters, 140 of
     * them from 450.96 to 461.4 s, after P2's plasma level fell 1 -> 0 at
     * 450.74 s: the game's camera closes in and a fighter is really off the
     * drawn screen (up to 3.4 half-widths), so the pan clamps to that side.
     * The renderer was not sampled in Arcade; its matrices are the constant
     * setup 0x140F39830 builds, sampled in Training. */
    assert(fights == 1642 && on_screen == 1501);
    /* P1 is drawn left of P2 in only 50 frames: the sides seldom crossed. */
    assert(p1_left == 50);
    /* The only fighting frames without a beacon: both fighters behind the
     * game's camera (view depth -505 .. -8) for 0.4 s at 456.4 s, inside the
     * close-up stretch above. No position is estimated there. */
    assert(missing[0] == 8 && missing[1] == 8);
    /* The jump and the frame-to-frame motion the pause test relies on. */
    assert(airborne > 20);
    assert(rng_moves * 20 > fights * 19);

    /* 3b. Training, where the renderer's matrices were sampled live: every
     *     fighting frame puts both fighters on screen, P1 starts on the right,
     *     and no camera, or one that is not a perspective camera, gives no
     *     beacon rather than an estimate. */
    {
        unsigned fought = 0, first = tcount;
        for (unsigned i = 0; i < tcount; i++) {
            if (trec[i][0] != 3 || mode_of(trec[i]) != 4) continue;
            stg2_frame_decode(trec[i], cam, &m);
            for (int p = 0; p < 2; p++) {
                assert(m.owner[p].beacon_valid);
                assert(m.owner[p].screen_pan > -1.0f && m.owner[p].screen_pan < 1.0f);
            }
            if (first == tcount) first = i;
            fought++;
        }
        assert(fought > 1000 && first < tcount);
        stg2_frame_decode(trec[first], cam, &m);
        assert(m.owner[0].screen_pan > 0.3f && m.owner[1].screen_pan < -0.3f);
        stg2_frame_decode(trec[first], NULL, &m);
        assert(!m.owner[0].beacon_valid && !m.owner[1].beacon_valid && m.owner[0].health.valid);
        {
            float flat[32];
            memcpy(flat, cam, sizeof flat);
            flat[27] = 0.0f;                              /* orthographic */
            stg2_frame_decode(trec[first], flat, &m);
            assert(!m.owner[0].beacon_valid);
        }
        printf("training: %u fighting frames, both on screen in all\n", fought);
    }

    /* 4. Ends: P1 finished with all three plasma levels. */
    {
        unsigned last_live = count;
        for (unsigned i = 0; i < count; i++) if (mode_of(rec[i]) == 11) last_live = i;
        assert(last_live < count);
        stg2_frame_decode(rec[last_live], cam, &m);
        assert(m.owner[0].resource.value == 3 * 0x90);
    }

    /* 5. Gates, each on its own, on a fighting sample. */
    {
        unsigned char *s = rec[first_fight];
        set16(s, 0x57C, -39);                             /* KO overkill */
        stg2_frame_decode(s, cam, &m);
        assert(m.owner[0].health.valid && m.owner[0].health.value == 0);
        set16(s, 0x57C, 201);                             /* over the maximum */
        stg2_frame_decode(s, cam, &m);
        assert(!m.owner[0].health.valid);
        set16(s, 0x57C, -201);
        stg2_frame_decode(s, cam, &m);
        assert(!m.owner[0].health.valid);
        set16(s, 0x57C, 200);
        s[0x428] = 13;                                    /* unknown fighter */
        stg2_frame_decode(s, cam, &m);
        assert(!m.owner[0].resource.valid && m.owner[1].resource.valid);
        s[0x428] = 2;
        s[0x4DFF8] = 4;                                   /* past the cap */
        stg2_frame_decode(s, cam, &m);
        assert(!m.owner[0].resource.valid);
        s[0x4DFF8] = 1;
        set16(s, 0x4DFF0, 0x91);                          /* fill past a level */
        stg2_frame_decode(s, cam, &m);
        assert(!m.owner[0].resource.valid);
        set16(s, 0x4DFF0, 0x90);
        stg2_frame_decode(s, cam, &m);
        assert(m.owner[0].resource.valid && m.owner[0].resource.value == 0x90 + 0x8F);
        s[0x4E598] = 1;                                   /* infinite timer */
        stg2_frame_decode(s, cam, &m);
        assert(!m.clock_valid);
        s[0x4E598] = 0;
        {
            float g[16], d;
            memcpy(g, s + 0x4D4B4, sizeof g);
            g[15] = 0.5f;                                 /* not affine */
            memcpy(s + 0x4D4B4, g, sizeof g);
            stg2_frame_decode(s, cam, &m);
            assert(!m.owner[0].beacon_valid && m.owner[0].health.valid);
            g[15] = 1.0f;
            memcpy(s + 0x4D4B4, g, sizeof g);
            /* Far along the camera's own backward axis: behind it. */
            d = -100000.0f;
            setf(s, 0x220, d * g[2]); setf(s, 0x224, d * g[6]); setf(s, 0x228, d * g[10]);
            stg2_frame_decode(s, cam, &m);
            assert(!m.owner[0].beacon_valid && m.owner[1].beacon_valid);
        }
        s[0] = 1;                                         /* title phase */
        stg2_frame_decode(s, cam, &m);
        assert(!m.in_match);
        s[0] = 3;
        set16(s, 0x4E5EC, 7);                             /* continue screen */
        stg2_frame_decode(s, cam, &m);
        assert(!m.in_match);
    }

    printf("PASS: %u live STG2 frames (%u fighting) -- match gate by mode, health both "
           "rounds with KO overkill, plasma levels and cap, clock, pan through the game's "
           "camera and the renderer's live matrices, jump pitch, motion, every gate\n", count, fights);
    return 0;
}
