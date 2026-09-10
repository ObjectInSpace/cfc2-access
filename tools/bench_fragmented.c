/* Measure gmd_text_by_index() in a FRAGMENTED address space.
 *
 * ⭐⭐ WHY THIS EXISTS. Every previous benchmark of this reader ran in a small
 * test process with one clean allocation, and every one of them understated the
 * live cost -- most recently by ~7000x:
 *
 *     bench:  4165 reads in   1.46 ms
 *     game:   4165 reads in  11,900 ms   (2026-09-09 23:52, a live stall)
 *
 * The cost is not the copy. It is mem_readable_run()'s VirtualQuery on every
 * read, which walks the process's VAD tree -- and the game has **2336 regions
 * over 1.6 GB** (its own log says so) while a test process has a handful.
 * A cost that depends on the whole address space CANNOT be measured in a
 * process that does not have one.
 *
 * So this benchmark builds the address space first: thousands of separate
 * reservations, so VirtualQuery has a real tree to walk. Then it measures the
 * reader with the validated-span fast path ON and OFF.
 *
 * ⚠ Still a floor, not the truth -- the game's regions come from a real heap
 * and allocator. But it is the first version of this benchmark that can show
 * the effect at all.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "gmd_find.h"
#include "memsafe.h"

/* Reserve many small, separate regions so the VAD tree is deep. Interleaving
 * reserve/commit prevents the allocator coalescing them into one region. */
static int fragment(int want_regions)
{
    int made = 0;
    for (int i = 0; i < want_regions; i++) {
        void *p = VirtualAlloc(NULL, 4096, MEM_RESERVE, PAGE_NOACCESS);
        if (!p) break;
        void *q = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT,
                               PAGE_READWRITE);
        if (!q) break;
        made += 2;
    }
    return made;
}

static int count_regions(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    BYTE *p = NULL;
    int n = 0;
    while (VirtualQuery(p, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        n++;
        BYTE *next = (BYTE *)mbi.BaseAddress + mbi.RegionSize;
        if (next <= p) break;
        p = next;
        if (n > 200000) break;
    }
    return n;
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: bench_fragmented <strings.bin>\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { printf("no file\n"); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    BYTE *blob = (BYTE *)VirtualAlloc(NULL, n + 4096,
                                      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (fread(blob, 1, n, f) != (size_t)n) { printf("short read\n"); return 1; }
    fclose(f);
    if (!mem_guard_install()) { printf("guard failed\n"); return 1; }

    printf("regions before fragmenting: %d\n", count_regions());
    int made = fragment(1500);
    printf("made %d reservations; regions now: %d "
           "(the game has ~2336)\n\n", made, count_regions());

    gmd_test_set_blob(blob, 4165);
    int total = gmd_text_count();

    LARGE_INTEGER fq, a, b;
    QueryPerformanceFrequency(&fq);

    /* Pass 1 builds the index AND the validated span, so this includes the
     * one-off indexing cost. */
    QueryPerformanceCounter(&a);
    for (int i = 0; i < total; i++) {
        char s[128];
        gmd_text_by_index(i, s, sizeof(s));
    }
    QueryPerformanceCounter(&b);
    double first = (b.QuadPart - a.QuadPart) * 1000.0 / fq.QuadPart;

    /* Pass 2: steady state, span cached. */
    QueryPerformanceCounter(&a);
    for (int i = 0; i < total; i++) {
        char s[128];
        gmd_text_by_index(i, s, sizeof(s));
    }
    QueryPerformanceCounter(&b);
    double second = (b.QuadPart - a.QuadPart) * 1000.0 / fq.QuadPart;

    printf("classify all %d strings:\n", total);
    printf("  first pass (builds index + span): %8.2f ms\n", first);
    printf("  second pass (span cached):        %8.2f ms  (%.4f ms/read)\n",
           second, second / total);

    /* The comparison that matters: what the SAME loop costs when every read
     * asks VirtualQuery, which is what the reader did before the span. */
    QueryPerformanceCounter(&a);
    volatile SIZE_T sink = 0;
    for (int i = 0; i < total; i++)
        sink += mem_readable_run(blob + (i % 1000), 255);
    QueryPerformanceCounter(&b);
    double vq = (b.QuadPart - a.QuadPart) * 1000.0 / fq.QuadPart;
    printf("  %d mem_readable_run calls:        %8.2f ms  (%.4f ms each)\n",
           total, vq, vq / total);

    printf("\n");
    if (second > 250.0) {
        printf("⛔ FAIL: %.2f ms to classify is a visible stall on the poll "
               "thread.\n", second);
        return 1;
    }
    printf("ok: %.2f ms with the span cached. ⚠ FLOOR, not truth -- the live "
           "process is more fragmented than this.\n", second);
    return 0;
}
