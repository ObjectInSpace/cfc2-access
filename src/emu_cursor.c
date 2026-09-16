/* emu_cursor -- see emu_cursor.h for the method and why each guard exists. */

#include "emu_cursor.h"
#include "emu_ram.h"
#include "memsafe.h"
#include "log.h"

#include <string.h>
#include <stdlib.h>

/* Read in 256 KB chunks under one fault guard, matching emu_ram.c. Large
 * enough that the per-chunk overhead is noise, small enough that one bad page
 * costs little. */
#define EC_CHUNK   0x40000u

static ULONGLONG g_win_base;        /* test override; 0 = derive from the game */
static SIZE_T    g_win_size;
static unsigned  g_page_cap = EC_MAX_PAGES;

/* --- Phase-1 pools: one 32-bit hash per page ---------------------------- */
static unsigned *g_hash[4];         /* indexed by ec_phase                   */
static unsigned  g_hash_n;          /* pages covered                         */

/* --- Phase-2 pools: full content for pages that reacted ----------------- */
typedef struct {
    ULONGLONG addr;
    BYTE     *bytes[4];             /* content per phase                     */
} ec_page;

static ec_page  *g_pages;
static unsigned  g_pages_n;

static ec_field *g_fields;
static unsigned  g_fields_n;

static ec_phase  g_next = EC_PHASE_BASELINE;
static BOOL      g_armed;
static ec_report g_rep;

const char *ec_status_name(ec_status s)
{
    switch (s) {
    case EC_OK:          return "OK";
    case EC_NOT_RUNNING: return "NOT_RUNNING";
    case EC_UNREADABLE:  return "UNREADABLE";
    case EC_NO_MEMORY:   return "NO_MEMORY";
    case EC_NOT_ARMED:   return "NOT_ARMED";
    }
    return "?";
}

const char *ec_phase_name(ec_phase p)
{
    switch (p) {
    case EC_PHASE_BASELINE: return "BASELINE";
    case EC_PHASE_CONTROL:  return "CONTROL";
    case EC_PHASE_MOVED:    return "MOVED";
    case EC_PHASE_RETURNED: return "RETURNED";
    }
    return "?";
}

/* ⭐ FNV-1a, mixing 8 bytes at a time. Lifted from the deleted heapdiff.c
 * (commit 075a080^), which carries the reason verbatim:
 *
 * SPEED IS CORRECTNESS HERE. Byte-at-a-time FNV is a serial dependency chain,
 * and the first in-game run never finished a single capture because of it.
 *
 * ⚠⚠ AND SAMPLING IS NOT THE FIX. Hashing a subset of each page would be fast
 * but could step right over a ~4-byte index -- a false NEGATIVE, which is the
 * failure that closed three earlier routes. Every byte is still mixed; only the
 * loop is cheaper. */
unsigned ec_hash_block(const BYTE *p, SIZE_T n)
{
    unsigned long long h = 14695981039346656037ULL;
    SIZE_T i = 0;
    unsigned r;

    for (; i + 8 <= n; i += 8) {
        unsigned long long w;
        memcpy(&w, p + i, 8);       /* p need not be 8-byte aligned */
        h ^= w;
        h *= 1099511628211ULL;
    }
    /* The tail is handled bytewise. EC_PAGE is a multiple of 8 so there is no
     * remainder in practice, but this function is exposed and directly tested,
     * and a tail bug hides until the one block that needs it. */
    for (; i < n; i++) {
        h ^= (unsigned long long)p[i];
        h *= 1099511628211ULL;
    }

    r = (unsigned)(h ^ (h >> 32));
    return r ? r : 1u;              /* 0 is reserved for "no page" */
}

/* Resolve the window to sweep. */
static ec_status window(ULONGLONG *base_out, SIZE_T *size_out)
{
    ULONGLONG base = 0;
    SIZE_T size = 0;
    emu_status s;

    if (g_win_base) {
        /* ⚠ Even a planted window must be PROVEN readable. Returning EC_OK for
         * memory we cannot read would let a test -- or a live run against a
         * stale arena pointer -- report a clean zero where the honest answer is
         * "could not look". */
        if (mem_readable_run((const void *)(ULONG_PTR)g_win_base, EC_PAGE)
                < EC_PAGE)
            return EC_UNREADABLE;
        *base_out = g_win_base;
        *size_out = g_win_size;
        return EC_OK;
    }

    s = emu_arena(&base, &size);
    if (s == EMU_NOT_RUNNING) return EC_NOT_RUNNING;
    if (s != EMU_OK)          return EC_UNREADABLE;

    /* ⚠ VirtualQuery reported 511 MB for a 256 MB request -- the allocator
     * returned a pointer inside a larger committed run, so the region is NOT
     * our allocation. Bound by what the constructor actually asks for, or the
     * sweep reads a neighbour's memory and reports its changes as ours. */
    if (size > (SIZE_T)EMU_ARENA_SIZE)
        size = (SIZE_T)EMU_ARENA_SIZE;

    if (size == 0) return EC_UNREADABLE;

    *base_out = base;
    *size_out = size;
    return EC_OK;
}

static void free_pools(void)
{
    unsigned i, p;

    for (i = 0; i < 4; i++) {
        if (g_hash[i]) { free(g_hash[i]); g_hash[i] = NULL; }
    }
    if (g_pages) {
        for (i = 0; i < g_pages_n; i++)
            for (p = 0; p < 4; p++)
                if (g_pages[i].bytes[p]) free(g_pages[i].bytes[p]);
        free(g_pages);
        g_pages = NULL;
    }
    if (g_fields) { free(g_fields); g_fields = NULL; }
    g_pages_n = 0;
    g_fields_n = 0;
    g_hash_n = 0;
}

void emu_cursor_reset(void)
{
    free_pools();
    g_armed = FALSE;
    g_next = EC_PHASE_BASELINE;
    memset(&g_rep, 0, sizeof g_rep);
}

void emu_cursor_test_window(ULONGLONG base, SIZE_T size)
{
    g_win_base = base;
    g_win_size = size;
    /* ⚠ Reset too. emu_ram_test_window does the same, deliberately: residue
     * from a previous case must not truncate the next one's sweep. */
    emu_cursor_reset();
}

void emu_cursor_test_page_cap(unsigned pages)
{
    g_page_cap = pages ? pages : EC_MAX_PAGES;
}

ec_phase emu_cursor_next_phase(void) { return g_next; }

/* Hash every page of the window into g_hash[phase].
 *
 * ⛔⛔ THIS MUST COMPLETE IN ONE CALL. It is deliberately NOT budgeted and
 * resumable the way emu_ram.c's scan is: a snapshot resumed across ticks blends
 * two moments -- the game runs in between and rewrites memory -- so the diff
 * would report changes that never coexisted.
 *
 * ⭐ ONE VirtualQuery PER REGION, never per page. A per-item VirtualQuery has
 * hung this game three times; the arena is one committed region, so this runs a
 * handful of times per pass rather than 65,536. */
static ec_status hash_pass(ec_phase phase, ULONGLONG base, SIZE_T size)
{
    static BYTE buf[EC_CHUNK];
    ULONGLONG addr = base, end = base + size;
    ULONGLONG region_end = 0;
    MEMORY_BASIC_INFORMATION mbi;
    unsigned idx = 0;
    DWORD t0 = GetTickCount();
    SIZE_T hashed = 0;

    while (addr < end && idx < g_hash_n) {
        SIZE_T want = EC_CHUNK, got;
        unsigned k, npages;

        if (addr + want > end)
            want = (SIZE_T)(end - addr);

        if (addr >= region_end) {
            if (VirtualQuery((LPCVOID)(ULONG_PTR)addr, &mbi, sizeof mbi)
                    != sizeof mbi) {
                /* Step over and continue; a gap is not a failure of the pass. */
                region_end = addr + want;
                for (k = 0; k < want / EC_PAGE && idx < g_hash_n; k++)
                    g_hash[phase][idx++] = 0;
                addr += want;
                continue;
            }
            region_end = (ULONGLONG)(ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;
            /* ⚠ PAGE_GUARD is 0x100 -- never mask Protect to a byte. A guard
             * page read would fault and, worse, arm the guard for the game. */
            if (mbi.State != MEM_COMMIT ||
                (mbi.Protect & PAGE_GUARD) ||
                (mbi.Protect & PAGE_NOACCESS)) {
                ULONGLONG skip = region_end > addr ? region_end - addr : want;
                if (skip > (ULONGLONG)(end - addr))
                    skip = (ULONGLONG)(end - addr);
                for (k = 0; k < skip / EC_PAGE && idx < g_hash_n; k++)
                    g_hash[phase][idx++] = 0;
                addr += skip;
                continue;
            }
        }

        if (addr + want > region_end)
            want = (SIZE_T)(region_end - addr);

        got = mem_readable_run((const void *)(ULONG_PTR)addr, want);
        if (got < want) {
            /* Only part is readable. Hash what we can, zero the rest. */
            if (got == 0) {
                for (k = 0; k < want / EC_PAGE && idx < g_hash_n; k++)
                    g_hash[phase][idx++] = 0;
                addr += want;
                continue;
            }
            want = got - (got % EC_PAGE);
            if (want == 0) { addr += got; continue; }
        }

        /* ⚠ The fault guard is a NET, not the check -- mem_readable_run above
         * is the check. mem_copy does both and reports failure rather than
         * leaving a half-filled buffer that would hash as real data. */
        if (!mem_copy(buf, (const void *)(ULONG_PTR)addr, want)) {
            for (k = 0; k < want / EC_PAGE && idx < g_hash_n; k++)
                g_hash[phase][idx++] = 0;
            addr += want;
            continue;
        }

        npages = (unsigned)(want / EC_PAGE);
        for (k = 0; k < npages && idx < g_hash_n; k++)
            g_hash[phase][idx++] = ec_hash_block(buf + k * EC_PAGE, EC_PAGE);
        hashed += want;
        addr += want;
    }

    /* Any pages we never reached read as "no page" rather than as unchanged. */
    while (idx < g_hash_n)
        g_hash[phase][idx++] = 0;

    g_rep.last_pass_ms = GetTickCount() - t0;
    g_rep.bytes_hashed = hashed;
    return EC_OK;
}

BOOL emu_cursor_rom_running(char *name_out, unsigned cap)
{
    char name[64];
    int idx = 0;

    if (name_out && cap) name_out[0] = '\0';

    /* ⛔ The INDEX alone is not the test -- index 0 is cvs1, a real game, so
     * EMU_OK here is true on the shell too. That is precisely the bug: the
     * first live run armed on it 24 s before the ROM loaded. */
    if (emu_rom_index(&idx) != EMU_OK)
        return FALSE;

    /* The NAME is the test.
     *
     * ⭐ No explicit empty-name check: emu_rom_name() ALREADY returns
     * EMU_NOT_RUNNING for a zero-length name (emu_ram.c, `if (i == 0)`) and
     * EMU_UNVERIFIED for an unterminated one. I wrote one anyway, and sabotage
     * showed it could not be made to fail -- the function it wrapped already
     * handled the case. Removed rather than kept as false coverage, exactly
     * like the float-band guard. */
    if (emu_rom_name(name, sizeof name) != EMU_OK)
        return FALSE;

    if (name_out && cap) {
        unsigned i = 0;
        while (i + 1 < cap && name[i]) { name_out[i] = name[i]; i++; }
        name_out[i] = '\0';
    }
    return TRUE;
}

ec_status emu_cursor_arm(void)
{
    ULONGLONG base;
    SIZE_T size;
    ec_status s;
    unsigned i;

    emu_cursor_reset();

    s = window(&base, &size);
    if (s != EC_OK) return s;

    g_hash_n = (unsigned)(size / EC_PAGE);
    if (g_hash_n == 0) return EC_UNREADABLE;

    for (i = 0; i < 4; i++) {
        g_hash[i] = (unsigned *)calloc(g_hash_n, sizeof(unsigned));
        if (!g_hash[i]) { free_pools(); return EC_NO_MEMORY; }
    }
    g_pages = (ec_page *)calloc(g_page_cap, sizeof(ec_page));
    if (!g_pages) { free_pools(); return EC_NO_MEMORY; }
    g_fields = (ec_field *)calloc(EC_MAX_FIELDS, sizeof(ec_field));
    if (!g_fields) { free_pools(); return EC_NO_MEMORY; }

    g_rep.pages_total = g_hash_n;

    s = hash_pass(EC_PHASE_BASELINE, base, size);
    if (s != EC_OK) return s;

    g_armed = TRUE;
    g_next = EC_PHASE_CONTROL;
    return EC_OK;
}

/* Read one field of the given width out of a page copy. */
static unsigned read_field(const BYTE *page, unsigned off, ec_width w)
{
    unsigned v = 0;
    switch (w) {
    case EC_U8:  v = page[off]; break;
    case EC_U16: { unsigned short t; memcpy(&t, page + off, 2); v = t; break; }
    case EC_U32: { unsigned t;       memcpy(&t, page + off, 4); v = t; break; }
    }
    return v;
}

/* Capture full content for the pages that reacted: changed between S1 and S2,
 * and quiet between S0 and S1.
 *
 * ⚠ The content set is easy to get INVERTED. Storing bytes for pages that
 * changed during the CONTROL keeps precisely the noisy pages and misses the
 * quiet ones -- and a page that reacts only to the cursor is by definition
 * quiet in the control. */
static void collect_pages(ULONGLONG base)
{
    unsigned i;

    g_rep.pages_noisy = 0;
    g_rep.pages_reacted = 0;
    g_rep.pages_seen = 0;
    g_pages_n = 0;

    for (i = 0; i < g_hash_n; i++) {
        unsigned h0 = g_hash[EC_PHASE_BASELINE][i];
        unsigned h1 = g_hash[EC_PHASE_CONTROL][i];
        unsigned h2 = g_hash[EC_PHASE_MOVED][i];

        if (!h0 || !h1 || !h2)      /* not present in all three: says nothing */
            continue;

        /* ⛔⛔ PAGE-LEVEL REVERSIBILITY IS FAR TOO STRICT -- do NOT reject a
         * page merely because it changed during the control.
         *
         * A page holding a cursor index will ALSO hold something that ticks:
         * an animation counter, a frame timer, RNG state. Discarding the whole
         * page because one neighbour moved throws away the field we want.
         * heapdiff proved this by planting an index and a counter on the same
         * page: "0 returned, 176 rejected".
         *
         * The page filter's only job is to NARROW the space to pages that
         * reacted to the press. The real discrimination -- quiet in the
         * control, stepped by one, RETURNED -- is applied PER FIELD in
         * judge_fields(), which is the granularity the verdict is reported at.
         *
         * `pages_noisy` is still counted, because it is useful to know how much
         * of the window churns on its own -- but it no longer excludes. */
        if (h0 != h1) g_rep.pages_noisy++;
        if (h1 == h2) continue;                            /* did not react     */

        g_rep.pages_reacted++;
        g_rep.pages_seen++;

        /* ⚠ The cap limits what we KEEP. pages_seen keeps counting, so
         * "I stopped early" can never be read as "there were no more". */
        if (g_pages_n >= g_page_cap) { g_rep.truncated = TRUE; continue; }

        g_pages[g_pages_n].addr = base + (ULONGLONG)i * EC_PAGE;
        g_pages_n++;
    }
    g_rep.pages_kept = g_pages_n;
}

/* Snapshot the content of every kept page for `phase`. */
static void snap_pages(ec_phase phase)
{
    unsigned i, k;

    for (i = 0; i < g_pages_n; i++) {
        BYTE *dst;
        /* Allocate ALL four phase buffers together. The baseline buffer is
         * only ever written by a memcpy from S3, so allocating it lazily on
         * its own phase would leave it NULL forever and judge_fields() would
         * silently skip the page. */
        for (k = 0; k < 4; k++) {
            if (!g_pages[i].bytes[k]) {
                g_pages[i].bytes[k] = (BYTE *)malloc(EC_PAGE);
                if (!g_pages[i].bytes[k]) { g_rep.truncated = TRUE; break; }
                memset(g_pages[i].bytes[k], 0, EC_PAGE);
            }
        }
        dst = g_pages[i].bytes[phase];
        if (!dst) continue;
        /* ⚠ On failure the page is ZEROED, not left stale. A stale copy would
         * silently compare equal to the previous phase and read as "quiet". */
        if (!mem_copy(dst, (const void *)(ULONG_PTR)g_pages[i].addr, EC_PAGE))
            memset(dst, 0, EC_PAGE);
    }
}

/* ⛔ THE FLOAT-BAND TEST WAS DELETED AS UNREACHABLE, not merely unused.
 *
 * heapdiff needed it because it ranked fields with no upper bound, so animating
 * IEEE floats (the highlight sliding to the new item and back) passed
 * reversibility and dominated its candidate lists.
 *
 * Here they cannot reach it. A float in any UI range has bits around 1.1
 * billion (22.93f -> 0x41DF70A4), and guard (e) refuses anything >= 64. The
 * float test could only ever fire on a denormal near zero, whose delta would
 * not be 1 anyway.
 *
 * ⭐ Found by SABOTAGE: deleting the float test left the whole suite green,
 * which means either the test was worthless or the case was. It was the test --
 * the bounds guard had been answering for it all along. A guard that cannot be
 * made to fail is not protecting anything, and keeping it would have implied
 * coverage that did not exist. See [[sabotage-answered-by-another-guard]]. */

/* Record or update a field candidate. */
static void note_field(ULONGLONG addr, ec_width w, unsigned base_v,
                       unsigned moved_v)
{
    unsigned i;

    for (i = 0; i < g_fields_n; i++) {
        if (g_fields[i].addr == addr && g_fields[i].width == w) {
            ec_field *f = &g_fields[i];
            f->cycles++;
            f->v_base = base_v;
            f->v_moved = moved_v;
            f->delta = (int)moved_v - (int)base_v;
            if (moved_v < f->vmin) f->vmin = moved_v;
            if (moved_v > f->vmax) f->vmax = moved_v;
            if (base_v  < f->vmin) f->vmin = base_v;
            if (base_v  > f->vmax) f->vmax = base_v;
            f->distinct++;
            /* ⚠ 0<->1 toggles pass "steps by one and returns" trivially. They
             * are flags ("is highlighted", "needs redraw"), not indices, and
             * they crowd out real hits unless labelled. */
            f->is_toggle = (f->vmax <= 1);
            return;
        }
    }
    if (g_fields_n >= EC_MAX_FIELDS) { g_rep.truncated = TRUE; return; }

    g_fields[g_fields_n].addr     = addr;
    g_fields[g_fields_n].width    = w;
    g_fields[g_fields_n].v_base   = base_v;
    g_fields[g_fields_n].v_moved  = moved_v;
    g_fields[g_fields_n].delta    = (int)moved_v - (int)base_v;
    g_fields[g_fields_n].cycles   = 1;
    g_fields[g_fields_n].distinct = 2;
    g_fields[g_fields_n].vmin     = base_v < moved_v ? base_v : moved_v;
    g_fields[g_fields_n].vmax     = base_v > moved_v ? base_v : moved_v;
    g_fields[g_fields_n].is_toggle = (g_fields[g_fields_n].vmax <= 1);
    g_fields_n++;
}

/* Drop any field that did NOT survive this cycle, so `cycles` always means
 * CONSECUTIVE cycles. A field that matches once, misses, then matches again is
 * a coincidence, not a cursor. */
static void prune_fields(unsigned expect_cycles)
{
    unsigned i, keep = 0;

    for (i = 0; i < g_fields_n; i++) {
        if (g_fields[i].cycles >= expect_cycles)
            g_fields[keep++] = g_fields[i];
    }
    g_fields_n = keep;
}

/* The field-level verdict: apply the SAME control one level down.
 *
 * ⚠⚠ A page-level control does not filter the fields inside a surviving page.
 * A page qualifies because ONE field tracks the cursor; the other forty fields
 * on it are per-frame churn and would be reported as if equally interesting. */
static void judge_fields(void)
{
    static const ec_width widths[3] = { EC_U8, EC_U16, EC_U32 };
    unsigned i, off, wi;

    g_rep.fields_examined = 0;
    g_rep.fields_returned = 0;
    g_rep.fields_counter  = 0;
    g_rep.fields_toggle   = 0;

    for (i = 0; i < g_pages_n; i++) {
        const BYTE *p0 = g_pages[i].bytes[EC_PHASE_BASELINE];
        const BYTE *p1 = g_pages[i].bytes[EC_PHASE_CONTROL];
        const BYTE *p2 = g_pages[i].bytes[EC_PHASE_MOVED];
        const BYTE *p3 = g_pages[i].bytes[EC_PHASE_RETURNED];

        if (!p0 || !p1 || !p2 || !p3) continue;

        for (wi = 0; wi < 3; wi++) {
            ec_width w = widths[wi];
            for (off = 0; off + (unsigned)w <= EC_PAGE; off++) {
                unsigned v0, v1, v2, v3;
                int d;

                g_rep.fields_examined++;

                v0 = read_field(p0, off, w);
                v1 = read_field(p1, off, w);
                if (v0 != v1) continue;          /* (a) noisy in the control */

                v2 = read_field(p2, off, w);
                if (v1 == v2) continue;          /* (b) did not react        */

                v3 = read_field(p3, off, w);
                if (v3 != v1) {                  /* (c) did NOT return       */
                    g_rep.fields_counter++;
                    continue;
                }
                g_rep.fields_returned++;

                d = (int)v2 - (int)v1;
                if (d != 1 && d != -1) continue; /* (d) stepped by one       */

                /* (e) bounded. ⭐ This also subsumes the float rejection an
                 * earlier draft had: an animating IEEE float's BITS are ~1.1
                 * billion, so it never survives to be tested for floatness. */
                if (v1 >= EC_MAX_VALUE || v2 >= EC_MAX_VALUE)
                    continue;

                if (v1 <= 1 && v2 <= 1) {
                    g_rep.fields_toggle++;
                    /* Still recorded, but flagged -- a toggle that keeps
                     * passing is worth seeing, just never ranked as an index. */
                }
                note_field(g_pages[i].addr + off, w, v1, v2);
            }
        }
    }
}

ec_status emu_cursor_step(ec_phase p)
{
    ULONGLONG base;
    SIZE_T size;
    ec_status s;

    if (!g_armed && p != EC_PHASE_BASELINE)
        return EC_NOT_ARMED;
    if (p != g_next)
        return EC_NOT_ARMED;

    s = window(&base, &size);
    if (s != EC_OK) return s;

    switch (p) {
    case EC_PHASE_CONTROL:
        s = hash_pass(EC_PHASE_CONTROL, base, size);
        if (s != EC_OK) return s;
        /* ⚠ If pages are already known (any cycle after the first), their S1
         * content must be captured NOW, while the control is current. */
        snap_pages(EC_PHASE_CONTROL);
        g_next = EC_PHASE_MOVED;
        return EC_OK;

    case EC_PHASE_MOVED:
        s = hash_pass(EC_PHASE_MOVED, base, size);
        if (s != EC_OK) return s;
        /* ⛔⛔ THE PAGE SET IS IDENTIFIED HERE, ONE CYCLE BEFORE IT CAN BE
         * JUDGED. The hashes for S0/S1/S2 exist, but the BYTES do not: we did
         * not know which pages to keep until now. Capturing S0/S1 content
         * retroactively is impossible -- those moments are gone.
         *
         * So cycle 1 only LEARNS the page set; cycle 2 onward has content at
         * every phase and can judge. The pages that react to a cursor move are
         * the same pages every time, which is what makes this sound.
         *
         * ⚠ An earlier draft captured content only from S2 and silently
         * compared against absent S0/S1 buffers -- judge_fields() then examined
         * ZERO fields while every other count looked healthy. */
        collect_pages(base);
        snap_pages(EC_PHASE_MOVED);
        g_next = EC_PHASE_RETURNED;
        return EC_OK;

    case EC_PHASE_RETURNED:
        s = hash_pass(EC_PHASE_RETURNED, base, size);
        if (s != EC_OK) return s;
        snap_pages(EC_PHASE_RETURNED);
        judge_fields();
        g_rep.cycles_done++;
        /* ⚠ Only prune once judging has actually had content to work with.
         * Cycle 1 has no S0/S1 bytes, so nothing can survive it and pruning
         * against it would clear the table every time. */
        if (g_rep.cycles_done >= 2)
            prune_fields(g_rep.cycles_done - 1);
        g_rep.candidates = g_fields_n;
        /* The next cycle's baseline IS this cycle's returned state -- the
         * cursor is back where it started, so S3 and the next S0 are the same
         * moment. Copying avoids a redundant pass over the pages. */
        {
            unsigned i;
            for (i = 0; i < g_pages_n; i++) {
                if (g_pages[i].bytes[EC_PHASE_BASELINE] &&
                    g_pages[i].bytes[EC_PHASE_RETURNED])
                    memcpy(g_pages[i].bytes[EC_PHASE_BASELINE],
                           g_pages[i].bytes[EC_PHASE_RETURNED], EC_PAGE);
            }
        }
        g_next = EC_PHASE_CONTROL;
        return EC_OK;

    case EC_PHASE_BASELINE:
    default:
        return emu_cursor_arm();
    }
}

static int cmp_field(const void *a, const void *b)
{
    const ec_field *x = (const ec_field *)a, *y = (const ec_field *)b;

    /* Toggles last, whatever else they score. */
    if (x->is_toggle != y->is_toggle) return x->is_toggle ? 1 : -1;
    if (x->cycles    != y->cycles)    return (int)y->cycles - (int)x->cycles;
    if (x->vmax      != y->vmax)      return (int)y->vmax - (int)x->vmax;
    return 0;
}

unsigned emu_cursor_candidates(ec_field *out, unsigned cap)
{
    unsigned n = g_fields_n < cap ? g_fields_n : cap;

    if (!out || cap == 0 || g_fields_n == 0) return 0;
    qsort(g_fields, g_fields_n, sizeof(ec_field), cmp_field);
    memcpy(out, g_fields, n * sizeof(ec_field));
    return n;
}

void emu_cursor_status(ec_report *out)
{
    if (out) *out = g_rep;
}
