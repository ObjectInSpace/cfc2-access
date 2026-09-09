/*
 * Does the string-blob index return the RIGHT string, and return it FAST?
 *
 * ⛔⛔ WHY THE SPEED HALF IS A TEST AND NOT A COMMENT.
 * The first gmd_text_by_index() walked the blob from its base on every call,
 * one GUARDED single-byte read per character. It was correct and unusably slow:
 * measured in-game, FOUR lookups took ELEVEN SECONDS on the poll thread, and a
 * run that should have logged dozens of menu movements recorded exactly one.
 * Nothing failed -- the mod just quietly stopped keeping up. A correctness-only
 * test would still pass today with that version restored, so the budget below
 * is what actually pins the fix.
 *
 * This drives the SHIPPING code against a synthetic blob planted in this
 * process, so it needs no game.
 */

#include "../src/gmd_find.h"
#include "../src/memsafe.h"
#include "../src/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int failures;

static void check(const char *name, int cond, const char *fmt, ...)
{
    if (cond) { printf("  PASS  %s\n", name); return; }
    printf("  FAIL  %s", name);
    if (fmt && *fmt) {
        va_list ap; va_start(ap, fmt);
        printf("  "); vprintf(fmt, ap); va_end(ap);
    }
    printf("\n");
    failures++;
}

int main(void)
{
    if (!mem_guard_install()) {
        printf("FATAL: fault guard self-test failed\n");
        return 1;
    }

    /* Plant a blob of GMD_LABEL_COUNT NUL-terminated strings: "s0", "s1", ...
     * Contents are unique per index, so a wrong offset is caught by VALUE and
     * not merely by length. */
    const int N = gmd_test_label_count();
    SIZE_T cap = (SIZE_T)N * 160 + 64;
    char *blob = (char *)VirtualAlloc(NULL, cap, MEM_COMMIT | MEM_RESERVE,
                                      PAGE_READWRITE);
    if (!blob) { printf("FATAL: alloc failed\n"); return 1; }

    /* ⚠ REALISTIC STRING LENGTHS. The first version of this test planted
     * ~6-byte strings, measured 4165 lookups at 6 ms, and passed -- while the
     * real thing took EIGHT SECONDS, because read_cstr() was doing one guarded
     * read PER BYTE and the real labels are far longer. A benchmark whose data
     * is unlike the real data measures the wrong thing. Real menu labels run to
     * ~30 characters and help blurbs to ~120, so plant that. */
    SIZE_T pos = 0;
    for (int i = 0; i < N; i++) {
        int n;
        if (i % 3 == 0)
            n = _snprintf(blob + pos, cap - pos,
                          "s%d_this_is_a_help_blurb_of_realistic_length_for_a_"
                          "menu_item_description_padding_padding", i);
        else
            n = _snprintf(blob + pos, cap - pos,
                          "s%d_menu_item_label_text", i);
        pos += (SIZE_T)n + 1;                  /* keep the NUL */
    }

    gmd_test_set_blob((BYTE *)blob, N);

    /* --- case 1: the first, a middle and the last string all read back ---- */
    char buf[128];
    int ok1 = gmd_text_by_index(0, buf, sizeof(buf)) &&
              strncmp(buf, "s0_", 3) == 0;
    int okm = gmd_text_by_index(N / 2, buf, sizeof(buf));
    char expect[64]; _snprintf(expect, sizeof(expect), "s%d_", N / 2);
    okm = okm && strncmp(buf, expect, strlen(expect)) == 0;
    int okl = gmd_text_by_index(N - 1, buf, sizeof(buf));
    _snprintf(expect, sizeof(expect), "s%d_", N - 1);
    okl = okl && strncmp(buf, expect, strlen(expect)) == 0;
    check("1. first, middle and last strings read back correctly",
          ok1 && okm && okl, "first=%d middle=%d last=%d", ok1, okm, okl);

    /* --- case 2: EVERY index is exact -------------------------------------
     * An off-by-one in the offset table would still return plausible strings,
     * so check all of them by value rather than spot-checking. */
    int bad = -1;
    for (int i = 0; i < N; i++) {
        if (!gmd_text_by_index(i, buf, sizeof(buf))) { bad = i; break; }
        _snprintf(expect, sizeof(expect), "s%d_", i);
        if (strncmp(buf, expect, strlen(expect)) != 0) { bad = i; break; }
    }
    check("2. all N indices return exactly their own string",
          bad < 0, "first wrong index = %d", bad);

    /* --- case 3: THE SPEED BUDGET -----------------------------------------
     * ⭐ The regression that shipped was purely one of cost. 4165 lookups must
     * finish in well under a second; the walking version needed seconds for
     * FOUR. The budget is deliberately loose (2 s) so this fails on the real
     * defect and not on a slow machine. */
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    for (int i = 0; i < N; i++)
        gmd_text_by_index(i, buf, sizeof(buf));
    QueryPerformanceCounter(&b);
    double ms = (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
    /* ⚠ THE BUDGET IS 50 ms, NOT 2 s, AND THAT MATTERS.
     *
     * The old 2 s budget passed the per-byte read_cstr() at 172 ms -- so it
     * would have shipped the regression that made a discovery block take EIGHT
     * SECONDS in-game. The discovery sweep performs roughly THREE TIMES this
     * many reads, so a per-lookup cost that looks harmless here is seconds
     * there.
     *
     * 50 ms leaves ~13x headroom over the measured 3.8 ms (slow machines, a
     * loaded CI box) while still failing the 172 ms version outright. A budget
     * loose enough to pass the bug is not a budget. */
    check("3. N lookups complete well inside the budget",
          ms < 50.0, "%d lookups took %.1f ms (budget 50 ms)", N, ms);
    printf("        (%d lookups in %.1f ms, %.3f ms each)\n", N, ms, ms / N);

    /* --- case 4: out-of-range is refused, not clamped ---------------------- */
    check("4. out-of-range indices are refused",
          !gmd_text_by_index(-1, buf, sizeof(buf)) &&
          !gmd_text_by_index(N, buf, sizeof(buf)) &&
          !gmd_text_by_index(N + 1000, buf, sizeof(buf)), "");

    printf("\n");
    if (failures) { printf("FAILED: %d case(s)\n", failures); return 1; }
    printf("all cases passed\n");
    return 0;
}
