/* Replays the ten labelled CVS1 match snapshots (2026-10-13, Training, cvs1u)
 * through the shipping frame decoder. Labels: tools/fixtures/cvs1-match-20261013.
 * Run from the repository root. */
#include "../src/cvs1_frame.h"
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

#define P_OFF 0x3E58u
#define P_SIZE 0x112u
#define A_OFF 0x3FB8u
#define A_SIZE 0xE00u
static unsigned char *snap[10];

static void load(void)
{
    FILE *f = fopen("tools/fixtures/cvs1-match-frames-20261013.bin", "rb");
    unsigned char magic[4];
    unsigned count;
    assert(f && fread(magic, 1, 4, f) == 4 && !memcmp(magic, "CV1F", 4));
    assert(fread(&count, 4, 1, f) == 1 && count == 10);
    for (unsigned i = 0; i < 10; i++) {
        snap[i] = calloc(1, 0x32AB0);
        assert(snap[i]);
        assert(fread(snap[i] + P_OFF, 1, P_SIZE, f) == P_SIZE);
        assert(fread(snap[i] + A_OFF, 1, A_SIZE, f) == A_SIZE);
    }
    assert(fgetc(f) == EOF);
    fclose(f);
}

static void set16(unsigned char *s, unsigned off, short v) { memcpy(s + off, &v, 2); }

int main(void)
{
    match_audio_sample m;
    load();

    /* 1. Round start: both in the match, both at full health 14400/14400,
     *    P1 left of P2, both standing. */
    cvs1_frame_decode(snap[0], &m);
    assert(m.in_match && m.motion_valid);
    for (int p = 0; p < 2; p++) {
        assert(m.owner[p].present && m.owner[p].health.valid);
        assert(m.owner[p].health.value == 14400 && m.owner[p].health.maximum == 14400);
        assert(m.owner[p].beacon_valid && m.owner[p].stance == AUDIO_STANCE_STAND);
    }
    assert(m.owner[0].screen_pan < 0.0f && m.owner[1].screen_pan > 0.0f);
    assert(!m.clock_valid);                      /* not traced: no fabricated timer */
    assert(!m.owner[0].resource.valid);          /* power gauge not traced */

    /* 2. P1 damaged to 4320; P2 untouched. */
    cvs1_frame_decode(snap[1], &m);
    assert(m.owner[0].health.valid && m.owner[0].health.value == 4320);
    assert(m.owner[1].health.value == 14400);

    /* 4. Mid-jump: P1 airborne, pitch inside the airborne range; P2 standing. */
    cvs1_frame_decode(snap[3], &m);
    assert(m.owner[0].stance > AUDIO_STANCE_AIR && m.owner[0].stance <= AUDIO_STANCE_AIR_TOP);
    assert(m.owner[1].stance == AUDIO_STANCE_STAND);

    /* 3 and 5. Crouch and knockdown are not traced: standing pitch, never a
     * guessed rung. */
    cvs1_frame_decode(snap[2], &m);
    assert(m.owner[0].stance == AUDIO_STANCE_STAND);
    cvs1_frame_decode(snap[4], &m);
    assert(m.owner[0].stance == AUDIO_STANCE_STAND);

    /* 8 and 9. Corners: P1 hard left, then hard right. */
    cvs1_frame_decode(snap[7], &m);
    assert(m.owner[0].screen_pan <= -0.99f);
    cvs1_frame_decode(snap[8], &m);
    assert(m.owner[0].screen_pan >= 0.99f);

    /* 10. Paused: still the match phase. Pause is detected over time by the
     *     shared controller, not by phase. */
    cvs1_frame_decode(snap[9], &m);
    assert(m.in_match);

    /* Sentinels and gates. */
    {
        unsigned char *s = snap[0];
        set16(s, A_OFF + 0x1FC, -1);              /* 0x1405B4E3B's -1 */
        cvs1_frame_decode(s, &m);
        assert(!m.owner[0].health.valid && m.owner[1].health.valid);
        set16(s, A_OFF + 0x1FC, 14400);
        set16(s, A_OFF + 0x200, 0);               /* no maximum: no scale */
        cvs1_frame_decode(s, &m);
        assert(!m.owner[0].health.valid);
        set16(s, A_OFF + 0x200, 14400);
        memcpy(s + P_OFF, "\x02\x01\x01\x06\x06", 5);   /* character select */
        cvs1_frame_decode(s, &m);
        assert(!m.in_match && !m.owner[0].present);
    }

    printf("PASS: 10 labelled CVS1 snapshots -- health/max, KO sentinel, position pan, "
           "airborne pitch, untraced stances standing, match gate\n");
    return 0;
}
