/* Replays the 2026-10-22 live Power Stone capture through the shipping
 * decoder and the shared selection channel. No game, no audible output. Run
 * from the repository root. Fixture: tools/make_pws1_fixtures.py. */
#include "../src/pws1_select.h"
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
/* The game's text, as menu_eng holds it for the stage themes' labels. */
static int gmd_ready = 1;
BOOL gmd_text_by_index(int index, char *out, unsigned cap)
{
    static const struct { int index; const char *text; } t[] = {
        { 3816, "07. Londo Stage" }, { 3817, "08. Mutsu Stage" }, { 3818, "09. Tong-An Stage" },
        { 3819, "10. Manches Stage" }, { 3820, "11. Dawnvolta Stage" }, { 3823, "14. Dullstown Stage" },
        { 3824, "15. Oedo Stage" }, { 3825, "16. Mahdad Stage" }, { 3827, "18. Skullhaven Stage" },
        { 3828, "19. Avalon Is. Stage 1" },
    };
    if (!gmd_ready) return FALSE;
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
        if (t[i].index == index) { strncpy(out, t[i].text, cap - 1); out[cap - 1] = 0; return TRUE; }
    return FALSE;
}
/* Audio routing is exercised by run_match_audio_test; the tick is not run here. */
#include <stdint.h>
uint64_t match_audio_begin(unsigned a) { (void)a; return 0; }
void match_audio_depart(uint64_t e) { (void)e; }
uint64_t match_audio_current(void) { return 0; }
LONG pws1_frame_count(void) { return 0; }

/* Same list as SELECT in tools/make_pws1_fixtures.py. */
#define RECORD (8 + 8 + 8 + 1 + 1 + 1 + 4 + 2 + 2 + 4 + 4)
#define MAXREC 256
static unsigned count;
static pws_select_sample rec[MAXREC];
static unsigned long long root_of[MAXREC];
static BYTE committed[MAXREC][2];   /* actor+4, the fighters' current IDs */
static BYTE side_state[MAXREC];

static void load(void)
{
    FILE *f = fopen("tools/fixtures/pws1-select-live-20261022.bin", "rb");
    BYTE magic[4], raw[RECORD];
    assert(f);
    assert(fread(magic, 1, 4, f) == 4 && !memcmp(magic, "PWSS", 4));
    assert(fread(&count, 4, 1, f) == 1 && count > 0 && count <= MAXREC);
    for (unsigned i = 0; i < count; i++) {
        const BYTE *r = raw + 16;
        pws_select_sample *s = &rec[i];
        assert(fread(raw, 1, RECORD, f) == RECORD);
        memcpy(&root_of[i], raw + 8, 8);
        memset(s, 0, sizeof *s);
        s->game = 4;
        s->variant = 1;                         /* pws1u */
        memcpy(s->phase, r, 8);
        s->gates[0] = r[8];
        s->gates[1] = r[9];
        s->mask = r[10];
        s->state[0] = r[11]; s->cursor[0] = r[12];
        s->state[1] = r[13]; s->cursor[1] = r[14];
        s->slot[0] = r[15]; committed[i][0] = r[16];
        s->slot[1] = r[17]; committed[i][1] = r[18];
        memcpy(&s->shuffle[0], r + 19, 4);
        memcpy(&s->shuffle[1], r + 23, 4);
        side_state[i] = r[11];
    }
    assert(fgetc(f) == EOF);
    fclose(f);
}

static void settle(unsigned i)
{
    pws1_select_observe(&rec[i], -1, root_of[i], 9);
    pws1_select_observe(&rec[i], -1, root_of[i], 9);
}

int main(void)
{
    /* The highlights side 0 settled on, in order, as shell IDs, per select
     * screen (the root moved each time). Shuffle samples are not highlights. */
    int seen[64], n = 0, picks = 0, entries = 0, shuffled = 0, screens = 0;
    unsigned long long root = 0;
    load();

    /* 1. Decode: the CPU side never reads as selecting; every screen entered
     *    on camera opens on cursor 1 (Falcon); confirmation commits the last
     *    highlight. The capture began on the first screen after the cursor
     *    had already moved. */
    for (unsigned i = 0; i <= count; i++) {
        int id = i < count ? pws_select_decode(&rec[i], 0) : -1;
        if (i == count || root_of[i] != root) {
            for (int k = 0; k < n; k++)
                printf("%s%s", k ? ", " : "  screen: ", character_name(4, (unsigned)seen[k], 1));
            if (n) printf("\n");
            if (screens == 2) {
                /* The screen walked in full: Falcon, then settled highlights
                 * between shuffles, confirmed on Kraken. */
                static const char *const want[] = { "Falcon", "Kraken", "Falcon", "Ayame",
                                                    "Falcon", "Valgas", "Kraken" };
                assert(n == (int)(sizeof want / sizeof *want));
                for (int k = 0; k < n; k++)
                    assert(!strcmp(character_name(4, (unsigned)seen[k], 1), want[k]));
            }
            if (i == count) break;
            root = root_of[i];
            n = 0;
            screens++;
        }
        assert(pws_select_decode(&rec[i], 1) == -1);
        if (id == -2) { assert(rec[i].shuffle[0]); shuffled++; }
        if (id >= 0) {
            if (!n && screens > 1) {
                entries++;
                assert(rec[i].cursor[0] == 1);
                assert(!strcmp(character_name(4, (unsigned)id, 1), "Falcon"));
            }
            if (!n || seen[n - 1] != id) { assert(n < 64); seen[n++] = id; }
            /* The game's own mapped ID for the fighter agrees, every sample. */
            assert(committed[i][0] == (BYTE)id);
        }
        /* Side state 2 -> 5 is the confirmation: the pick is what was last
         * highlighted, and the fighter record holds it. */
        if (i && side_state[i - 1] == 2 && side_state[i] == 5 && root_of[i] == root_of[i - 1]) {
            assert(n > 0 && committed[i][0] == (BYTE)seen[n - 1]);
            picks++;
        }
    }
    printf("screens %d, entered on camera %d, picks %d, shuffle samples %d\n",
           screens, entries, picks, shuffled);
    assert(entries >= 2 && picks >= 2 && shuffled > 0);

    /* 2. Spoken through the shared channel: the first highlight once, a new
     *    one once more, a refused utterance retried, a new root re-announces. */
    {
        /* a: the first highlight of the first screen entered on camera; b: a
         * different highlight on that screen; c: the next screen's first. */
        unsigned a = count, b = count, c = count;
        for (unsigned i = 0; i < count; i++) {
            int id = pws_select_decode(&rec[i], 0);
            if (id < 0 || root_of[i] == root_of[0]) continue;
            if (a == count) a = i;
            else if (root_of[i] == root_of[a]) {
                if (b == count && id != pws_select_decode(&rec[a], 0)) b = i;
            } else if (c == count) c = i;
        }
        assert(a < count && b < count && c < count);
        pws1_select_observe(NULL, -1, 0, 0);
        int before = calls;
        settle(a);
        assert(calls == before + 1 && !wcscmp(last, L"Falcon"));
        settle(a);
        assert(calls == before + 1);
        settle(b);
        assert(calls == before + 2);
        {
            wchar_t want[64];
            swprintf(want, 64, L"%hs", character_name(4, (unsigned)pws_select_decode(&rec[b], 0), 1));
            assert(!wcscmp(last, want));
        }
        accepts = 0;
        settle(a);
        int refused = calls;
        accepts = 1;
        pws1_select_observe(&rec[a], -1, root_of[a], 9);
        assert(calls == refused + 1 && !wcscmp(last, L"Falcon"));
        /* A new select screen (new root) opening on the same highlight is
         * announced again. */
        int before2 = calls;
        settle(c);
        assert(calls == before2 + 1 && !wcscmp(last, L"Falcon"));
        /* The held-direction shuffle silences without forgetting. */
        {
            pws_select_sample shuffle = rec[c];
            int before3 = calls;
            shuffle.shuffle[0] = 1;
            pws1_select_observe(&shuffle, -1, root_of[c], 9);
            pws1_select_observe(&shuffle, -1, root_of[c], 9);
            settle(c);
            assert(calls == before3);
        }
        pws1_select_observe(NULL, -1, 0, 0);
    }

    /* 3. The Versus stage select, from the 2026-10-24 capture: on the screen
     *    exactly while phase/sub-state/step read 3/1/1; the cursor through the
     *    game's table gives the stage ID the game itself stored at R+0x27FD1
     *    (it follows the cursor about 0.3 s later); named from the theme's
     *    title without its track number; spoken through the channel. */
    {
        FILE *f = fopen("tools/fixtures/pws1-stage-live-20261024.bin", "rb");
        unsigned char magic[4];
        unsigned n, on = 0, agree = 0, lag = 0, names = 0;
        int prev = -9;
        struct { double t; unsigned long long root; BYTE phase[3], gate[2]; short cursor; BYTE id; } g[64];
        assert(f && fread(magic, 1, 4, f) == 4 && !memcmp(magic, "PWSG", 4));
        assert(fread(&n, 4, 1, f) == 1 && n > 0 && n <= 64);
        for (unsigned i = 0; i < n; i++) {
            assert(fread(&g[i].t, 8, 1, f) == 1 && fread(&g[i].root, 8, 1, f) == 1);
            assert(fread(g[i].phase, 1, 3, f) == 3 && fread(g[i].gate, 1, 2, f) == 2);
            assert(fread(&g[i].cursor, 2, 1, f) == 1 && fread(&g[i].id, 1, 1, f) == 1);
        }
        assert(fgetc(f) == EOF);
        fclose(f);
        for (unsigned i = 0; i < n; i++) {
            int id = pws1_stage_decode(g[i].phase[0], g[i].phase[1], g[i].phase[2], g[i].cursor);
            bool screen = g[i].phase[0] == 3 && g[i].phase[1] == 1 && g[i].phase[2] == 1;
            assert((id != -1) == screen);
            if (!screen) continue;
            on++;
            assert(id >= 0);
            if (id == g[i].id) agree++; else lag++;
            if (id != prev) {
                wchar_t name[64];
                assert(pws1_stage_name(id, name, 64));
                printf("  stage: cursor %d -> %ls\n", g[i].cursor, name);
                names++;
                prev = id;
            }
        }
        /* Two highlights: cursor 6 (ID 1, Ryoma's Mutsu), then 7 (ID 9,
         * Valgas's Avalon Island); the game's own stage ID agreed with the
         * table on every sample of the screen. */
        assert(on == 2 && names == 2 && lag == 0 && agree == on);
        {
            wchar_t name[64];
            assert(pws1_stage_decode(3, 1, 1, 6) == 1 && pws1_stage_decode(3, 1, 1, 7) == 9);
            assert(pws1_stage_name(1, name, 64) && !wcscmp(name, L"Mutsu Stage"));
            assert(pws1_stage_name(9, name, 64) && !wcscmp(name, L"Avalon Is. Stage 1"));
            assert(pws1_stage_name(5, name, 64) && !wcscmp(name, L"Dullstown Stage"));
            /* Every cursor a different stage, all ten stages. */
            unsigned mask = 0;
            for (int c = 0; c < 10; c++) {
                int id = pws1_stage_decode(3, 1, 1, c);
                assert(id >= 0 && id < 10 && !(mask & (1u << id)));
                mask |= 1u << id;
            }
            assert(mask == 0x3FF);
            /* The two cursors the lock skips are the boss stages. */
            assert(pws1_stage_decode(3, 1, 1, 2) == 8 && pws1_stage_decode(3, 1, 1, 7) == 9);
            assert(pws1_stage_decode(3, 1, 1, 10) == -2 && pws1_stage_decode(3, 1, 1, -1) == -2);
            assert(pws1_stage_decode(3, 1, 2, 6) == -1 && pws1_stage_decode(2, 1, 1, 6) == -1);
            assert(!pws1_stage_name(10, name, 64));
        }
        /* Spoken: the first highlight, then the move; a missing text speaks
         * nothing rather than a guess. */
        {
            pws_select_sample none;
            memset(&none, 0, sizeof none);
            none.game = 4; none.variant = 1;
            pws1_select_observe(NULL, -1, 0, 0);
            int before = calls;
            pws1_select_observe(&none, 1, g[0].root, 9);
            pws1_select_observe(&none, 1, g[0].root, 9);
            assert(calls == before + 1 && !wcscmp(last, L"Mutsu Stage"));
            pws1_select_observe(&none, 9, g[0].root, 9);
            pws1_select_observe(&none, 9, g[0].root, 9);
            assert(calls == before + 2 && !wcscmp(last, L"Avalon Is. Stage 1"));
            gmd_ready = 0;
            pws1_select_observe(&none, 5, g[0].root, 9);
            pws1_select_observe(&none, 5, g[0].root, 9);
            assert(calls == before + 2);
            gmd_ready = 1;
            pws1_select_observe(NULL, -1, 0, 0);
        }
    }

    printf("PASS: %u live PWS1 select samples, highlight paths and confirmations, re-entry, "
           "shuffle, shared-channel speech/retry/re-announce, Versus stage select\n", count);
    return 0;
}
