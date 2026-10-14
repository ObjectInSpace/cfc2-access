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

/* The 2026-10-13 grid fixture predates the wider phase copy: 0x112 bytes. */
#define GRID_PHASE 0x112u
#define RECORD (2 + GRID_PHASE + 2 * CVS1_ACTOR_HEAD + 2)
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
        memset(&rec[i], 0, sizeof rec[i]);
        side_of[i] = raw[0];
        id_of[i] = raw[1];
        memcpy(rec[i].phase, raw + 2, GRID_PHASE);
        memcpy(rec[i].actor[0], raw + 2 + GRID_PHASE, CVS1_ACTOR_HEAD);
        memcpy(rec[i].actor[1], raw + 2 + GRID_PHASE + CVS1_ACTOR_HEAD, CVS1_ACTOR_HEAD);
        rec[i].owner[0] = raw[RECORD - 2];
        rec[i].owner[1] = raw[RECORD - 1];
        rec[i].variant = 1;                       /* the capture ran cvs1u */
    }
    assert(fgetc(f) == EOF);
    fclose(f);
}

/* The 2026-10-14 select-menu fixture (tools/make_cvs1_menu_fixture.py):
 * groove, speed and order moments with hand-read expectations. */
#define MENU_RECORD (8 + 4 + CVS1_PHASE_SIZE + 2 * CVS1_ACTOR_HEAD + 4 + 8 + 32)
static unsigned menu_count;
static double menu_t[32];
static BYTE menu_kind[32][2], menu_value[32][2];
static cvs1_select_sample menu[32];

static void load_menus(void)
{
    FILE *f = fopen("tools/fixtures/cvs1-select-menus-20261014.bin", "rb");
    BYTE magic[4], raw[MENU_RECORD], *p;
    assert(f);
    assert(fread(magic, 1, 4, f) == 4 && !memcmp(magic, "CV1M", 4));
    assert(fread(&menu_count, 4, 1, f) == 1 && menu_count > 0 && menu_count <= 32);
    for (unsigned i = 0; i < menu_count; i++) {
        cvs1_select_sample *s = &menu[i];
        assert(fread(raw, 1, MENU_RECORD, f) == MENU_RECORD);
        memset(s, 0, sizeof *s);
        memcpy(&menu_t[i], raw, 8);
        menu_kind[i][0] = raw[8];  menu_value[i][0] = raw[9];
        menu_kind[i][1] = raw[10]; menu_value[i][1] = raw[11];
        p = raw + 12;
        memcpy(s->phase, p, CVS1_PHASE_SIZE);       p += CVS1_PHASE_SIZE;
        memcpy(s->actor[0], p, CVS1_ACTOR_HEAD);    p += CVS1_ACTOR_HEAD;
        memcpy(s->actor[1], p, CVS1_ACTOR_HEAD);    p += CVS1_ACTOR_HEAD;
        s->owner[0] = p[0]; s->owner[1] = p[1]; s->speed[0] = p[2]; s->speed[1] = p[3];
        p += 4;
        memcpy(s->team, p, 8);                      p += 8;
        memcpy(s->order, p, 32);
        s->variant = 1;
    }
    assert(fgetc(f) == EOF);
    fclose(f);
}

/* The record whose expectation for side 0 is (kind, value), first match. */
static unsigned moment(unsigned kind, unsigned value)
{
    for (unsigned i = 0; i < menu_count; i++)
        if (menu_kind[i][0] == kind && menu_value[i][0] == value) return i;
    assert(!"moment not in fixture");
    return 0;
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

    /* 5. Live select menus (2026-10-14): every moment decodes to the
     *    hand-read expectation on both sides, and to nothing else. */
    load_menus();
    for (unsigned i = 0; i < menu_count; i++) {
        game_selection_choice c[GAME_SELECTION_OWNERS];
        cvs1_selection_copy(&menu[i], c);
        for (unsigned side = 0; side < 2; side++) {
            if (menu_kind[i][side] == 0xFF) {
                if (c[side].status != GAME_SELECTION_INACTIVE)
                    fprintf(stderr, "moment %.2f side %u: expected inactive, got kind %u '%ls'\n",
                           menu_t[i], side, c[side].kind, c[side].label);
                assert(c[side].status == GAME_SELECTION_INACTIVE);
            } else {
                if (c[side].status != GAME_SELECTION_VALID || c[side].kind != menu_kind[i][side] ||
                    c[side].value != menu_value[i][side])
                    fprintf(stderr, "moment %.2f side %u: expected kind %u value %u, got status %d kind %u value %u\n",
                           menu_t[i], side, menu_kind[i][side], menu_value[i][side],
                           (int)c[side].status, c[side].kind, c[side].value);
                assert(c[side].status == GAME_SELECTION_VALID);
                assert(c[side].kind == menu_kind[i][side] && c[side].value == menu_value[i][side]);
            }
        }
    }

    /* 6. The words: grooves by the shell's names, speeds, and order as the
     *    fighter's name plus the fight it is being placed for. */
    {
        game_selection_choice c[GAME_SELECTION_OWNERS];
        cvs1_selection_copy(&menu[moment(CVS1_KIND_GROOVE, 0)], c);
        assert(!wcscmp(c[0].label, L"Capcom Groove"));
        cvs1_selection_copy(&menu[moment(CVS1_KIND_SPEED, 1)], c);
        assert(!wcscmp(c[0].label, L"Turbo"));
        cvs1_selection_copy(&menu[moment(CVS1_KIND_SPEED, 0)], c);
        assert(!wcscmp(c[0].label, L"Normal"));
        cvs1_selection_copy(&menu[moment(CVS1_KIND_ORDER, 0)], c);
        assert(!wcscmp(c[0].label, L"Ryu first") && c[0].visit == 0);
        cvs1_selection_copy(&menu[moment(CVS1_KIND_ORDER, 1)], c);
        assert(!wcscmp(c[0].label, L"Ken first"));
        /* SNK, from the live groove record with the byte flipped. */
        cvs1_select_sample g = menu[moment(CVS1_KIND_GROOVE, 0)];
        g.phase[0x76] = 1;
        cvs1_selection_copy(&g, c);
        assert(c[0].kind == CVS1_KIND_GROOVE && !wcscmp(c[0].label, L"SNK Groove"));
    }

    /* 7. Each menu gate refuses on its own, from live records. */
    {
        cvs1_select_sample g = menu[moment(CVS1_KIND_GROOVE, 0)], h;
        h = g; h.phase[0x113] = 0;  assert(cvs1_groove_decode(&h, 0) == -1); /* confirmed  */
        h = g; h.phase[0x65] = 0;   assert(cvs1_groove_decode(&h, 0) == -1); /* not human  */
        h = g; h.phase[4] = 6;      assert(cvs1_groove_decode(&h, 0) == -1); /* step       */
        h = g; h.phase[0x76] = 2;   assert(cvs1_groove_decode(&h, 0) == -2); /* bad value  */
        h = g; h.phase[0x113] = 3; h.phase[0x65] = 3; h.phase[0x77] = 1;
        assert(cvs1_groove_decode(&h, 1) == 1);                              /* side 1 own byte */
        /* The grid path's state 3 (code-traced, not seen live) still reads it. */
        h = rec[0]; h.actor[0][4] = 3; h.phase[0x76] = 1;
        assert(cvs1_groove_decode(&h, 0) == 1);

        cvs1_select_sample s = menu[moment(CVS1_KIND_SPEED, 1)];
        h = s; h.actor[0][4] = 4;   assert(cvs1_speed_decode(&h, 0) == -1);  /* state      */
        h = s; h.owner[0] = 1;      assert(cvs1_speed_decode(&h, 0) == -1);  /* owner      */
        h = s; h.phase[0x48] = 1;   assert(cvs1_speed_decode(&h, 0) == -1);  /* mode       */
        h = s; h.speed[0] = 2;      assert(cvs1_speed_decode(&h, 0) == -2);  /* bad value  */
        assert(cvs1_speed_decode(&s, 1) == -1);                              /* CPU side   */

        cvs1_select_sample o = menu[moment(CVS1_KIND_ORDER, 1)];
        h = o; h.phase[5] = 2;      assert(cvs1_order_decode(&h, 0) == -1);  /* step       */
        h = o; h.phase[0x64] = 2;   assert(cvs1_order_decode(&h, 0) == -1);  /* CPU side 0 */
        assert(cvs1_order_decode(&o, 1) == -1);                              /* CPU side 1 */
        h = o; h.phase[0x104] = 0;  assert(cvs1_order_decode(&h, 0) == -1);  /* one left   */
        h = o; h.phase[0x10C] = 2;  assert(cvs1_order_decode(&h, 0) == -2);  /* past count */
        h = o; h.order[0][1] = -1;  assert(cvs1_order_decode(&h, 0) == -2);  /* used slot  */
        h = o; h.team[0][1] = 0x23; assert(cvs1_order_decode(&h, 0) == -2);  /* no name    */
        h = o; h.team[0][1] = 0x0D; assert(cvs1_order_decode(&h, 0) == 0x0D);/* via team   */
        h = o; h.phase[0x106] = 1;
        {
            game_selection_choice c[GAME_SELECTION_OWNERS];
            cvs1_selection_copy(&h, c);
            assert(!wcscmp(c[0].label, L"Ken second") && c[0].visit == 1);
        }
    }

    /* 8. Spoken in the live order: groove, speed toggles, order toggles. */
    {
        static const wchar_t *const want[] = {
            L"Capcom Groove", L"Turbo", L"Normal", L"Turbo",
            L"Ryu first", L"Ken first", L"Ryu first", L"Ken first" };
        unsigned n = 0;
        cvs1_select_observe(NULL, 0, 0);
        int before = calls;
        for (unsigned i = 0; i < menu_count; i++) {
            int was = calls;
            settle(&menu[i], 0x15c71000ULL);
            if (calls != was) {
                assert(n < sizeof want / sizeof want[0]);
                if (wcscmp(last, want[n])) fprintf(stderr, "spoken %u: '%ls', want '%ls'\n", n, last, want[n]);
                assert(!wcscmp(last, want[n]));
                n++;
            }
        }
        assert(n == sizeof want / sizeof want[0] && calls == before + (int)n);
    }

    printf("PASS: %u live CVS1 highlights decoded, names by game version, shared-channel "
           "speech/retry/visit/depart, every gate; %u live menu moments: groove, speed, "
           "order, their gates and spoken sequence\n", count, menu_count);
    return 0;
}
