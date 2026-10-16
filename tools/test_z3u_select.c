/* Replays the 2026-10-14 live Alpha 3 capture through the shipping decoder and
 * the shared selection channel. No game, no audible output. Run from the
 * repository root. Fixture: tools/make_z3u_select_fixture.py. */
#include "../src/z3u_select.h"
#include "../src/character_name_table.h"
#include "../src/z3u_branch.h"
#include "../src/frame_liveness.h"
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
LONG z3u_frame_count(void) { return 0; }
void z3u_text_observe(ULONGLONG root) { (void)root; }   /* run_z3u_text_test */
void audio_beacon_set_owned(uint64_t e, unsigned p, BOOL l, float x, float s)
{ (void)e; (void)p; (void)l; (void)x; (void)s; }

/* The 2026-10-14 fixture predates the mode field: six fields per side. */
#define OLD_FIELDS 6
#define RECORD (6 + 3 + 2 * OLD_FIELDS)
#define ROOT 0x16c71000ULL
static unsigned count;
static z3u_select_sample rec[512];

static void load(void)
{
    FILE *f = fopen("tools/fixtures/z3u-select-live-20261014.bin", "rb");
    BYTE magic[4], raw[RECORD];
    assert(f);
    assert(fread(magic, 1, 4, f) == 4 && !memcmp(magic, "Z3US", 4));
    assert(fread(&count, 4, 1, f) == 1 && count > 0 && count <= 512);
    for (unsigned i = 0; i < count; i++) {
        assert(fread(raw, 1, RECORD, f) == RECORD);
        memcpy(rec[i].mode, raw, 6);
        rec[i].speed = raw[6];
        rec[i].entered = raw[7];
        rec[i].joined = raw[8];
        memset(rec[i].obj, 0, sizeof rec[i].obj);
        memcpy(rec[i].obj[0], raw + 9, OLD_FIELDS);
        memcpy(rec[i].obj[1], raw + 9 + OLD_FIELDS, OLD_FIELDS);
        rec[i].variant = 1;                       /* the capture ran a3u */
        rec[i].id_limit = z3u_ordinary.id_limit;
    }
    assert(fgetc(f) == EOF);
    fclose(f);
}

static void settle(const z3u_select_sample *s)
{
    z3u_select_observe(s, ROOT, 7);
    z3u_select_observe(s, ROOT, 7);
}

/* First record whose side-0 decode is (kind, value). */
static unsigned find(unsigned kind, int value)
{
    for (unsigned i = 0; i < count; i++) {
        unsigned k;
        if (z3u_select_decode(&rec[i], 0, &k) == value && k == kind) return i;
    }
    return count;
}

int main(void)
{
    /* The highlights, in order, from the trace's own cursor bytes (object
     * +0x12 x, +0x16 y) through the grid 0x1412DBB90: Ryu at (0,3), right
     * along row 3 to Ken, wrap to Ryu, up to Dee Jay, down to Juli, Ryu,
     * Juli, right along row 4 to Cammy, up to Karin and R.Mika, right to
     * Birdie (picked). Then the ism menu Z/A -> V -> Z/A -> X (picked), and
     * the speed menu Turbo -> Normal -> Turbo (picked). */
    static const struct { unsigned kind; int value; } path[] = {
        {0, 0}, {0, 24}, {0, 27}, {0, 29}, {0, 28}, {0, 25}, {0, 1}, {0, 0}, {0, 34},
        {0, 30}, {0, 0}, {0, 30}, {0, 15}, {0, 6}, {0, 22}, {0, 29}, {0, 26}, {0, 8},
        {1, 1}, {1, 2}, {1, 1}, {1, 0},
        {2, 1}, {2, 0}, {2, 1},
    };
    struct { unsigned kind; int value; } seen[64];
    int n = 0;
    load();

    /* 1. The decoded sequence on the human side is exactly that path, and the
     *    CPU side never reads as choosing. */
    for (unsigned i = 0; i < count; i++) {
        unsigned k = 99, k1;
        int v = z3u_select_decode(&rec[i], 0, &k);
        assert(z3u_select_decode(&rec[i], 1, &k1) == -1);
        assert(v != -2);
        if (v >= 0 && (!n || seen[n - 1].kind != k || seen[n - 1].value != v)) {
            assert(n < 64);
            seen[n].kind = k;
            seen[n].value = v;
            n++;
        }
    }
    assert(n == (int)(sizeof path / sizeof path[0]));
    for (int i = 0; i < n; i++) assert(seen[i].kind == path[i].kind && seen[i].value == path[i].value);

    /* 2. The match that follows is not a select screen. */
    {
        unsigned k;
        assert(z3u_select_decode(&rec[count - 1], 0, &k) == -1);
        assert(rec[count - 1].obj[0][Z3U_ID] == 8);          /* Birdie fights */
        assert((signed char)rec[count - 1].obj[0][Z3U_ISM] == -1);   /* X-ism */
    }

    /* 3. Labels: names per game version, isms per release, speed, random. */
    {
        game_selection_choice c[GAME_SELECTION_OWNERS];
        z3u_select_sample s = rec[find(0, 28)];
        z3u_selection_copy(&s, c);
        assert(c[0].status == GAME_SELECTION_VALID && c[0].kind == 0 && !wcscmp(c[0].label, L"Vega"));
        assert(c[1].status == GAME_SELECTION_INACTIVE);
        s.variant = 0;
        z3u_selection_copy(&s, c);
        assert(!wcscmp(c[0].label, L"Balrog"));             /* Japanese ROM name */
        s = rec[find(1, 1)];
        z3u_selection_copy(&s, c);
        assert(c[0].kind == 1 && !wcscmp(c[0].label, L"A-ism"));
        s.variant = 0;
        z3u_selection_copy(&s, c);
        assert(!wcscmp(c[0].label, L"Z-ism"));
        s = rec[find(1, 0)];
        z3u_selection_copy(&s, c);
        assert(!wcscmp(c[0].label, L"X-ism"));
        s = rec[find(1, 2)];
        z3u_selection_copy(&s, c);
        assert(!wcscmp(c[0].label, L"V-ism"));
        s = rec[find(2, 1)];
        z3u_selection_copy(&s, c);
        assert(c[0].kind == 2 && !wcscmp(c[0].label, L"Turbo"));
        s = rec[find(2, 0)];
        z3u_selection_copy(&s, c);
        assert(!wcscmp(c[0].label, L"Normal"));
        s = rec[find(0, 8)];
        s.obj[0][Z3U_RANDOM] = 0x26;
        s.obj[0][Z3U_ID] = 13;                             /* a roll in progress */
        z3u_selection_copy(&s, c);
        assert(c[0].value == 0xFF && !wcscmp(c[0].label, L"Random"));
    }

    /* 4. Spoken through the shared channel: settle, speak once, re-speak only
     *    on a new highlight; a refused utterance is retried; a new kind is a
     *    new utterance even with the same value; depart re-arms. */
    {
        unsigned a = find(0, 0), b = find(0, 24), z = find(1, 1), t = find(2, 1);
        assert(a < count && b < count && z < count && t < count);
        z3u_select_observe(NULL, 0, 0);
        int before = calls;
        settle(&rec[a]);
        assert(calls == before + 1 && !wcscmp(last, L"Ryu"));
        settle(&rec[a]);
        assert(calls == before + 1);
        settle(&rec[b]);
        assert(calls == before + 2 && !wcscmp(last, L"E.Honda"));
        accepts = 0;
        settle(&rec[a]);
        int refused = calls;
        accepts = 1;
        z3u_select_observe(&rec[a], ROOT, 7);
        assert(calls == refused + 1 && !wcscmp(last, L"Ryu"));
        settle(&rec[z]);
        assert(!wcscmp(last, L"A-ism"));
        before = calls;
        settle(&rec[t]);                                   /* value 1 again, kind 2 */
        assert(calls == before + 1 && !wcscmp(last, L"Turbo"));
        z3u_select_observe(NULL, 0, 0);
        before = calls;
        settle(&rec[t]);
        assert(calls == before + 1);
    }

    /* 5. Every gate refuses on its own. */
    {
        unsigned i = find(0, 24), k;
        z3u_select_sample s;
        assert(i < count);
        s = rec[i]; s.mode[0] = 0;              assert(z3u_select_decode(&s, 0, &k) == -1); /* task  */
        s = rec[i]; s.mode[2] = 2;              assert(z3u_select_decode(&s, 0, &k) == -1); /* mode  */
        s = rec[i]; s.mode[3] = 1;              assert(z3u_select_decode(&s, 0, &k) == -1); /* word  */
        s = rec[i]; s.mode[4] = 3;              assert(z3u_select_decode(&s, 0, &k) == -1); /* screen */
        s = rec[i]; s.entered = 2;              assert(z3u_select_decode(&s, 0, &k) == -1); /* CPU   */
        s = rec[i]; s.entered = 0; s.joined = 1; assert(z3u_select_decode(&s, 0, &k) == 24);
        s = rec[i]; s.obj[0][Z3U_SIDE] = 1;     assert(z3u_select_decode(&s, 0, &k) == -1); /* owner */
        s = rec[i]; s.obj[0][Z3U_SUB] = 2;      assert(z3u_select_decode(&s, 0, &k) == -1); /* sub   */
        s = rec[i]; s.obj[0][Z3U_STATE] = 4;    assert(z3u_select_decode(&s, 0, &k) == -1); /* state */
        s = rec[i]; s.obj[0][Z3U_ID] = 38;      assert(z3u_select_decode(&s, 0, &k) == -2); /* range */
        s = rec[i]; s.obj[0][Z3U_ID] = 37;      assert(z3u_select_decode(&s, 0, &k) == 37); /* Balrog */
        s = rec[i]; s.obj[0][Z3U_STATE] = 2; s.obj[0][Z3U_ISM] = 2;
        assert(z3u_select_decode(&s, 0, &k) == -2 && k == Z3U_KIND_ISM);
        s = rec[i]; s.obj[0][Z3U_STATE] = 3; s.speed = 2;
        assert(z3u_select_decode(&s, 0, &k) == -2 && k == Z3U_KIND_SPEED);
        {
            game_selection_choice c[GAME_SELECTION_OWNERS];
            s = rec[i]; s.variant = 2;
            z3u_selection_copy(&s, c);
            assert(c[0].status == GAME_SELECTION_UNAVAILABLE);  /* version */
        }
        /* Side 1 reads its own object when human. */
        s = rec[i]; s.entered = 3;
        memcpy(s.obj[1], s.obj[0], Z3U_FIELDS); s.obj[1][Z3U_SIDE] = 1;
        assert(z3u_select_decode(&s, 1, &k) == 24);
        assert(z3u_select_decode(NULL, 0, &k) == -1 && z3u_select_decode(&s, 2, &k) == -1);
        assert(z3u_select_decode(&s, 0, NULL) == -1);
    }

    /* 6. The alternate copy (Additional Characters on): its grid adds native
     *    IDs 38..41, named through the shell's +1 from 0x26 (0x14008221C).
     *    The ordinary limit still refuses them; IDs below 38 name as before. */
    {
        static const wchar_t *const extra[4] = { L"Eagle", L"Maki", L"Yun", L"Ingrid" };
        unsigned i = find(0, 24), k;
        game_selection_choice c[GAME_SELECTION_OWNERS];
        z3u_select_sample s;
        assert(i < count);
        assert(z3u_branch_for_rom(6) == &z3u_ordinary && z3u_branch_for_rom(7) == &z3u_ordinary);
        assert(z3u_branch_for_rom(16) == &z3u_alternate && z3u_branch_for_rom(17) == &z3u_alternate);
        assert(z3u_branch_for_rom(4) == NULL && z3u_branch_for_rom(8) == NULL);
        for (unsigned id = 38; id < 42; id++) {
            s = rec[i]; s.id_limit = z3u_alternate.id_limit; s.obj[0][Z3U_ID] = (BYTE)id;
            assert(z3u_select_decode(&s, 0, &k) == (int)id);
            z3u_selection_copy(&s, c);
            assert(c[0].status == GAME_SELECTION_VALID && !wcscmp(c[0].label, extra[id - 38]));
            s.id_limit = z3u_ordinary.id_limit;
            assert(z3u_select_decode(&s, 0, &k) == -2);
        }
        s = rec[i]; s.id_limit = z3u_alternate.id_limit; s.obj[0][Z3U_ID] = 42;
        assert(z3u_select_decode(&s, 0, &k) == -2);
        s = rec[i]; s.id_limit = z3u_alternate.id_limit; s.obj[0][Z3U_ID] = 37;
        z3u_selection_copy(&s, c);
        assert(!wcscmp(c[0].label, L"Balrog"));              /* US name, unshifted */
        /* A spoken extra fighter through the channel, on an alternate ROM. */
        s = rec[i]; s.id_limit = z3u_alternate.id_limit; s.obj[0][Z3U_ID] = 39;
        z3u_select_observe(NULL, 0, 0);
        z3u_select_observe(&s, ROOT, 17);
        z3u_select_observe(&s, ROOT, 17);
        assert(!wcscmp(last, L"Maki"));
    }

    /* 7. Frame liveness (src/frame_liveness.h), the gate that stops a quit
     *    game's frozen select screen speaking again ("Game: Ryu", 2026-10-15
     *    log 02:52:04). One value is not motion; an advance is live until
     *    the counter stalls for 250 ms; a resume is live again. */
    {
        frame_liveness l = { 0, 0, FALSE };
        assert(!frame_liveness_live(&l, 500, 1000, 250));    /* first sighting  */
        assert(!frame_liveness_live(&l, 500, 1033, 250));    /* frozen block    */
        assert(frame_liveness_live(&l, 502, 1066, 250));     /* running         */
        assert(frame_liveness_live(&l, 502, 1300, 250));     /* within 250 ms   */
        assert(!frame_liveness_live(&l, 502, 1316, 250));    /* quit or paused  */
        assert(!frame_liveness_live(&l, 502, 9000, 250));
        assert(frame_liveness_live(&l, 503, 9033, 250));     /* resumed         */
    }

    /* 8. EX Mode Select, live on the alternate copy (2026-10-15,
     *    tools/make_z3u_mode_fixture.py): every moment decodes to its
     *    hand-read expectation on both sides and to nothing else. */
    static unsigned mode_count;
    {
        FILE *f = fopen("tools/fixtures/z3u-mode-select-20261015.bin", "rb");
        BYTE magic[4], raw[8 + 4 + 9 + 2 * 7];
        static z3u_select_sample m[32];
        static double mt[32];
        static BYTE want[32][2][2];
        assert(f);
        assert(fread(magic, 1, 4, f) == 4 && !memcmp(magic, "Z3UM", 4));
        assert(fread(&mode_count, 4, 1, f) == 1 && mode_count > 0 && mode_count <= 32);
        for (unsigned i = 0; i < mode_count; i++) {
            z3u_select_sample *s = &m[i];
            assert(fread(raw, 1, sizeof raw, f) == sizeof raw);
            memset(s, 0, sizeof *s);
            memcpy(&mt[i], raw, 8);
            want[i][0][0] = raw[8];  want[i][0][1] = raw[9];
            want[i][1][0] = raw[10]; want[i][1][1] = raw[11];
            memcpy(s->mode, raw + 12, 6);
            s->speed = raw[18]; s->entered = raw[19]; s->joined = raw[20];
            memcpy(s->obj[0], raw + 21, 7);
            memcpy(s->obj[1], raw + 28, 7);
            s->variant = 1;                               /* a3uu */
            s->id_limit = z3u_alternate.id_limit;
        }
        assert(fgetc(f) == EOF);
        fclose(f);

        for (unsigned i = 0; i < mode_count; i++) {
            game_selection_choice c[GAME_SELECTION_OWNERS];
            z3u_selection_copy(&m[i], c);
            for (unsigned side = 0; side < 2; side++) {
                if (want[i][side][0] == 0xFF) {
                    if (c[side].status != GAME_SELECTION_INACTIVE)
                        fprintf(stderr, "mode moment %.2f side %u: expected inactive, got '%ls'\n",
                                mt[i], side, c[side].label);
                    assert(c[side].status == GAME_SELECTION_INACTIVE);
                } else {
                    if (c[side].status != GAME_SELECTION_VALID || c[side].kind != want[i][side][0] ||
                        c[side].value != want[i][side][1])
                        fprintf(stderr, "mode moment %.2f side %u: expected %u/%u, got %d %u/%u\n",
                                mt[i], side, want[i][side][0], want[i][side][1],
                                (int)c[side].status, c[side].kind, c[side].value);
                    assert(c[side].status == GAME_SELECTION_VALID);
                    assert(c[side].kind == want[i][side][0] && c[side].value == want[i][side][1]);
                }
            }
        }

        /* 9. The words, and the mode gate. */
        {
            static const wchar_t *const names[4] = {
                L"Normal mode", L"Mazi mode", L"Saikyo mode", L"Classical mode" };
            game_selection_choice c[GAME_SELECTION_OWNERS];
            unsigned k;
            for (unsigned v = 0; v < 4; v++) {
                z3u_select_sample s = m[0];
                s.obj[0][Z3U_MODE] = (BYTE)v;
                z3u_selection_copy(&s, c);
                assert(c[0].kind == Z3U_KIND_MODE && !wcscmp(c[0].label, names[v]));
            }
            z3u_select_sample s = m[0];
            s.obj[0][Z3U_MODE] = 4;      assert(z3u_select_decode(&s, 0, &k) == -2);  /* masked */
            s.obj[0][Z3U_MODE] = 0xFF;   assert(z3u_select_decode(&s, 0, &k) == -2);
            s = m[0]; s.obj[0][Z3U_SUB] = 2; assert(z3u_select_decode(&s, 0, &k) == -1);  /* confirmed */
            s = m[0]; s.obj[0][Z3U_SIDE] = 1; assert(z3u_select_decode(&s, 0, &k) == -1); /* owner */
        }

        /* 10. Spoken in the live order: both sides at the first moment, then
         *     only what changed. */
        {
            static const wchar_t *const spoken[] = {
                L"Normal mode. Ken", L"Mazi mode", L"Normal mode", L"Classical mode",
                L"Saikyo mode", L"Mazi mode", L"Normal mode", L"Turbo",
                L"Normal mode", L"Mazi mode", L"Normal mode", L"Classical mode",
                L"Saikyo mode", L"Mazi mode", L"Saikyo mode", L"Normal mode", L"A-ism" };
            unsigned n = 0;
            z3u_select_observe(NULL, 0, 0);
            int before = calls;
            for (unsigned i = 0; i < mode_count; i++) {
                int was = calls;
                z3u_select_observe(&m[i], ROOT, 17);
                z3u_select_observe(&m[i], ROOT, 17);
                if (calls != was) {
                    assert(n < sizeof spoken / sizeof spoken[0]);
                    if (wcscmp(last, spoken[n]))
                        fprintf(stderr, "mode spoken %u at %.2f: '%ls', want '%ls'\n", n, mt[i], last, spoken[n]);
                    assert(!wcscmp(last, spoken[n]));
                    n++;
                }
            }
            assert(n == sizeof spoken / sizeof spoken[0] && calls == before + (int)n);
        }
    }

    printf("PASS: %u live Alpha 3 samples, character/ism/speed path, names by version, "
           "random, shared-channel speech/retry/kind/depart, every gate, alternate-copy "
           "extra fighters, frame liveness; %u live EX Mode Select moments, mode names, "
           "gates and spoken order\n", count, mode_count);
    return 0;
}
