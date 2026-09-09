/*
 * Does the resolver pick the blob matching the GAME's language?
 *
 * ⛔⛔ THE BUG, REPRODUCED. The label array is language-independent (labels are
 * hashes), so ONE label array can be paired with several string blobs -- and
 * after a language change the old blob is still resident. The resolver took the
 * FIRST offset whose text matched ANY shipped language, and the stale French
 * blob sat at the first offset tried. The user's game displayed English while
 * the mod spoke French, with every structural check passing: closed-loop label
 * verification, geometry, probe counts, all green.
 *
 * ⭐ What makes this testable at all: every shipped table names ITSELF uniquely
 * at index 0 -- "Japanese [jp]" in English, "Japonais [jp]" in French. So the
 * blob can be asked what it is.
 *
 * This plants TWO blobs the way memory actually looked -- French first, English
 * later -- and requires the resolver to return English when that is what the
 * game is running. Taking the first match fails it.
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

/* Write `n` NUL-terminated strings: index 0 is the language's self-name, the
 * rest are filler, mirroring how the real blob is laid out. */
static void plant(char *at, SIZE_T cap, const char *idx0, int n, const char *tag)
{
    SIZE_T pos = 0;
    int k = _snprintf(at, cap, "%s", idx0);
    pos += (SIZE_T)k + 1;
    for (int i = 1; i < n; i++) {
        k = _snprintf(at + pos, cap - pos, "%s_%d", tag, i);
        pos += (SIZE_T)k + 1;
    }
}

int main(void)
{
    if (!mem_guard_install()) {
        printf("FATAL: fault guard self-test failed\n");
        return 1;
    }

    const int N = gmd_test_label_count();

    /* --- case 1: the blob names its own language ------------------------- */
    SIZE_T cap = (SIZE_T)N * 24 + 64;
    char *fr = (char *)VirtualAlloc(NULL, cap, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_READWRITE);
    char *en = (char *)VirtualAlloc(NULL, cap, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_READWRITE);
    if (!fr || !en) { printf("FATAL: alloc failed\n"); return 1; }

    plant(fr, cap, "Japonais [jp]", N, "fr");
    plant(en, cap, "Japanese [jp]", N, "en");

    gmd_test_set_blob((BYTE *)fr, N);
    const char *lf = gmd_text_language();
    gmd_test_set_blob((BYTE *)en, N);
    const char *le = gmd_text_language();

    check("1. a blob reports its own language from index 0",
          lf && le && strcmp(lf, "fre") == 0 && strcmp(le, "eng") == 0,
          "french=%s english=%s", lf ? lf : "(null)", le ? le : "(null)");

    /* --- case 2: an unrecognised blob returns NULL, not a guess -----------
     * ⭐ A table we cannot identify must be reported as unknown. Returning some
     * plausible default is how the original bug served French with confidence. */
    plant(en, cap, "Zzzz [qq]", N, "xx");
    gmd_test_set_blob((BYTE *)en, N);
    const char *lu = gmd_text_language();
    check("2. an unrecognised blob returns NULL rather than guessing",
          lu == NULL, "got %s", lu ? lu : "(null)");

    /* --- case 3: the strings really are distinguishable ------------------
     * Guards the premise the whole fix rests on: if two shipped tables shared
     * an index-0 string, no amount of checking could tell them apart. */
    plant(fr, cap, "Japonais [jp]", N, "fr");
    plant(en, cap, "Japanese [jp]", N, "en");
    gmd_test_set_blob((BYTE *)fr, N);
    char a[128]; gmd_text_by_index(0, a, sizeof(a));
    gmd_test_set_blob((BYTE *)en, N);
    char b[128]; gmd_text_by_index(0, b, sizeof(b));
    check("3. the two tables' index-0 strings genuinely differ",
          strcmp(a, b) != 0, "both read %s", a);

    VirtualFree(fr, 0, MEM_RELEASE);
    VirtualFree(en, 0, MEM_RELEASE);

    printf("\n");
    if (failures) { printf("FAILED: %d case(s)\n", failures); return 1; }
    printf("all cases passed\n");
    return 0;
}
