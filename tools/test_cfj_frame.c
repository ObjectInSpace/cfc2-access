/* Replays the twelve labelled CFJ match snapshots (2026-10-14, Arcade, cfe)
 * and three trace moments through the shipping frame decoder. Labels:
 * tools/fixtures/cfj-match-20261014. Run from the repository root. */
#include "../src/cfj_frame.h"
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

#define LOW_OFF 0xC8u
#define LOW_SIZE 0x110u
#define F_OFF 0x1E678u
#define F_SIZE 0xCC0u
#define F1 (F_OFF + 0x660u)
#define RECORDS 15      /* 12 labelled snapshots + round setup, KO, select */
static unsigned char *snap[RECORDS];

static void load(void)
{
    FILE *f = fopen("tools/fixtures/cfj-match-frames-20261014.bin", "rb");
    unsigned char magic[4];
    unsigned count;
    assert(f && fread(magic, 1, 4, f) == 4 && !memcmp(magic, "CFJF", 4));
    assert(fread(&count, 4, 1, f) == 1 && count == RECORDS);
    for (unsigned i = 0; i < RECORDS; i++) {
        snap[i] = calloc(1, 0x26E80);
        assert(snap[i]);
        assert(fread(snap[i] + LOW_OFF, 1, LOW_SIZE, f) == LOW_SIZE);
        assert(fread(snap[i] + F_OFF, 1, F_SIZE, f) == F_SIZE);
    }
    assert(fgetc(f) == EOF);
    fclose(f);
}

static void set16(unsigned char *s, unsigned off, short v) { memcpy(s + off, &v, 2); }

int main(void)
{
    match_audio_sample m;
    load();

    /* 1. Round start, during the intro: in the match, P1 left of P2, both
     *    standing, clock 99. Health is not fed until fighting starts. */
    cfj_frame_decode(snap[0], &m);
    assert(m.in_match && m.motion_valid);
    assert(m.owner[0].screen_pan < 0.0f && m.owner[1].screen_pan > 0.0f);
    for (int p = 0; p < 2; p++) {
        assert(m.owner[p].present && !m.owner[p].health.valid);
        assert(m.owner[p].beacon_valid && m.owner[p].stance == AUDIO_STANCE_STAND);
    }
    assert(m.clock_valid && m.clock == 99);

    /* 2. Fighting: P1 damaged to 13025 of 14800; P2 (Felicia) full 12600.
     *    Gauges by type: P1 type 0 caps at 115, P2 type 3 at 240. */
    cfj_frame_decode(snap[1], &m);
    assert(m.owner[0].health.valid && m.owner[0].health.value == 13025 &&
           m.owner[0].health.maximum == 14800);
    assert(m.owner[1].health.valid && m.owner[1].health.value == 12600 &&
           m.owner[1].health.maximum == 12600);
    assert(m.owner[0].resource.valid && m.owner[0].resource.value == 7 &&
           m.owner[0].resource.maximum == 115);
    assert(m.owner[1].resource.valid && m.owner[1].resource.value == 8 &&
           m.owner[1].resource.maximum == 240);
    assert(m.clock_valid && m.clock == 75);

    /* 3-5. Crouch is not traced: standing pitch, never a guessed rung. */
    for (int i = 2; i <= 4; i++) {
        cfj_frame_decode(snap[i], &m);
        assert(m.owner[0].stance == AUDIO_STANCE_STAND);
    }

    /* 6. Mid-jump: P1 airborne at 41.4, pitch inside the airborne range. */
    cfj_frame_decode(snap[5], &m);
    assert(m.owner[0].stance > AUDIO_STANCE_AIR && m.owner[0].stance <= AUDIO_STANCE_AIR_TOP);
    assert(m.owner[1].stance == AUDIO_STANCE_STAND);

    /* 8. Round over on time (stage 6, clock 0): still live, gauge 100 of 115;
     *    P2's 147 of 240. */
    cfj_frame_decode(snap[7], &m);
    assert(m.owner[0].resource.valid && m.owner[0].resource.value == 100);
    assert(m.owner[1].resource.valid && m.owner[1].resource.value == 147);
    assert(m.clock_valid && m.clock == 0);

    /* 9. Just after the super: P1's gauge spent to 0; P2's full at its cap. */
    cfj_frame_decode(snap[8], &m);
    assert(m.owner[0].resource.valid && m.owner[0].resource.value == 0);
    assert(m.owner[1].resource.valid && m.owner[1].resource.value == 240);

    /* 10 and 11. Corners: P1 hard left, then hard right. */
    cfj_frame_decode(snap[9], &m);
    assert(m.owner[0].screen_pan <= -0.99f);
    cfj_frame_decode(snap[10], &m);
    assert(m.owner[0].screen_pan >= 0.99f);

    /* 12. Paused: still the match phase. Pause is detected over time by the
     *     shared controller (and the worker's frame-stall guard), not by phase. */
    cfj_frame_decode(snap[11], &m);
    assert(m.in_match);

    /* Round setup: the fighter records are cleared and the gauge reads 0;
     * nothing is fed, so no tone sweeps at a round start. */
    cfj_frame_decode(snap[12], &m);
    assert(m.in_match);
    for (int p = 0; p < 2; p++)
        assert(!m.owner[p].health.valid && !m.owner[p].resource.valid);

    /* KO frame: P1's health holds 0x1407E640D's -1 sentinel. */
    cfj_frame_decode(snap[13], &m);
    assert(!m.owner[0].health.valid && m.owner[1].health.valid);

    /* Character select is not a match. */
    cfj_frame_decode(snap[14], &m);
    assert(!m.in_match && !m.owner[0].present);

    /* Every gauge type's cap, and no guessed cap. */
    {
        static const int cap[6] = { 115, 170, 200, 240, 180, 210 };
        unsigned char *s = snap[1];
        for (int t = 0; t < 6; t++) {
            s[F_OFF + 0x221] = (unsigned char)t;
            set16(s, F_OFF + 0x294, (short)cap[t]);
            cfj_frame_decode(s, &m);
            assert(m.owner[0].resource.valid && m.owner[0].resource.maximum == cap[t]);
            set16(s, F_OFF + 0x294, (short)(cap[t] + 1));
            cfj_frame_decode(s, &m);
            assert(!m.owner[0].resource.valid);    /* over the cap: no wrong fill */
        }
        s[F_OFF + 0x221] = 6;
        set16(s, F_OFF + 0x294, 10);
        cfj_frame_decode(s, &m);
        assert(!m.owner[0].resource.valid);        /* unknown type: silent */
        s[F_OFF + 0x221] = 0;
    }

    /* Live-round gate, maximum, phase. */
    {
        unsigned char *s = snap[1];
        s[0xCC] = 8;                                  /* transition */
        cfj_frame_decode(s, &m);
        assert(m.in_match && !m.owner[0].health.valid);
        s[0xCC] = 7;                                  /* round over */
        cfj_frame_decode(s, &m);
        assert(m.owner[0].health.valid);
        s[0xCC] = 4;
        set16(s, F_OFF + 0x170, 0);                   /* no maximum: no scale */
        cfj_frame_decode(s, &m);
        assert(!m.owner[0].health.valid);
        set16(s, F_OFF + 0x170, 14800);
        set16(s, F1 + 0x16C, 12601);                  /* over the maximum */
        cfj_frame_decode(s, &m);
        assert(!m.owner[1].health.valid);
        set16(s, F1 + 0x16C, 12600);
        s[0xCA] = 1;                                  /* select's 02 01 01 01 */
        cfj_frame_decode(s, &m);
        assert(!m.in_match);
    }

    printf("PASS: 12 labelled CFJ snapshots + round setup, KO and select -- health/max, "
           "gauge caps by type, clock, live-round gate, KO sentinel, pan, airborne pitch, "
           "match gate\n");
    return 0;
}
