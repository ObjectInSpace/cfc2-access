/* Replays every distinct frame-decoder input of the 2026-10-22 live Power
 * Stone capture through the shipping frame decoder: select, a fight left
 * early, a full round lost by KO with a pause, round two's start; and every
 * sample of the fought round through the pickup decoder. The renderer's
 * projection was sampled beside the snapshots. Fixtures:
 * tools/make_pws1_fixtures.py. Run from the repository root. */
#include "../src/pws1_frame.h"
#include "../src/audio.h"
#include "../libs/minhook/include/MinHook.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The hook is never installed here. */
MH_STATUS WINAPI MH_CreateHook(LPVOID t, LPVOID d, LPVOID *o) { (void)t; (void)d; (void)o; return MH_UNKNOWN; }
MH_STATUS WINAPI MH_EnableHook(LPVOID t) { (void)t; return MH_UNKNOWN; }
MH_STATUS WINAPI MH_DisableHook(LPVOID t) { (void)t; return MH_UNKNOWN; }

#define SIZE 0x428F0u
#define A0 0x18428u
#define A1 0x1FCB8u
/* Same lists as RANGES and PICKUPS in tools/make_pws1_fixtures.py. */
static const unsigned ranges[][2] = {
    { A0 + 3, 1 }, { A0 + 0x30, 0xC }, { A0 + 0x7790, 2 }, { A0 + 0x77EC, 1 },
    { A1 + 3, 1 }, { A1 + 0x30, 0xC }, { A1 + 0x7790, 2 }, { A1 + 0x77EC, 1 },
    { 0x27974, 0x40 }, { 0x27E58, 1 }, { 0x27E5D, 1 }, { 0x27E7D, 1 }, { 0x27E92, 1 },
    { 0x27F94, 4 }, { 0x27FAC, 2 }, { 0x27FCD, 1 }, { 0x280C4, 1 },
};
static const unsigned extra[][2] = { { 0x27FF2, 1 }, { A0 + 0x7780, 8 }, { A1 + 0x7780, 8 } };
static const unsigned pool_fields[][2] = { { 0, 16 }, { 0x30, 12 }, { 0x3C8, 1 } };
#define N(a) (sizeof a / sizeof a[0])
#define MAXREC 4096
static unsigned count, nsnap, npick;
static unsigned char *rec[MAXREC], *snap[16], *pk[MAXREC];
static double when[MAXREC], pwhen[MAXREC];
static unsigned long long proot[MAXREC];
static float proj[16];

static void read_ranges(FILE *f, unsigned char *s, const unsigned (*r)[2], unsigned n)
{
    for (unsigned k = 0; k < n; k++) assert(fread(s + r[k][0], 1, r[k][1], f) == r[k][1]);
}

static void load(const char *path, const char *magic4, unsigned *n, unsigned char **out, double *t)
{
    FILE *f = fopen(path, "rb");
    unsigned char magic[4];
    assert(f && fread(magic, 1, 4, f) == 4 && !memcmp(magic, magic4, 4));
    assert(fread(n, 4, 1, f) == 1 && *n > 0 && *n <= MAXREC);
    for (unsigned i = 0; i < *n; i++) {
        out[i] = calloc(1, SIZE);
        assert(out[i]);
        if (t) assert(fread(&t[i], 8, 1, f) == 1);
        read_ranges(f, out[i], ranges, N(ranges));
    }
    assert(fgetc(f) == EOF);
    fclose(f);
}

static void load_pickups(void)
{
    FILE *f = fopen("tools/fixtures/pws1-pickup-frames-20261022.bin", "rb");
    unsigned char magic[4];
    assert(f && fread(magic, 1, 4, f) == 4 && !memcmp(magic, "PWSP", 4));
    assert(fread(&npick, 4, 1, f) == 1 && npick > 0 && npick <= MAXREC);
    for (unsigned i = 0; i < npick; i++) {
        unsigned char used;
        pk[i] = calloc(1, SIZE);
        assert(pk[i]);
        assert(fread(&pwhen[i], 8, 1, f) == 1 && fread(&proot[i], 8, 1, f) == 1);
        read_ranges(f, pk[i], ranges, N(ranges));
        read_ranges(f, pk[i], extra, N(extra));
        assert(fread(&used, 1, 1, f) == 1);
        for (unsigned u = 0; u < used; u++) {
            unsigned char idx;
            assert(fread(&idx, 1, 1, f) == 1 && idx < 100);
            for (unsigned k = 0; k < N(pool_fields); k++)
                assert(fread(pk[i] + idx * 0x3E0u + pool_fields[k][0], 1, pool_fields[k][1], f) == pool_fields[k][1]);
        }
    }
    assert(fgetc(f) == EOF);
    fclose(f);
}

static int hp_of(const unsigned char *s, unsigned a) { short v; memcpy(&v, s + a + 0x7790, 2); return v; }
static void set16(unsigned char *s, unsigned off, short v) { memcpy(s + off, &v, 2); }
static bool in_match_of(const unsigned char *s)
{ return s[0x27E58] == 1 && s[0x27E5D] == 4 && s[0x27FAC] >= 2 && s[0x27FAC] <= 5; }

/* Distinct consecutive values of a side's gauge while it is fed, from `from`. */
static int path_of(unsigned side, bool health, unsigned from, unsigned to, int out[128])
{
    int n = 0;
    for (unsigned i = from; i < to; i++) {
        match_audio_sample m;
        const match_audio_gauge *g;
        pws1_frame_decode(rec[i], &m);
        g = health ? &m.owner[side].health : &m.owner[side].resource;
        if (!g->valid) continue;
        if (!n || out[n - 1] != g->value) {
            assert(n < 128);
            out[n++] = g->value;
        }
    }
    return n;
}

static void expect_path(const char *what, const int *got, int n, const int *want, int wn)
{
    if (n != wn || memcmp(got, want, sizeof *want * (size_t)wn)) {
        fprintf(stderr, "%s:", what);
        for (int i = 0; i < n; i++) fprintf(stderr, " %d", got[i]);
        fprintf(stderr, "\n");
        assert(0);
    }
}

static void fighter_pt(const unsigned char *s, unsigned a, float pt[3]) { memcpy(pt, s + a + 0x30, 12); }

int main(void)
{
    match_audio_sample m;
    unsigned in = 0, ko, round2, first_fight = 0;
    int last_clock = 999;
    load("tools/fixtures/pws1-match-frames-20261022.bin", "PWSF", &count, rec, when);
    load("tools/fixtures/pws1-snapshot-frames-20261022.bin", "PWSN", &nsnap, snap, NULL);
    load_pickups();
    assert(nsnap == 13);
    ko = round2 = count;
    {
        FILE *f = fopen("tools/fixtures/pws1-render-20261022.bin", "rb");
        assert(f && fread(proj, 4, 16, f) == 16 && fgetc(f) == EOF);
        fclose(f);
        /* What the renderer held while fighting: a perspective camera with
         * x scale 2.0059, y 2.6746 and w = -z. */
        assert(proj[0] > 2.005f && proj[0] < 2.007f && proj[5] > 2.674f && proj[5] < 2.675f);
        assert(proj[11] == -1.0f && proj[15] == 0.0f);
    }

    /* 1. Every live sample: the gate follows the phase and sub-state; health
     *    and stones only from fighting on; no fighter beacons at all. */
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *s = rec[i];
        unsigned sub = s[0x27FAC];
        pws1_frame_decode(s, &m);
        assert(m.in_match == in_match_of(s));
        if (!m.in_match) { assert(!m.owner[0].present && !m.owner[1].present && !m.clock_valid); continue; }
        in++;
        for (int p = 0; p < 2; p++) {
            const match_audio_owner *o = &m.owner[p];
            assert(o->present && !o->beacon_valid);
            assert(o->health.valid == (sub >= 3) && o->resource.valid == (sub >= 3));
            assert(o->health.maximum == 240 && o->resource.maximum == 3);
            assert(o->health.value >= 0 && o->health.value <= 240);
            assert(o->resource.value >= 0 && o->resource.value <= 3);
        }
        /* The clock: 99 at each round start, one step at a time, never up
         * within a round. */
        assert(m.clock_valid && m.clock <= 99);
        if (last_clock != 999 && m.clock != last_clock)
            assert(m.clock == last_clock - 1 || m.clock == 99);
        last_clock = m.clock;
        if (ko == count && hp_of(s, A0) == -1) ko = i;
        if (ko < count && round2 == count && sub == 3 && hp_of(s, A0) == 240) round2 = i;
    }
    assert(ko < count && round2 < count);

    /* 2. Health exactly as the game took it in the round lost by KO: the
     *    upward steps are the game's own recovery (0x140991360), the -1 KO
     *    reads as 0; the CPU was never touched. Round two starts at 240. */
    for (first_fight = ko; first_fight > 0 && rec[first_fight - 1][0x27FAC] != 2; first_fight--) {}
    {
        static const int p1[] = { 240, 215, 208, 201, 202, 193, 156, 141, 126, 111, 96, 81, 71,
                                  69, 68, 59, 50, 41, 32, 29, 26, 23, 20, 17, 16, 15, 14, 13,
                                  12, 9, 6, 3, 0 };
        static const int p2[] = { 240 };
        int got[128], n;
        n = path_of(0, true, first_fight, round2, got);
        expect_path("p1 health", got, n, p1, (int)N(p1));
        n = path_of(1, true, first_fight, round2, got);
        expect_path("p2 health", got, n, p2, 1);
        pws1_frame_decode(rec[round2], &m);
        assert(m.owner[0].health.value == 240 && m.owner[1].health.value == 240);
        assert(m.clock_valid && m.clock == 99);
        pws1_frame_decode(rec[ko], &m);
        assert(m.owner[0].health.valid && m.owner[0].health.value == 0);
        /* The KO came with 77 on the clock, 22 counted seconds in. */
        assert(m.clock_valid && m.clock == 77);
    }

    /* 3. Stones: P1 dropped its one when first hit; the CPU collected to
     *    three, transformed, lost them all when it ended, and did it again. */
    {
        static const int p1[] = { 1, 0 };
        static const int p2[] = { 1, 2, 3, 0, 1, 2, 3 };
        int got[128], n;
        n = path_of(0, false, first_fight, round2, got);
        expect_path("p1 stones", got, n, p1, 2);
        n = path_of(1, false, first_fight, round2, got);
        expect_path("p2 stones", got, n, p2, (int)N(p2));
    }

    /* 4. Pause: the frame counter the pings advance on stood still for the
     *    whole 3.7 s pause and moved on every other fought sample. */
    {
        unsigned moved = 0, pairs = 0, long_still = 0;
        for (unsigned i = first_fight + 1; i < ko; i++) {
            if (!pws1_fighting(rec[i]) || !pws1_fighting(rec[i - 1])) continue;
            pairs++;
            if (pws1_frame_counter(rec[i]) != pws1_frame_counter(rec[i - 1])) moved++;
            if (when[i] - when[i - 1] > 3.0) {
                long_still++;
                assert(when[i] > 343945.0 && when[i] < 343946.5);
                assert(pws1_frame_counter(rec[i]) - pws1_frame_counter(rec[i - 1]) < 60);
            }
        }
        assert(long_still == 1 && moved == pairs);
    }

    /* 5. The projection: the pans the offline pairing tool computed for the
     *    fighters in the retained snapshots (camera-brackets.json), to 1e-4. */
    {
        static const float want[13][2] = {
            { -0.67124f, 0.73329f }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 },
            { 0.00318f, 0.02866f }, { -0.19991f, 0.36345f }, { -0.47169f, 0.71057f },
            { -0.09978f, 0.16057f }, { -0.03318f, 0.05781f }, { 0.33972f, -0.47658f },
            { 0.27587f, -0.30910f },
        };
        for (unsigned k = 0; k < 13; k++) {
            if (!pws1_fighting(snap[k])) continue;
            for (int p = 0; p < 2; p++) {
                float pt[3], pan;
                fighter_pt(snap[k], p ? A1 : A0, pt);
                assert(pws1_project(snap[k], proj, pt, &pan));
                if (fabsf(pan - want[k][p]) > 1e-4f)
                    fprintf(stderr, "snapshot %u side %d: %f, want %f\n", k + 1, p, pan, want[k][p]);
                assert(fabsf(pan - want[k][p]) <= 1e-4f);
            }
        }
        {
            float pt[3], pan, flat[16], g[16];
            unsigned char *s = snap[7];
            fighter_pt(s, A0, pt);
            assert(!pws1_project(s, NULL, pt, &pan));                 /* no camera */
            memcpy(flat, proj, sizeof flat);
            flat[11] = 0.0f;                                          /* orthographic */
            assert(!pws1_project(s, flat, pt, &pan));
            memcpy(g, s + 0x27974, sizeof g);
            {
                float bent[16];
                memcpy(bent, g, sizeof g);
                bent[15] = 0.5f;                                      /* not affine */
                memcpy(s + 0x27974, bent, sizeof bent);
                assert(!pws1_project(s, proj, pt, &pan));
                memcpy(s + 0x27974, g, sizeof g);
            }
            /* Far along the view's +z axis: w = -z is negative, behind it. */
            pt[0] = 100000.0f * g[2]; pt[1] = 100000.0f * g[6]; pt[2] = 100000.0f * g[10];
            assert(!pws1_project(s, proj, pt, &pan));
        }
    }

    /* 6. Pickups, every fought sample of the capture. */
    {
        unsigned stones_ok = 0, over = 0, run = 0, worst_run = 0, props_min = 100, props_max = 0;
        unsigned placed = 0, present = 0, seen29 = 0, seen46 = 0, behind = 0, past_edge = 0;
        double first46 = 0, last46 = 0;
        for (unsigned i = 0; i < npick; i++) {
            pickup_sample s[PICKUP_SLOTS];
            unsigned stones = 0, props = 0;
            unsigned char *r = pk[i];
            assert(pws1_fighting(r));
            pws1_pickups_decode(r, proot[i], proj, s);
            for (unsigned k = 0; k < PICKUP_SLOTS; k++) {
                if (!s[k].present) continue;
                present++;
                {
                    float pt[3], pan;
                    bool on;
                    memcpy(pt, r + k * 0x3E0 + 0x30, 12);
                    /* Placed exactly while on screen; off screen it is
                     * silent (user, 2026-10-27): past an edge, or the one
                     * stone beside and behind the camera (343923.26 to
                     * 343925.05). */
                    on = pws1_project(r, proj, pt, &pan);
                    assert(s[k].placed == on);
                    if (on) {
                        placed++;
                        assert(s[k].pan == pan && s[k].pan >= -1.0f && s[k].pan <= 1.0f);
                        assert(s[k].distance >= 0.0f && s[k].distance < 3000.0f);
                    } else if (k == 2 && pwhen[i] > 343923.2 && pwhen[i] < 343925.1) {
                        behind++;
                    } else {
                        past_edge++;
                    }
                }
                switch (s[k].family) {
                case AUDIO_PING_STONE: stones++; assert(r[k * 0x3E0 + 4] == 5); break;
                case AUDIO_PING_PROP:  props++;  assert(r[k * 0x3E0 + 4] == 1); break;
                case AUDIO_PING_ITEM:
                    assert(r[k * 0x3E0 + 4] == 6 && s[k].variant == r[k * 0x3E0 + 0x3C8]);
                    /* Both items of the capture: code 8 from 343924, code 1
                     * from 343934.9 to the KO, blinking before it went. */
                    if (k == 29) { assert(s[k].variant == 8); seen29++; }
                    else if (k == 46) {
                        assert(s[k].variant == 1);
                        if (!seen46++) first46 = pwhen[i];
                        last46 = pwhen[i];
                    } else assert(0);
                    break;
                default: assert(0);   /* no food was dropped in this round */
                }
            }
            if (props < props_min) props_min = props;
            if (props > props_max) props_max = props;
            /* Stones lying in the arena plus stones held never pass 3 (one
             * can be neither for a while: before the third appears at a
             * round start, and after a transformation scatters them); 4
             * only for the moment of a pick-up or a drop. */
            {
                unsigned held = r[A0 + 0x77EC] + r[A1 + 0x77EC];
                if (stones + held <= 3) { stones_ok++; run = 0; }
                else { assert(stones + held == 4); over++; if (++run > worst_run) worst_run = run; }
            }
        }
        printf("pickups: %u samples, %u stone-accounting exceptions (longest %u), props %u..%u, "
               "item 29 in %u, item 46 in %u (%.2f..%.2f)\n",
               npick, over, worst_run, props_min, props_max, seen29, seen46, first46, last46);
        fflush(stdout);
        assert(stones_ok + over == npick);
        /* 19 samples in all, never more than 0.35 s at a time. */
        assert(over == 19 && worst_run == 7);
        assert(seen29 > 50 && seen46 > 100);
        assert(first46 > 343934.8 && first46 < 343935.0 && last46 > 343949.8);
        assert(props_min > 0);
        /* Off screen and silent: the stone beside and behind the camera from
         * 343923.26 to 343925.05 (36 samples: 34 behind it, the 2 either side
         * past the right edge, which the old clamp placed at +1), and 825
         * other sightings past an edge. */
        printf("pickups on screen %u, past an edge %u, behind the camera %u\n", placed, past_edge, behind);
        fflush(stdout);
        assert(placed + behind + past_edge == present && behind == 36 && placed > past_edge);
        /* Off-screen sounds switched on (Shift+F9): that stone pans hard
         * right (it lay right of the camera), as before the rule; off again,
         * silent. */
        {
        int checked = 0;
        for (unsigned i = 0; i < npick; i++) {
            pickup_sample s[PICKUP_SLOTS];
            float pt[3], pan;
            if (!(pwhen[i] > 343923.3 && pwhen[i] < 343925.0)) continue;
            memcpy(pt, pk[i] + 2 * 0x3E0 + 0x30, 12);
            if (pws1_project(pk[i], proj, pt, &pan)) continue;
            assert(audio_option_cycle(AUDIO_OPT_OFFSCREEN) == 1);
            pws1_pickups_decode(pk[i], proot[i], proj, s);
            assert(s[2].present && s[2].placed && s[2].pan == 1.0f);
            assert(audio_option_cycle(AUDIO_OPT_OFFSCREEN) == 0);
            pws1_pickups_decode(pk[i], proot[i], proj, s);
            assert(s[2].present && !s[2].placed);
            checked = 1;
            break;
        }
        assert(checked);
        }
    }

    /* 7. Pickup gates on one fought sample with item 46 lying out. */
    {
        unsigned i = 0;
        pickup_sample s[PICKUP_SLOTS];
        unsigned char *r;
        while (i < npick && !(pk[i][46 * 0x3E0] && pk[i][46 * 0x3E0 + 4] == 6)) i++;
        assert(i < npick);
        r = pk[i];
        pws1_pickups_decode(r, proot[i], proj, s);
        assert(s[46].present && s[46].placed && s[46].family == AUDIO_PING_ITEM);
        {
            /* Distance from the human fighter (side 0), by hand. */
            float a[3], b[3], d;
            memcpy(a, r + 46 * 0x3E0 + 0x30, 12);
            memcpy(b, r + A0 + 0x30, 12);
            d = sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
            assert(fabsf(s[46].distance - d) < 1e-2f);
            /* Both sides human: the nearer fighter counts. */
            r[0x27FF2] = 3;
            memcpy(r + A1 + 0x30, a, 12);
            pws1_pickups_decode(r, proot[i], proj, s);
            assert(s[46].distance < 1e-3f);
            memcpy(r + A1 + 0x30, b, 12);
            r[0x27FF2] = 0;                               /* nobody to measure from */
            pws1_pickups_decode(r, proot[i], proj, s);
            assert(s[46].present && !s[46].placed);
            r[0x27FF2] = 1;
        }
        {
            unsigned long long addr = proot[i] + 46ull * 0x3E0;
            memcpy(r + A1 + 0x7780, &addr, 8);            /* carried by the CPU */
            pws1_pickups_decode(r, proot[i], proj, s);
            assert(!s[46].present);
            memset(r + A1 + 0x7780, 0, 8);
        }
        r[46 * 0x3E0 + 0x3C8] = 16;                       /* food: the middle size */
        pws1_pickups_decode(r, proot[i], proj, s);
        assert(s[46].family == AUDIO_PING_FOOD && s[46].variant == 1);
        r[46 * 0x3E0 + 0x3C8] = 18;                       /* past the item table */
        pws1_pickups_decode(r, proot[i], proj, s);
        assert(!s[46].present);
        r[46 * 0x3E0 + 0x3C8] = 1;
        r[46 * 0x3E0 + 1] = 0;                            /* blinking: still there */
        pws1_pickups_decode(r, proot[i], proj, s);
        assert(s[46].present);
        r[46 * 0x3E0 + 1] = 1;
        r[46 * 0x3E0 + 2] = 4;                            /* not a class the collector takes */
        pws1_pickups_decode(r, proot[i], proj, s);
        assert(!s[46].present);
        r[46 * 0x3E0 + 2] = 3;
        r[46 * 0x3E0] = 0;                                /* record not in use */
        pws1_pickups_decode(r, proot[i], proj, s);
        assert(!s[46].present);
        r[46 * 0x3E0] = 1;
        {
            /* A stone or prop that is not shown is held, or broken: silent. */
            for (unsigned k = 0; k < PICKUP_SLOTS; k++) {
                unsigned char *o = r + k * 0x3E0;
                if (o[0] && (o[4] == 5 || o[4] == 1) && (o[2] == 2 || o[2] == 3)) {
                    pws1_pickups_decode(r, proot[i], proj, s);
                    bool was = s[k].present;
                    unsigned char shown = o[1];
                    o[1] = 0;
                    pws1_pickups_decode(r, proot[i], proj, s);
                    assert(!s[k].present);
                    o[1] = shown;
                    (void)was;
                }
            }
        }
        r[0x27FAD] = 2;                                   /* round decided: nothing */
        pws1_pickups_decode(r, proot[i], proj, s);
        for (unsigned k = 0; k < PICKUP_SLOTS; k++) assert(!s[k].present);
        r[0x27FAD] = 0;
        r[0x280C4] = 1;                                   /* two-view layout */
        pws1_pickups_decode(r, proot[i], proj, s);
        for (unsigned k = 0; k < PICKUP_SLOTS; k++) assert(!s[k].present);
        r[0x280C4] = 0;
    }

    /* 8. Match gates, each on its own, on a fought sample. */
    {
        unsigned char *s = rec[round2];
        set16(s, A0 + 0x7790, -1);                        /* the KO frame */
        pws1_frame_decode(s, &m);
        assert(m.owner[0].health.valid && m.owner[0].health.value == 0);
        set16(s, A0 + 0x7790, -2);                        /* not a value damage leaves */
        pws1_frame_decode(s, &m);
        assert(!m.owner[0].health.valid && m.owner[1].health.valid);
        set16(s, A0 + 0x7790, 241);                       /* over the cap */
        pws1_frame_decode(s, &m);
        assert(!m.owner[0].health.valid);
        set16(s, A0 + 0x7790, 240);
        s[A0 + 0x77EC] = 4;                               /* a fourth stone */
        pws1_frame_decode(s, &m);
        assert(!m.owner[0].resource.valid && m.owner[0].health.valid);
        s[A0 + 0x77EC] = 3;
        s[A0 + 3] = 1;                                    /* record names the other side */
        pws1_frame_decode(s, &m);
        assert(!m.owner[0].present && m.owner[1].present);
        s[A0 + 3] = 0;
        s[0x27E7D] = 1;                                   /* clock not counted */
        pws1_frame_decode(s, &m);
        assert(!m.clock_valid);
        s[0x27E7D] = 0;
        s[0x27E92] = 4;                                   /* no timer */
        pws1_frame_decode(s, &m);
        assert(!m.clock_valid);
        s[0x27E92] = 3;
        s[0x27FAC] = 2;                                   /* round intro */
        pws1_frame_decode(s, &m);
        assert(m.in_match && !m.owner[0].health.valid);
        s[0x27FAC] = 1;                                   /* before the intro */
        pws1_frame_decode(s, &m);
        assert(!m.in_match);
        s[0x27FAC] = 6;
        pws1_frame_decode(s, &m);
        assert(!m.in_match);
        s[0x27FAC] = 3;
        s[0x27E5D] = 0;                                   /* attract demo fights too */
        pws1_frame_decode(s, &m);
        assert(!m.in_match && !pws1_fighting(s));
        s[0x27E5D] = 4;
        s[0x27E58] = 0;
        pws1_frame_decode(s, &m);
        assert(!m.in_match);
    }

    /* Pickup announcements over every fought sample: a stone event per stone
     *  gained, for that side; a lifted event per new carried object, of its
     *  kind; and no food, since none appeared in this round -- the game's own
     *  slow recovery (240, 215, 208, 201, 202 ...) must not read as eating. */
    {
        pws1_pickup_watch w;
        unsigned stones = 0, rises = 0, lifts = 0, lifted = 0, food = 0;
        unsigned char prev_st[2] = { 0 };
        unsigned long long prev_c[2] = { 0 };
        int have = 0;
        memset(&w, 0, sizeof w);
        for (unsigned i = 0; i < npick; i++) {
            unsigned who[6], kind[6], n;
            unsigned char *r = pk[i];
            /* The fixture keeps fought samples only; across a gap (round two's
             * intro, 10 s, where health refills to 240) the hook sees frames
             * that are not fought, which forget everything. */
            if (i && pwhen[i] - pwhen[i - 1] > 1.0) { memset(&w, 0, sizeof w); have = 0; }
            n = pws1_watch_events(r, proot[i], &w, who, kind);
            for (unsigned p = 0; p < 2; p++) {
                unsigned a = p ? A1 : A0;
                unsigned char st = r[a + 0x77EC];
                unsigned long long c;
                unsigned want_stone, want_lift;
                memcpy(&c, r + a + 0x7780, 8);
                want_stone = have && st > prev_st[p];
                want_lift = have && c && c != prev_c[p];
                rises += want_stone;
                lifts += want_lift;
                for (unsigned k = 0; k < n; k++) {
                    if (who[k] != p) continue;
                    if (kind[k] == PICKUP_STONE) { assert(want_stone); stones++; want_stone = 0; }
                    else if (kind[k] == PICKUP_FOOD) {
                        short h0, h1;
                        food++;
                        memcpy(&h1, r + a + 0x7790, 2);
                        memcpy(&h0, pk[i - 1] + a + 0x7790, 2);
                        printf("  food event: side %u at %.2f (prev %.2f), health %d -> %d, sub %u step %u\n",
                               p, pwhen[i], pwhen[i - 1], h0, h1, r[0x27FAC], r[0x27FAD]);
                    }
                    else {
                        const unsigned char *o = r + (c - proot[i]);
                        assert(want_lift);
                        assert(kind[k] == (o[4] == 1 ? PICKUP_THROWABLE : PICKUP_ITEM));
                        lifted++;
                        want_lift = 0;
                    }
                }
                assert(!want_stone && !want_lift);
                prev_st[p] = st;
                prev_c[p] = c;
            }
            have = 1;
        }
        printf("pickup events: %u stones gained (%u rises), %u lifted (%u new carried), %u food\n",
               stones, rises, lifted, lifts, food);
        fflush(stdout);
        /* No object was carried in this round's fought samples, so lifting
         * rests on the code (0x14098AB00) and the constructed case below. */
        assert(stones == rises && rises > 3 && lifted == lifts && food == 0);
        /* Food: a lifted food item is food; eating (health +24 at once) is
         * food; the slow recovery is not. Constructed on a fought sample. */
        {
            unsigned char *r = pk[npick / 2];
            unsigned who[6], kind[6], n;
            short hp;
            memset(&w, 0, sizeof w);
            pws1_watch_events(r, proot[npick / 2], &w, who, kind);       /* seed */
            memcpy(&hp, r + A0 + 0x7790, 2);
            if (hp > 200) { hp = 100; memcpy(r + A0 + 0x7790, &hp, 2); pws1_watch_events(r, proot[npick / 2], &w, who, kind); }
            hp += 1; memcpy(r + A0 + 0x7790, &hp, 2);
            assert(pws1_watch_events(r, proot[npick / 2], &w, who, kind) == 0);
            hp += 24; memcpy(r + A0 + 0x7790, &hp, 2);
            n = pws1_watch_events(r, proot[npick / 2], &w, who, kind);
            assert(n == 1 && who[0] == 0 && kind[0] == PICKUP_FOOD);
            /* Lifted: the carried pointer naming a prop, then an item, then
             * food (which then suppresses the eating rule for 3 s). */
            {
                unsigned prop = 100;
                unsigned long long c;
                for (unsigned k = 0; k < 100; k++)
                    if (r[k * 0x3E0] && r[k * 0x3E0 + 4] == 1) { prop = k; break; }
                assert(prop < 100);
                c = proot[npick / 2] + prop * 0x3E0ull;
                memcpy(r + A0 + 0x7780, &c, 8);
                n = pws1_watch_events(r, proot[npick / 2], &w, who, kind);
                assert(n == 1 && kind[0] == PICKUP_THROWABLE);
                assert(pws1_watch_events(r, proot[npick / 2], &w, who, kind) == 0);   /* still held */
                r[prop * 0x3E0 + 4] = 6; r[prop * 0x3E0 + 0x3C8] = 3;
                memset(r + A0 + 0x7780, 0, 8);
                pws1_watch_events(r, proot[npick / 2], &w, who, kind);
                memcpy(r + A0 + 0x7780, &c, 8);
                n = pws1_watch_events(r, proot[npick / 2], &w, who, kind);
                assert(n == 1 && kind[0] == PICKUP_ITEM);
                r[prop * 0x3E0 + 0x3C8] = 16;
                memset(r + A0 + 0x7780, 0, 8);
                pws1_watch_events(r, proot[npick / 2], &w, who, kind);
                memcpy(r + A0 + 0x7780, &c, 8);
                n = pws1_watch_events(r, proot[npick / 2], &w, who, kind);
                assert(n == 1 && kind[0] == PICKUP_FOOD);
                hp += 48; memcpy(r + A0 + 0x7790, &hp, 2);           /* eaten: not twice */
                assert(pws1_watch_events(r, proot[npick / 2], &w, who, kind) == 0);
                r[prop * 0x3E0 + 4] = 1;
                memset(r + A0 + 0x7780, 0, 8);
            }
        }
    }

    printf("PASS: %u live PWS1 frames (%u in a match), %u fought pickup samples -- gate by phase "
           "and sub-state, health through KO with recovery, stones, clock, pause, projection = "
           "pairing tool, pickups by family and type, every gate, pickup announcements\n", count, in, npick);
    return 0;
}
