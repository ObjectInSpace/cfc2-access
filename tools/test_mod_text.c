/* Offline test for src/mod_text.c: every phrase in every language, the
 * placeholders each translation keeps, reordering, language following and
 * the UTF-8 path. Run from the repository root. */
#include "../src/mod_text.c"
#include <assert.h>
#include <stdio.h>

/* The set of {n} placeholders a template uses, as a bit mask, or -1 when a
 * brace is malformed. */
static int slots(const wchar_t *t)
{
    int mask = 0;
    for (; *t; t++) {
        if (*t != L'{') continue;
        if (t[1] < L'0' || t[1] > L'2' || t[2] != L'}') return -1;
        mask |= 1 << (t[1] - L'0');
    }
    return mask;
}

int main(void)
{
    wchar_t out[256];
    char out8[256];
    int cells = 0;

    /* 1. Every phrase has its own text in every language, with exactly the
     *    English placeholders: a translation may reorder them, never drop or
     *    add one. */
    for (int id = 0; id < MT_COUNT; id++) {
        int want = slots(g_text[id][MT_ENGLISH]);
        assert(g_text[id][MT_ENGLISH] && g_text[id][MT_ENGLISH][0] && want >= 0);
        for (int l = 0; l < MT_NLANG; l++) {
            const wchar_t *t = g_text[id][l];
            if (!t || !t[0] || slots(t) != want)
                fprintf(stderr, "phrase %d language %s: %ls\n", id, g_codes[l], t ? t : L"(null)");
            assert(t && t[0]);
            assert(slots(t) == want);
            cells++;
        }
    }

    /* 2. English is what the other suites pin. */
    mt_set_language("eng");
    mt_fmt_n(out, 256, MT_POS, 3, 6);              assert(!wcscmp(out, L"3 of 6"));
    mt_fmt_n(out, 256, MT_TEAM_RATIO, 2, 4);       assert(!wcscmp(out, L"Team ratio 2 of 4"));
    mt_fmt(out, 256, MT_ORDER, L"Ryu", mt(MT_FIRST), NULL); assert(!wcscmp(out, L"Ryu first"));
    mt_fmt(out, 256, MT_MODE, mt(MT_MODE_NORMAL), NULL, NULL); assert(!wcscmp(out, L"Normal mode"));
    mt_fmt(out, 256, MT_PLAYER_N, L"2", NULL, NULL); assert(!wcscmp(out, L"Player 2"));
    assert(!wcscmp(mt(MT_STAGE_PREFIX), L"Stage: ") && !wcscmp(mt(MT_TURBO), L"Turbo"));

    /* 3. A language orders its own words: Japanese puts the total first. */
    mt_set_language("jpn");
    mt_fmt_n(out, 256, MT_POS, 3, 6);              assert(!wcscmp(out, L"6中3"));
    mt_set_language("ger");
    mt_fmt_n(out, 256, MT_POS, 3, 6);              assert(!wcscmp(out, L"3 von 6"));
    assert(!wcscmp(mt(MT_RANDOM), L"Zufällig"));     /* the collection's own word */

    /* 4. Following the game: an unreadable or unknown language keeps the
     *    current one; the explicit seam can return to unknown = English. */
    mt_update_language("fre");                     assert(mt_language() == 2);
    mt_update_language(NULL);                      assert(mt_language() == 2);
    mt_update_language("xxx");                     assert(mt_language() == 2);
    mt_set_language(NULL);                         assert(mt_language() == -1);
    assert(!wcscmp(mt(MT_YOU), L"You"));
    assert(!wcscmp(mt_in(MT_YOU, 99), L"You") && !wcscmp(mt_in(MT_COUNT, 0), L""));

    /* 5. The UTF-8 path, as the menu speech uses it, round-trips. */
    mt_set_language("rus");
    mt_fmt8(out8, sizeof out8, MT_POS, "3", "6", NULL);
    assert(!strcmp(out8, "3 \xd0\xb8\xd0\xb7 6"));  /* "3 из 6" */
    mt_set_language("eng");
    mt_fmt8(out8, sizeof out8, MT_OR, "Yes", "No", NULL);
    assert(!strcmp(out8, "Yes or No"));

    /* 6. Output never overruns, and a missing argument reads as empty. */
    mt_fmt(out, 4, MT_POS, L"123", L"456", NULL);  assert(wcslen(out) == 3);
    mt_fmt(out, 256, MT_POS, NULL, L"6", NULL);    assert(!wcscmp(out, L" of 6"));

    printf("PASS: %d phrases x %d languages = %d cells, placeholders kept, reorder, "
           "language following, UTF-8 path, bounds\n", MT_COUNT, MT_NLANG, cells);
    return 0;
}
