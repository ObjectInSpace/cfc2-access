/* Replays the 2026-10-14 live CFJ capture through the shipping decoder and
 * the shared selection channel. No game, no audible output. Run from the
 * repository root. Fixture: tools/make_cfj_select_fixture.py. */
#include "../src/cfj_select.h"
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
LONG cfj_frame_count(void) { return 0; }
void audio_beacon_set_owned(uint64_t e, unsigned p, BOOL l, float x, float s)
{ (void)e; (void)p; (void)l; (void)x; (void)s; }

#define RECORD (2 + 1 + CFJ_SIDES_SIZE)
#define ROOT 0x174c1000ULL
static unsigned count;
static cfj_select_sample rec[256];
static BYTE committed[2][2];

static void load(void)
{
    FILE *f = fopen("tools/fixtures/cfj-select-live-20261014.bin", "rb");
    BYTE magic[4], raw[RECORD];
    assert(f);
    assert(fread(magic, 1, 4, f) == 4 && !memcmp(magic, "CFJS", 4));
    assert(fread(&count, 4, 1, f) == 1 && count > 0 && count <= 256);
    for (unsigned i = 0; i < count; i++) {
        assert(fread(raw, 1, RECORD, f) == RECORD);
        memcpy(rec[i].phase, raw, 2);
        rec[i].humans = raw[2];
        memcpy(rec[i].sides, raw + 3, CFJ_SIDES_SIZE);
        rec[i].variant = 1;                       /* the capture ran cfe */
    }
    assert(fread(committed, 1, 4, f) == 4);
    assert(fgetc(f) == EOF);
    fclose(f);
}

static void settle(const cfj_select_sample *s)
{
    cfj_select_observe(s, ROOT, 5);
    cfj_select_observe(s, ROOT, 5);
}

int main(void)
{
    /* The highlights the player's cursor visited, in order: first pick
     * Ryu -> left wraps to Demitri -> right back to Ryu (picked); second pick
     * starts on Guile -> Demitri -> Guile -> Sakura -> Rose -> Alex (picked).
     * Each is the grid cell the game's own x/y bytes name. */
    static const int path[] = { 0, 22, 0, 3, 22, 3, 11, 19, 26 };
    int seen[64], n = 0, last_slot[2] = { -1, -1 };
    load();

    /* 1. The decoded sequence on the human side is exactly that path, and the
     *    CPU side never reads as selecting. */
    for (unsigned i = 0; i < count; i++) {
        int id = cfj_select_decode(&rec[i], 0);
        assert(cfj_select_decode(&rec[i], 1) == -1);
        if (id >= 0) {
            unsigned slot = rec[i].sides[0x06];
            if (!n || seen[n - 1] != id) seen[n++] = id;
            last_slot[slot] = id;
        }
    }
    assert(n == (int)(sizeof path / sizeof path[0]));
    for (int i = 0; i < n; i++) assert(seen[i] == path[i]);

    /* 2. Ground truth independent of the decoder: the last highlight of each
     *    pick is the native ID the game committed to the fighter record. */
    assert(last_slot[0] == committed[0][0] && last_slot[1] == committed[0][1]);
    assert(committed[0][0] == 0 && committed[0][1] == 26);   /* Ryu, Alex */

    /* 3. After the picks the screen is left: the final sample is inactive. */
    assert(cfj_select_decode(&rec[count - 1], 0) == -1);

    /* 4. Names come from the shell's own table, per game version. */
    {
        unsigned bison = count;
        cfj_select_sample s;
        game_selection_choice c[GAME_SELECTION_OWNERS];
        for (unsigned i = 0; i < count; i++) if (cfj_select_decode(&rec[i], 0) == 0) bison = i;
        assert(bison < count);
        s = rec[bison];
        s.sides[0x0E] = 4;                         /* select number 4 = native 10 */
        cfj_selection_copy(&s, c);
        assert(c[0].status == GAME_SELECTION_VALID && !wcscmp(c[0].label, L"M.Bison"));
        assert(c[1].status == GAME_SELECTION_INACTIVE);
        s.variant = 0;
        cfj_selection_copy(&s, c);
        assert(!wcscmp(c[0].label, L"Vega"));     /* Japanese ROM name */
    }

    /* 5. Spoken through the shared channel: settle, speak once, re-speak only
     *    on a new highlight; a refused utterance is retried; depart re-arms. */
    {
        unsigned a = count, b = count;
        for (unsigned i = 0; i < count && b == count; i++) {
            int id = cfj_select_decode(&rec[i], 0);
            if (id < 0) continue;
            if (a == count) a = i;
            else if (id != cfj_select_decode(&rec[a], 0)) b = i;
        }
        assert(a < count && b < count);
        cfj_select_observe(NULL, 0, 0);
        int before = calls;
        settle(&rec[a]);
        assert(calls == before + 1 && !wcscmp(last, L"Ryu"));
        settle(&rec[a]);
        assert(calls == before + 1);
        settle(&rec[b]);
        assert(calls == before + 2 && !wcscmp(last, L"Demitri"));
        accepts = 0;
        settle(&rec[a]);
        int refused = calls;
        accepts = 1;
        cfj_select_observe(&rec[a], ROOT, 5);
        assert(calls == refused + 1 && !wcscmp(last, L"Ryu"));
        /* The same character as the second pick's first highlight is new. */
        {
            cfj_select_sample next = rec[a];
            next.sides[0x06] = 1;
            next.sides[0x0F] = next.sides[0x0E];
            int before2 = calls;
            settle(&next);
            assert(calls == before2 + 1);
        }
        cfj_select_observe(NULL, 0, 0);
        int before3 = calls;
        settle(&rec[a]);
        assert(calls == before3 + 1);
    }

    /* 6. Every gate refuses on its own. */
    {
        unsigned i = count;
        cfj_select_sample s;
        for (unsigned j = 0; j < count && i == count; j++) if (cfj_select_decode(&rec[j], 0) >= 0) i = j;
        assert(i < count);
        s = rec[i]; s.phase[0] = 3;          assert(cfj_select_decode(&s, 0) == -1); /* phase   */
        s = rec[i]; s.phase[1] = 1;          assert(cfj_select_decode(&s, 0) == -1); /* sub     */
        s = rec[i]; s.humans = 2;            assert(cfj_select_decode(&s, 0) == -1); /* CPU     */
        s = rec[i]; s.sides[0x08] = 2;       assert(cfj_select_decode(&s, 0) == -1); /* done    */
        s = rec[i]; s.sides[0x06] = 2;       assert(cfj_select_decode(&s, 0) == -2); /* slot    */
        s = rec[i]; s.sides[0x06] = 0xFF;    assert(cfj_select_decode(&s, 0) == -2);
        s = rec[i]; s.sides[0x0E] = 0xFF;    assert(cfj_select_decode(&s, 0) == -2); /* none    */
        s = rec[i]; s.sides[0x0E] = 23;      assert(cfj_select_decode(&s, 0) == -2); /* range   */
        s = rec[i]; s.variant = 2;           assert(cfj_select_decode(&s, 0) == -2); /* version */
        s = rec[i]; s.sides[0x0E] = 22;      assert(cfj_select_decode(&s, 0) == 34); /* Ingrid  */
        /* Side 1 reads its own record when human. */
        s = rec[i]; s.humans = 3; memcpy(s.sides + 0x10, s.sides, 0x10);
        assert(cfj_select_decode(&s, 1) == cfj_select_decode(&s, 0));
        assert(cfj_select_decode(NULL, 0) == -1 && cfj_select_decode(&s, 2) == -1);
    }

    printf("PASS: %u live CFJ samples, highlight path and committed picks, names by game "
           "version, shared-channel speech/retry/visit/depart, every gate\n", count);
    return 0;
}
