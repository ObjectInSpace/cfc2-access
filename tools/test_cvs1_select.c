/* Replays the 2026-10-13 live CVS1 capture through the shipping decoder and
 * the shared selection channel. No game, no audible output. Run from the
 * repository root. Fixture: tools/make_cvs1_select_fixture.py. */
#include "../src/cvs1_select.h"
#include "../src/character_name_table.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int calls, accepts = 1;
static wchar_t last[600];
BOOL speech_say(const wchar_t *s, BOOL interrupt)
{ (void)interrupt; calls++; wcsncpy(last, s, 599); return accepts; }
void log_line(const char *fmt, ...) { (void)fmt; }
BOOL mem_guard_install(void) { return FALSE; }
BOOL mem_copy(void *d, const void *s, SIZE_T n) { (void)d; (void)s; (void)n; return FALSE; }
/* Audio routing is exercised by run_match_audio_test; the tick is not run here. */
#include <stdint.h>
uint64_t match_audio_begin(unsigned a) { (void)a; return 0; }
void match_audio_depart(uint64_t e) { (void)e; }
uint64_t match_audio_current(void) { return 0; }
LONG cvs1_frame_count(void) { return 0; }
void cvs1_text_observe(ULONGLONG root) { (void)root; }   /* own suite */
void audio_beacon_set_owned(uint64_t e, unsigned p, BOOL l, float x, float s)
{ (void)e; (void)p; (void)l; (void)x; (void)s; }

#define RECORD (2 + CVS1_PHASE_SIZE + 2 * CVS1_ACTOR_HEAD + 2)
static unsigned count;
static BYTE side_of[256], id_of[256];
static cvs1_select_sample rec[256];

static void load(void)
{
    FILE *f = fopen("tools/fixtures/cvs1-select-live-20261013.bin", "rb");
    BYTE magic[4], raw[RECORD];
    assert(f);
    assert(fread(magic, 1, 4, f) == 4 && !memcmp(magic, "CV1S", 4));
    assert(fread(&count, 4, 1, f) == 1 && count > 0 && count <= 256);
    for (unsigned i = 0; i < count; i++) {
        assert(fread(raw, 1, RECORD, f) == RECORD);
        side_of[i] = raw[0];
        id_of[i] = raw[1];
        memcpy(rec[i].phase, raw + 2, CVS1_PHASE_SIZE);
        memcpy(rec[i].actor[0], raw + 2 + CVS1_PHASE_SIZE, CVS1_ACTOR_HEAD);
        memcpy(rec[i].actor[1], raw + 2 + CVS1_PHASE_SIZE + CVS1_ACTOR_HEAD, CVS1_ACTOR_HEAD);
        rec[i].owner[0] = raw[RECORD - 2];
        rec[i].owner[1] = raw[RECORD - 1];
        rec[i].variant = 1;                       /* the capture ran cvs1u */
    }
    assert(fgetc(f) == EOF);
    fclose(f);
}

/* Two observations settle a highlight (selection_channel). */
static void settle(const cvs1_select_sample *s, ULONGLONG root)
{
    cvs1_select_observe(s, root, 1);
    cvs1_select_observe(s, root, 1);
}

int main(void)
{
    load();

    /* 1. Every live highlight decodes to the ID the capture saw, on the side
     *    it saw it, and the other side is not selecting (CPU, actor state 0). */
    for (unsigned i = 0; i < count; i++) {
        assert(cvs1_select_decode(&rec[i], side_of[i]) == id_of[i]);
        assert(cvs1_select_decode(&rec[i], 1u - side_of[i]) == -1);
    }

    /* 2. Names come from the shell's own table, per game version. */
    {
        unsigned akuma = count;
        for (unsigned i = 0; i < count; i++) if (id_of[i] == 13) akuma = i;
        assert(akuma < count);                    /* ID 13 was highlighted live */
        game_selection_choice c[GAME_SELECTION_OWNERS];
        cvs1_selection_copy(&rec[akuma], c);
        assert(c[0].status == GAME_SELECTION_VALID && !wcscmp(c[0].label, L"Akuma"));
        assert(c[1].status == GAME_SELECTION_INACTIVE);
        rec[akuma].variant = 0;
        cvs1_selection_copy(&rec[akuma], c);
        assert(!wcscmp(c[0].label, L"Gouki"));    /* Japanese ROM name */
        rec[akuma].variant = 1;
    }

    /* 3. Spoken through the shared channel: settle, speak once, re-speak only
     *    on a new highlight. */
    {
        unsigned a = 0, b = 1;
        while (b < count && id_of[b] == id_of[a]) b++;
        assert(b < count);
        cvs1_select_observe(NULL, 0, 0);
        int before = calls;
        settle(&rec[a], 0x15d11000ULL);
        assert(calls == before + 1);
        {
            wchar_t want[64];
            swprintf(want, 64, L"%hs", character_name(0, id_of[a], 1));
            assert(!wcscmp(last, want));
        }
        settle(&rec[a], 0x15d11000ULL);
        assert(calls == before + 1);              /* no repeat */
        settle(&rec[b], 0x15d11000ULL);
        assert(calls == before + 2);
        {
            wchar_t want[64];
            swprintf(want, 64, L"%hs", character_name(0, id_of[b], 1));
            assert(!wcscmp(last, want));
        }
        /* A refused utterance is retried, not lost. */
        accepts = 0;
        settle(&rec[a], 0x15d11000ULL);
        int refused = calls;
        accepts = 1;
        cvs1_select_observe(&rec[a], 0x15d11000ULL, 1);
        assert(calls == refused + 1);
        /* The same character in the NEXT team slot is a new visit. */
        {
            cvs1_select_sample next = rec[a];
            unsigned slot = next.phase[0xA0];
            next.phase[0x78 + slot + 1] = next.phase[0x78 + slot];
            next.phase[0xA0] = (BYTE)(slot + 1);
            int before2 = calls;
            settle(&next, 0x15d11000ULL);
            assert(calls == before2 + 1);
        }
        /* Leaving CVS1 departs; returning speaks again. */
        cvs1_select_observe(NULL, 0, 0);
        int before3 = calls;
        settle(&rec[a], 0x15d11000ULL);
        assert(calls == before3 + 1);
    }

    /* 4. Every gate refuses on its own. */
    {
        cvs1_select_sample s;
        unsigned i = 0;
        s = rec[i]; s.phase[3] = 7;            assert(cvs1_select_decode(&s, 0) == -1); /* phase   */
        s = rec[i]; s.phase[0x48] = 1;         assert(cvs1_select_decode(&s, 0) == -1); /* mode    */
        s = rec[i]; s.actor[0][4] = 6;         assert(cvs1_select_decode(&s, 0) == -1); /* state   */
        s = rec[i]; s.owner[0] = 1;            assert(cvs1_select_decode(&s, 0) == -1); /* owner   */
        s = rec[i]; s.phase[0xA0] = 4;         assert(cvs1_select_decode(&s, 0) == -2); /* slot    */
        s = rec[i]; s.phase[0xA0] = 0xFF;      assert(cvs1_select_decode(&s, 0) == -2);
        s = rec[i]; s.phase[0xA0] = 0; s.phase[0x78] = 0xFF;
        assert(cvs1_select_decode(&s, 0) == -2);                                       /* none    */
        s.phase[0x78] = 0x23;                  assert(cvs1_select_decode(&s, 0) == -2); /* no name */
        s.phase[0x78] = 0x22; s.variant = 2;   assert(cvs1_select_decode(&s, 0) == -2); /* version */
        s.variant = 1;                         assert(cvs1_select_decode(&s, 0) == 0x22);
        assert(cvs1_select_decode(NULL, 0) == -1 && cvs1_select_decode(&s, 2) == -1);
    }

    /* 5. Groove choice, actor state 3. Synthetic from a live record: no
     *    capture has the groove screen yet. */
    {
        cvs1_select_sample g = rec[0];
        game_selection_choice c[GAME_SELECTION_OWNERS];
        g.actor[0][4] = 3;
        g.phase[0x76] = 0;
        assert(cvs1_select_decode(&g, 0) == -1);          /* not the grid */
        assert(cvs1_groove_decode(&g, 0) == 0);
        cvs1_selection_copy(&g, c);
        assert(c[0].status == GAME_SELECTION_VALID && c[0].kind == 1 &&
               !wcscmp(c[0].label, L"Capcom Groove"));
        assert(c[1].status == GAME_SELECTION_INACTIVE);     /* CPU side */
        g.phase[0x76] = 1;
        cvs1_selection_copy(&g, c);
        assert(c[0].value == 1 && !wcscmp(c[0].label, L"SNK Groove"));
        g.phase[0x76] = 2;
        assert(cvs1_groove_decode(&g, 0) == -2);
        cvs1_selection_copy(&g, c);
        assert(c[0].status == GAME_SELECTION_UNAVAILABLE);
        g.phase[0x76] = 1;
        { cvs1_select_sample h = g; h.owner[0] = 1;   assert(cvs1_groove_decode(&h, 0) == -1); }
        { cvs1_select_sample h = g; h.phase[0x48] = 1; assert(cvs1_groove_decode(&h, 0) == -1); }
        { cvs1_select_sample h = g; h.phase[3] = 7;    assert(cvs1_groove_decode(&h, 0) == -1); }
        /* Side 1 reads its own byte. */
        g.actor[1][4] = 3; g.owner[1] = 1; g.phase[0x77] = 0;
        assert(cvs1_groove_decode(&g, 1) == 0 && cvs1_groove_decode(&g, 0) == 1);

        /* Spoken: groove, the toggle, then the grid's first character. */
        g = rec[0]; g.actor[0][4] = 3; g.phase[0x76] = 0;
        cvs1_select_observe(NULL, 0, 0);
        int before = calls;
        settle(&g, 0x15d11000ULL);
        assert(calls == before + 1 && !wcscmp(last, L"Capcom Groove"));
        g.phase[0x76] = 1;
        settle(&g, 0x15d11000ULL);
        assert(calls == before + 2 && !wcscmp(last, L"SNK Groove"));
        settle(&rec[0], 0x15d11000ULL);
        assert(calls == before + 3);
        {
            wchar_t want[64];
            swprintf(want, 64, L"%hs", character_name(0, id_of[0], 1));
            assert(!wcscmp(last, want));
        }
    }

    printf("PASS: %u live CVS1 highlights decoded, names by game version, shared-channel "
           "speech/retry/visit/depart, every gate, groove choice\n", count);
    return 0;
}
