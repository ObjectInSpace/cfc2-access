/*
 * Offline self-test for the ROM information table reader.
 *
 * ⭐⭐ WHAT THIS IS PROTECTING
 * The game titles are read out of the exe's own data at a fixed address. Two
 * things can go wrong and only one of them is obvious:
 *   - the read fails (obvious: no title);
 *   - the read SUCCEEDS on the wrong data (not obvious at all: a plausible
 *     string, spoken confidently, indistinguishable from the right one).
 * Every case below exists for the second kind.
 *
 * ⚠ The table is planted synthetically, because the real one lives at
 * 0x141208b00 inside a running game. What is tested is the PARSING and the
 * IDENTITY CHECK, not the real addresses -- those are verified in-game by
 * rom_table_ready(), which is why it exists as a positive check rather than a
 * readability test.
 */

#include "../src/rom_table.h"
#include "../src/memsafe.h"

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

/* A synthetic table with the same shape as the game's: an array of 40-byte
 * records, and a null-terminated index of pointers to them. */
#define NREC 18
static char  g_strings[NREC][4][64];
static char  g_records[NREC][ROM_RECORD_SIZE];
static void *g_index[NREC + 1];

static const char *k_codes[NREC] = {
    "cvs1","cvs1u","cvs2","cvs2u","cfj","cfe","z3u","a3u","pws1",
    "pws1u","pws2","pws2u","mjus","pjus","stg2","pswd","z3uu","a3uu"
};

static void build_table(int nrec)
{
    memset(g_index, 0, sizeof(g_index));
    for (int i = 0; i < nrec; i++) {
        _snprintf(g_strings[i][0], 64, "%s", k_codes[i]);
        _snprintf(g_strings[i][1], 64, "Title Of Game %d", i);
        _snprintf(g_strings[i][2], 64, "(Region %d)", i);
        g_strings[i][3][0] = '\0';
        char *rec = g_records[i];
        memset(rec, 0, ROM_RECORD_SIZE);
        *(char **)(rec + 0x00) = g_strings[i][0];
        *(char **)(rec + 0x08) = g_strings[i][1];
        *(char **)(rec + 0x10) = g_strings[i][2];
        *(char **)(rec + 0x18) = g_strings[i][3];
        g_index[i] = rec;
    }
    g_index[nrec] = NULL;
    rom_table_test_redirect((ULONGLONG)(ULONG_PTR)g_index,
                            (ULONGLONG)(ULONG_PTR)g_records[0]);
}

int main(void)
{
    if (!mem_guard_install()) {
        printf("could not arm the fault guard\n");
        return 1;
    }
    build_table(NREC);

    /* --- case 1: the count comes from the null terminator ---------------- */
    check("1. the table's length is found from its terminator",
          rom_table_count() == NREC, "count=%d", rom_table_count());

    /* --- case 2: titles and codes read correctly ------------------------- */
    {
        char t[128] = "", c[32] = "", r[64] = "";
        int ok = rom_table_title(3, t, sizeof(t)) &&
                 rom_table_code(3, c, sizeof(c)) &&
                 rom_table_region(3, r, sizeof(r));
        check("2. a record's code, title and region all read back",
              ok && !strcmp(t, "Title Of Game 3") &&
              !strcmp(c, "cvs2u") && !strcmp(r, "(Region 3)"),
              "ok=%d t=\"%s\" c=\"%s\" r=\"%s\"", ok, t, c, r);
    }

    /* --- case 3: out-of-range indices are refused ------------------------
     * ⭐ Including the terminator slot itself: reading index NREC must fail,
     * not return whatever follows the array. */
    {
        char t[128] = "x";
        int past = !rom_table_title(NREC, t, sizeof(t));
        int neg  = !rom_table_title(-1, t, sizeof(t));
        int absurd = !rom_table_title(9999, t, sizeof(t));
        check("3. indices past the terminator, negative, or absurd are refused",
              past && neg && absurd, "past=%d neg=%d absurd=%d", past, neg, absurd);
    }

    /* --- case 4: THE IDENTITY CHECK -------------------------------------
     * ⭐⭐ THE MOST IMPORTANT CASE. rom_table_ready() must verify it is looking
     * at THE ROM TABLE, not merely at readable memory. The whole image is
     * readable; readability proves nothing. Same lesson as the GMD language
     * check, which passed a stale FRENCH table because it verified structure
     * and never asked which table it had. */
    check("4. a correct table is recognised", rom_table_ready(), "");

    /* --- case 5: a table with the WRONG first code is rejected ----------- */
    {
        _snprintf(g_strings[0][0], 64, "%s", "zzzz");
        int rejected = !rom_table_ready();
        _snprintf(g_strings[0][0], 64, "%s", "cvs1");
        check("5. a table whose first entry is not cvs1 is rejected",
              rejected, "");
    }

    /* --- case 6: a table of the WRONG LENGTH is rejected -----------------
     * ⭐ A different roster means a game update, and every index derived from
     * this table is then suspect. Refusing is what stops a stale mapping from
     * naming the wrong game. */
    {
        build_table(NREC - 2);
        int rejected = !rom_table_ready();
        build_table(NREC);
        check("6. a table with a different entry count is rejected",
              rejected, "");
    }

    /* --- case 7: the index mapping, and its bounds -----------------------
     * ⚠ This is the HYPOTHESIS rom = 2*game + version. The test pins the
     * arithmetic and the bounds; it does NOT establish that the hypothesis is
     * true of the game, which only a run can do. */
    {
        int g0j = rom_table_index_for(0, 0);
        int g0u = rom_table_index_for(0, 1);
        int g7j = rom_table_index_for(7, 0);
        int g7u = rom_table_index_for(7, 1);
        int bad_ver = rom_table_index_for(0, 2);
        int bad_game = rom_table_index_for(99, 0);
        int neg = rom_table_index_for(-1, 0);
        check("7. rom=2*game+version, with out-of-range inputs refused",
              g0j == 0 && g0u == 1 && g7j == 14 && g7u == 15 &&
              bad_ver == -1 && bad_game == -1 && neg == -1,
              "g0j=%d g0u=%d g7j=%d g7u=%d bad_ver=%d bad_game=%d neg=%d",
              g0j, g0u, g7j, g7u, bad_ver, bad_game, neg);
    }

    /* --- case 8: an unreadable table yields 0, not a partial count -------
     * ⭐⭐ "Could not measure" must be distinguishable from "measured nothing".
     * A count of 0 from an unreadable address must not look like an empty
     * table that was successfully read. */
    {
        /* ⚠⚠ A table that is readable at slot 0 and UNREADABLE partway.
         *
         * This case needs a real page boundary, and getting it wrong is easy:
         * the first version pointed just past a static array and passed the
         * sabotage, because the bytes after a BSS array are still committed and
         * readable -- the reader found zeros and stopped at a legitimate
         * terminator. It was not testing what its comment claimed.
         *
         * So: commit two pages, free the second, and put the index in the tail
         * of the first. Slot 0 is then readable and the walk runs into genuinely
         * unmapped memory a few slots later -- where "return 0" and "return the
         * partial count i" finally disagree. */
        int n_partial = -1;
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char *two = (char *)VirtualAlloc(NULL, pg * 2, MEM_RESERVE | MEM_COMMIT,
                                         PAGE_READWRITE);
        if (two) {
            VirtualFree(two + pg, pg, MEM_DECOMMIT);
            /* Four valid pointers, ending 4 slots before the cliff. */
            void **edge = (void **)(two + pg - 4 * sizeof(void *));
            for (int i = 0; i < 4; i++)
                edge[i] = g_records[i];
            rom_table_test_redirect((ULONGLONG)(ULONG_PTR)edge,
                                    (ULONGLONG)(ULONG_PTR)g_records[0]);
            n_partial = rom_table_count();
            VirtualFree(two, 0, MEM_RELEASE);
        }

        rom_table_test_redirect(0x10ULL, 0x10ULL);   /* certainly unmapped */
        int n = rom_table_count();
        char t[64] = "x";
        int no_title = !rom_table_title(0, t, sizeof(t));
        int not_ready = !rom_table_ready();
        build_table(NREC);
        check("8. an unreadable table reports 0 and refuses every read",
              n == 0 && no_title && not_ready && n_partial == 0 && two != NULL,
              "n=%d n_partial=%d no_title=%d not_ready=%d",
              n, n_partial, no_title, not_ready);
    }

    printf("\n");
    if (failures) { printf("FAILED: %d case(s)\n", failures); return 1; }
    printf("all cases passed\n");
    return 0;
}
