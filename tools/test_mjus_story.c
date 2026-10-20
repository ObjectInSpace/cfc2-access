/* Project Justice story dialogue: which line is on screen, and its text.
 * Replays tools/fixtures/mjus-story-20261019.bin (two live story scenes, see
 * make_mjus_story_fixture.py) through the pure reader in src/mjus_story.c. */
#include "../src/mjus_story.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

void log_line(const char *fmt, ...) { (void)fmt; }
BOOL speech_say(const wchar_t *s, BOOL interrupt) { (void)s; (void)interrupt; return TRUE; }
void speech_stop(void) {}

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static unsigned char root[0x93F30 + 16 * 0x18];

static void set_rec(int side, int state, int msg)
{
    unsigned char *r = root + 0x2600 + side * 0x5C;
    memcpy(r + 0x24, &state, 4);
    memcpy(r + 0x2C, &msg, 4);
}

/* Feeds one frame; commits every new line (an accepting queue). */
static int step(mjus_story_edges *e, int out[2])
{
    mjus_story_view v;
    int keys[2], sides[2], n;
    mjus_story_read(root, &v);
    n = mjus_story_new(e, &v, keys, sides);
    for (int i = 0; i < n; i++) { out[i] = keys[i]; mjus_story_commit(e, &v, sides[i]); }
    mjus_story_settle(e, &v);
    return n;
}

static void case_replay(void)
{
    static const int want1[] = { 0x2600, 0x2601, 0x2602, 0x2603, 0x2604 };
    static const int want3[] = { 0x4300, 0x4301, 0x4302, 0x4303, 0x4304, 0x4305, 0x4306, 0x4307 };
    int got[2][16], ngot[2] = { 0, 0 }, cap = -1, lang_ok = 1, missing = 0;
    int intros[4][2], nintro = 0, intro = MJUS_STORY_NONE;
    unsigned count;
    mjus_story_edges e;
    FILE *f = fopen("tools/fixtures/mjus-story-20261019.bin", "rb");
    unsigned char hdr[8], rec[4 + 1 + 3 + 16 * 5 + 2 * 0x5C];
    CHECK(f != NULL, "fixture missing");
    if (!f) return;
    CHECK(fread(hdr, 1, 8, f) == 8 && !memcmp(hdr, "MJST", 4), "fixture header");
    memcpy(&count, hdr + 4, 4);
    CHECK(count > 20, "fixture has %u records", count);
    mjus_story_reset(&e);
    for (unsigned i = 0; i < count; i++) {
        int out[2], n, which;
        if (fread(rec, 1, sizeof rec, f) != sizeof rec) { CHECK(0, "short record %u", i); break; }
        if (rec[4] != cap) { cap = rec[4]; mjus_story_reset(&e); }   /* a new session */
        memset(root, 0, sizeof root);
        /* The game's addresses, literally: a wrong offset in the header must
         * not be cancelled by placing the fixture with that same offset. */
        root[0x206C] = rec[5];
        root[0x2259] = rec[6];
        root[0x2238] = rec[7];
        for (int t = 0; t < 16; t++) {
            static const int at[5] = { 0, 1, 5, 7, 8 };
            for (int k = 0; k < 5; k++) root[0x93F30 + t * 0x18 + at[k]] = rec[8 + t * 5 + k];
        }
        memcpy(root + 0x2600, rec + 8 + 80, 2 * 0x5C);
        {
            mjus_story_view v;
            mjus_story_read(root, &v);
            if (v.intro_school != intro && v.intro_school != MJUS_STORY_NONE && nintro < 4) {
                intros[nintro][0] = cap;
                intros[nintro++][1] = v.intro_school;
            }
            intro = v.intro_school;
        }
        n = step(&e, out);
        which = cap == 1 ? 0 : 1;
        for (int k = 0; k < n; k++) {
            if (ngot[which] < 16) got[which][ngot[which]++] = out[k];
            if (!mjus_story_lookup(out[k], FALSE) || !mjus_story_lookup(out[k], TRUE)) missing++;
        }
        if (rec[5] == 0) lang_ok = 0;            /* both captures are English */
    }
    fclose(f);
    CHECK(lang_ok, "a capture read as Japanese");
    CHECK(missing == 0, "%d shown line(s) without a transcription", missing);
    CHECK(ngot[0] == 5 && !memcmp(got[0], want1, sizeof want1), "scene 38: %d lines, first %#x", ngot[0], ngot[0] ? got[0][0] : -1);
    CHECK(ngot[1] == 8 && !memcmp(got[1], want3, sizeof want3), "scene 67: %d lines, first %#x", ngot[1], ngot[1] ? got[1][0] : -1);
    /* Each capture opens one intro: Seijyun (5) in mjus-1, Taiyo (0) in mjus-3. */
    CHECK(nintro == 2 && intros[0][0] == 1 && intros[0][1] == 5 && intros[1][0] == 3 && intros[1][1] == 0,
          "intros: %d, first cap %d school %d", nintro, nintro ? intros[0][0] : -1, nintro ? intros[0][1] : -1);
}

static void case_narration(void)
{
    int p[3];
    const wchar_t *t;
    CHECK(mjus_narration_pages(0, p) == 3 && p[0] == 0 && p[1] == 1 && p[2] == 2, "Taiyo: the opening, then 1, 2");
    CHECK(mjus_narration_pages(5, p) == 3 && p[0] == 0 && p[1] == 11 && p[2] == 12, "Seijyun: the opening, then 11, 12");
    CHECK(mjus_narration_pages(6, p) == 2 && p[0] == 13 && p[1] == 14, "Darkside: no shared opening");
    CHECK(mjus_narration_pages(7, p) == 0 && mjus_narration_pages(-1, p) == 0, "no such school");
    t = mjus_narration_text(11, FALSE);
    CHECK(t && !wcsncmp(t, L"Akira Kazama was transferred", 28), "P11 English");
    t = mjus_narration_text(0, TRUE);
    CHECK(t && !wcsncmp(t, L"\x5fcc\x91ce\x96f9", 3), "P00 Japanese");
    CHECK(!mjus_narration_text(15, FALSE) && !mjus_narration_text(-1, TRUE), "out of range");
    /* The intro gate: routine 7, part 1, step 1 on; another part is not it. */
    {
        mjus_story_view v;
        unsigned char *task = root + 0x93F30 + 3 * 0x18;
        memset(root, 0, sizeof root);
        root[0x2238] = 2;
        task[0] = 1; task[5] = 7; task[7] = 1; task[8] = 0;
        mjus_story_read(root, &v);
        CHECK(v.intro_school == MJUS_STORY_NONE, "step 0 is still loading");
        task[8] = 2;
        mjus_story_read(root, &v);
        CHECK(v.intro_school == 2, "intro shown: %d", v.intro_school);
        task[7] = 2;
        mjus_story_read(root, &v);
        CHECK(v.intro_school == MJUS_STORY_NONE, "the dialogue part is not the intro");
        task[7] = 1; task[0] = 0;
        mjus_story_read(root, &v);
        CHECK(v.intro_school == MJUS_STORY_NONE, "a dead task is not the intro");
        task[0] = 1; task[5] = 6;
        mjus_story_read(root, &v);
        CHECK(v.intro_school == MJUS_STORY_NONE, "another routine is not the intro");
    }
}

static void case_lookup(void)
{
    const wchar_t *t = mjus_story_lookup(0x4303, FALSE);
    CHECK(t && !wcscmp(t, L"Greetings, Batsu Ichimonji and Kyosuke Kagami."), "67.03 English");
    t = mjus_story_lookup(0x0303, FALSE);       /* a blank, filled from the overlay */
    CHECK(t && wcsstr(t, L"Daigo from Gedo High was famous"), "03.03 English fill");
    t = mjus_story_lookup(0x0303, TRUE);
    CHECK(t && wcsstr(t, L"\x5916\x9053\x9ad8\x6821\x306e"), "03.03 Japanese fill");
    t = mjus_story_lookup(0x0807, TRUE);
    CHECK(t && !wcsncmp(t, L"\x30a2\x30f3\x30b3\x30af", 4), "08.07 Japanese uses the overlaid reading");
    CHECK(!mjus_story_lookup(0x030B, FALSE) && !mjus_story_lookup(0x030B, TRUE), "a words cell is never a line");
    CHECK(!mjus_story_lookup(0x6400, FALSE), "an unknown scene (100; scenes are 0..99)");
    CHECK(!mjus_story_lookup(-1, FALSE), "no message");
    CHECK(mjus_story_lookup(0x0000, FALSE) && mjus_story_lookup(0x6309, TRUE), "the first and the last keys");
}

static void case_language(void)
{
    mjus_story_view v;
    memset(root, 0, sizeof root);
    root[0x2259] = 67;
    set_rec(0, 1, 3);
    mjus_story_read(root, &v);
    CHECK(v.japanese, "R+0x206C 0 is Japanese");
    CHECK(v.shown[0] == 0x4303 && v.shown[1] == MJUS_STORY_NONE, "shown %#x %#x", v.shown[0], v.shown[1]);
    root[0x206C] = 1;
    mjus_story_read(root, &v);
    CHECK(!v.japanese, "R+0x206C 1 is English");
    set_rec(0, 1, 300);
    mjus_story_read(root, &v);
    CHECK(v.shown[0] == MJUS_STORY_NONE, "an out-of-range message is not a line");
}

static void case_edges(void)
{
    mjus_story_edges e;
    mjus_story_view v;
    int keys[2], sides[2], out[2];
    memset(root, 0, sizeof root);
    root[0x206C] = 1;
    root[0x2259] = 67;
    mjus_story_reset(&e);
    set_rec(0, 1, 0);
    /* A refused queue: the line is offered again next frame. */
    mjus_story_read(root, &v);
    CHECK(mjus_story_new(&e, &v, keys, sides) == 1, "first offer");
    mjus_story_settle(&e, &v);
    mjus_story_read(root, &v);
    CHECK(mjus_story_new(&e, &v, keys, sides) == 1 && keys[0] == 0x4300, "retried after no commit");
    /* Accepted, then held for many frames: once. */
    CHECK(step(&e, out) == 1, "accepted");
    CHECK(step(&e, out) == 0 && step(&e, out) == 0, "a held line repeats");
    /* Closing keeps it; the other side speaks. */
    set_rec(0, 2, 0);
    set_rec(1, 1, 1);
    CHECK(step(&e, out) == 1 && out[0] == 0x4301, "the other side");
    set_rec(0, 1, 0);
    CHECK(step(&e, out) == 0, "closing then reshown is not a new line");
    /* Emptied, then the same line again (a replayed scene): spoken again. */
    set_rec(0, 0, 0);
    CHECK(step(&e, out) == 0, "empty");
    set_rec(0, 1, 0);
    CHECK(step(&e, out) == 1 && out[0] == 0x4300, "re-armed after the record emptied");
    /* Both sides new in one frame: side order. */
    mjus_story_reset(&e);
    set_rec(0, 1, 4);
    set_rec(1, 1, 5);
    CHECK(step(&e, out) == 2 && out[0] == 0x4304 && out[1] == 0x4305, "two at once");
}

int main(void)
{
    case_replay();
    case_lookup();
    case_narration();
    case_language();
    case_edges();
    if (failures) { printf("mjus story: %d FAILED\n", failures); return 1; }
    printf("mjus story: all cases passed\n");
    return 0;
}
