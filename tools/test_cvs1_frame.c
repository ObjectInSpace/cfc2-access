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
void cvs1_text_capture(void) {}   /* run_cvs1_text_test */

#define P_OFF 0x3E58u
#define P_SIZE 0x112u
#define A_OFF 0x3FB8u
#define A_SIZE 0xE00u
#define RECORDS 13          /* 10 labelled Training snapshots + 3 Arcade moments */
static unsigned char *snap[RECORDS];

static void load(void)
{
    FILE *f = fopen("tools/fixtures/cvs1-match-frames-20261013.bin", "rb");
    unsigned char magic[4];
    unsigned count;
    assert(f && fread(magic, 1, 4, f) == 4 && !memcmp(magic, "CV1F", 4));
    assert(fread(&count, 4, 1, f) == 1 && count == RECORDS);
    for (unsigned i = 0; i < RECORDS; i++) {
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
    /* Training starts the gauge full: 144 of a 144 cap (groove flag 0), and
     * its clock is frozen at 99. */
    for (int p = 0; p < 2; p++)
        assert(m.owner[p].resource.valid && m.owner[p].resource.value == 144 &&
               m.owner[p].resource.maximum == 144);
    assert(m.clock_valid && m.clock == 99);

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

    /* 7. Right after the super: P1's gauge spent to 2, P2's still full. */
    cvs1_frame_decode(snap[6], &m);
    assert(m.owner[0].resource.valid && m.owner[0].resource.value == 2);
    assert(m.owner[1].resource.value == 144);

    /* Arcade: 64 before the super, 16 after (one level of 48), and the
     * round-intro sample where the gauge blips to 0 is NOT live: gauges are
     * invalid there, so no tone sweeps at a round start. */
    cvs1_frame_decode(snap[10], &m);
    assert(m.owner[0].resource.valid && m.owner[0].resource.value == 64 &&
           m.owner[0].resource.maximum == 144);
    cvs1_frame_decode(snap[11], &m);
    assert(m.owner[0].resource.valid && m.owner[0].resource.value == 16);
    cvs1_frame_decode(snap[12], &m);
    assert(m.in_match && !m.owner[0].resource.valid && !m.owner[0].health.valid);
    assert(m.clock_valid);

    /* The other groove's ceiling is 100. */
    {
        unsigned char *s = snap[11];
        s[A_OFF + 0x2C1] = 1;
        cvs1_frame_decode(s, &m);
        assert(m.owner[0].resource.valid && m.owner[0].resource.maximum == 100);
        s[A_OFF + 0x2D1] = 101;
        cvs1_frame_decode(s, &m);
        assert(!m.owner[0].resource.valid);   /* over the cap: no wrong fill */
    }

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

    printf("PASS: 10 labelled CVS1 snapshots + 3 Arcade moments -- health/max, gauge and "
           "groove caps, clock, live-round gate, KO sentinel, pan, airborne pitch, match gate\n");
    return 0;
}
