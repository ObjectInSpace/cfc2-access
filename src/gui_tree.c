/*
 * Instantiated GUI tree walk. See gui_tree.h for why this is the only route
 * left and what it is allowed to claim.
 *
 * Read-only: VirtualQuery, then guarded reads. No writes, no hooks.
 */
#include "gui_tree.h"
#include "gui_find.h"
#include "gui_probes.h"
#include "memsafe.h"
#include "log.h"

#include <string.h>
#include <stdio.h>

#define CHUNK   (256 * 1024)

/* The pool of one layout runs to ~9 KB in the shipped files (result: 497
 * names). Measure the resident span rather than assuming it: the span bounds
 * the pointer test, and a span that is too generous would accept pointers into
 * neighbouring data as "into the pool". */
#define POOL_MAX_SPAN (64 * 1024)

/* Give up after this long and report PARTIAL results. A pass that never
 * returns is worse than one that admits it ran out of time: the log's last
 * line is then "anchoring on ...", which looks exactly like a crash. */
/* 45 s. Was 20 s, which pass B hit (919 of 2400 regions walked) once deref
 * hashing was added -- an INCOMPLETE pass, so its "nothing changed" could not
 * be trusted. The region cache below should bring a pass back under ~15 s;
 * this headroom is so a slower machine reports a real result rather than a
 * partial one. The deadline still exists: never hang, always report. */
#define GTREE_DEADLINE_MS 45000

BOOL gui_tree_points_into_pool(uintptr_t v, uintptr_t base, SIZE_T span)
{
    return v >= base && v < base + span;
}

/* Is this byte plausible pool content? The pool is printable ASCII and NULs. */
static int pool_byte(BYTE c)
{
    return (c >= 32 && c < 127) || c == 0;
}

/* Find the TRUE bounds of the pool containing `seed`.
 *
 * ⚠ THIS IS NOT OPTIONAL PRECISION -- getting it wrong silently breaks the
 * whole walk. gui_find reports `pool_base` as the 256 KB CHUNK it happened to
 * read (base + off) and `pool_span` as the bytes it read, NOT the pool's real
 * address and length. Anchoring the pointer test on those values tests
 * membership of an arbitrary chunk-aligned window, which is both far too wide
 * (adjacent float data counts as "the pool", manufacturing nodes) and, as the
 * offline harness proved, capable of not containing the real pool at all: the
 * first version measured 8192 bytes from the chunk start, the planted pointers
 * targeted the actual pool further in, and the walk found ZERO of 12 planted
 * nodes while reporting a clean complete scan.
 *
 * That failure is the exact shape of the two closed routes -- a confident
 * "complete scan, nothing found" produced by measuring the wrong thing. So the
 * bounds are grown OUTWARD from a byte known to be inside the pool.
 *
 * Returns FALSE if `seed` is not in pool-like content at all. */
static BOOL bound_pool(uintptr_t seed, uintptr_t *out_base, SIZE_T *out_span)
{
    BYTE c;
    if (!mem_copy(&c, (const void *)seed, 1) || !pool_byte(c))
        return FALSE;

    /* Walk BACK to the start, name by name.
     *
     * ⚠ The byte-class rule ("stop after 4 consecutive non-pool bytes") is as
     * wrong going backwards as forwards, and for the same reason: float data
     * sprinkled with isolated printable bytes never produces 4 bad in a row.
     * It ran 4096 bytes past the start of a planted 2315-byte pool.
     *
     * Structure instead: step back over NUL padding, then back over one name's
     * worth of printable bytes, and stop the moment the shape breaks. */
    uintptr_t lo = seed;
    while (lo > 0x10000 && seed - lo < POOL_MAX_SPAN) {
        /* Step back over NUL padding. */
        SIZE_T nuls = 0;
        while (lo > 0x10000 && seed - lo < POOL_MAX_SPAN) {
            if (!mem_copy(&c, (const void *)(lo - 1), 1))
                goto back_done;
            if (c != 0)
                break;
            lo--;
            if (++nuls > 64)
                goto back_done;     /* long NUL run: start of the pool */
        }
        /* Step back over one name. */
        SIZE_T len = 0;
        for (;;) {
            if (lo <= 0x10000 || seed - lo >= POOL_MAX_SPAN)
                goto back_done;
            if (!mem_copy(&c, (const void *)(lo - 1), 1))
                goto back_done;
            if (c == 0)
                break;              /* reached this name's leading NUL */
            if (c < 32 || c >= 127)
                goto back_done;     /* not a name: pool starts here */
            lo--;
            if (++len > 128)
                goto back_done;     /* too long to be a name */
        }
    }
back_done:

    /* ⚠ THE FORWARD BOUND NEEDS STRUCTURE, NOT A BYTE CLASS.
     *
     * "Stop after 4 consecutive non-pool bytes" is too weak going forward: the
     * float data after the pool is full of isolated printable bytes, so a
     * pattern of 3 bad + 1 printable + 3 bad never trips the threshold and the
     * bound walks far past the real end. Measured on the SAME pool across two
     * runs: span 8355 one run, 6907 the next -- the end it found depended on
     * transient neighbouring data, and the 8355 case ran into an uncommitted
     * page and aborted the whole pass.
     *
     * A pool is a run of NUL-TERMINATED NAMES. So require that shape: walk
     * name by name, and stop at the first "name" that is not one -- too long,
     * or containing a non-printable byte. Real pool names are short
     * identifiers; MTFramework's longest here are texture paths well under
     * 128. A run of 128+ printable bytes with no NUL is not a name. */
    uintptr_t hi = seed;
    while (hi - seed < POOL_MAX_SPAN) {
        /* Skip the NUL padding between names. */
        SIZE_T nuls = 0;
        while (hi - seed < POOL_MAX_SPAN) {
            if (!mem_copy(&c, (const void *)hi, 1))
                goto done;
            if (c != 0)
                break;
            hi++;
            /* A long NUL run is the end of the pool, not padding. */
            if (++nuls > 64)
                goto done;
        }
        /* Now read one name. */
        SIZE_T len = 0;
        for (;;) {
            if (hi - seed >= POOL_MAX_SPAN)
                goto done;
            if (!mem_copy(&c, (const void *)hi, 1))
                goto done;
            if (c == 0)
                break;                  /* name ended: keep it */
            if (c < 32 || c >= 127)
                goto done;              /* not a name: pool ended here */
            hi++;
            if (++len > 128)
                goto done;              /* too long to be a name */
        }
    }
done:

    if (hi <= lo)
        return FALSE;
    *out_base = lo;
    *out_span = (SIZE_T)(hi - lo);
    return TRUE;
}

/* Is a previously located pool still there?
 *
 * Guards the cross-pass cache. A cached address that has been freed or reused
 * would otherwise be scanned silently, and the walk would report node
 * candidates from whatever now occupies that memory -- a fabricated result,
 * which is worse than no result. Cheap: samples the ends and the middle rather
 * than re-validating every byte. */
static BOOL still_a_pool(uintptr_t base, SIZE_T span)
{
    if (!base || span < 64)
        return FALSE;
    /* Say WHY a seed was rejected. The seed logged the right address and span,
     * yet every pass still reported "(fresh search)" and paid a 3.8 s
     * gui_find -- with no way to tell which check refused it. */
    SIZE_T run = mem_readable_run((const void *)base, span);
    if (run < span) {
        log_line("gtree: cached pool %p rejected -- only %llu of %llu bytes "
                 "readable (the pool spans %d pages and is not page-aligned).",
                 (void *)base, (unsigned long long)run,
                 (unsigned long long)span,
                 (int)(((base + span - 1) >> 12) - (base >> 12) + 1));
        return FALSE;
    }

    SIZE_T probe[5];
    probe[0] = 0;
    probe[1] = span / 4;
    probe[2] = span / 2;
    probe[3] = (span * 3) / 4;
    probe[4] = span - 1;

    for (int i = 0; i < 5; i++) {
        BYTE c;
        if (!mem_copy(&c, (const void *)(base + probe[i]), 1)) {
            log_line("gtree: cached pool %p rejected -- probe %d (offset "
                     "%llu) unreadable.", (void *)base, i,
                     (unsigned long long)probe[i]);
            return FALSE;
        }
        if (!pool_byte(c)) {
            log_line("gtree: cached pool %p rejected -- probe %d (offset "
                     "%llu) is 0x%02x, not pool content.",
                     (void *)base, i, (unsigned long long)probe[i], c);
            return FALSE;
        }
    }
    return TRUE;
}

/* Locate the pool that gui_find identified, by finding its anchor name inside
 * the chunk and growing outward from there. */
static BOOL locate_pool(const gui_result *g, uintptr_t *base, SIZE_T *span)
{
    if (g->layout_index < 0 || g->layout_index >= GUI_NLAYOUTS)
        return FALSE;
    const gui_layout_probe *p = &g_gui_layouts[g->layout_index];
    /* volatile: live across the MEM_GUARDED longjmp below. See the note in
     * gui_tree_scan -- -Wclobbered is a real defect here, not noise. */
    const char *volatile key = p->anchor ? p->anchor : p->discrim;
    if (!key)
        return FALSE;

    /* Re-read the chunk gui_find matched in and find where the anchor actually
     * sits. That byte is inside the pool by construction. */
    static BYTE buf[CHUNK];
    SIZE_T want = g->pool_span;
    if (want > sizeof(buf)) want = sizeof(buf);
    SIZE_T got = mem_readable_run((const void *)g->pool_base, want);
    if (got < 16)
        return FALSE;

    volatile BOOL faulted;
    MEM_GUARDED(faulted, memcpy(buf, (const void *)g->pool_base, got));
    if (faulted)
        return FALSE;

    SIZE_T at = gui_find_delimited(buf, got, key);
    if (at == (SIZE_T)-1)
        return FALSE;

    return bound_pool((uintptr_t)g->pool_base + at, base, span);
}

/* The located pool, cached across passes. File scope so it can be SEEDED from
 * an inventory scan the mod already ran -- see gui_tree_seed(). */
static uintptr_t  cached_pool;
static SIZE_T     cached_span;
static const char *cached_layout;

/* Seed the pool cache from an inventory scan that has ALREADY run.
 *
 * ⭐ Why: gui_find walks 1.6 GB and took ~15 s in this build. The mod already
 * runs it once at beat 4 for the layout inventory, and the tree walk was then
 * repeating the identical work on the first F5 press -- 15 s during which the
 * log said only "triggered by F5", which is indistinguishable from a stall.
 *
 * Sound for the same reason the cross-pass cache is: all 25 layouts stay
 * resident permanently, so a pool located at beat 4 is still there later. The
 * address is re-validated on use, so a stale seed costs a fresh search rather
 * than a wrong answer. */
void gui_tree_seed(const gui_result *g)
{
    uintptr_t base;
    SIZE_T span;
    if (!g || !g->pool_base)
        return;
    if (!locate_pool(g, &base, &span))
        return;
    cached_pool = base;
    cached_span = span;
    cached_layout = g->layout_name;
    log_line("gtree: pool cache seeded from the inventory scan -- layout %s, "
             "%p span %llu bytes. The first capture now skips the ~15 s "
             "layout search.",
             g->layout_name ? g->layout_name : "(unidentified)",
             (void *)base, (unsigned long long)span);
}

/* A local copy of the pool, taken once per pass.
 *
 * ⚠⚠ THIS IS WHY THE SECOND IN-GAME RUN ALSO HUNG. Validating each hit by
 * reading the game's memory cost 2-3 VirtualQuery syscalls, and -- see the cap
 * bug fixed at the hit site -- the number of hits is NOT bounded by the node
 * cap, because a REJECTED hit never advances it. Millions of rejected hits at
 * 3 syscalls each is unbounded work, and the pass never finished.
 *
 * The pool is a few KB and we already know exactly where it is. Copy it ONCE,
 * then every name validation is pure local memory: zero syscalls per hit,
 * however many hits there are. */
static BYTE  g_pool[POOL_MAX_SPAN];
static SIZE_T g_pool_len;

/* Snapshot the pool. Returns FALSE if it could not be read whole. */
static BOOL snapshot_pool(uintptr_t base, SIZE_T span)
{
    if (span == 0)
        return FALSE;
    if (span > sizeof(g_pool))
        span = sizeof(g_pool);

    /* ⚠ TRUNCATE, do not abort. Requiring the WHOLE span to be readable threw
     * away an entire pass when the span overshot into an uncommitted page --
     * and the span is measured, so it can overshoot. A partial pool is still a
     * usable pool: every name inside the readable part validates normally, and
     * the pointer test is narrowed to what was actually captured, which is
     * conservative rather than wrong. */
    SIZE_T got = mem_readable_run((const void *)base, span);
    if (got < 64)
        return FALSE;

    volatile BOOL faulted;
    MEM_GUARDED(faulted, memcpy(g_pool, (const void *)base, got));
    if (faulted)
        return FALSE;
    g_pool_len = got;
    if (got < span)
        log_line("gtree: pool snapshot truncated to %llu of %llu bytes (the "
                 "measured span ran into unreadable memory); the pointer test "
                 "is narrowed to the captured part.",
                 (unsigned long long)got, (unsigned long long)span);
    return TRUE;
}

/* Read the NUL-terminated name at `p`, which is known to sit inside the pool.
 * Returns FALSE if it is not a plausible name.
 *
 * Reads from the SNAPSHOT, never from the game -- no syscalls, so this stays
 * cheap no matter how many candidate hits the scan produces. */
static BOOL read_pool_name(uintptr_t p, uintptr_t base, SIZE_T span,
                           char *out, int cap, int *trunc)
{
    (void)span;
    if (p < base)
        return FALSE;
    SIZE_T off = p - base;
    if (off >= g_pool_len)
        return FALSE;

    /* A pooled name starts right after a NUL. This is the same discipline
     * gui_find uses: it separates a real pool entry from a pointer into the
     * MIDDLE of a longer string, which would yield a plausible but wrong name
     * (exactly the "icon_status_btl_BM_NOMIP inside a texture path" trap). */
    if (off > 0 && g_pool[off - 1] != 0)
        return FALSE;

    int n = 0;
    while (n < cap - 1 && off + (SIZE_T)n < g_pool_len) {
        BYTE c = g_pool[off + n];
        if (c == 0)
            break;
        if (c < 32 || c >= 127)
            return FALSE;
        out[n] = (char)c;
        n++;
    }
    if (n < 2)
        return FALSE;
    *trunc = (n == cap - 1);
    out[n] = '\0';
    return TRUE;
}

/* Does this region deserve a look? Same rules as gui_find. */
static BOOL want_region(const MEMORY_BASIC_INFORMATION *mbi)
{
    if (mbi->State != MEM_COMMIT)
        return FALSE;
    /* PAGE_GUARD is 0x100 -- never mask Protect to a byte, that clears it and
     * makes this test dead code (learned the hard way in gmd_find). */
    if (mbi->Protect & (PAGE_NOACCESS | PAGE_GUARD))
        return FALSE;
    DWORD w = mbi->Protect & (PAGE_READWRITE | PAGE_READONLY |
                              PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                              PAGE_EXECUTE_READWRITE);
    if (!w)
        return FALSE;
    return mbi->Type == MEM_PRIVATE || mbi->Type == MEM_MAPPED;
}

/* Hash the memory a pointer word points AT -- one hop out from the node.
 *
 * Returns 0 when `v` is not a followable pointer, which is the common case and
 * must stay cheap.
 *
 * ⚠ COST DISCIPLINE. This adds a readability check per pointer word, and an
 * uncapped per-word syscall is exactly what stalled two earlier in-game runs.
 * It is affordable now only because it runs on KEPT nodes (bounded by
 * GTREE_MAX_NODES), never on the millions of rejected candidate hits. The
 * obvious-nonsense cases are rejected by arithmetic before any syscall. */
/* Remember recently-seen readable regions.
 *
 * ⚠ Added because the first in-game run WITH deref hashing hit the 20 s
 * deadline: ~1789 kept nodes x up to 33 pointer words is ~59k VirtualQuery
 * syscalls on top of the 1.6 GB walk, and pass B stopped early (919 of 2400
 * regions) leaving the result incomplete.
 *
 * Node targets cluster hard in one arena -- every site observed lives within
 * about a megabyte of the pool -- so a handful of remembered regions absorbs
 * nearly all lookups. Correctness is unaffected: this only SKIPS the
 * VirtualQuery, and the read itself is still inside the fault guard, which is
 * the real protection (VirtualQuery is TOCTOU anyway). */
#define DEREF_CACHE 16
static struct { uintptr_t base, end; } g_rcache[DEREF_CACHE];
static int g_rcache_next;

static BOOL region_known_readable(uintptr_t v, SIZE_T want)
{
    for (int i = 0; i < DEREF_CACHE; i++)
        if (g_rcache[i].end && v >= g_rcache[i].base &&
            v + want <= g_rcache[i].end)
            return TRUE;
    return FALSE;
}

static void remember_region(uintptr_t v)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)v, &mbi, sizeof(mbi)))
        return;
    if (mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return;
    g_rcache[g_rcache_next].base = (uintptr_t)mbi.BaseAddress;
    g_rcache[g_rcache_next].end  = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    g_rcache_next = (g_rcache_next + 1) % DEREF_CACHE;
}

static unsigned deref_hash_of(uintptr_t v)
{
    /* Cheap rejects first: null, tiny, unaligned, or a non-canonical/kernel
     * address. Pointers to heap objects are at least 8-byte aligned. */
    if (v < 0x10000 || (v & 7) != 0 || v >= 0x7FFFFFFFFFFFULL)
        return 0;

    BYTE buf[64];
    SIZE_T got;
    if (region_known_readable(v, sizeof(buf))) {
        got = sizeof(buf);           /* no syscall: the region is known */
    } else {
        got = mem_readable_run((const void *)v, sizeof(buf));
        if (got >= 16)
            remember_region(v);
    }
    if (got < 16)
        return 0;

    volatile BOOL faulted;
    MEM_GUARDED(faulted, memcpy(buf, (const void *)v, got));
    if (faulted)
        return 0;

    /* FNV-1a over the target. A hash, not the bytes: 3000+ nodes x 33 words of
     * raw dump would be unreadable and enormous, while a hash still changes
     * whenever the target does -- which is the only question being asked. */
    /* volatile: live across the MEM_GUARDED longjmp above. -Wclobbered has
     * been a real defect three times in this project, never noise. */
    unsigned h = 2166136261u;
    for (volatile SIZE_T i = 0; i < got; i++) {
        h ^= buf[i];
        h *= 16777619u;
    }
    /* Never return 0 for real content: 0 means "not followable". */
    return h ? h : 1u;
}

/* Fill in the window of words around a pointer site.
 *
 * ⚠ SPEED IS CORRECTNESS HERE, not polish. The first in-game run never
 * finished a single pass: the log stops after "anchoring on layout ..." with
 * no crash recorded in the Windows event log. Two costs, both syscall-bound:
 *
 *  - `mem_copy` per word calls `mem_readable_run` -> VirtualQuery. 33 words
 *    meant 33 syscalls for a window that is one contiguous 264-byte read.
 *  - the "is this a heap pointer" classification called `mem_readable` AGAIN
 *    per non-pool word: another 33 syscalls per node.
 *
 * ~66 syscalls per node, and the node count is unbounded until the cap. The
 * classification was never evidence of anything -- it is a readability tally,
 * and the RAW WORDS are the measurement. Dropped.
 *
 * Now: one readability check, one guarded bulk copy. */
static void capture_window(gtree_node *N, uintptr_t site,
                           uintptr_t pool, SIZE_T span)
{
    uintptr_t start = site - (uintptr_t)GTREE_BACK * 8;
    if (start < 0x10000)
        return;                      /* leaves every word_ok at 0 */

    BYTE tmp[GTREE_WINDOW * 8];
    SIZE_T got = mem_readable_run((const void *)start, sizeof(tmp));
    got &= ~(SIZE_T)7;               /* whole words only */
    if (got == 0)
        return;

    volatile BOOL faulted;
    MEM_GUARDED(faulted, memcpy(tmp, (const void *)start, got));
    if (faulted)
        return;

    int nwords = (int)(got / 8);
    for (int i = 0; i < nwords && i < GTREE_WINDOW; i++) {
        uintptr_t v;
        memcpy(&v, tmp + (SIZE_T)i * 8, 8);
        N->word_ok[i] = 1;
        N->word[i] = v;
        if (gui_tree_points_into_pool(v, pool, span))
            N->nptr_pool++;
        else
            N->deref_hash[i] = deref_hash_of(v);
    }
}

BOOL gui_tree_scan(gtree_result *out, const char *tag)
{
    memset(out, 0, sizeof(*out));

    if (!mem_guard_install()) {
        log_line("gtree[%s]: fault guard self-test FAILED -- not scanning.",
                 tag);
        return FALSE;
    }

    /* Step 1: find a resident pool to anchor on. gui_find cannot say which
     * screen is displayed (that is settled and closed), but locating a pool is
     * exactly what it IS still good for.
     *
     * ⚠ CACHED ACROSS PASSES, and that is a correctness matter as much as a
     * speed one. gui_find walks the full 1.6 GB and takes ~6 s in-game; doing
     * it per pass meant every pass paid ~12 s (gui_find, then this walk) and
     * the first in-game run never completed one.
     *
     * Caching is sound here precisely BECAUSE of the finding that closed
     * route 2: all 25 layouts are resident simultaneously and permanently, so
     * the pool's address does not change as the player navigates. The whole
     * point of this walk is that the POOL is constant while the words around
     * pointers into it are not.
     *
     * The cached address is re-validated below, so a freed or moved pool
     * re-runs the search rather than silently scanning stale memory. */
    uintptr_t pool = 0;
    SIZE_T span = 0;
    BOOL from_cache = FALSE;

    if (cached_pool && still_a_pool(cached_pool, cached_span)) {
        from_cache = TRUE;
        pool = cached_pool;
        span = cached_span;
        out->layout = cached_layout;
    } else {
        gui_result g;
        if (!gui_find(&g) || !g.pool_base) {
            log_line("gtree[%s]: no layout pool located -- nothing to anchor "
                     "on.", tag);
            return FALSE;
        }
        if (!locate_pool(&g, &pool, &span)) {
            log_line("gtree[%s]: gui_find reported a pool in the chunk at %p, "
                     "but its true bounds could not be established -- not "
                     "scanning. (Anchoring on the chunk address instead would "
                     "test the wrong range and report a confident, false "
                     "'nothing found'.)", tag, (void *)g.pool_base);
            return FALSE;
        }
        cached_pool = pool;
        cached_span = span;
        cached_layout = g.layout_name;
        out->layout = g.layout_name;
    }
    out->pool_base = pool;
    out->pool_span = span;

    if (span < 64) {
        log_line("gtree[%s]: pool at %p measured only %llu bytes -- too short "
                 "to anchor on; not scanning.", tag, (void *)pool,
                 (unsigned long long)span);
        return FALSE;
    }

    /* Snapshot the pool ONCE, so per-hit name validation costs no syscalls.
     * Without this the pass does not finish -- see the note on g_pool. */
    if (!snapshot_pool(pool, span)) {
        log_line("gtree[%s]: could not snapshot the pool at %p (%llu bytes) "
                 "-- not scanning. Validating hits against the game's memory "
                 "instead would cost syscalls per hit and never finish.",
                 tag, (void *)pool, (unsigned long long)span);
        cached_pool = 0;             /* force a fresh search next pass */
        return FALSE;
    }

    /* Keep the span and the snapshot in step. snapshot_pool may truncate, and
     * a span wider than what was captured would let the pointer test accept
     * addresses that read_pool_name can never validate -- inflating the hit
     * count with candidates that are all discarded. */
    if (g_pool_len < span) {
        span = g_pool_len;
        out->pool_span = span;
        cached_span = span;
    }

    log_line("gtree[%s]: anchoring on layout %s, pool %p span %llu bytes%s; "
             "pool snapshotted. Looking for POINTERS into it -- the file has "
             "no pointers, so a pointer here can only have been written by "
             "the runtime.",
             tag, out->layout ? out->layout : "(unidentified)",
             (void *)pool, (unsigned long long)span,
             from_cache ? " (cached)" : " (fresh search)");

    /* Clear the deref region cache each pass. Regions can be freed between
     * passes, and a stale entry would skip the readability check on memory
     * that is gone. The fault guard would still catch the read, but a scan
     * that relies on the guard for ordinary control flow is one absorbed fault
     * away from looking like a working experiment -- this project has been
     * bitten by exactly that. */
    memset(g_rcache, 0, sizeof(g_rcache));
    g_rcache_next = 0;

    LONG faults0 = mem_guard_fault_count();
    DWORD t0 = GetTickCount();

    /* Exclude our own stack: reading a stack guard page raises
     * STATUS_GUARD_PAGE_VIOLATION and longjmp-ing out of it leaves the guard
     * unreset and the thread broken. Excluding beats catching. */
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    uintptr_t stack_lo = (uintptr_t)tib->StackLimit;
    uintptr_t stack_hi = (uintptr_t)tib->StackBase;

    static BYTE buf[CHUNK];
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t maxa = (uintptr_t)si.lpMaximumApplicationAddress;

    BOOL timed_out = FALSE;

    while (addr < maxa) {
        /* A wall-clock deadline. Two in-game runs stalled with the log's last
         * line being "anchoring on ...", which is indistinguishable from a
         * crash until the Windows event log is checked. A scan that runs long
         * must SAY SO and return partial results rather than hang the worker
         * thread. Checked per region, which is frequent enough to be prompt
         * and far too cheap to matter. */
        if (GetTickCount() - t0 > GTREE_DEADLINE_MS) {
            timed_out = TRUE;
            break;
        }

        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)))
            break;
        out->regions_walked++;
        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        SIZE_T rsize = mbi.RegionSize;
        uintptr_t next = base + rsize;
        if (next <= addr)
            break;                      /* no forward progress: stop */

        if (!want_region(&mbi) || (base < stack_hi && next > stack_lo)) {
            addr = next;
            continue;
        }
        out->regions_scanned++;

        /* volatile: live across the MEM_GUARDED longjmp. A stale register copy
         * restored by longjmp could loop forever -- -Wclobbered has been a real
         * defect twice in this project, never noise. */
        for (volatile SIZE_T off = 0; off < rsize; off += CHUNK) {
            SIZE_T want = rsize - off;
            if (want > CHUNK) want = CHUNK;

            SIZE_T got = mem_readable_run((const void *)(base + off), want);
            if (got < 8)
                continue;

            volatile BOOL faulted;
            MEM_GUARDED(faulted, memcpy(buf, (const void *)(base + off), got));
            if (faulted)
                continue;
            out->bytes_scanned += got;

            /* Pointers are 8-byte aligned in practice; scanning at 8 keeps the
             * pass fast enough to answer while the player is still on the same
             * screen. (Byte stride was mandatory in gmd_find because the GMD
             * entry array starts at file offset 0x2d and its fields are
             * misaligned by construction. A heap-allocated object's pointer
             * fields are not: the allocator returns 16-byte-aligned blocks.
             * Different structure, different correct stride.) */
            for (SIZE_T i = 0; i + 8 <= got; i += 8) {
                uintptr_t v;
                memcpy(&v, buf + i, 8);
                if (!gui_tree_points_into_pool(v, pool, span))
                    continue;

                uintptr_t site = base + off + i;
                /* Skip pointers found INSIDE the pool itself. */
                if (gui_tree_points_into_pool(site, pool, span))
                    continue;

                out->nnodes_seen++;

                /* ⚠ CAP ON HITS SEEN, NOT ON NODES KEPT.
                 *
                 * This was the second in-game hang. The cap used to test
                 * `nnodes` (nodes KEPT), but a hit rejected by read_pool_name
                 * never increments that -- so the cap never engaged and every
                 * one of potentially millions of rejected hits did full
                 * validation work. Bound the loop by what it actually
                 * ENCOUNTERS. */
                if (out->nnodes_seen > GTREE_MAX_SEEN ||
                    out->nnodes >= GTREE_MAX_NODES)
                    continue;

                gtree_node *N = &out->node[out->nnodes];
                memset(N, 0, sizeof(*N));
                N->site = site;
                N->target = v;
                if (!read_pool_name(v, pool, span, N->name,
                                    (int)sizeof(N->name), &N->name_trunc))
                    continue;       /* not a real pool entry: discard */

                capture_window(N, site, pool, span);
                out->nnodes++;
            }
        }
        addr = next;
    }

    out->ms = GetTickCount() - t0;
    out->faults_absorbed = mem_guard_fault_count() - faults0;
    out->ok = out->nnodes > 0;

    out->incomplete = timed_out;
    if (timed_out)
        log_line("gtree[%s]: ⚠⚠ DEADLINE (%d ms) hit -- scan stopped early and "
                 "is INCOMPLETE. Results below are partial; 'not found' here "
                 "is not proof of absence.", tag, GTREE_DEADLINE_MS);

    log_line("gtree[%s]: walked %d regions, scanned %d (%llu KB) in %lu ms; "
             "guard absorbed %ld fault(s); %d pool pointer(s) found, %d kept.",
             tag, out->regions_walked, out->regions_scanned,
             (unsigned long long)(out->bytes_scanned / 1024),
             (unsigned long)out->ms, out->faults_absorbed,
             out->nnodes_seen, out->nnodes);

    if (out->nnodes == 0) {
        if (out->faults_absorbed > 0)
            log_line("gtree[%s]: ⚠ faults absorbed -- PARTIAL scan, so zero "
                     "pointers here is not proof of absence.", tag);
        else
            log_line("gtree[%s]: complete scan, ZERO pointers into the pool. "
                     "That is a real finding, not a silent miss: it would mean "
                     "the runtime does not refer to nodes by pooled name, and "
                     "this entry point is wrong.", tag);
    } else if (out->nnodes_seen > out->nnodes) {
        log_line("gtree[%s]: ⚠ %d hit(s) seen, %d kept -- the rest were not "
                 "real pool entries (a pointer into the MIDDLE of a name, or "
                 "into non-name pool bytes). The dump is a SAMPLE.",
                 tag, out->nnodes_seen, out->nnodes);
    }
    if (out->nnodes_seen > GTREE_MAX_SEEN)
        log_line("gtree[%s]: ⚠ hit the %d-hit inspection budget -- the scan "
                 "stopped inspecting early and is INCOMPLETE.",
                 tag, GTREE_MAX_SEEN);
    return out->ok;
}

void gui_tree_report(const gtree_result *r, const char *tag)
{
    if (!r->ok)
        return;

    log_line("gtree[%s]: --- node candidate dump (%d) ---", tag, r->nnodes);
    for (int i = 0; i < r->nnodes; i++) {
        const gtree_node *N = &r->node[i];
        log_line("gtree[%s]: node %3d  site=%p -> %-40s pool_ptrs=%d",
                 tag, i, (void *)N->site, N->name, N->nptr_pool);

        /* One line per 4 words keeps the log readable and diffable. The
         * offsets are relative to the POINTER SITE, so word +0 is always the
         * name pointer itself and the columns line up across nodes. */
        for (int w = 0; w < GTREE_WINDOW; w += 4) {
            char line[256];
            int n = 0;
            n += _snprintf(line + n, sizeof(line) - n, "%+4d:",
                           (w - GTREE_BACK) * 8);
            for (int k = w; k < w + 4 && k < GTREE_WINDOW; k++) {
                if (!N->word_ok[k])
                    n += _snprintf(line + n, sizeof(line) - n, " %16s", "--");
                else
                    n += _snprintf(line + n, sizeof(line) - n, " %016llx",
                                   (unsigned long long)N->word[k]);
            }
            line[sizeof(line) - 1] = '\0';
            log_line("gtree[%s]:   %s", tag, line);
        }
    }
}

void gui_tree_diff(const gtree_result *a, const gtree_result *b)
{
    if (!a->ok || !b->ok) {
        log_line("gtree diff: one pass has no nodes -- nothing to compare.");
        return;
    }

    int compared = 0, changed_nodes = 0, changed_words = 0, changed_derefs = 0;

    log_line("gtree diff: comparing pass A (%d nodes) with pass B (%d nodes). "
             "Matching by SITE ADDRESS -- the identity of the memory being "
             "watched, which needs no uniqueness assumption and cannot "
             "mispair.", a->nnodes, b->nnodes);

    for (int i = 0; i < a->nnodes; i++) {
        const gtree_node *A = &a->node[i];

        /* ⭐ MATCH BY SITE ADDRESS, not by name.
         *
         * Name matching required the name to be UNIQUE in both passes, because
         * pairing two arbitrary same-named nodes would manufacture bogus
         * differences. But layouts reuse names heavily -- "a_list" occurs many
         * times -- so in the first real run that rule discarded 140 of 192
         * nodes and compared only 52. A negative drawn from 27% of an
         * already-capped sample is not a negative.
         *
         * The site address is the honest key: it is exactly the identity of
         * the memory being watched, needs no uniqueness assumption, and cannot
         * mispair. Measured in-game: node addresses were byte-identical across
         * three passes (3117 hits / 192 kept every time), so this pairs far
         * more nodes AND pairs them correctly.
         *
         * A node that moves between passes simply drops out of the comparison,
         * which is reported rather than hidden. */
        int bidx = -1;
        for (int k = 0; k < b->nnodes; k++)
            if (b->node[k].site == A->site) { bidx = k; break; }
        if (bidx < 0)
            continue;

        const gtree_node *B = &b->node[bidx];
        /* Same address must still mean the same node. If the name changed, the
         * allocation was reused and the words are not comparable. */
        if (strcmp(A->name, B->name) != 0)
            continue;
        compared++;

        int diffs = 0;
        for (int w = 0; w < GTREE_WINDOW; w++) {
            if (!A->word_ok[w] || !B->word_ok[w])
                continue;

            BOOL word_changed = (A->word[w] != B->word[w]);
            /* ONE HOP OUT: the pointer itself may be stable while the object
             * it points at changes. That is the likelier shape here, since
             * every word beside the name pointer proved static across three
             * screens. Only compare when both sides actually followed
             * something (0 = not followable). */
            BOOL deref_changed = (A->deref_hash[w] && B->deref_hash[w] &&
                                  A->deref_hash[w] != B->deref_hash[w]);
            if (!word_changed && !deref_changed)
                continue;

            if (diffs == 0)
                log_line("gtree diff: node %-40s (A site=%p, B site=%p)",
                         A->name, (void *)A->site, (void *)B->site);
            diffs++;
            changed_words++;
            if (word_changed)
                log_line("gtree diff:   word %+4d  A=%016llx  B=%016llx",
                         (w - GTREE_BACK) * 8,
                         (unsigned long long)A->word[w],
                         (unsigned long long)B->word[w]);
            if (deref_changed) {
                changed_derefs++;
                log_line("gtree diff:   word %+4d  ptr %016llx STABLE but its "
                         "TARGET changed: hash %08x -> %08x",
                         (w - GTREE_BACK) * 8,
                         (unsigned long long)A->word[w],
                         A->deref_hash[w], B->deref_hash[w]);
            }
        }
        if (diffs)
            changed_nodes++;
    }

    /* A POSITIVE count of what was examined. "No changes" and "nothing was
     * comparable" are completely different findings and must never look the
     * same -- see memory probe-zero-checked-looks-like-zero-failed. */
    log_line("gtree diff: compared %d of %d node(s) matched by site address; "
             "%d node(s) differ, %d word(s) changed (%d of those are a STABLE "
             "pointer whose TARGET changed, one hop out).",
             compared, a->nnodes, changed_nodes, changed_words,
             changed_derefs);

    if (compared == 0) {
        log_line("gtree diff: ⚠ NOTHING was comparable -- no node kept the "
                 "same address. This says nothing about live state.");
    } else if (changed_words == 0) {
        if (a->incomplete || b->incomplete) {
            /* A truncated pass must never produce a confident negative. */
            log_line("gtree diff: ⚠⚠ nothing changed, BUT pass %s stopped at "
                     "the deadline and is INCOMPLETE. This is NOT evidence of "
                     "absence -- the nodes that differ may simply not have "
                     "been reached. Re-run and require both passes complete.",
                     a->incomplete ? (b->incomplete ? "A and B" : "A") : "B");
        } else {
            log_line("gtree diff: every compared word AND every followed "
                     "target is IDENTICAL across two COMPLETE passes. On "
                     "DIFFERENT screens that is a real negative: neither these "
                     "objects nor the objects they point at carry the live "
                     "state. The next hop out, or a different root entirely, "
                     "is where to look.");
        }
    } else {
        log_line("gtree diff: ⭐ the changed word offsets above are the ONLY "
                 "candidates for live state. A word that changes is not yet "
                 "proven to be focus -- next step is to confirm it tracks the "
                 "cursor rather than merely being a timer or animation phase.");
    }
}
