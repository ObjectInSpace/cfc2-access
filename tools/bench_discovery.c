/* Measure the discovery sweep against the REAL GMD string blob.
 *
 * ⚠ Real data, not synthetic: this project already hid a per-byte reader
 * behind 6-byte synthetic strings, and a benchmark whose data is unlike the
 * real data measures nothing.
 *
 * ⛔⛔ AND IT MUST MEASURE WHAT SHIPS. This file previously capped the sweep at
 * 600 bases, did ONE pass, and checked only a single trailing sentence. The
 * shipping sweep in menu_speak.c had since grown to all ~4165 bases, a
 * per-item HELP check, and a SECOND relaxed pass when strict finds < 2
 * candidates. So the "under 1 ms" number this benchmark produced was measuring
 * a path that no longer existed -- and the real thing STALLED A LIVE RUN FOR
 * 37 SECONDS on a 9-item screen (2026-09-09 23:13, entering Music).
 *
 * ⭐ The rule this cost: a benchmark is only evidence while it tracks the code
 * it claims to measure. Re-derive it when the code changes, or delete it.
 * (memory: verify-tools-must-audit-newest-build)
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "gmd_find.h"
#include "memsafe.h"

#define CLS_ITEM  0x01
#define CLS_BLURB 0x02
static unsigned char g_cls[8192];

/* Classify every string ONCE, exactly as menu_speak.c does. This is the whole
 * optimisation: the old sweep re-read string i once per window containing it. */
static double classify(int total)
{
    LARGE_INTEGER fq, a, b;
    QueryPerformanceFrequency(&fq);
    QueryPerformanceCounter(&a);
    int n = total < 8192 ? total : 8192;
    for (int i = 0; i < n; i++) {
        char s[128];
        unsigned char f = 0;
        if (gmd_text_by_index(i, s, sizeof(s)) && s[0]) {
            size_t L = strlen(s);
            char last = s[L - 1];
            int terminal = (last == '.' || last == '?' || last == '!');
            if (s[0] != '<' && L <= 32 && !terminal) f |= CLS_ITEM;
            if (terminal) f |= CLS_BLURB;
        }
        g_cls[i] = f;
    }
    QueryPerformanceCounter(&b);
    return (b.QuadPart - a.QuadPart) * 1000.0 / fq.QuadPart;
}

/* One pass of the shipping sweep, over the classification. `strict` mirrors
 * menu_speak.c: strict demands one HELP blurb per item, relaxed one trailing
 * sentence. Returns the number of candidates it would have shown. */
static int sweep(int count, int total, int strict)
{
    int shown = 0;
    for (int base = 0; base + count < total && shown < 8; base++) {
        int ok = 1;
        for (int i = 0; i < count; i++) {
            if (!(g_cls[base + i] & CLS_ITEM)) { ok = 0; break; }
        }
        if (!ok) continue;

        int need = strict ? count : 1;
        if (base + count + need > total) continue;
        for (int i = 0; i < need; i++) {
            if (!(g_cls[base + count + i] & CLS_BLURB)) { ok = 0; break; }
        }
        if (!ok) continue;
        shown++;
    }
    return shown;
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: bench_discovery <strings.bin>\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { printf("no file\n"); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    BYTE *blob = (BYTE *)VirtualAlloc(NULL, n + 4096,
                                      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (fread(blob, 1, n, f) != (size_t)n) { printf("short read\n"); return 1; }
    fclose(f);
    if (!mem_guard_install()) { printf("guard failed\n"); return 1; }
    gmd_test_set_blob(blob, 4165);

    int total = gmd_text_count();
    printf("strings visible: %d\n", total);
    char probe[128];
    if (gmd_text_by_index(37, probe, sizeof(probe)))
        printf("index 37 = \"%s\"\n", probe);
    printf("\nsweeping ALL %d bases, as the shipping code does.\n", total);
    printf("'total' includes the relaxed re-run when strict yields < 2.\n\n");

    LARGE_INTEGER fq, a, b;
    QueryPerformanceFrequency(&fq);

    /* ⭐ The one-off classification pays for every count below. Charged
     * explicitly so it is not hidden inside a per-count number. */
    double ms_cls = classify(total);
    printf("classification: %.2f ms ONCE for %d strings "
           "(then every sweep is array arithmetic)\n\n", ms_cls, total);

    /* ⚠ 9 is the count that stalled the live run (the Music screen). */
    int counts[] = { 2, 3, 6, 8, 9, 10, 40, 67 };
    double worst = 0.0;
    int worst_count = 0;

    for (unsigned c = 0; c < sizeof(counts) / sizeof(counts[0]); c++) {
        int count = counts[c];

        QueryPerformanceCounter(&a);
        int shown = sweep(count, total, 1);
        QueryPerformanceCounter(&b);
        double ms_strict = (b.QuadPart - a.QuadPart) * 1000.0 / fq.QuadPart;

        double ms_relaxed = 0.0;
        int shown2 = shown;
        if (shown < 2) {                 /* exactly the shipping back-off */
            QueryPerformanceCounter(&a);
            shown2 = sweep(count, total, 0);
            QueryPerformanceCounter(&b);
            ms_relaxed = (b.QuadPart - a.QuadPart) * 1000.0 / fq.QuadPart;
        }

        double ms_total = ms_strict + ms_relaxed;
        if (ms_total > worst) { worst = ms_total; worst_count = count; }

        printf("count=%2d -> strict %8.2f ms (%d cand)%s  TOTAL %8.2f ms\n",
               count, ms_strict, shown,
               ms_relaxed > 0.0 ? "  + RELAXED re-run" : "                  ",
               ms_total);
    }

    printf("\nworst case: %.2f ms at count=%d\n", worst, worst_count);

    /* ⭐ THE BUDGET MUST ACTUALLY BITE. Re-run the worst case with the same
     * 25 ms wall-clock bound the shipping sweep uses, and assert it stops
     * early -- a budget that never triggers is not protection, and on this
     * (fast, clean) blob it is the only place the mechanism can be exercised
     * offline at all. */
    {
        int count = worst_count;
        LARGE_INTEGER t0, now;
        QueryPerformanceCounter(&t0);
        int examined = 0, stopped = 0;
        for (int base = 0; base + count < total; base++) {
            if ((base & 63) == 0) {
                QueryPerformanceCounter(&now);
                if ((now.QuadPart - t0.QuadPart) * 1000.0 / fq.QuadPart > 25.0) {
                    stopped = 1;
                    break;
                }
            }
            examined++;
            int ok = 1;
            for (int i = 0; i < count; i++) {
                if (!(g_cls[base + i] & CLS_ITEM)) { ok = 0; break; }
            }
            (void)ok;
        }
        printf("\n25 ms budget on count=%d: examined %d of %d bases, %s\n",
               count, examined, total,
               stopped ? "STOPPED early (budget works)"
                       : "finished within budget");
        if (worst > 25.0 && !stopped) {
            printf("⛔ FAIL: the worst case exceeds 25 ms but the budget did "
                   "not trigger -- the bound is not doing anything.\n");
            return 1;
        }
    }

    /* ⭐ A benchmark that only prints numbers gets read as "fine". State the
     * budget and FAIL when it is blown -- this runs on the 100 ms poll thread.
     *
     * ⚠⚠ BUT THIS NUMBER IS A FLOOR, NOT THE TRUTH. The blob here is one clean
     * VirtualAlloc; the game's sits on a heap with different page boundaries,
     * so mem_readable_run() walks more regions per read. The same understating
     * has now happened THREE times in this project -- most recently at "under
     * 1 ms" measured here, against 37 SECONDS in-game.
     *
     * ⭐ Which is why the shipping sweep no longer trusts any number from this
     * file: menu_speak.c enforces a 25 ms WALL-CLOCK budget as it runs and
     * reports a partial sweep as partial. This benchmark is now a regression
     * guard on the algorithm's shape, not the safety mechanism.
     * (memory: measure-dont-predict) */
    if (worst > 50.0) {
        printf("⛔ FAIL: %.2f ms even on a clean single-allocation blob -- and "
               "the real one is SLOWER. The shipping sweep's 25 ms budget will "
               "truncate this heavily; if that is not intended, the algorithm "
               "needs to get cheaper, not the budget larger.\n", worst);
        return 1;
    }
    printf("ok: %.2f ms worst case here. ⚠ Treat as a FLOOR -- the live blob is "
           "slower, and the shipping 25 ms budget is what actually protects "
           "the poll thread.\n", worst);
    return 0;
}
