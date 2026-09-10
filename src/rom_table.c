/*
 * Reading the shell's ROM information table -- see rom_table.h.
 */

#include "rom_table.h"
#include "memsafe.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

/* Read a NUL-terminated string through a guarded copy.
 *
 * ⚠ One guarded BLOCK read, not one guarded read per byte. A per-byte reader
 * shipped once in this project and cost 172 ms on a sweep that should have
 * taken 4 -- the benchmark missed it because its synthetic strings were 6
 * bytes long and the real ones were not. */
static BOOL read_cstr(ULONGLONG addr, char *out, unsigned cap)
{
    if (!out || cap == 0)
        return FALSE;
    out[0] = '\0';
    if (!addr)
        return FALSE;

    char buf[256];
    unsigned want = (cap < sizeof(buf)) ? cap : (unsigned)sizeof(buf);

    /* ⭐ Only read as far as the page actually allows: a string near the end
     * of a committed region must not drag the read into unmapped memory. */
    SIZE_T run = mem_readable_run((const void *)(ULONG_PTR)addr, want);
    if (run == 0)
        return FALSE;
    if (run < want)
        want = (unsigned)run;

    if (!mem_copy(buf, (const void *)(ULONG_PTR)addr, want))
        return FALSE;

    /* ⚠ The block may not contain a terminator; refuse rather than return a
     * truncated string that looks complete. */
    unsigned n = 0;
    while (n < want && buf[n] != '\0')
        n++;
    if (n >= want)
        return FALSE;
    if (n + 1 > cap)
        return FALSE;

    memcpy(out, buf, n + 1);
    return TRUE;
}

/* ⭐ TEST SEAM. The table's addresses are compile-time constants because ASLR
 * is off, which is right for the game and impossible for a test: the suite
 * cannot plant data at 0x141208b00. So the two addresses are read through
 * these, and the test redirects them at a synthetic table.
 *
 * ⚠ Redirection is TEST-ONLY and must never fire in the game: it is set only
 * by rom_table_test_redirect(), which the shipping code never calls, and the
 * defaults are the real constants. */
static ULONGLONG g_index_addr = ROM_INDEX_ADDR;
static ULONGLONG g_table_addr = ROM_TABLE_ADDR;

void rom_table_test_redirect(ULONGLONG index_addr, ULONGLONG table_addr)
{
    g_index_addr = index_addr ? index_addr : ROM_INDEX_ADDR;
    g_table_addr = table_addr ? table_addr : ROM_TABLE_ADDR;
}

static BOOL read_qword(ULONGLONG addr, ULONGLONG *out)
{
    if (!mem_readable((const void *)(ULONG_PTR)addr, sizeof(*out)))
        return FALSE;
    return mem_copy(out, (const void *)(ULONG_PTR)addr, sizeof(*out));
}

/* The address of record `i`, via the INDEX table rather than by multiplying
 * the record size.
 *
 * ⭐ Deliberate: the index table is what the game itself uses, so following it
 * survives a layout change that a computed offset would silently misread. */
static BOOL record_addr(int i, ULONGLONG *out)
{
    if (i < 0)
        return FALSE;
    ULONGLONG p = 0;
    if (!read_qword(g_index_addr + (ULONGLONG)i * 8, &p))
        return FALSE;
    if (p == 0)
        return FALSE;               /* the table's null terminator */
    *out = p;
    return TRUE;
}

static BOOL record_field(int i, unsigned off, char *out, unsigned cap)
{
    ULONGLONG rec = 0;
    if (!record_addr(i, &rec))
        return FALSE;
    ULONGLONG str = 0;
    if (!read_qword(rec + off, &str))
        return FALSE;
    return read_cstr(str, out, cap);
}

int rom_table_count(void)
{
    /* Walk to the null terminator. ⚠ Bounded: an unterminated or misread table
     * must not spin. 64 is far above the 18 entries this game ships. */
    enum { MAX = 64 };
    for (int i = 0; i < MAX; i++) {
        ULONGLONG p = 0;
        if (!read_qword(g_index_addr + (ULONGLONG)i * 8, &p))
            return 0;               /* could not read -> 0, not a partial count */
        if (p == 0)
            return i;
    }
    return 0;                       /* no terminator found -> not our table */
}

BOOL rom_table_title(int i, char *out, unsigned cap)
{
    return record_field(i, 0x08, out, cap);
}

BOOL rom_table_code(int i, char *out, unsigned cap)
{
    return record_field(i, 0x00, out, cap);
}

BOOL rom_table_region(int i, char *out, unsigned cap)
{
    return record_field(i, 0x10, out, cap);
}

int rom_table_index_for(int game, int version)
{
    /* ⛔⛔ THE HYPOTHESIS. rom = 2*game + version, because the table runs in
     * Japanese/US pairs. Consistent with the pair ordering and with the
     * game_00..game_70 archive numbering -- and CONFIRMED BY NEITHER. Nothing
     * speaks a title derived from this until a run shows it matching the game
     * the user is actually standing on. */
    if (game < 0 || version < 0 || version > 1)
        return -1;
    int idx = 2 * game + version;
    int n = rom_table_count();
    if (n <= 0 || idx >= n)
        return -1;
    return idx;
}

/* Case-insensitive ASCII compare. ⚠ NOT stricmp/_stricmp: those are
 * locale-sensitive, and this project already shipped a locale bug (the Turkish
 * dotless-i class of problem is exactly what a locale-aware tolower() invites
 * on a string like "Fighting"). The titles are ASCII; compare them as ASCII. */
static BOOL ascii_ieq(const char *a, const char *b)
{
    if (!a || !b)
        return FALSE;
    for (;; a++, b++) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb)
            return FALSE;
        if (ca == '\0')
            return TRUE;
    }
}

BOOL rom_table_has_title(const char *s)
{
    if (!s || !*s)
        return FALSE;
    /* ⚠ Identity first. Without rom_table_ready() this would compare against
     * whatever happens to sit at those addresses, and a chance match would be
     * indistinguishable from a real one. */
    if (!rom_table_ready())
        return FALSE;

    int n = rom_table_count();
    for (int i = 0; i < n; i++) {
        char t[128];
        if (!rom_table_title(i, t, sizeof(t)) || !t[0])
            continue;
        /* ⭐ Case-insensitive because the shell displays BOTH casings for the
         * same game -- the 17:32 log contains "Capcom Vs. SNK" and "Capcom vs.
         * SNK" in the same run. An exact compare would name a game on one
         * screen and refuse it on another, which is worse than either. */
        if (ascii_ieq(s, t))
            return TRUE;
    }
    return FALSE;
}

BOOL rom_table_ready(void)
{
    /* ⭐⭐ A POSITIVE IDENTITY CHECK, not merely "is this address readable".
     * A readable address proves nothing -- the whole image is readable. Ask
     * the table to identify itself: record 0 must be "cvs1", and the index
     * table's first entry must point at the record array's first record.
     *
     * ⚠ Same lesson as the GMD language check, which passed a stale FRENCH
     * table because it only verified structure and never asked WHICH table it
     * had. Structure is not identity. */
    ULONGLONG first = 0;
    if (!read_qword(g_index_addr, &first))
        return FALSE;
    if (first != g_table_addr)
        return FALSE;

    char code[16];
    if (!rom_table_code(0, code, sizeof(code)))
        return FALSE;
    if (strcmp(code, "cvs1") != 0)
        return FALSE;

    /* And the count must be the 18 this build ships. A different count means a
     * game update changed the roster, and every index below is then suspect. */
    return rom_table_count() == 18;
}
