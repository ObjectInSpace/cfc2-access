/* Replays the 2026-10-19 live Project Justice captures through the shipping
 * decoder and the shared selection channel. No game, no audible output. Run
 * from the repository root. Fixture: tools/make_mjus_fixtures.py. */
#include "../src/mjus_select.h"
#include "../src/character_name_table.h"
#include "../src/mod_text.h"
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
#include <stdint.h>
uint64_t match_audio_begin(unsigned a) { (void)a; return 0; }
void match_audio_depart(uint64_t e) { (void)e; }
uint64_t match_audio_current(void) { return 0; }
LONG mjus_frame_count(void) { return 0; }
void mjus_text_observe(ULONGLONG root) { (void)root; }
void audio_beacon_set_owned(uint64_t e, unsigned p, BOOL l, float x, float s)
{ (void)e; (void)p; (void)l; (void)x; (void)s; }

#define TASK_BYTES 5
#define BASE (16 * TASK_BYTES + 3 + 1 + 2 * 0x30)
#define RECORD (BASE + 1 + MJUS_TEAM_SIZE + MJUS_PICK_SIZE)
#define ROOT 0x173c1000ULL
static unsigned count;
static mjus_select_sample rec[1024];
static BYTE committed[3], story_team[2][3];

static void load(void)
{
    FILE *f = fopen("tools/fixtures/mjus-select-live-20261019.bin", "rb");
    BYTE magic[4], raw[RECORD];
    assert(f);
    assert(fread(magic, 1, 4, f) == 4 && !memcmp(magic, "MJSL", 4));
    assert(fread(&count, 4, 1, f) == 1 && count > 0 && count <= 1024);
    for (unsigned i = 0; i < count; i++) {
        assert(fread(raw, 1, RECORD, f) == RECORD);
        memset(&rec[i], 0, sizeof rec[i]);
        rec[i].task_state = rec[i].task_step = 0xFF;
        /* As the tick does: the routine-6 task's +7 and +8. */
        for (unsigned t = 0; t < 16; t++) {
            const BYTE *r = raw + t * TASK_BYTES;          /* +0 +1 +5 +7 +8 */
            if ((r[0] | r[1]) && r[2] == MJUS_SELECT_ROUTINE) {
                rec[i].task_state = r[3];
                rec[i].task_step = r[4];
            }
        }
        memcpy(rec[i].menu, raw + 16 * TASK_BYTES, 3);
        rec[i].humans = raw[16 * TASK_BYTES + 3];
        memcpy(rec[i].sides[0], raw + 16 * TASK_BYTES + 4, 0x30);
        memcpy(rec[i].sides[1], raw + 16 * TASK_BYTES + 4 + 0x30, 0x30);
        rec[i].phase = raw[BASE];
        memcpy(rec[i].team, raw + BASE + 1, MJUS_TEAM_SIZE);
        memcpy(rec[i].pick, raw + BASE + 1 + MJUS_TEAM_SIZE, MJUS_PICK_SIZE);
        rec[i].variant = 1;                              /* the captures ran pjus */
    }
    assert(fread(committed, 1, 3, f) == 3);
    assert(fread(story_team, 1, 6, f) == 6);
    assert(fgetc(f) == EOF);
    fclose(f);
}

static void settle(const mjus_select_sample *s)
{
    mjus_select_observe(s, ROOT, 13);
    mjus_select_observe(s, ROOT, 13);
}

int main(void)
{
    int modes[64], nm = 0, schools[64], ns = 0, last_pick[3] = { -1, -1, -1 }, first_pick = -1;
    unsigned picks_seen = 0;
    load();

    /* 1. The menus, in live order. Session 1: the cursor toggled Free, Story,
     *    Free before Free was confirmed. Session 2 and the story capture:
     *    Story; then the school list stepped up and down between Taiyo (0) and
     *    the last of seven (6) before Taiyo was confirmed both times. */
    for (unsigned i = 0; i < count; i++) {
        int m = mjus_mode_decode(&rec[i]), s = mjus_school_decode(&rec[i]);
        assert(!(m >= 0 && s != -1));                 /* never both at once */
        if (m >= 0 && (!nm || modes[nm - 1] != m)) modes[nm++] = m;
        if (s >= 0 && (!ns || schools[ns - 1] != s)) schools[ns++] = s;
        if (m >= 0 || s != -1) {
            assert(mjus_select_decode(&rec[i], 0) == -1 && mjus_select_decode(&rec[i], 1) == -1);
            if (s != -1) assert(rec[i].menu[1] == 7);  /* secret characters on: 7 schools */
        }
    }
    {
        /* ... and the first capture: Free once, back to Story, confirmed. */
        static const int want_modes[] = { 0, 1, 0, 1, 0, 1, 0 };
        if (nm != 7) { for (int i = 0; i < nm; i++) fprintf(stderr, "mode %d\n", modes[i]); }
        /* Schools: Taiyo twice (0 <-> 6 <-> 0); then the first capture went
         * from 0 up to 6 and up again to 5, Seijyun. */
        static const int want_schools[] = { 0, 6, 0, 6, 0, 6, 0, 6, 5 };
        assert(nm == 7 && !memcmp(modes, want_modes, sizeof want_modes));
        assert(ns == 9 && !memcmp(schools, want_schools, sizeof want_schools));
    }

    /* 1b. The story teammate choice, three live choices in capture order.
     *     Ground truth independent of the decoder: the team the game wrote to
     *     R+0x21CC. In each confirmed choice the last highlight before the
     *     first pick, and before the second, are the team's first two members
     *     (the third fills itself). The unconfirmed first capture offered
     *     Seijyun's Akira, Zaki and Yurika. */
    {
        int choice = -1, last_id[3][2] = { { -1, -1 }, { -1, -1 }, { -1, -1 } };
        unsigned offered_mask[3] = { 0, 0, 0 };
        BYTE was_cursor = 0;
        for (unsigned i = 0; i < count; i++) {
            int id = mjus_team_decode(&rec[i], 0);
            assert(mjus_team_decode(&rec[i], 1) == -1);
            if (rec[i].pick[1] == 0xFE && was_cursor != 0xFE && rec[i].team[0]) choice++;
            was_cursor = rec[i].pick[1];
            if (id == -1) continue;
            assert(id >= 0 && choice >= 0 && choice < 3);
            /* Never both screens: free select is off while the story chooses. */
            assert(mjus_select_decode(&rec[i], 0) == -1 && mjus_mode_decode(&rec[i]) == -1);
            offered_mask[choice] |= 1u << id;
            if (mjus_team_picked(&rec[i]) < 2) last_id[choice][mjus_team_picked(&rec[i])] = id;
        }
        assert(choice == 2);
        assert(offered_mask[0] == 0xE && offered_mask[1] == 0xE);              /* Batsu, Hinata, Kyosuke */
        assert(offered_mask[2] == ((1u << 0x0B) | (1u << 0x17) | (1u << 0x19)));
        assert(story_team[0][0] == 1 && story_team[0][1] == 3 && story_team[0][2] == 2);
        assert(story_team[1][0] == 1 && story_team[1][1] == 2 && story_team[1][2] == 3);
        for (int c = 0; c < 2; c++)
            for (int k = 0; k < 2; k++) assert(last_id[c][k] == story_team[c][k]);
        assert(!strcmp(character_name(6, 0x0B, 1), "Akira") && !strcmp(character_name(6, 0x17, 1), "Zaki") &&
               !strcmp(character_name(6, 0x19, 1), "Yurika"));

        /* Spoken through the channel, the story run's choice is the path the
         * cursor took, with no name said twice in a row: the pick's visit
         * moves in the same frame as the cursor's jump, not a frame later. */
        {
            wchar_t said[64][32];
            int n = 0, before, c = -1;
            BYTE was = 0;
            mjus_select_observe(NULL, 0, 0);
            for (unsigned i = 0; i < count; i++) {
                if (rec[i].pick[1] == 0xFE && was != 0xFE && rec[i].team[0]) c++;
                was = rec[i].pick[1];
                if (c != 1) continue;
                before = calls;
                settle(&rec[i]);
                if (calls != before && n < 64) wcsncpy(said[n++], last, 31);
            }
            assert(n >= 4);
            for (int k = 1; k < n; k++) assert(wcscmp(said[k], said[k - 1]));
            mjus_select_observe(NULL, 0, 0);
        }
    }

    /* 2. Free select. Ground truth independent of the decoder: the last
     *    highlight of each slot is what the game committed (R+0x255C..E),
     *    the three characters the player picked: Wild Daigo, Edge, Batsu. */
    for (unsigned i = 0; i < count; i++) {
        int id = mjus_select_decode(&rec[i], 0);
        assert(mjus_select_decode(&rec[i], 1) == -1);  /* one human */
        if (id < 0) continue;
        unsigned slot = rec[i].sides[0][0x17];
        if (first_pick < 0) first_pick = id;
        last_pick[slot] = id;
        picks_seen |= 1u << slot;
    }
    assert(picks_seen == 7);
    assert(first_pick == 1);                           /* the cursor starts on Batsu */
    assert(committed[0] == 0x1C && committed[1] == 0x0A && committed[2] == 0x01);
    for (int k = 0; k < 3; k++) assert(last_pick[k] == committed[k]);
    assert(!strcmp(character_name(6, committed[0], 1), "Wild Daigo"));
    assert(!strcmp(character_name(6, committed[1], 1), "Edge"));

    /* 2b. Versus, then Training (mjus-6). Side records +0x2A are 0xFF, both
     *     sides are human and each picks three. Ground truth independent of
     *     the decoder: each side's last highlight per slot is what the game
     *     committed (R+0x255C, R+0x25DC). Both sides must be read. */
    {
        FILE *f = fopen("tools/fixtures/mjus-versus-20261020.bin", "rb");
        BYTE magic[4], raw[1 + RECORD], commits[2][6];
        unsigned n;
        int vlast[2][2][3], vseen[2][2] = { { 0, 0 }, { 0, 0 } };
        memset(vlast, 0xFF, sizeof vlast);
        assert(f);
        assert(fread(magic, 1, 4, f) == 4 && !memcmp(magic, "MJSV", 4));
        assert(fread(&n, 4, 1, f) == 1 && n > 0 && n <= 1024);
        for (unsigned i = 0; i < n; i++) {
            mjus_select_sample v;
            unsigned session;
            assert(fread(raw, 1, sizeof raw, f) == sizeof raw);
            session = raw[0];
            assert(session < 2);
            memset(&v, 0, sizeof v);
            v.task_state = v.task_step = 0xFF;
            for (unsigned t = 0; t < 16; t++) {
                const BYTE *r = raw + 1 + t * TASK_BYTES;
                if ((r[0] | r[1]) && r[2] == MJUS_SELECT_ROUTINE) { v.task_state = r[3]; v.task_step = r[4]; }
            }
            memcpy(v.menu, raw + 1 + 16 * TASK_BYTES, 3);
            v.humans = raw[1 + 16 * TASK_BYTES + 3];
            memcpy(v.sides[0], raw + 1 + 16 * TASK_BYTES + 4, 0x30);
            memcpy(v.sides[1], raw + 1 + 16 * TASK_BYTES + 4 + 0x30, 0x30);
            v.phase = raw[1 + BASE];
            v.variant = 1;
            assert(mjus_mode_decode(&v) == -1 && mjus_school_decode(&v) == -1);
            for (unsigned side = 0; side < 2; side++) {
                int id = mjus_select_decode(&v, side);
                if (id < 0) continue;
                vlast[session][side][v.sides[side][0x17]] = id;
                vseen[session][side] |= 1 << v.sides[side][0x17];
            }
        }
        assert(fread(commits, 1, sizeof commits, f) == sizeof commits);
        assert(fgetc(f) == EOF);
        fclose(f);
        for (int s = 0; s < 2; s++)
            for (int side = 0; side < 2; side++) {
                assert(vseen[s][side] == 7);
                for (int k = 0; k < 3; k++) assert(vlast[s][side][k] == commits[s][side * 3 + k]);
            }
        /* Versus: side 1 picked 0x10, 0x07, 0x0D; side 2 0x12, 0x05, 0x07. */
        assert(commits[0][0] == 0x10 && commits[0][3] == 0x12);
    }

    /* 3. Words: modes and schools follow the language; names by version. */
    {
        unsigned im = count, is = count, ic = count;
        game_selection_choice c[GAME_SELECTION_OWNERS];
        for (unsigned i = 0; i < count; i++) {
            if (im == count && mjus_mode_decode(&rec[i]) == 1) im = i;
            if (is == count && mjus_school_decode(&rec[i]) == 0) is = i;
            if (ic == count && mjus_select_decode(&rec[i], 0) == 0x1C) ic = i;
        }
        assert(im < count && is < count && ic < count);
        mt_set_language("eng");
        mjus_selection_copy(&rec[im], c);
        assert(c[MJUS_MENU_OWNER].kind == MJUS_KIND_MODE && !wcscmp(c[MJUS_MENU_OWNER].label, L"Free mode"));
        assert(c[0].status == GAME_SELECTION_INACTIVE);
        mjus_selection_copy(&rec[is], c);
        assert(c[MJUS_MENU_OWNER].kind == MJUS_KIND_SCHOOL &&
               !wcscmp(c[MJUS_MENU_OWNER].label, L"Taiyo High School"));
        mt_set_language("jpn");
        mjus_selection_copy(&rec[is], c);
        assert(!wcscmp(c[MJUS_MENU_OWNER].label, L"太陽学園"));
        mjus_selection_copy(&rec[im], c);
        assert(!wcscmp(c[MJUS_MENU_OWNER].label, L"フリーモード"));
        mt_set_language("fre");
        mjus_selection_copy(&rec[is], c);
        assert(!wcscmp(c[MJUS_MENU_OWNER].label, L"Taiyo High School"));
        mt_set_language("eng");
        mjus_selection_copy(&rec[ic], c);
        assert(c[0].status == GAME_SELECTION_VALID && !wcscmp(c[0].label, L"Wild Daigo"));
        {
            mjus_select_sample s = rec[ic];
            s.sides[0][0x18] = 0x12;                   /* the shell's US remap of 0x12 */
            mjus_selection_copy(&s, c);
            assert(!wcscmp(c[0].label, L"Chairperson"));
            s.variant = 0;
            mjus_selection_copy(&s, c);
            assert(!wcscmp(c[0].label, L"Iincyo"));
        }
    }

    /* 4. Spoken through the shared channel in live order; each pick is a new
     *    visit, so its first highlight speaks even when unchanged. */
    {
        int before;
        mjus_select_observe(NULL, 0, 0);
        before = calls;
        for (unsigned i = 0; i < count; i++) settle(&rec[i]);
        assert(calls > before + 50);
        mjus_select_observe(NULL, 0, 0);
        unsigned ic = count;
        for (unsigned i = 0; i < count && ic == count; i++)
            if (mjus_select_decode(&rec[i], 0) == 0x1C) ic = i;
        before = calls;
        settle(&rec[ic]);
        assert(calls == before + 1 && !wcscmp(last, L"Wild Daigo"));
        settle(&rec[ic]);
        assert(calls == before + 1);
        {
            mjus_select_sample next = rec[ic];
            next.sides[0][0x17] = 1;
            next.sides[0][0x19] = 0x1C;
            settle(&next);
            assert(calls == before + 2 && !wcscmp(last, L"Wild Daigo"));
        }
        accepts = 0;
        {
            mjus_select_sample other = rec[ic];
            other.sides[0][0x18] = 0x0A;
            settle(&other);
            int refused = calls;
            accepts = 1;
            mjus_select_observe(&other, ROOT, 13);
            assert(calls == refused + 1 && !wcscmp(last, L"Edge"));
        }
        mjus_select_observe(NULL, 0, 0);
    }

    /* 5. Every gate refuses on its own. */
    {
        unsigned i = count, m = count, s = count;
        mjus_select_sample t;
        for (unsigned j = 0; j < count; j++) {
            if (i == count && mjus_select_decode(&rec[j], 0) >= 0) i = j;
            if (m == count && mjus_mode_decode(&rec[j]) >= 0) m = j;
            if (s == count && mjus_school_decode(&rec[j]) >= 0) s = j;
        }
        assert(i < count && m < count && s < count);
        t = rec[i]; t.task_state = 1;          assert(mjus_select_decode(&t, 0) == -1); /* screen  */
        t = rec[i]; t.task_step = 2;           assert(mjus_select_decode(&t, 0) == -1); /* step    */
        t = rec[i]; t.task_state = 0xFF;       assert(mjus_select_decode(&t, 0) == -1); /* no task */
        t = rec[i]; t.humans = 2;              assert(mjus_select_decode(&t, 0) == -1); /* CPU     */
        t = rec[i]; t.sides[0][0x2A] = 0;      assert(mjus_select_decode(&t, 0) == -1); /* story   */
        t = rec[i]; t.sides[0][0x17] = 3;      assert(mjus_select_decode(&t, 0) == -1); /* done    */
        t = rec[i]; t.sides[0][0x18 + t.sides[0][0x17]] = MJUS_ID_LIMIT;
                                               assert(mjus_select_decode(&t, 0) == -2); /* range   */
        t = rec[i]; t.variant = 2;             assert(mjus_select_decode(&t, 0) == -2); /* version */
        t = rec[i]; t.humans = 3; memcpy(t.sides[1], t.sides[0], 0x30);
        assert(mjus_select_decode(&t, 1) == mjus_select_decode(&t, 0));                 /* side 2  */
        assert(mjus_select_decode(NULL, 0) == -1 && mjus_select_decode(&t, 2) == -1);
        t = rec[m]; t.task_step = 2;           assert(mjus_mode_decode(&t) == -1);
        t = rec[m]; t.menu[2] = 1;             assert(mjus_mode_decode(&t) == -1);       /* confirmed */
        t = rec[s]; t.menu[1] = 5;             assert(mjus_school_decode(&t) == -2);     /* count  */
        t = rec[s]; t.menu[0] = 7;             assert(mjus_school_decode(&t) == -2);     /* range  */
        t = rec[s]; t.humans = 3;              assert(mjus_school_decode(&t) == -1);     /* no side */
        t = rec[s]; t.sides[0][0x2A] = 1;      assert(mjus_school_decode(&t) == -1);     /* free   */
        t = rec[s]; t.menu[2] = 1;             assert(mjus_school_decode(&t) == -1);
        {
            unsigned k = count;
            for (unsigned j = 0; j < count && k == count; j++) if (mjus_team_decode(&rec[j], 0) >= 0) k = j;
            assert(k < count);
            t = rec[k]; t.phase = 2;           assert(mjus_team_decode(&t, 0) == -1);   /* fighting */
            t = rec[k]; t.menu[2] = 1;         assert(mjus_team_decode(&t, 0) == -1);   /* done     */
            t = rec[k]; t.pick[1] = 0xFE;      assert(mjus_team_decode(&t, 0) == -1);   /* idle     */
            t = rec[k]; t.team[0] = 2;         assert(mjus_team_decode(&t, 0) == -1);   /* side 2   */
            t = rec[k]; t.humans = 2;          assert(mjus_team_decode(&t, 0) == -1);   /* CPU      */
            t = rec[k]; t.pick[2 + t.pick[1]] = 1; assert(mjus_team_decode(&t, 0) == -1); /* picked */
            t = rec[k]; t.pick[1] = t.team[2]; assert(mjus_team_decode(&t, 0) == -2);   /* range    */
            t = rec[k]; t.team[2] = 0;         assert(mjus_team_decode(&t, 0) == -2);   /* count    */
            t = rec[k]; t.team[3 + t.pick[1]] = MJUS_ID_LIMIT; assert(mjus_team_decode(&t, 0) == -2);
            t = rec[k]; t.team[0] = 2; t.humans = 2; assert(mjus_team_decode(&t, 1) == mjus_team_decode(&rec[k], 0));
        }
    }

    printf("PASS: %u live Project Justice samples: menu and school order, committed picks, "
           "words by language and version, shared-channel speech/visit/retry, every gate\n", count);
    return 0;
}
