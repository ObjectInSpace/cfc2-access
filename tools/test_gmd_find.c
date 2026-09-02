/*
 * Offline self-test for the heap scanner.
 *
 * The scanner normally only runs inside the game, where a failure costs a
 * launch and a wrong answer is hard to tell from a right one. This harness
 * allocates a REAL decompressed menu_eng image in this process's own heap and
 * requires gmd_find() to locate it, classify it as FILE_IMAGE, verify the
 * label chain, and resolve a display string.
 *
 * That is the positive control for the whole approach: if the scanner cannot
 * find a known-good table planted at a known address, its silence in the game
 * would mean nothing.
 *
 * Build+run:  see tools/run_gmd_test.ps1
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <windows.h>

#include "../src/gmd_find.h"
#include "../src/gmd_probes.h"

/* log_line is normally the mod's file logger; here just print. */
void log_init(void) {}
void log_shutdown(void) {}
void log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);   /* the scanner may die mid-scan; never lose the trail */
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <menu_eng.bin>\n", argv[0]);
        return 2;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Commit it as private memory, exactly like a heap allocation, so the
     * scanner's MEM_PRIVATE filter sees it the way it would in the game. */
    void *mem = VirtualAlloc(NULL, (SIZE_T)len, MEM_COMMIT | MEM_RESERVE,
                             PAGE_READWRITE);
    if (!mem) { fprintf(stderr, "VirtualAlloc failed\n"); return 2; }
    if (fread(mem, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "short read\n"); return 2;
    }
    fclose(f);

    unsigned char *expect_entries = (unsigned char *)mem + GMD_ENTRIES_AT;
    printf("planted %ld bytes at %p; entry array should be at %p\n\n",
           len, mem, (void *)expect_entries);
    fflush(stdout);

    gmd_result r;
    BOOL ok = gmd_find(&r);

    printf("\n---- results ----\n");
    int fails = 0;
    struct { const char *name; int cond; } checks[] = {
        { "gmd_find returned success",      ok },
        { "array_base is where we planted", r.array_base == expect_entries },
        { "classified as FILE_IMAGE",       r.form == GMD_FORM_FILE_IMAGE },
        { "found most probes",              r.probes_found >= GMD_NPROBES / 2 },
        { "geometry exact",                 r.geometry_exact },
        { "label chain verified",           r.label_verified },
        { "display text verified",          r.text_verified },
    };
    for (unsigned i = 0; i < sizeof(checks)/sizeof(checks[0]); i++) {
        printf("  %-34s %s\n", checks[i].name, checks[i].cond ? "PASS" : "FAIL");
        if (!checks[i].cond) fails++;
    }
    printf("  probes found: %d/%d\n", r.probes_found, r.probes_total);
    if (r.sample_label[0]) printf("  label: \"%s\"\n", r.sample_label);
    if (r.sample_text[0])  printf("  text:  \"%s\"\n", r.sample_text);

    /* NEGATIVE CONTROL: wipe the planted image and require the scanner to
     * report absence. Without this, a scanner that "finds" a table no matter
     * what would pass every check above. */
    printf("\n---- negative control (image wiped) ----\n");
    memset(mem, 0, (size_t)len);
    gmd_result r2;
    BOOL ok2 = gmd_find(&r2);
    printf("  %-34s %s\n", "reports absence after wipe",
           !ok2 ? "PASS" : "FAIL");
    if (ok2) fails++;

    printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
