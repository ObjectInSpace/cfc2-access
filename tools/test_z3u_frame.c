/* Replays the thirteen labelled Alpha 3 match snapshots (2026-10-14, Arcade,
 * z3u) and three trace moments through the shipping frame decoder. Labels:
 * tools/fixtures/z3u-match-20261014. Run from the repository root. */
#include "../src/z3u_frame.h"
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
void z3u_text_capture(const z3u_branch *b) { (void)b; }   /* run_z3u_text_test */
void z3u_text_collect(const z3u_branch *b) { (void)b; }

#define SIZE 0x12F98u
#define P0 0x1B0u
#define P1 (0x1B0u + 0x550u)
#define RECORDS 16      /* 13 labelled snapshots + intro fill, KO, select */
static const struct { unsigned off, n; } g_regions[] = { { 0x0, 0x100 }, { 0x1B0, 0xAA0 }, { 0xC548, 0x18 } };
static unsigned char *snap[RECORDS];

static void load(void)
{
    FILE *f = fopen("tools/fixtures/z3u-match-frames-20261014.bin", "rb");
    unsigned char magic[4];
    unsigned count;
    assert(f && fread(magic, 1, 4, f) == 4 && !memcmp(magic, "Z3UF", 4));
    assert(fread(&count, 4, 1, f) == 1 && count == RECORDS);
    for (unsigned i = 0; i < RECORDS; i++) {
        snap[i] = calloc(1, SIZE);
        assert(snap[i]);
        for (unsigned r = 0; r < 3; r++)
            assert(fread(snap[i] + g_regions[r].off, 1, g_regions[r].n, f) == g_regions[r].n);
    }
    assert(fgetc(f) == EOF);
    fclose(f);
}

static void set16(unsigned char *s, unsigned off, short v) { memcpy(s + off, &v, 2); }

int main(void)
{
    match_audio_sample m;
    load();

    /* 1. Round start: fighting, P1 (Ken) left of P2 (R.Mika), both standing,
     *    full health 144 of 144, both gauges full (this version starts the
     *    match with them full), clock 99. Screen X 116 and 268 of 384. */
    z3u_frame_decode(snap[0], &m);
    assert(m.in_match && m.motion_valid);
    assert(m.owner[0].screen_pan < -0.35f && m.owner[0].screen_pan > -0.45f);
    assert(m.owner[1].screen_pan > 0.35f && m.owner[1].screen_pan < 0.45f);
    for (int p = 0; p < 2; p++) {
        assert(m.owner[p].present && m.owner[p].beacon_valid);
        assert(m.owner[p].stance == AUDIO_STANCE_STAND);
        assert(m.owner[p].health.valid && m.owner[p].health.value == 144 &&
               m.owner[p].health.maximum == 144);
        assert(m.owner[p].resource.valid && m.owner[p].resource.value == 144 &&
               m.owner[p].resource.maximum == 144);
    }
    assert(m.clock_valid && m.clock == 99);

    /* 2. P1 damaged to 133; P2 in the air (height 72). */
    z3u_frame_decode(snap[1], &m);
    assert(m.owner[0].health.valid && m.owner[0].health.value == 133);
    assert(m.owner[0].stance == AUDIO_STANCE_STAND);
    assert(m.owner[1].stance > AUDIO_STANCE_AIR && m.owner[1].stance <= AUDIO_STANCE_AIR_TOP);
    assert(m.clock == 96);

    /* 3-5. Crouch is not traced: standing pitch, never a guessed rung. */
    for (int i = 2; i <= 4; i++) {
        z3u_frame_decode(snap[i], &m);
        assert(m.owner[0].stance == AUDIO_STANCE_STAND);
    }

    /* 6. Mid-jump: P1 at height 79.75, pitch inside the airborne range. */
    z3u_frame_decode(snap[5], &m);
    assert(m.owner[0].stance > AUDIO_STANCE_AIR && m.owner[0].stance <= AUDIO_STANCE_AIR_TOP);
    assert(m.owner[1].stance == AUDIO_STANCE_STAND);

    /* 8. Gauge stocked: 144 of 144 (X-ism's single level). */
    z3u_frame_decode(snap[7], &m);
    assert(m.owner[0].resource.valid && m.owner[0].resource.value == 144);

    /* 9. Just after the super: P1's gauge spent to 0; P2 health 54. */
    z3u_frame_decode(snap[8], &m);
    assert(m.owner[0].resource.valid && m.owner[0].resource.value == 0);
    assert(m.owner[1].health.valid && m.owner[1].health.value == 54);

    /* 10. Taken on the continue screen (screen word 4): not a match. */
    z3u_frame_decode(snap[9], &m);
    assert(!m.in_match && !m.owner[0].present);

    /* 11 and 12. Corners: P1 hard left (screen X 24), then hard right (360). */
    z3u_frame_decode(snap[10], &m);
    assert(m.owner[0].screen_pan <= -0.87f);
    z3u_frame_decode(snap[11], &m);
    assert(m.owner[0].screen_pan >= 0.87f);

    /* 13. Paused: still the match. Pause is the worker's frame-stall guard. */
    z3u_frame_decode(snap[12], &m);
    assert(m.in_match);

    /* Intro of the new match: health refilled and the gauge animating up.
     * Nothing is fed, so no tone sweeps at a round start. */
    z3u_frame_decode(snap[13], &m);
    assert(m.in_match);
    for (int p = 0; p < 2; p++)
        assert(!m.owner[p].health.valid && !m.owner[p].resource.valid);

    /* KO frame: P1's health holds the -1 sentinel. */
    z3u_frame_decode(snap[14], &m);
    assert(!m.owner[0].health.valid && m.owner[1].health.valid);

    /* Character select is not a match. */
    z3u_frame_decode(snap[15], &m);
    assert(!m.in_match && !m.owner[0].present);

    /* Gates and bounds. */
    {
        unsigned char *s = snap[1];
        set16(s, 0x2E, 2);                              /* intro */
        z3u_frame_decode(s, &m);
        assert(m.in_match && !m.owner[0].health.valid && !m.owner[0].resource.valid);
        set16(s, 0x2E, 5);                              /* round over */
        z3u_frame_decode(s, &m);
        assert(m.owner[0].health.valid);
        set16(s, 0x2E, 6);
        z3u_frame_decode(s, &m);
        assert(!m.owner[0].health.valid);
        set16(s, 0x2E, 3);
        set16(s, P1 + 0x88, 145);                       /* over the maximum */
        z3u_frame_decode(s, &m);
        assert(!m.owner[1].health.valid);
        set16(s, P1 + 0x88, 144);
        set16(s, P0 + 0x1EE, 145);                      /* over the gauge cap */
        z3u_frame_decode(s, &m);
        assert(!m.owner[0].resource.valid);
        set16(s, P0 + 0x1EE, 144);
        z3u_frame_decode(s, &m);
        assert(m.owner[0].resource.valid);
        s[0xF1] = 100;                                  /* not a clock value */
        z3u_frame_decode(s, &m);
        assert(!m.clock_valid);
        s[0xF1] = 96;
        set16(s, 0x2C, 2);                              /* select screen */
        z3u_frame_decode(s, &m);
        assert(!m.in_match);
        set16(s, 0x2C, 3);
        set16(s, 0x2A, 1);                              /* game mode */
        z3u_frame_decode(s, &m);
        assert(!m.in_match);
        set16(s, 0x2A, 2);
        set16(s, 0x28, 0);                              /* game task idle */
        z3u_frame_decode(s, &m);
        assert(!m.in_match);
        set16(s, 0x28, 1);
        /* The camera moves the pan, not the fighter: same world X, camera
         * scrolled right by 96 pixels, pan shifts by 0.5. */
        {
            float before, after;
            z3u_frame_decode(s, &m);
            before = m.owner[0].screen_pan;
            set16(s, 0xC55A, (short)(383 + 96));
            z3u_frame_decode(s, &m);
            after = m.owner[0].screen_pan;
            assert(before - after > 0.49f && before - after < 0.51f);
        }
    }

    /* 8. The alternate copy (z3uu/a3uu). No live capture of it exists yet, so
     *    each labelled snapshot is moved into an alternate-shaped block by the
     *    traced field map (z3u_branch.h): root words to +0x50 in place, the
     *    clock and camera moved, each player object rebased with every byte
     *    from +0x51 shifted by 8. The alternate decode must equal the
     *    ordinary one, and the ordinary layout over that block must not --
     *    a check of the plumbing against the map, not of the map itself. */
    /*    The relocation uses the traced numbers as literals, never the
     *    struct under test, or a wrong entry would move both sides at once. */
    {
        unsigned char *alt = malloc(0x1B528);
        assert(alt && z3u_alternate.state_size == 0x1B528);
        for (unsigned i = 0; i < RECORDS; i++) {
            match_audio_sample a, o;
            memset(alt, 0, 0x1B528);
            memcpy(alt, snap[i], 0x51);
            alt[0x129] = snap[i][0xF1];                               /* clock  */
            memcpy(alt + 0xE4EA, snap[i] + 0xC55A, 2);                /* camera */
            for (unsigned p = 0; p < 2; p++) {
                const unsigned char *src = snap[i] + 0x1B0 + p * 0x550;
                unsigned char *dst = alt + 0x1F8 + p * 0x580;
                memcpy(dst, src, 0x51);
                memcpy(dst + 0x59, src + 0x51, 0x550 - 0x51);
            }
            z3u_frame_decode(snap[i], &o);
            z3u_frame_decode_branch(&z3u_alternate, alt, &a);
            assert(a.in_match == o.in_match && a.clock_valid == o.clock_valid && a.clock == o.clock);
            assert(a.motion_valid == o.motion_valid && a.motion_signature == o.motion_signature);
            for (unsigned p = 0; p < 2; p++) {
                assert(a.owner[p].health.valid == o.owner[p].health.valid &&
                       a.owner[p].health.value == o.owner[p].health.value);
                assert(a.owner[p].resource.valid == o.owner[p].resource.valid &&
                       a.owner[p].resource.value == o.owner[p].resource.value);
                assert(a.owner[p].screen_pan == o.owner[p].screen_pan &&
                       a.owner[p].stance == o.owner[p].stance);
            }
        }
        {
            /* The ordinary layout reads the round-start block wrongly. */
            match_audio_sample a, o;
            z3u_frame_decode(snap[0], &o);
            memset(alt, 0, 0x1B528);
            memcpy(alt, snap[0], 0x51);
            for (unsigned p = 0; p < 2; p++)
                memcpy(alt + 0x1F8 + p * 0x580 + 0x59, snap[0] + 0x1B0 + p * 0x550 + 0x51,
                       0x550 - 0x51);
            z3u_frame_decode(alt, &a);
            assert(a.owner[0].health.value != o.owner[0].health.value ||
                   a.owner[1].health.value != o.owner[1].health.value);
        }
        free(alt);
    }

    printf("PASS: 13 labelled Alpha 3 snapshots + intro fill, KO and select -- health, "
           "super gauge, clock, live-round gate, KO sentinel, camera pan, airborne pitch, "
           "match gate; alternate-copy layout matches the ordinary decode\n");
    return 0;
}
