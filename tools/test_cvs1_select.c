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
#define MENU_MAX 64
static unsigned menu_count;
static double menu_t[MENU_MAX];
static BYTE menu_kind[MENU_MAX][2], menu_value[MENU_MAX][2];
static cvs1_select_sample menu[MENU_MAX];

/* Both menu fixtures share the record; the 2026-10-15 branch-B one
 * (tools/make_cvs1_versus_fixture.py) has magic CV1V. */
static void load_records(const char *path, const char *want)
{
    FILE *f = fopen(path, "rb");
    BYTE magic[4], raw[MENU_RECORD], *p;
    assert(f);
    assert(fread(magic, 1, 4, f) == 4 && !memcmp(magic, want, 4));
    assert(fread(&menu_count, 4, 1, f) == 1 && menu_count > 0 && menu_count <= MENU_MAX);
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

static void load_menus(void)
{ load_records("tools/fixtures/cvs1-select-menus-20261014.bin", "CV1M"); }

/* Check every moment of the loaded fixture against its expectation. 0xFE
 * marks a total-ratio value the trace cannot hold: it must equal the
 * injected global instead. */
static void check_moments(void)
{
    for (unsigned i = 0; i < menu_count; i++) {
        game_selection_choice c[GAME_SELECTION_OWNERS];
        cvs1_selection_copy(&menu[i], c);
        for (unsigned side = 0; side < 2; side++) {
            unsigned want = menu_value[i][side];
            if (want == 0xFE) want = (unsigned)menu[i].total_cursor[side];
            if (menu_kind[i][side] == 0xFF) {
                if (c[side].status != GAME_SELECTION_INACTIVE)
                    fprintf(stderr, "moment %.2f side %u: expected inactive, got kind %u '%ls'\n",
                           menu_t[i], side, c[side].kind, c[side].label);
                assert(c[side].status == GAME_SELECTION_INACTIVE);
            } else {
                if (c[side].status != GAME_SELECTION_VALID || c[side].kind != menu_kind[i][side] ||
                    c[side].value != want)
                    fprintf(stderr, "moment %.2f side %u: expected kind %u value %u, got status %d kind %u value %u\n",
                           menu_t[i], side, menu_kind[i][side], want,
                           (int)c[side].status, c[side].kind, c[side].value);
                assert(c[side].status == GAME_SELECTION_VALID);
                assert(c[side].kind == menu_kind[i][side] && c[side].value == want);
            }
        }
    }
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
    check_moments();

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

    unsigned arcade_menus = menu_count;

    /* 9. Branch B, live Versus and Training (2026-10-15): every moment decodes
     *    to its hand-read expectation on both sides and to nothing else. The
     *    ratio globals are injected: total-ratio cursors 1 and 3 (code-traced
     *    only), 4 points left on each side. */
    load_records("tools/fixtures/cvs1-select-versus-20261015.bin", "CV1V");
    for (unsigned i = 0; i < menu_count; i++) {
        menu[i].total_cursor[0] = 1; menu[i].total_cursor[1] = 3;
        menu[i].ratio_left[0] = 4;   menu[i].ratio_left[1] = 4;
        /* The stage menu has its own fixture (case 13); its globals are not
         * in these records, so mark it chosen here. */
        menu[i].stage_chosen = 1;
    }
    check_moments();

    /* 10. The words. */
    {
        game_selection_choice c[GAME_SELECTION_OWNERS];
        cvs1_selection_copy(&menu[moment(CVS1_KIND_TOTAL_RATIO, 0xFE)], c);
        assert(!wcscmp(c[0].label, L"Team ratio 2 of 4") && !wcscmp(c[1].label, L"Team ratio 4 of 4"));
        cvs1_selection_copy(&menu[moment(CVS1_KIND_RATIO, 1)], c);
        assert(!wcscmp(c[0].label, L"Ratio 2 of 4") && c[0].visit == 0);
        cvs1_selection_copy(&menu[moment(CVS1_KIND_RATIO, 0)], c);
        assert(!wcscmp(c[0].label, L"Ratio 1 of 4"));
        cvs1_selection_copy(&menu[moment(CVS1_KIND_GROOVE, 1)], c);
        assert(!wcscmp(c[0].label, L"SNK Groove"));
        cvs1_selection_copy(&menu[moment(CVS1_KIND_CHARACTER, 0x00)], c);
        assert(!wcscmp(c[0].label, L"Ryu"));
    }

    /* 11. Each branch-B gate refuses on its own, from live records. */
    {
        cvs1_select_sample r = menu[moment(CVS1_KIND_RATIO, 1)], g = menu[moment(CVS1_KIND_GROOVE, 1)];
        cvs1_select_sample k = menu[moment(CVS1_KIND_CHARACTER, 0x0C)], t = menu[2], h;
        assert(cvs1_ratio_decode(&r, 0) == 1 && cvs1_total_ratio_decode(&t, 0) == 1);
        h = r; h.phase[0x48] = 0;     assert(cvs1_ratio_decode(&h, 0) == -1);   /* branch A  */
        h = r; h.phase[0x48] = 2;     assert(cvs1_ratio_decode(&h, 0) == -1);   /* branch A  */
        h = r; h.phase[0x48] = 7;     assert(cvs1_ratio_decode(&h, 0) == 1);    /* branch B  */
        h = r; h.phase[3] = 3;        assert(cvs1_ratio_decode(&h, 0) == -1);   /* phase     */
        h = r; h.phase[0x65] = 2;     assert(cvs1_ratio_decode(&h, 0) == -1);   /* not human */
        h = r; h.actor[0][5] = 6;     assert(cvs1_ratio_decode(&h, 0) == -1);   /* countdown */
        h = r; h.owner[0] = 1;        assert(cvs1_ratio_decode(&h, 0) == -1);   /* owner     */
        h = r; h.phase[0xDE] = 1;     assert(cvs1_ratio_decode(&h, 0) == -1);   /* auto pick */
        h = r; h.phase[0x4B] = 1;     assert(cvs1_ratio_decode(&h, 0) == -1);
        h = r; h.ratio_left[0] = 0;   assert(cvs1_ratio_decode(&h, 0) == -2);   /* no points */
        h = r; h.ratio_left[0] = 1;   assert(cvs1_ratio_decode(&h, 0) == -2);   /* past left */
        h = r; h.phase[0xA0] = 4;     assert(cvs1_ratio_decode(&h, 0) == -2);   /* slot      */
        assert(cvs1_ratio_decode(&r, 1) == -1);                                 /* P2 state 4 */
        h = t; h.phase[0x48] = 4;     assert(cvs1_total_ratio_decode(&h, 0) == -1); /* Training */
        h = t; h.phase[0x4B] = 1;     assert(cvs1_total_ratio_decode(&h, 0) == -1);
        h = t; h.total_cursor[0] = 4; assert(cvs1_total_ratio_decode(&h, 0) == -2);
        h = t; h.actor[0][5] = 2;     assert(cvs1_total_ratio_decode(&h, 0) == -1); /* countdown */
        /* Branch A's groove screen shares the 02 01 01 02 prefix: mode 0 with
         * this record's step 0 is neither. */
        h = g; h.phase[0x48] = 0;     assert(cvs1_groove_decode(&h, 0) == -1);
        h = g; h.actor[0][5] = 4;     assert(cvs1_groove_decode(&h, 0) == -1);
        h = k; h.actor[0][4] = 4; h.actor[0][5] = 4;
        assert(cvs1_select_decode(&h, 0) == -1);                                /* state 4 is not the grid here */
        h = k; h.phase[0x79] = 0x23;  assert(cvs1_select_decode(&h, 0) == 0x0C); /* other slot ignored */
    }

    /* 12. Spoken in the live order through the shared channel. */
    {
        /* Names are the shell table's for the IDs read off the trace. Both
         * sides' totals open together and are grouped; P2's total is not
         * repeated while P1 moves on; one ratio menu per chosen character. */
        static const wchar_t *const want[] = {
            L"Team ratio 2 of 4. Team ratio 4 of 4",
            L"Capcom Groove", L"SNK Groove", L"Capcom Groove",
            L"Ryu", L"Cammy", L"Kyo", L"Nakoruru",
            L"Ratio 2 of 4", L"Ratio 1 of 4", L"Ratio 2 of 4", L"Ratio 1 of 4",
            L"Guile", L"King", L"Vice",
            L"Capcom Groove", L"SNK Groove", L"Capcom Groove",
            L"Kyo", L"Yuri", L"Ken", L"Dhalsim",
            L"Ratio 1 of 4", L"Ratio 3 of 4", L"Ratio 1 of 4", L"Ratio 2 of 4", L"Ratio 3 of 4",
            /* Training */
            L"Capcom Groove", L"SNK Groove", L"Capcom Groove",
            L"Ryu", L"Sakura", L"Kyo", L"Ryu",
            L"Capcom Groove", L"SNK Groove",
            L"Kyo", L"Yuri", L"Vice", L"Yuri" };
        unsigned n = 0;
        cvs1_select_observe(NULL, 0, 0);
        int before = calls;
        for (unsigned i = 0; i < menu_count; i++) {
            int was = calls;
            settle(&menu[i], 0x17741000ULL);
            if (calls != was) {
                assert(n < sizeof want / sizeof want[0]);
                if (wcscmp(last, want[n]))
                    fprintf(stderr, "versus spoken %u at %.2f: '%ls', want '%ls'\n", n, menu_t[i], last, want[n]);
                assert(!wcscmp(last, want[n]));
                n++;
            }
        }
        assert(n == sizeof want / sizeof want[0] && calls == before + (int)n);
    }

    /* 13. The branch-B stage menu (tools/make_cvs1_stage_fixture.py): its
     *     cursor recovered from the traced stage byte through the game's own
     *     permutation table. Shared owner, numbered words, the chosen skip. */
    unsigned stage_count;
    {
        FILE *f = fopen("tools/fixtures/cvs1-stage-select-20261015.bin", "rb");
        BYTE magic[4], raw[8 + 2 + CVS1_PHASE_SIZE + 2 * CVS1_ACTOR_HEAD + 2];
        static cvs1_select_sample st[32];
        static BYTE want[32];
        static double t[32];
        assert(f);
        assert(fread(magic, 1, 4, f) == 4 && !memcmp(magic, "CV1T", 4));
        assert(fread(&stage_count, 4, 1, f) == 1 && stage_count > 0 && stage_count <= 32);
        for (unsigned i = 0; i < stage_count; i++) {
            BYTE *p = raw + 10;
            assert(fread(raw, 1, sizeof raw, f) == sizeof raw);
            memset(&st[i], 0, sizeof st[i]);
            memcpy(&t[i], raw, 8);
            want[i] = raw[8];
            st[i].stage_cursor = (signed char)raw[9];
            memcpy(st[i].phase, p, CVS1_PHASE_SIZE);      p += CVS1_PHASE_SIZE;
            memcpy(st[i].actor[0], p, CVS1_ACTOR_HEAD);   p += CVS1_ACTOR_HEAD;
            memcpy(st[i].actor[1], p, CVS1_ACTOR_HEAD);   p += CVS1_ACTOR_HEAD;
            st[i].owner[0] = p[0]; st[i].owner[1] = p[1];
            st[i].ratio_left[0] = st[i].ratio_left[1] = 4;
            st[i].variant = 1;
        }
        assert(fgetc(f) == EOF);
        fclose(f);

        for (unsigned i = 0; i < stage_count; i++) {
            game_selection_choice c[GAME_SELECTION_OWNERS];
            int v = cvs1_stage_decode(&st[i]);
            if (v != (want[i] == 0xFF ? -1 : (int)want[i]))
                fprintf(stderr, "stage moment %.2f: got %d, want %u\n", t[i], v, want[i]);
            assert(v == (want[i] == 0xFF ? -1 : (int)want[i]));
            cvs1_selection_copy(&st[i], c);
            if (want[i] != 0xFF) {
                assert(c[CVS1_STAGE_OWNER].status == GAME_SELECTION_VALID &&
                       c[CVS1_STAGE_OWNER].kind == CVS1_KIND_STAGE && c[CVS1_STAGE_OWNER].value == want[i]);
                assert(c[0].status == GAME_SELECTION_INACTIVE && c[1].status == GAME_SELECTION_INACTIVE);
            } else {
                assert(c[CVS1_STAGE_OWNER].status == GAME_SELECTION_INACTIVE);
            }
        }
        {
            game_selection_choice c[GAME_SELECTION_OWNERS];
            cvs1_select_sample s = st[0];
            cvs1_selection_copy(&s, c);
            assert(!wcscmp(c[CVS1_STAGE_OWNER].label, L"Random stage"));
            s = st[3];
            cvs1_selection_copy(&s, c);
            assert(!wcscmp(c[CVS1_STAGE_OWNER].label, L"Ayuthaya Ruins"));
            s = st[0]; s.stage_chosen = 1;   assert(cvs1_stage_decode(&s) == -1); /* chosen     */
            s = st[0]; s.phase[0x48] = 0;    assert(cvs1_stage_decode(&s) == -1); /* branch A   */
            s = st[0]; s.actor[0][5] = s.actor[1][5] = 1;
            assert(cvs1_stage_decode(&s) == -1);                                  /* countdown  */
            s = st[0]; s.actor[1][4] = 0;    assert(cvs1_stage_decode(&s) == 0);  /* either side */
            s = st[0]; s.stage_cursor = 16;  assert(cvs1_stage_decode(&s) == -2);
            s = st[0]; s.stage_cursor = -1;  assert(cvs1_stage_decode(&s) == -2);
        }
        {
            static const wchar_t *const spoken[] = {
                L"Random stage", L"Suzaku Castle", L"Random stage", L"Ayuthaya Ruins", L"Random stage",
                L"Suzaku Castle", L"Ayuthaya City", L"Metro City",
                L"Team ratio 1 of 4. Team ratio 1 of 4",       /* the next menu */
                L"Random stage", L"Ayuthaya Ruins", L"Random stage" };
            unsigned n = 0;
            cvs1_select_observe(NULL, 0, 0);
            int before = calls;
            for (unsigned i = 0; i < stage_count; i++) {
                int was = calls;
                settle(&st[i], 0x17741000ULL);
                if (calls != was) {
                    assert(n < sizeof spoken / sizeof spoken[0]);
                    if (wcscmp(last, spoken[n]))
                        fprintf(stderr, "stage spoken %u at %.2f: '%ls', want '%ls'\n", n, t[i], last, spoken[n]);
                    assert(!wcscmp(last, spoken[n]));
                    n++;
                }
            }
            assert(n == sizeof spoken / sizeof spoken[0] && calls == before + (int)n);
        }
    }

    printf("PASS: %u live CVS1 highlights decoded, names by game version, shared-channel "
           "speech/retry/visit/depart, every gate; %u live menu moments: groove, speed, "
           "order, their gates and spoken sequence; %u live Versus/Training moments: "
           "total ratio, groove, grid, ratio and their gates; %u stage-menu moments\n",
           count, arcade_menus, menu_count, stage_count);
    return 0;
}
