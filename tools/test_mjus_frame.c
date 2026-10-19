/* Replays the 2026-10-19 Project Justice story capture (two fights, pjus)
 * through the shipping frame decoder. Fixture: tools/make_mjus_fixtures.py.
 * Run from the repository root. */
#include "../src/mjus_frame.h"
#include "../src/audio.h"
#include "../libs/minhook/include/MinHook.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The hooks are never installed here. */
MH_STATUS WINAPI MH_CreateHook(LPVOID t, LPVOID d, LPVOID *o) { (void)t; (void)d; (void)o; return MH_UNKNOWN; }
MH_STATUS WINAPI MH_EnableHook(LPVOID t) { (void)t; return MH_UNKNOWN; }
MH_STATUS WINAPI MH_DisableHook(LPVOID t) { (void)t; return MH_UNKNOWN; }
void mjus_text_capture(void) {}
BOOL mjus_text_install(void) { return FALSE; }
void mjus_text_remove(void) {}

#define F(p) (0x3B48u + (p) * 0x608u)
#define PER_FIGHTER (12 + 2 + 1 + 2)
#define RECORD (4 + 1 + 2 * PER_FIGHTER)
static unsigned count;
static float t_of[8192];
static unsigned char raw_of[8192][RECORD - 4];
static unsigned char *block;

static void load(void)
{
    FILE *f = fopen("tools/fixtures/mjus-match-frames-20261019.bin", "rb");
    unsigned char magic[4], raw[RECORD];
    assert(f && fread(magic, 1, 4, f) == 4 && !memcmp(magic, "MJMF", 4));
    assert(fread(&count, 4, 1, f) == 1 && count > 100 && count <= 8192);
    for (unsigned i = 0; i < count; i++) {
        assert(fread(raw, 1, RECORD, f) == RECORD);
        memcpy(&t_of[i], raw, 4);
        memcpy(raw_of[i], raw + 4, RECORD - 4);
    }
    assert(fgetc(f) == EOF);
    fclose(f);
    block = calloc(1, 0x9E8A8);
    assert(block);
}

static const unsigned char *at(unsigned i)
{
    const unsigned char *r = raw_of[i];
    block[0x21C0] = r[0];
    for (unsigned p = 0; p < 2; p++) {
        const unsigned char *q = r + 1 + p * PER_FIGHTER;
        memcpy(block + F(p) + 0xEC, q, 12);
        memcpy(block + F(p) + 0x2DE, q + 12, 2);
        block[F(p) + 0x2FF] = q[14];
        memcpy(block + F(p) + 0x3CC, q + 15, 2);
    }
    return block;
}

static int hp(unsigned i, unsigned p) { short v; memcpy(&v, raw_of[i] + 1 + p * PER_FIGHTER + 12, 2); return v; }
static int facing(unsigned i, unsigned p) { return raw_of[i][1 + p * PER_FIGHTER + 14] & 1; }
static float height(unsigned i, unsigned p) { float v; memcpy(&v, raw_of[i] + 1 + p * PER_FIGHTER + 4, 4); return v; }

/* ---- the camera run (mjus-5): world positions, heights, facing, view ---- */
#define CAM_FIGHTER (12 + 12 + 1)
#define CAM_RECORD (4 + 1 + 2 * CAM_FIGHTER + 64)
static float proj[16];
static unsigned cam_count;
static unsigned char cam_raw[2048][CAM_RECORD];

static void load_camera(void)
{
    FILE *f = fopen("tools/fixtures/mjus-camera-20261019.bin", "rb");
    unsigned char magic[4];
    assert(f && fread(magic, 1, 4, f) == 4 && !memcmp(magic, "MJCM", 4));
    assert(fread(proj, 4, 16, f) == 16);
    assert(fread(&cam_count, 4, 1, f) == 1 && cam_count > 500 && cam_count <= 2048);
    for (unsigned i = 0; i < cam_count; i++) assert(fread(cam_raw[i], 1, CAM_RECORD, f) == CAM_RECORD);
    assert(fgetc(f) == EOF);
    fclose(f);
}

/* Fills the block and the camera for camera record i. */
static const unsigned char *cam_at(unsigned i, float cam[32])
{
    const unsigned char *r = cam_raw[i] + 4;
    memset(block + 0x21C0, 0, 1);
    block[0x21C0] = r[0];
    for (unsigned p = 0; p < 2; p++) {
        const unsigned char *q = r + 1 + p * CAM_FIGHTER;
        memcpy(block + F(p) + 0xD0, q, 12);
        memcpy(block + F(p) + 0xEC, q + 12, 12);
        block[F(p) + 0x2FF] = q[24];
        short full = 200;
        memcpy(block + F(p) + 0x2DE, &full, 2);
    }
    memcpy(cam, r + 1 + 2 * CAM_FIGHTER, 64);
    memcpy(cam + 16, proj, 64);
    return block;
}

int main(void)
{
    match_audio_sample m;
    unsigned in_match = 0, flips = 0, kos = 0, first = ~0u;
    int last_hp0 = 999, prev_face = -1, max_guts = 0;
    load();
    load_camera();

    for (unsigned i = 0; i < count; i++) {
        mjus_frame_decode(at(i), NULL, &m);
        assert(m.in_match == (raw_of[i][0] == 2));
        assert(!m.clock_valid);                       /* no clock cue, by request */
        if (!m.in_match) { last_hp0 = 999; prev_face = -1; continue; }
        in_match++;
        if (first == ~0u) first = i;
        for (unsigned p = 0; p < 2; p++) {
            match_audio_owner *o = &m.owner[p];
            /* Health: 0..200, maximum 200; damage only ever lowers it in a fight. */
            assert(o->health.maximum == 200);
            assert(o->health.valid == (hp(i, p) >= 0 && hp(i, p) <= 200));
            /* Guts: five levels of 256. */
            assert(o->resource.valid && o->resource.maximum == 1280);
            assert(o->resource.value >= 0 && o->resource.value <= 1280);
            if (o->resource.value > max_guts) max_guts = o->resource.value;
            /* No camera, no beacon: the pan is never estimated. */
            assert(!o->beacon_valid);
            (void)height;
        }
        if (hp(i, 0) <= last_hp0) last_hp0 = hp(i, 0); else assert(!"P1 health rose mid-fight");
        if (hp(i, 0) == 0) kos++;
        if (prev_face >= 0 && prev_face != facing(i, 0)) flips++;
        prev_face = facing(i, 0);
    }

    /* The fight starts with full health and three guts levels. */
    mjus_frame_decode(at(first), NULL, &m);
    assert(m.owner[0].health.value == 200 && m.owner[1].health.value == 200);
    assert(m.owner[0].resource.value == 768 && m.owner[1].resource.value == 768);

    assert(in_match > 500 && kos > 0 && flips >= 2);
    assert(max_guts == 1280);                         /* P2 reached the fifth level */

    /* The beacons through the game's camera, five live fights. Ground truth
     * independent of the projection: the facing flag (0x140B98A1A swaps the
     * controls by it). The two agree except while the flag catches up after
     * a cross-over, so every disagreement must sit within a second of the
     * flag changing. Both fighters land on screen in nearly every sample. */
    {
        float cam[32], last_flip_t = -100.0f, t;
        unsigned on_screen = 0, agree = 0, opposite = 0, late = 0, airborne = 0, cam_flips = 0;
        int prev = -1;
        for (unsigned i = 0; i < cam_count; i++) {
            const unsigned char *r = cam_raw[i];
            int f0 = r[5 + 24] & 1, f1 = r[5 + CAM_FIGHTER + 24] & 1;
            memcpy(&t, r, 4);
            if (prev >= 0 && f0 != prev) { last_flip_t = t; cam_flips++; }
            prev = f0;
            mjus_frame_decode(cam_at(i, cam), cam, &m);
            assert(m.in_match);
            if (!m.owner[0].beacon_valid || !m.owner[1].beacon_valid) continue;
            for (unsigned p = 0; p < 2; p++) {
                float h;
                memcpy(&h, r + 5 + p * CAM_FIGHTER + 16, 4);
                assert(m.owner[p].screen_pan >= -1.0f && m.owner[p].screen_pan <= 1.0f);
                if (h > 0) { assert(m.owner[p].stance > AUDIO_STANCE_STAND); airborne += !p; }
                else assert(m.owner[p].stance == AUDIO_STANCE_STAND);
            }
            on_screen += fabsf(m.owner[0].screen_pan) < 1.0f && fabsf(m.owner[1].screen_pan) < 1.0f;
            if (f0 == f1) continue;
            opposite++;
            if ((m.owner[0].screen_pan < m.owner[1].screen_pan) == (f0 == 0)) agree++;
            else if (t - last_flip_t > 1.0f) late++;
        }
        printf("camera: %u samples, %u both on screen, side agrees %u of %u, %u late\n",
               cam_count, on_screen, agree, opposite, late);
        assert(on_screen * 100 >= cam_count * 97);
        assert(agree * 100 >= opposite * 93);
        assert(late * 100 <= opposite * 2);
        assert(airborne > 10 && cam_flips >= 4);

        /* Refusals: no camera, a camera that is not perspective, behind it. */
        {
            float pan, pt[3] = { 0, 0, 0 }, bad[32];
            cam_at(0, cam);
            memcpy(bad, cam, sizeof bad);
            bad[16 + 11] = 0.0f;                       /* [2][3]: no perspective divide */
            mjus_frame_decode(block, bad, &m);
            assert(!m.owner[0].beacon_valid && !m.owner[1].beacon_valid);
            mjus_frame_decode(block, NULL, &m);
            assert(!m.owner[0].beacon_valid && m.owner[0].health.valid);
            /* A point at the camera itself is behind the near plane. */
            pt[0] = -(cam[12] * cam[0] + cam[13] * cam[1] + cam[14] * cam[2]);
            pt[1] = -(cam[12] * cam[4] + cam[13] * cam[5] + cam[14] * cam[6]);
            pt[2] = -(cam[12] * cam[8] + cam[13] * cam[9] + cam[14] * cam[10]);
            assert(!mjus_project_x(cam, pt, &pan));
        }
    }

    /* Sentinels and limits. */
    {
        unsigned i = first;
        unsigned char *b = (unsigned char *)at(i);
        short v = -6;                                 /* the live KO frame */
        memcpy(b + F(0) + 0x2DE, &v, 2);
        mjus_frame_decode(b, NULL, &m);
        assert(!m.owner[0].health.valid && m.owner[1].health.valid);
        b = (unsigned char *)at(i);
        b[F(0) + 0x3CD] = 5; b[F(0) + 0x3CC] = 1;     /* past the fifth level */
        mjus_frame_decode(b, NULL, &m);
        assert(!m.owner[0].resource.valid);
        b[F(0) + 0x3CD] = 6; b[F(0) + 0x3CC] = 0;
        mjus_frame_decode(b, NULL, &m);
        assert(!m.owner[0].resource.valid);
        b = (unsigned char *)at(i);
        b[0x21C0] = 3;                                /* results screen */
        mjus_frame_decode(b, NULL, &m);
        assert(!m.in_match);
    }

    printf("PASS: %u Project Justice frames (%u in a fight): health 0..200 falling, KO sentinel, "
           "guts to five levels, %u cross-overs; %u camera samples: beacons from the game's "
           "projection agree with the facing flag, jump pitch, refusals; no clock\n",
           count, in_match, flips, cam_count);
    return 0;
}
