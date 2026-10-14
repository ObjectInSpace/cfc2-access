/* Replays the 2026-10-14 live Alpha 3 capture through the shipping decoder and
 * the shared selection channel. No game, no audible output. Run from the
 * repository root. Fixture: tools/make_z3u_select_fixture.py. */
#include "../src/z3u_select.h"
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
LONG z3u_frame_count(void) { return 0; }
void z3u_text_observe(ULONGLONG root) { (void)root; }   /* run_z3u_text_test */
void audio_beacon_set_owned(uint64_t e, unsigned p, BOOL l, float x, float s)
{ (void)e; (void)p; (void)l; (void)x; (void)s; }

#define RECORD (6 + 3 + 2 * Z3U_FIELDS)
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
        memcpy(rec[i].obj, raw + 9, 2 * Z3U_FIELDS);
        rec[i].variant = 1;                       /* the capture ran a3u */
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

    printf("PASS: %u live Alpha 3 samples, character/ism/speed path, names by version, "
           "random, shared-channel speech/retry/kind/depart, every gate\n", count);
    return 0;
}
