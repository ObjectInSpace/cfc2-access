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

    VirtualFree(mem, 0, MEM_RELEASE);

    /* ================= LIVE_TABLE, with a MOVED string blob =================
     *
     * The FILE_IMAGE case above is not the one that runs in the game, and it
     * was the only case covered -- so the LIVE_TABLE path had no offline
     * coverage at all. It broke in exactly the way an untested path does:
     * the string-blob delta was hardcoded to 0x140000, the runtime put the
     * blob at 0x170000 on a later run, and gmd_find reported
     * text_verified=no on a perfectly good table while its own fallback
     * search printed the correct string one line later.
     *
     * Planting at a DELIBERATELY DIFFERENT delta is the whole point: a test
     * that plants at the hardcoded offset passes no matter how rigid the
     * code is. */
    printf("\n==== LIVE_TABLE with a moved string blob ====\n");
    {
        const SIZE_T MOVED = 0x1B0000;   /* not in g_strings_hints */
        const SIZE_T span  = MOVED + 0x40000;

        BYTE *base = (BYTE *)VirtualAlloc(NULL, span,
                                          MEM_COMMIT | MEM_RESERVE,
                                          PAGE_READWRITE);
        if (!base) { fprintf(stderr, "alloc failed\n"); return 2; }

        /* Entry array: 0x20-byte entries, absolute label pointers, pad
         * 0xCDCDCDCD -- the live form. Labels and strings at our own deltas. */
        BYTE *labels  = base + 0x30000;
        BYTE *strings = base + MOVED;
        SIZE_T lpos = 0, spos = 0;

        int nent = 0;
        for (int i = 0; i < GMD_NPROBES; i++)
            if (g_gmd_probes[i].index + 1 > nent) nent = g_gmd_probes[i].index + 1;

        /* Strings in section order; entry N's text is the Nth string. */
        for (int i = 0; i < nent; i++) {
            const char *txt = "filler";
            for (int v = 0; v < GMD_NVERIFY; v++)
                if (g_gmd_verify[v].index == i) txt = g_gmd_verify[v].text[0];
            size_t n = strlen(txt) + 1;
            memcpy(strings + spos, txt, n);
            spos += n;
        }

        for (int i = 0; i < GMD_NPROBES; i++) {
            const gmd_probe *p = &g_gmd_probes[i];
            BYTE *e = base + (SIZE_T)0x20 * p->index;
            size_t n = strlen(p->label) + 1;
            memcpy(labels + lpos, p->label, n);
            *(int *)(e + 0x00)      = p->index;          /* sectionId */
            *(unsigned *)(e + 0x04) = p->hash1;
            *(unsigned *)(e + 0x08) = p->hash2;
            *(unsigned *)(e + 0x0c) = 0xCDCDCDCDu;       /* pad */
            *(UINT64 *)(e + 0x10)   = (UINT64)(uintptr_t)(labels + lpos);
            *(UINT64 *)(e + 0x18)   = 0;
            lpos += n;
        }

        printf("planted live table at %p, strings at +0x%llx "
               "(deliberately NOT a known delta)\n",
               (void *)base, (unsigned long long)MOVED);
        fflush(stdout);

        gmd_result r3;
        BOOL ok3 = gmd_find(&r3);
        printf("\n---- results ----\n");
        struct { const char *name; int cond; } lc[] = {
            { "gmd_find returned success",       ok3 },
            { "classified as LIVE_TABLE",
              r3.form == GMD_FORM_LIVE_TABLE },
            { "label chain verified",            r3.label_verified },
            { "text verified DESPITE moved blob", r3.text_verified },
        };
        for (unsigned i = 0; i < sizeof(lc)/sizeof(lc[0]); i++) {
            printf("  %-36s %s\n", lc[i].name, lc[i].cond ? "PASS" : "FAIL");
            if (!lc[i].cond) fails++;
        }
        if (r3.sample_text[0]) printf("  text:  \"%s\"\n", r3.sample_text);

        memset(base, 0, span);
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
