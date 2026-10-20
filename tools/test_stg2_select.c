/* Replays the 2026-10-20 live Plasma Sword capture through the shipping
 * decoder and the shared selection channel. No game, no audible output. Run
 * from the repository root. Fixture: tools/make_stg2_fixtures.py. */
#include "../src/stg2_select.h"
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
LONG stg2_frame_count(void) { return 0; }
void audio_beacon_set_owned(uint64_t e, unsigned p, BOOL l, float x, float s)
{ (void)e; (void)p; (void)l; (void)x; (void)s; }

#define RECORD (8 + 1 + 4 + 1 + 2 + 2 + 4 + 4)
#define ROOT 0x144e1000ULL
static unsigned count;
static stg2_select_sample rec[64];
static double when[64];
static BYTE committed[4];   /* native IDs P1, P2; row flags P1, P2 */

static void load(void)
{
    FILE *f = fopen("tools/fixtures/stg2-select-live-20261020.bin", "rb");
    BYTE magic[4], raw[RECORD];
    assert(f);
    assert(fread(magic, 1, 4, f) == 4 && !memcmp(magic, "STGS", 4));
    assert(fread(&count, 4, 1, f) == 1 && count > 0 && count <= 64);
    for (unsigned i = 0; i < count; i++) {
        BYTE *r = raw + 8;
        assert(fread(raw, 1, RECORD, f) == RECORD);
        memset(&rec[i], 0, sizeof rec[i]);
        memcpy(&when[i], raw, 8);
        rec[i].phase = r[0];
        memcpy(rec[i].window + STG2_W_STATE, r + 1, 4);
        rec[i].window[STG2_W_HUMANS] = r[5];
        memcpy(rec[i].window + STG2_W_MODE, r + 6, 2);
        memcpy(rec[i].window + STG2_W_SUB, r + 8, 2);
        memcpy(&rec[i].roulette[0], r + 10, 4);
        memcpy(&rec[i].roulette[1], r + 14, 4);
    }
    assert(fread(committed, 1, 4, f) == 4);
    assert(fgetc(f) == EOF);
    fclose(f);
}

static void settle(const stg2_select_sample *s)
{
    stg2_select_observe(s, ROOT, 15);
    stg2_select_observe(s, ROOT, 15);
}

static unsigned first_selecting(void)
{
    for (unsigned i = 0; i < count; i++) if (stg2_select_decode(&rec[i], 0) >= 0) return i;
    assert(0);
    return 0;
}

int main(void)
{
    /* The highlights the player's cursor visited, in order, as shell IDs:
     * Hayato (select 1) -> left wraps to the unlocked top-row cell 23, Kaede
     * -> right back to Hayato -> right to June (2) -> down to Ele (12), picked. */
    static const int path[] = { 1, 28, 1, 2, 18 };
    static const wchar_t *const names[] = { L"Hayato", L"Kaede", L"Hayato", L"June", L"Ele" };
    int seen[64], n = 0, final = -1;
    load();

    /* 1. The decoded sequence on the human side is exactly that path, and the
     *    CPU side never reads as selecting. */
    for (unsigned i = 0; i < count; i++) {
        int id = stg2_select_decode(&rec[i], 0);
        assert(stg2_select_decode(&rec[i], 1) == -1);
        assert(id != -2);              /* no unreadable moment in the capture */
        if (id >= 0) {
            if (!n || seen[n - 1] != id) seen[n++] = id;
            final = id;
        }
    }
    if (n != (int)(sizeof path / sizeof path[0])) {
        for (int i = 0; i < n; i++) fprintf(stderr, "seen %d\n", seen[i]);
    }
    assert(n == (int)(sizeof path / sizeof path[0]));
    for (int i = 0; i < n; i++) assert(seen[i] == path[i]);

    /* 2. Ground truth independent of the decoder: the last highlight is what
     *    the game committed -- native ID 2 with the row flag, i.e. 2 | 0x10. */
    assert(committed[0] == 2 && committed[2] == 1);
    assert(final == (committed[0] | (committed[2] ? 0x10 : 0)));

    /* 3. After the pick the screen is left: the final sample is inactive. */
    assert(stg2_select_decode(&rec[count - 1], 0) == -1);

    /* 4. Names come from the shell's own table. */
    {
        int k = 0, prev = -1;
        for (unsigned i = 0; i < count; i++) {
            game_selection_choice c[GAME_SELECTION_OWNERS];
            int id = stg2_select_decode(&rec[i], 0);
            if (id < 0 || id == prev) continue;
            prev = id;
            stg2_selection_copy(&rec[i], c);
            assert(c[0].status == GAME_SELECTION_VALID && !wcscmp(c[0].label, names[k]));
            assert(c[1].status == GAME_SELECTION_INACTIVE);
            k++;
        }
        assert(k == n);
    }

    /* 5. Every cell, read off the image's tables (0x141983BD7 native IDs,
     *    0x141983BFF row flags) through the pick's own rule; and the grid
     *    covers exactly the shell's STG2 move-list roster (pointer table
     *    0x1412048C0 entry 7: 1..12 and 17..28), each cell once. */
    {
        static const wchar_t *const cell[STG2_SELECT_COUNT] = { NULL,
            L"Hayato", L"June", L"Saturn", L"Gamof", L"Gerelt", L"Vector", L"Zelkin",
            L"Gore", L"Blood", L"Ghost Bilstein",
            L"Black Hayato", L"Ele", L"Prince", L"Gantetsu", L"Claire", L"Omega", L"Eagle",
            L"Luca", L"Shaker", L"Bilstein",
            L"Rain", L"Byakko", L"Kaede", L"Rai-on" };
        stg2_select_sample s = rec[first_selecting()];
        unsigned mask = 0;
        for (unsigned v = 1; v < STG2_SELECT_COUNT; v++) {
            game_selection_choice c[GAME_SELECTION_OWNERS];
            int id;
            s.window[STG2_W_NUMBER] = (BYTE)v;
            id = stg2_select_decode(&s, 0);
            assert(id > 0 && id < 32 && !(mask & (1u << id)));
            mask |= 1u << id;
            stg2_selection_copy(&s, c);
            if (wcscmp(c[0].label, cell[v])) fprintf(stderr, "cell %u: %ls, want %ls\n", v, c[0].label, cell[v]);
            assert(!wcscmp(c[0].label, cell[v]));
        }
        assert(mask == (0x1FFEu | 0x1FFE0000u));
    }

    /* 6. Spoken through the shared channel: settle, speak once, re-speak only
     *    on a new highlight; a refused utterance is retried; depart re-arms. */
    {
        unsigned a = first_selecting(), b = count;
        for (unsigned i = a; i < count && b == count; i++)
            if (stg2_select_decode(&rec[i], 0) >= 0 &&
                stg2_select_decode(&rec[i], 0) != stg2_select_decode(&rec[a], 0)) b = i;
        assert(b < count);
        stg2_select_observe(NULL, 0, 0);
        int before = calls;
        settle(&rec[a]);
        assert(calls == before + 1 && !wcscmp(last, L"Hayato"));
        settle(&rec[a]);
        assert(calls == before + 1);
        settle(&rec[b]);
        assert(calls == before + 2 && !wcscmp(last, L"Kaede"));
        accepts = 0;
        settle(&rec[a]);
        int refused = calls;
        accepts = 1;
        stg2_select_observe(&rec[a], ROOT, 15);
        assert(calls == refused + 1 && !wcscmp(last, L"Hayato"));
        stg2_select_observe(NULL, 0, 0);
        int before3 = calls;
        settle(&rec[a]);
        assert(calls == before3 + 1);
        /* The held-direction shuffle silences without forgetting: when it
         * stops on the same character, nothing is repeated. */
        {
            stg2_select_sample shuffle = rec[a];
            game_selection_choice c[GAME_SELECTION_OWNERS];
            shuffle.roulette[0] = 1;
            stg2_selection_copy(&shuffle, c);
            assert(c[0].status == GAME_SELECTION_UNAVAILABLE);
            int before4 = calls;
            settle(&shuffle);
            settle(&rec[a]);
            assert(calls == before4);
        }
    }

    /* 7. Every gate refuses on its own. */
    {
        unsigned i = first_selecting();
        stg2_select_sample s;
        WORD w;
        s = rec[i]; s.phase = 1;                         assert(stg2_select_decode(&s, 0) == -1);
        s = rec[i]; w = 2; memcpy(s.window + STG2_W_MODE, &w, 2); assert(stg2_select_decode(&s, 0) == -1);
        s = rec[i]; w = 3; memcpy(s.window + STG2_W_SUB, &w, 2);  assert(stg2_select_decode(&s, 0) == -1);
        s = rec[i]; s.window[STG2_W_HUMANS] = 2;         assert(stg2_select_decode(&s, 0) == -1);
        s = rec[i]; s.window[STG2_W_STATE] = 1;          assert(stg2_select_decode(&s, 0) == -1);
        s = rec[i]; s.window[STG2_W_STATE] = 3;          assert(stg2_select_decode(&s, 0) == -1);
        s = rec[i]; s.roulette[0] = 1;                   assert(stg2_select_decode(&s, 0) == -2);
        s = rec[i]; s.roulette[1] = 1;                   assert(stg2_select_decode(&s, 0) >= 0);
        s = rec[i]; s.window[STG2_W_NUMBER] = 0;         assert(stg2_select_decode(&s, 0) == -2);
        s = rec[i]; s.window[STG2_W_NUMBER] = 25;        assert(stg2_select_decode(&s, 0) == -2);
        s = rec[i]; s.window[STG2_W_NUMBER] = 0xFF;      assert(stg2_select_decode(&s, 0) == -2);
        /* Side 1 reads its own number when human. */
        s = rec[i]; s.window[STG2_W_HUMANS] = 3;
        s.window[STG2_W_STATE + 1] = 0; s.window[STG2_W_NUMBER + 1] = 21;
        assert(stg2_select_decode(&s, 1) == 12);         /* Rain */
        assert(stg2_select_decode(NULL, 0) == -1 && stg2_select_decode(&s, 2) == -1);
    }

    printf("PASS: %u live STG2 select samples, highlight path and committed pick, names, "
           "grid = shell roster, shared-channel speech/retry/depart/shuffle, every gate\n", count);
    return 0;
}
