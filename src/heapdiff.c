/*
 * Differential heap scan. See heapdiff.h for why the control is the method.
 *
 * Read-only: VirtualQuery, then guarded reads. No writes, no hooks.
 */
#include "heapdiff.h"
#include "memsafe.h"
#include "speech.h"
#include "log.h"

#include <string.h>
#include <stdio.h>

#define HD_DEADLINE_MS 45000

/* Blocks are copied a chunk at a time so the fault guard costs one setjmp per
 * chunk rather than one per block. Must be a multiple of HD_BLOCK. */
#define HD_CHUNK        (256 * 1024)

/* Log progress roughly this often, so a long capture is visible while running. */
#define HD_REPORT_EVERY (256 * 1024 * 1024)

/* FNV-1a, mixed 8 bytes at a time. Not cryptographic and does not need to be:
 * it must only change when the bytes change.
 *
 * ⚠ SPEED IS CORRECTNESS HERE. Byte-at-a-time FNV is a serial dependency chain
 * (~1 byte/cycle), and hashing 1.55 GB of 4 KB blocks meant **1.66 billion**
 * iterations -- the first in-game run never finished a single capture.
 *
 * ⚠⚠ AND SAMPLING IS NOT THE FIX. Hashing a subset of each block would be fast
 * but could miss a change entirely: a focus/selection index is about 4 bytes,
 * so a strided sample can step right over it. That is a false NEGATIVE, which
 * is exactly the failure that closed the previous three routes. So this still
 * mixes EVERY byte -- it just does 8 at a time (512 iterations per 4 KB block
 * instead of 4096). Coverage is identical; only the loop is cheaper.
 *
 * The tail is handled bytewise. HD_BLOCK is a multiple of 8 so there is no
 * remainder in practice, but this function is exposed and directly tested, and
 * a tail bug is precisely the kind that hides until the one block that needs
 * it. */
unsigned hd_hash_block(const BYTE *p, SIZE_T n)
{
    unsigned long long h = 14695981039346656037ULL;
    SIZE_T i = 0;

    for (; i + 8 <= n; i += 8) {
        unsigned long long w;
        memcpy(&w, p + i, 8);       /* p need not be 8-byte aligned */
        h ^= w;
        h *= 1099511628211ULL;
    }
    for (; i < n; i++) {
        h ^= (unsigned long long)p[i];
        h *= 1099511628211ULL;
    }

    /* Fold to 32 bits: both halves contribute, so a change confined to either
     * still shows up. */
    unsigned r = (unsigned)(h ^ (h >> 32));
    return r ? r : 1u;      /* 0 is reserved for "no block" */
}

/* Same region filter as the tree walk, which ran repeatedly in-game without
 * ever destabilising the process. */
static BOOL want_region(const MEMORY_BASIC_INFORMATION *mbi)
{
    if (mbi->State != MEM_COMMIT)
        return FALSE;
    /* PAGE_GUARD is 0x100 -- never mask Protect to a byte. */
    if (mbi->Protect & (PAGE_NOACCESS | PAGE_GUARD))
        return FALSE;
    DWORD w = mbi->Protect & (PAGE_READWRITE | PAGE_READONLY |
                              PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                              PAGE_EXECUTE_READWRITE);
    if (!w)
        return FALSE;
    /* ⚠ WRITABLE ONLY. Live state must be written by the game, so read-only
     * and executable pages cannot hold it -- excluding them removes a large
     * slice of the address space (mapped images, code) that could only ever
     * contribute noise or cost. */
    if (!(mbi->Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                          PAGE_EXECUTE_READWRITE)))
        return FALSE;
    return mbi->Type == MEM_PRIVATE || mbi->Type == MEM_MAPPED;
}

static hd_block *g_pool_a1, *g_pool_a2, *g_pool_b;

static BOOL alloc_pools(void)
{
    SIZE_T n = (SIZE_T)HD_MAX_BLOCKS * sizeof(hd_block);
    if (!g_pool_a1) g_pool_a1 = (hd_block *)VirtualAlloc(
        NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_pool_a2) g_pool_a2 = (hd_block *)VirtualAlloc(
        NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_pool_b)  g_pool_b  = (hd_block *)VirtualAlloc(
        NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return g_pool_a1 && g_pool_a2 && g_pool_b;
}

BOOL hd_capture(hd_snapshot *out, const char *tag)
{
    hd_block *store = out->block;
    memset(out, 0, sizeof(*out));
    out->block = store;

    if (!mem_guard_install()) {
        log_line("heapdiff[%s]: fault guard self-test FAILED -- not scanning.",
                 tag);
        return FALSE;
    }

    LONG faults0 = mem_guard_fault_count();
    DWORD t0 = GetTickCount();

    /* Exclude our own stack: reading a stack guard page raises
     * STATUS_GUARD_PAGE_VIOLATION and longjmp-ing out of it leaves the guard
     * unreset and the thread broken. */
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    uintptr_t stack_lo = (uintptr_t)tib->StackLimit;
    uintptr_t stack_hi = (uintptr_t)tib->StackBase;

    /* Our own snapshot pools must not be scanned: they change every capture by
     * construction and would be pure self-inflicted noise. */
    uintptr_t own[3] = { (uintptr_t)g_pool_a1, (uintptr_t)g_pool_a2,
                         (uintptr_t)g_pool_b };
    SIZE_T own_len = (SIZE_T)HD_MAX_BLOCKS * sizeof(hd_block);

    static BYTE buf[HD_CHUNK];
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t maxa = (uintptr_t)si.lpMaximumApplicationAddress;
    volatile BOOL timed_out = FALSE;
    SIZE_T last_report = 0;

    while (addr < maxa) {
        if (GetTickCount() - t0 > HD_DEADLINE_MS) { timed_out = TRUE; break; }

        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)))
            break;
        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        SIZE_T rsize = mbi.RegionSize;
        uintptr_t next = base + rsize;
        if (next <= addr)
            break;

        BOOL is_own = FALSE;
        for (int i = 0; i < 3; i++)
            if (own[i] && base < own[i] + own_len && next > own[i])
                is_own = TRUE;

        if (!want_region(&mbi) || is_own ||
            (base < stack_hi && next > stack_lo)) {
            addr = next;
            continue;
        }
        out->regions++;

        /* Walk the region in CHUNKS of several blocks.
         *
         * ⚠ Two per-block costs are deliberately gone from this loop:
         *
         *  - `mem_readable_run` per block: the VirtualQuery above already
         *    established this whole region is MEM_COMMIT and readable, so
         *    re-querying every 4 KB was ~406,000 redundant syscalls. The fault
         *    guard is the real protection anyway -- memsafe.h is explicit that
         *    VirtualQuery is inherently TOCTOU and catching the fault is what
         *    actually saves us.
         *  - `setjmp` per block: now one per chunk, ~64x fewer.
         *
         * volatile: live across the MEM_GUARDED longjmp. -Wclobbered has been
         * a real defect several times in this project, never noise. */
        for (volatile SIZE_T off = 0; off + HD_BLOCK <= rsize;
             off += HD_CHUNK) {
            if (out->nblocks >= HD_MAX_BLOCKS) {
                out->truncated = 1;
                break;
            }
            /* Check the DEADLINE here, in the INNER loop.
             *
             * ⛔ It used to be tested only once per REGION, which made it
             * useless: this process has 28 MB and 13 MB regions (~7000 blocks
             * each), so a capture could run for minutes without ever reaching
             * the check. The whole point of a deadline is "never hang, always
             * report", and placing it in the outer loop silently voided that
             * guarantee -- the first in-game run hung with the log stopped at
             * "capture A1", indistinguishable from a crash. */
            if (GetTickCount() - t0 > HD_DEADLINE_MS) {
                timed_out = TRUE;
                break;
            }

            SIZE_T want = rsize - off;
            if (want > HD_CHUNK) want = HD_CHUNK;
            want &= ~(SIZE_T)(HD_BLOCK - 1);     /* whole blocks only */
            if (want == 0)
                break;
            SIZE_T nblk = want / HD_BLOCK;

            volatile BOOL faulted;
            MEM_GUARDED(faulted, memcpy(buf, (const void *)(base + off), want));

            if (!faulted) {
                for (SIZE_T k = 0; k < nblk &&
                                   out->nblocks < HD_MAX_BLOCKS; k++) {
                    out->block[out->nblocks].addr = base + off + k * HD_BLOCK;
                    out->block[out->nblocks].hash =
                        hd_hash_block(buf + k * HD_BLOCK, HD_BLOCK);
                    out->nblocks++;
                    out->bytes += HD_BLOCK;
                }
            } else {
                /* ⚠ A CHUNK-LEVEL GUARD ABORTS THE WHOLE COPY, so a single bad
                 * page would silently cost all 64 blocks -- and a block missing
                 * from a snapshot is skipped by hd_changed as "not present in
                 * both", quietly REMOVING candidates. That is a false-negative
                 * path, so retry block by block and keep the good ones. */
                out->fallback_chunks++;
                for (SIZE_T k = 0; k < nblk &&
                                   out->nblocks < HD_MAX_BLOCKS; k++) {
                    uintptr_t at = base + off + k * HD_BLOCK;
                    if (mem_readable_run((const void *)at, HD_BLOCK) < HD_BLOCK)
                        continue;
                    volatile BOOL f2;
                    MEM_GUARDED(f2, memcpy(buf, (const void *)at, HD_BLOCK));
                    if (f2)
                        continue;
                    out->block[out->nblocks].addr = at;
                    out->block[out->nblocks].hash =
                        hd_hash_block(buf, HD_BLOCK);
                    out->nblocks++;
                    out->bytes += HD_BLOCK;
                }
            }

            /* Progress, so a slow capture is visible WHILE it runs rather than
             * only in hindsight. A keypress must never buy silence -- that
             * ambiguity has already cost this project several runs. */
            if (out->bytes - last_report >= HD_REPORT_EVERY) {
                last_report = out->bytes;
                log_line("heapdiff[%s]: ... %d blocks (%llu MB) in %lu ms",
                         tag, out->nblocks,
                         (unsigned long long)(out->bytes / 1048576),
                         (unsigned long)(GetTickCount() - t0));
            }
        }
        if (out->truncated || timed_out)
            break;
        addr = next;
    }

    out->ms = GetTickCount() - t0;
    out->faults = mem_guard_fault_count() - faults0;

    log_line("heapdiff[%s]: %d blocks (%llu MB) across %d regions in %lu ms; "
             "guard absorbed %ld fault(s); %d chunk(s) needed per-block "
             "fallback%s",
             tag, out->nblocks, (unsigned long long)(out->bytes / 1048576),
             out->regions, (unsigned long)out->ms, out->faults,
             out->fallback_chunks,
             out->truncated ? "  ⚠ TRUNCATED (coverage PARTIAL)" : "");
    if (timed_out)
        log_line("heapdiff[%s]: ⚠⚠ DEADLINE hit -- snapshot is PARTIAL. Any "
                 "comparison against it understates what changed.", tag);

    return out->nblocks > 0;
}

/* Both snapshots are built by the same ascending region walk, so their block
 * lists are sorted by address. Merge-compare rather than search. */
int hd_changed(const hd_snapshot *a, const hd_snapshot *b,
               uintptr_t *out, int cap)
{
    int i = 0, j = 0, n = 0;
    while (i < a->nblocks && j < b->nblocks) {
        uintptr_t xa = a->block[i].addr, xb = b->block[j].addr;
        if (xa < xb)      { i++; continue; }
        if (xa > xb)      { j++; continue; }
        /* Present in BOTH: only now can a hash difference mean anything.
         * A block in one snapshot only means memory was allocated or freed,
         * which says nothing about the screen. */
        if (a->block[i].hash != b->block[j].hash) {
            if (out && n < cap)
                out[n] = xa;
            n++;
        }
        i++; j++;
    }
    return n;
}

/* Accumulated candidates: addresses that survived EVERY trial so far.
 * Declared here because the byte-level watch below reads them. */
static uintptr_t g_cand[HD_MAX_REPORT * 64];
static int       g_ncand;
static int       g_have_cand;
static int       g_trial;

/* --- Byte-level watch ----------------------------------------------------
 *
 * Keeps full page CONTENT for the surviving candidates so the exact changed
 * bytes can be named. See heapdiff.h for why this replaces further narrowing. */
/* Three slots, not two: the watch protocol needs a baseline, a control repeat
 * and a post-move capture held at once. */
/* Four slots: baseline, control, after-move, after-move-BACK. The fourth is
 * the reversibility test -- see hd_wstage in heapdiff.h. */
static BYTE      g_wslot[4][HD_WATCH_PAGES][HD_BLOCK];
static int       g_wslot_ok[4][HD_WATCH_PAGES];
static hd_wstage g_wstage;

static BYTE      g_watch[2][HD_WATCH_PAGES][HD_BLOCK];
static uintptr_t g_watch_addr[HD_WATCH_PAGES];
static int       g_watch_ok[2][HD_WATCH_PAGES];
static int       g_watch_n;
static int       g_watch_slot;      /* which half was written last */
static int       g_watch_have;      /* how many captures taken (0,1,2+) */

BOOL hd_watch_capture(const char *tag)
{
    if (g_ncand <= 0) {
        log_line("watch[%s]: no candidates yet -- run trials first.", tag);
        return FALSE;
    }
    /* ⚠ REFUSE TO WATCH AN UNFILTERED SET.
     *
     * Measured 2026-09-02: with ONE trial done (369 candidates) F7 happily
     * watched the first 32 and produced 110 byte-runs of pure noise -- a
     * double-buffer pointer rebase (+0x300000 then -0x300000 alternating),
     * frame counters, a churning pointer table. It looked like data and was
     * worth nothing, and nothing in the output said the set was unfiltered.
     *
     * The whole value of the watch is that the pages were already narrowed.
     * Watching a wide set produces a wall of plausible-looking noise, which is
     * worse than producing nothing. */
    if (g_trial < 3 || g_ncand > HD_WATCH_PAGES) {
        log_line("watch[%s]: REFUSED -- %d candidate(s) after %d trial(s). "
                 "The watch needs a NARROWED set (<= %d pages, >= 3 trials); "
                 "otherwise it reports frame counters and double-buffer "
                 "pointer swaps that look exactly like real fields. Run more "
                 "F6 trials first -- alternating screen change and cursor "
                 "move is what actually narrows it.",
                 tag, g_ncand, g_trial, HD_WATCH_PAGES);
        return FALSE;
    }
    if (!mem_guard_install())
        return FALSE;

    int slot = g_watch_slot ^ 1;
    int n = g_ncand < HD_WATCH_PAGES ? g_ncand : HD_WATCH_PAGES;
    int got = 0;

    for (int i = 0; i < n; i++) {
        g_watch_addr[i] = g_cand[i];
        g_watch_ok[slot][i] = 0;
        if (mem_readable_run((const void *)g_cand[i], HD_BLOCK) < HD_BLOCK)
            continue;
        volatile BOOL faulted;
        MEM_GUARDED(faulted,
                    memcpy(g_watch[slot][i], (const void *)g_cand[i],
                           HD_BLOCK));
        if (faulted)
            continue;
        g_watch_ok[slot][i] = 1;
        got++;
    }
    g_watch_n = n;
    g_watch_slot = slot;
    if (g_watch_have < 2) g_watch_have++;

    log_line("watch[%s]: captured %d of %d candidate page(s)%s",
             tag, got, n,
             g_ncand > HD_WATCH_PAGES ? "  (candidate list capped)" : "");
    return got > 0;
}

void hd_watch_report(void)
{
    if (g_watch_have < 2) {
        log_line("watch: only one capture so far -- press again after a "
                 "cursor move or screen change to see what differs.");
        return;
    }

    int cur = g_watch_slot, prev = g_watch_slot ^ 1;
    int pages_changed = 0, total_runs = 0;

    for (int i = 0; i < g_watch_n; i++) {
        if (!g_watch_ok[cur][i] || !g_watch_ok[prev][i])
            continue;
        const BYTE *a = g_watch[prev][i], *b = g_watch[cur][i];
        if (memcmp(a, b, HD_BLOCK) == 0)
            continue;
        pages_changed++;
        log_line("watch: page %p changed:", (void *)g_watch_addr[i]);

        /* Report contiguous byte RUNS, and for 4- and 8-byte runs also show
         * the value as an integer -- a selection index moving from N to N+1
         * is the single most informative thing this can print. */
        SIZE_T off = 0;
        int runs = 0;
        while (off < HD_BLOCK && runs < 24) {
            if (a[off] == b[off]) { off++; continue; }
            SIZE_T s = off;
            while (off < HD_BLOCK && a[off] != b[off]) off++;
            SIZE_T len = off - s;
            runs++; total_runs++;

            /* ⚠ WIDEN A SHORT RUN TO ITS ALIGNED FIELD.
             *
             * Byte-equality runs UNDERSTATE the field. A selection index
             * stepping 7 -> 8 changes only the LOW byte -- the upper three are
             * zero in both -- so the run is 1 byte and would print as raw hex,
             * hiding exactly the N -> N+1 pattern this whole exercise is
             * looking for. Caught by the offline harness, which planted that
             * precise case.
             *
             * So if a short run sits inside one aligned 4-byte slot, report
             * the whole slot as a u32. */
            if (len <= 4) {
                SIZE_T fs = s & ~(SIZE_T)3;
                if (fs + 4 <= HD_BLOCK && s + len <= fs + 4) {
                    s = fs;
                    len = 4;
                }
            }

            if (len == 4) {
                unsigned va, vb;
                memcpy(&va, a + s, 4); memcpy(&vb, b + s, 4);
                log_line("watch:   +0x%03llx  %4llu B  u32 %10u -> %-10u "
                         "(delta %+d)",
                         (unsigned long long)s, (unsigned long long)len,
                         va, vb, (int)(vb - va));
            } else if (len == 8) {
                unsigned long long va, vb;
                memcpy(&va, a + s, 8); memcpy(&vb, b + s, 8);
                log_line("watch:   +0x%03llx  %4llu B  u64 %llu -> %llu",
                         (unsigned long long)s, (unsigned long long)len,
                         va, vb);
            } else {
                log_line("watch:   +0x%03llx  %4llu B  %02x %02x %02x %02x "
                         "-> %02x %02x %02x %02x",
                         (unsigned long long)s, (unsigned long long)len,
                         a[s], len > 1 ? a[s+1] : 0, len > 2 ? a[s+2] : 0,
                         len > 3 ? a[s+3] : 0,
                         b[s], len > 1 ? b[s+1] : 0, len > 2 ? b[s+2] : 0,
                         len > 3 ? b[s+3] : 0);
            }
        }
        if (runs >= 24)
            log_line("watch:   ... more runs on this page (capped)");
    }

    log_line("watch: %d of %d watched page(s) changed, %d byte run(s). "
             "⚠ A changed field is a CANDIDATE, not proof of focus -- check "
             "it tracks the cursor before believing it.",
             pages_changed, g_watch_n, total_runs);
}

hd_wstage hd_watch_stage(void) { return g_wstage; }

/* --- Tracking the index candidates across repeated cursor presses -------- */
#define HD_TRACK_MAX  32
#define HD_TRACK_HIST 16

static struct {
    uintptr_t addr;                    /* absolute address of the field */
    unsigned  v[HD_TRACK_HIST];
    int       n;
} g_track[HD_TRACK_MAX];
static int g_ntrack;

void hd_track_latch(void)
{
    /* The verdict fills g_track as it reports. This just clears it so the
     * next verdict starts a fresh series. */
    g_ntrack = 0;
    for (int i = 0; i < HD_TRACK_MAX; i++) g_track[i].n = 0;
    log_line("track: cleared. The next F7 verdict will latch its index "
             "candidates for tracking.");
}

void hd_track_sample(void)
{
    if (g_ntrack == 0) {
        log_line("track: nothing latched -- run the F7 verdict first so it "
                 "has index candidates to follow.");
        speech_say(L"Nothing latched yet. Run the F7 sequence first.", FALSE);
        return;
    }
    if (!mem_guard_install())
        return;

    for (int i = 0; i < g_ntrack; i++) {
        if (g_track[i].n >= HD_TRACK_HIST)
            continue;
        unsigned v = 0;
        if (!mem_copy(&v, (const void *)g_track[i].addr, 4))
            continue;
        g_track[i].v[g_track[i].n++] = v;
    }

    log_line("track: === series after %d sample(s) ===",
             g_track[0].n);
    for (int i = 0; i < g_ntrack; i++) {
        char line[256];
        int p = 0;
        p += _snprintf(line + p, sizeof(line) - p, "%p:",
                       (void *)g_track[i].addr);
        for (int k = 0; k < g_track[i].n && p < 200; k++)
            p += _snprintf(line + p, sizeof(line) - p, " %u",
                           g_track[i].v[k]);
        line[sizeof(line) - 1] = 0;

        /* Classify the series. A selection index walks by +-1 with each
         * press; anything else is not what we are looking for. */
        const char *verdict = "";
        int n = g_track[i].n;
        if (n >= 3) {
            int all_one = 1, any_move = 0, monotonic = 1;
            for (int k = 1; k < n; k++) {
                int d = (int)g_track[i].v[k] - (int)g_track[i].v[k - 1];
                if (d != 0) any_move = 1;
                if (d != 1 && d != -1 && d != 0) all_one = 0;
                if (k > 1) {
                    int pd = (int)g_track[i].v[k-1] - (int)g_track[i].v[k-2];
                    if (d != 0 && pd != 0 && ((d > 0) != (pd > 0)))
                        monotonic = 0;
                }
            }
            if (!any_move)      verdict = "   <- NEVER MOVED (not it)";
            else if (all_one && !monotonic)
                verdict = "   <- ⭐⭐ steps by ONE and REVERSES: INDEX";
            else if (all_one)   verdict = "   <- steps by one, one direction "
                                          "only so far";
            else                verdict = "   <- irregular (not an index)";
        }
        log_line("track:   %s%s", line, verdict);
    }
    log_line("track: press the cursor one item and sample again. A selection "
             "index walks by one each press and REVERSES when you go back; "
             "anything else is not it.");
    speech_say(L"Sampled.", FALSE);
}

/* Capture the candidate pages into one of the three watch slots. */
static int wcapture(int slot, const char *tag)
{
    if (g_ncand <= 0 || g_trial < 3 || g_ncand > HD_WATCH_PAGES) {
        log_line("watch[%s]: REFUSED -- %d candidate(s) after %d trial(s); "
                 "need <= %d after >= 3 trials.",
                 tag, g_ncand, g_trial, HD_WATCH_PAGES);
        return -1;
    }
    if (!mem_guard_install())
        return -1;

    int n = g_ncand, got = 0;
    for (int i = 0; i < n; i++) {
        g_watch_addr[i] = g_cand[i];
        g_wslot_ok[slot][i] = 0;
        if (mem_readable_run((const void *)g_cand[i], HD_BLOCK) < HD_BLOCK)
            continue;
        volatile BOOL faulted;
        MEM_GUARDED(faulted,
                    memcpy(g_wslot[slot][i], (const void *)g_cand[i],
                           HD_BLOCK));
        if (faulted)
            continue;
        g_wslot_ok[slot][i] = 1;
        got++;
    }
    g_watch_n = n;
    log_line("watch[%s]: captured %d of %d candidate page(s)", tag, got, n);
    return got;
}

/* Is the aligned 4-byte field at (page i, offset off) different between two
 * slots? Fields, not byte runs -- the unit the report speaks in. */
static int field_differs(int sa, int sb, int i, SIZE_T off)
{
    unsigned a, b;
    memcpy(&a, g_wslot[sa][i] + off, 4);
    memcpy(&b, g_wslot[sb][i] + off, 4);
    return a != b;
}

void hd_watch_step(void)
{
    switch (g_wstage) {
    case HD_W_WANT_1:
        if (wcapture(0, "W1") < 0) {
            speech_say(L"Not enough trials yet. Run more F6 trials.", FALSE);
            return;
        }
        g_wstage = HD_W_WANT_2;
        speech_say(L"Baseline captured. Do not touch anything. "
                   L"Press F7 again.", FALSE);
        log_line("watch: NEXT press = W2, the CONTROL. Change NOTHING before "
                 "it. Fields that move on their own are subtracted -- the "
                 "page-level control never tested individual fields, which is "
                 "why 47 of them looked reactive last run.");
        return;

    case HD_W_WANT_2:
        if (wcapture(1, "W2") < 0) return;
        {
            int noisy = 0;
            for (int i = 0; i < g_watch_n; i++) {
                if (!g_wslot_ok[0][i] || !g_wslot_ok[1][i]) continue;
                for (SIZE_T off = 0; off + 4 <= HD_BLOCK; off += 4)
                    if (field_differs(0, 1, i, off)) noisy++;
            }
            log_line("watch: control: %d field(s) changed with NOTHING "
                     "touched. These are excluded.", noisy);
        }
        g_wstage = HD_W_WANT_3;
        speech_say(L"Control captured. Now move the cursor exactly one item, "
                   L"then press F7.", FALSE);
        log_line("watch: NEXT press = W3. Move the cursor EXACTLY ONE item, "
                 "then press.");
        return;

    case HD_W_WANT_3: {
        if (wcapture(2, "W3") < 0) return;
        /* Count only -- the verdict needs the reversibility capture. Reporting
         * here is what produced a false positive last time: a field that
         * "reacted to the cursor" looked like a finding when it was a counter
         * reacting to ANY input. */
        int reacted = 0;
        for (int i = 0; i < g_watch_n; i++) {
            if (!g_wslot_ok[0][i] || !g_wslot_ok[1][i] || !g_wslot_ok[2][i])
                continue;
            for (SIZE_T off = 0; off + 4 <= HD_BLOCK; off += 4)
                if (field_differs(1, 2, i, off) && !field_differs(0, 1, i, off))
                    reacted++;
        }
        log_line("watch: %d field(s) reacted to the move and were quiet in "
                 "the control. NOT reporting them yet -- a counter reacts to "
                 "any input too.", reacted);
        g_wstage = HD_W_WANT_4;
        speech_say(L"Move captured. Now move the cursor BACK to where it was, "
                   L"then press F7.", FALSE);
        log_line("watch: NEXT press = W4, the REVERSIBILITY test. Move the "
                 "cursor BACK one item, to the item it started on, then "
                 "press. Real state returns to its earlier value; a counter "
                 "keeps climbing.");
        return;
    }

    case HD_W_WANT_4: {
        if (wcapture(3, "W4") < 0) return;

        int reverted = 0, counted = 0, other = 0;
        int idx_like = 0, listed_small = 0;
        /* Start a fresh tracking series: this verdict's candidates replace
         * whatever the previous one latched. */
        hd_track_latch();
        log_line("watch: === VERDICT: fields that RETURNED to their original "
                 "value when the cursor moved back ===");
        for (int i = 0; i < g_watch_n; i++) {
            if (!g_wslot_ok[1][i] || !g_wslot_ok[2][i] || !g_wslot_ok[3][i])
                continue;
            for (SIZE_T off = 0; off + 4 <= HD_BLOCK; off += 4) {
                if (!field_differs(1, 2, i, off))  continue;  /* no reaction */
                if (field_differs(0, 1, i, off))   continue;  /* noisy */

                unsigned b, c, d;
                memcpy(&b, g_wslot[1][i] + off, 4);   /* before the move */
                memcpy(&c, g_wslot[2][i] + off, 4);   /* after the move   */
                memcpy(&d, g_wslot[3][i] + off, 4);   /* after moving back */

                if (d == b) {
                    /* ⭐ REVERSIBLE: this is what state looks like.
                     *
                     * ⚠ But reversible is NOT rare. Measured in-game: 2658
                     * fields returned, dominated by (a) IEEE floats -- the
                     * highlight's position/scale animating to the new item and
                     * back -- and (b) pointers into per-item node structures.
                     * Both are true positives for "reversible" and useless for
                     * naming the selected item.
                     *
                     * So RANK. A selection index is a small integer that steps
                     * by ONE. Report those first and separately; everything
                     * else is counted but not listed, or the signal is buried
                     * under thousands of legitimate animation values. */
                    reverted++;
                    int delta = (int)(c - b);
                    int stepped_one = (delta == 1 || delta == -1);
                    /* A float in a UI range decodes as ~0x3F000000-0x43000000;
                     * exclude that band from "small integer". */
                    int looks_float = (b >= 0x38000000u && b <= 0x4A000000u) ||
                                      (b >= 0xB8000000u && b <= 0xCA000000u);
                    int small_int = (b < 4096 && c < 4096 && !looks_float);

                    if (stepped_one && small_int) {
                        idx_like++;
                        /* Latch it so hd_track_sample() can follow it across
                         * repeated cursor presses -- the only confirmation
                         * available, since addresses change between runs. */
                        if (g_ntrack < HD_TRACK_MAX) {
                            g_track[g_ntrack].addr = g_watch_addr[i] + off;
                            g_track[g_ntrack].n = 0;
                            g_ntrack++;
                        }
                        log_line("watch:   ⭐⭐ %p +0x%03llx  %u -> %u -> %u  "
                                 "STEPPED BY %+d AND RETURNED -- INDEX "
                                 "CANDIDATE",
                                 (void *)g_watch_addr[i],
                                 (unsigned long long)off, b, c, d, delta);
                    } else if (small_int && listed_small < 32) {
                        listed_small++;
                        log_line("watch:   ⭐ %p +0x%03llx  %u -> %u -> %u  "
                                 "(small, delta %+d)",
                                 (void *)g_watch_addr[i],
                                 (unsigned long long)off, b, c, d, delta);
                    }
                } else if ((c > b && d > c) || (c < b && d < c)) {
                    counted++;      /* kept moving the same way: a counter */
                } else {
                    other++;
                }
            }
        }
        log_line("watch: %d field(s) RETURNED (candidate state), %d kept "
                 "climbing (counters -- rejected), %d changed some other way.",
                 reverted, counted, other);
        log_line("watch: of those, %d STEPPED BY EXACTLY ONE and are small "
                 "integers -- the shape of a selection index. The rest are "
                 "mostly IEEE floats (highlight position/scale animating) and "
                 "pointers into per-item nodes: reversible, but they cannot "
                 "name the selected item.", idx_like);
        if (idx_like == 0 && reverted > 0)
            log_line("watch: ⚠ nothing stepped by one. Either the index is "
                     "not on these pages, or the cursor moved more than one "
                     "item. Move EXACTLY one item.");
        if (reverted == 0)
            log_line("watch: ⚠ nothing returned. Either the cursor did not go "
                     "back to the same item, or the selection is not on these "
                     "pages. A counter passing every earlier filter is exactly "
                     "how the last candidate fooled us.");

        g_wstage = HD_W_WANT_1;
        {
            wchar_t msg[160];
            _snwprintf(msg, 159, L"%d index candidates stepped by one. "
                                 L"%d fields returned in total, %d were "
                                 L"counters. See the log.",
                       idx_like, reverted, counted);
            msg[159] = L'\0';
            speech_say(msg, FALSE);
        }
        return;
    }
    }
}

/* --- Follow the candidate pointer pair ----------------------------------
 * See heapdiff.h. Reports the pointers as u64, the signed delta since the
 * last press in both bytes and 0x90-elements, and dumps the record. */
void hd_probe_ptr(void)
{
    /* The offsets the field-level watch surfaced. Kept as a small table so a
     * different candidate can be added without restructuring. */
    static const SIZE_T OFFS[] = { 0x978, 0x980 };
    static unsigned long long prev[2];
    static int have_prev;

    if (g_ncand <= 0) {
        log_line("probe: no candidate page -- run F6 trials first.");
        speech_say(L"No candidate page yet.", FALSE);
        return;
    }
    if (!mem_guard_install())
        return;

    /* Use the first candidate page; after 13 trials that is the single
     * surviving page. */
    uintptr_t page = g_cand[0];
    BYTE buf[HD_BLOCK];
    if (mem_readable_run((const void *)page, HD_BLOCK) < HD_BLOCK) {
        log_line("probe: candidate page %p not readable.", (void *)page);
        return;
    }
    volatile BOOL faulted;
    MEM_GUARDED(faulted, memcpy(buf, (const void *)page, HD_BLOCK));
    if (faulted) {
        log_line("probe: fault reading candidate page %p.", (void *)page);
        return;
    }

    log_line("probe: page %p", (void *)page);
    for (int k = 0; k < 2; k++) {
        unsigned long long v;
        memcpy(&v, buf + OFFS[k], 8);

        if (have_prev) {
            long long d = (long long)(v - prev[k]);
            /* Report the step in ELEMENTS: a selection pointer moves +-1
             * element per cursor step. A counter climbs regardless of
             * direction, which is the distinction that matters. */
            if (d % 0x90 == 0)
                log_line("probe:   +0x%03llx = %016llx   delta %+lld bytes "
                         "= %+lld element(s) of 0x90",
                         (unsigned long long)OFFS[k], v, d, d / 0x90);
            else
                log_line("probe:   +0x%03llx = %016llx   delta %+lld bytes "
                         "(NOT a multiple of 0x90)",
                         (unsigned long long)OFFS[k], v, d);
        } else {
            log_line("probe:   +0x%03llx = %016llx   (first reading)",
                     (unsigned long long)OFFS[k], v);
        }
        prev[k] = v;
    }
    have_prev = 1;

    /* Dump the record the first pointer targets. If it carries a label hash
     * or a string pointer, the selected item can be NAMED. */
    unsigned long long p0;
    memcpy(&p0, buf + OFFS[0], 8);
    if (p0 >= 0x10000 && mem_readable_run((const void *)p0, 0x90) >= 0x90) {
        BYTE rec[0x90];
        MEM_GUARDED(faulted, memcpy(rec, (const void *)p0, 0x90));
        if (!faulted) {
            log_line("probe: record at %016llx (0x90 bytes):", p0);
            for (int r = 0; r < 0x90; r += 16)
                log_line("probe:   +%02x  %02x %02x %02x %02x %02x %02x %02x "
                         "%02x  %02x %02x %02x %02x %02x %02x %02x %02x",
                         r, rec[r], rec[r+1], rec[r+2], rec[r+3],
                         rec[r+4], rec[r+5], rec[r+6], rec[r+7],
                         rec[r+8], rec[r+9], rec[r+10], rec[r+11],
                         rec[r+12], rec[r+13], rec[r+14], rec[r+15]);
            /* Any 8-byte value in the record that points at readable ASCII is
             * worth calling out -- that would be the item's name. */
            for (int r = 0; r + 8 <= 0x90; r += 8) {
                unsigned long long q;
                memcpy(&q, rec + r, 8);
                if (q < 0x10000 || mem_readable_run((const void *)q, 16) < 16)
                    continue;
                BYTE s[33];
                MEM_GUARDED(faulted, memcpy(s, (const void *)q, 32));
                if (faulted) continue;
                s[32] = 0;
                int printable = 1;
                for (int j = 0; j < 8; j++)
                    if (s[j] && (s[j] < 32 || s[j] >= 127)) printable = 0;
                if (printable && s[0])
                    log_line("probe:   +%02x -> STRING \"%s\"", r, (char *)s);
            }
        }
    } else {
        log_line("probe: %016llx is not readable as a 0x90-byte record.", p0);
    }

    speech_say(L"Probe logged.", FALSE);
}

/* --- ONE-KEY HUNT --------------------------------------------------------
 * See heapdiff.h. Four presses, one key, page hashes and field content in the
 * same sweep. */

#define HUNT_MAX_PAGES 8192          /* pages we keep CONTENT for */

/* ⭐ ACCUMULATE ACROSS HUNTS.
 *
 * Each hunt re-derives its candidates and reports what moved on THAT move, so
 * the same field showing up run after run was invisible in the log -- four
 * hunts had to be cross-referenced BY HAND to notice that page 0x4A7F000 (and
 * offsets +0xbc8, +0xc70, +0xa78) recurred every single time. That is the
 * tool's job, not the reader's.
 *
 * This is the intersection idea already used for pages, applied to FIELDS: a
 * field seen in several independent hunts is far stronger evidence than one
 * seen once, because each hunt has its own control and its own move. */
#define HUNT_SEEN_MAX 512

static struct {
    uintptr_t page;
    unsigned  off;
    int       hits;          /* how many hunts reported it */
    unsigned  last_from, last_to;
} g_seen[HUNT_SEEN_MAX];
static int g_nseen;
static int g_hunt_no;

static void seen_record(uintptr_t page, unsigned off, unsigned a, unsigned b)
{
    for (int i = 0; i < g_nseen; i++) {
        if (g_seen[i].page == page && g_seen[i].off == off) {
            g_seen[i].hits++;
            g_seen[i].last_from = a;
            g_seen[i].last_to = b;
            return;
        }
    }
    if (g_nseen < HUNT_SEEN_MAX) {
        g_seen[g_nseen].page = page;
        g_seen[g_nseen].off = off;
        g_seen[g_nseen].hits = 1;
        g_seen[g_nseen].last_from = a;
        g_seen[g_nseen].last_to = b;
        g_nseen++;
    }
}

/* Report fields seen in more than one hunt, most-seen first. */
static void seen_report(void)
{
    if (g_hunt_no < 2)
        return;
    log_line("hunt: === fields seen in MULTIPLE hunts (%d hunts so far) ===",
             g_hunt_no);
    int shown = 0;
    for (int want = g_hunt_no; want >= 2 && shown < 24; want--) {
        for (int i = 0; i < g_nseen && shown < 24; i++) {
            if (g_seen[i].hits != want)
                continue;
            /* A field that only ever toggles 0<->1 is a per-item boolean
             * ("is highlighted"), not a selection index. Say so rather than
             * letting it crowd the list. */
            int boolish = (g_seen[i].last_from <= 1 && g_seen[i].last_to <= 1);
            log_line("hunt:   %d/%d hunts  %p +0x%03x  last %u -> %u%s",
                     g_seen[i].hits, g_hunt_no, (void *)g_seen[i].page,
                     g_seen[i].off, g_seen[i].last_from, g_seen[i].last_to,
                     boolish ? "   (0/1 toggle -- a flag, not an index)"
                             : "   ⭐ INDEX-SHAPED");
            shown++;
        }
    }
    if (shown == 0)
        log_line("hunt:   (nothing recurred yet -- run another hunt)");
}

static hd_snapshot g_h[4];           /* hashes, all pages, 4 stages */
static BYTE      (*g_hbytes)[HD_BLOCK];   /* content, interesting pages only */
static uintptr_t  *g_haddr;
static int         g_hn;             /* how many pages have content */
static int         g_hstage;

/* Content is kept for a page in every stage, so the array is stage-major. */
static BYTE *hunt_page(int stage, int i)
{
    return g_hbytes[(SIZE_T)stage * HUNT_MAX_PAGES + i];
}

static BOOL hunt_alloc(void)
{
    if (!g_hbytes) {
        g_hbytes = (BYTE (*)[HD_BLOCK])VirtualAlloc(
            NULL, (SIZE_T)4 * HUNT_MAX_PAGES * HD_BLOCK,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        g_haddr = (uintptr_t *)VirtualAlloc(
            NULL, (SIZE_T)HUNT_MAX_PAGES * sizeof(uintptr_t),
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
    for (int s = 0; s < 4; s++) {
        if (!g_h[s].block)
            g_h[s].block = (hd_block *)VirtualAlloc(
                NULL, (SIZE_T)HD_MAX_BLOCKS * sizeof(hd_block),
                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!g_h[s].block) return FALSE;
    }
    return g_hbytes && g_haddr;
}

/* Read the current bytes of the pages we are tracking into `stage`. */
static void hunt_fill_content(int stage)
{
    for (int i = 0; i < g_hn; i++) {
        if (mem_readable_run((const void *)g_haddr[i], HD_BLOCK) < HD_BLOCK)
            continue;
        volatile BOOL faulted;
        MEM_GUARDED(faulted, memcpy(hunt_page(stage, i),
                                    (const void *)g_haddr[i], HD_BLOCK));
        (void)faulted;
    }
}

/* Pages whose hash differs between two stages, appended to the content set. */
static void hunt_collect(int sa, int sb)
{
    int i = 0, j = 0;
    while (i < g_h[sa].nblocks && j < g_h[sb].nblocks &&
           g_hn < HUNT_MAX_PAGES) {
        uintptr_t xa = g_h[sa].block[i].addr, xb = g_h[sb].block[j].addr;
        if (xa < xb) { i++; continue; }
        if (xa > xb) { j++; continue; }
        if (g_h[sa].block[i].hash != g_h[sb].block[j].hash) {
            int dup = 0;
            for (int k = 0; k < g_hn; k++)
                if (g_haddr[k] == xa) { dup = 1; break; }
            if (!dup) g_haddr[g_hn++] = xa;
        }
        i++; j++;
    }
}

void hd_hunt(void)
{
    if (!hunt_alloc()) {
        log_line("hunt: allocation failed.");
        return;
    }

    switch (g_hstage) {
    case 0:
        g_hn = 0;
        hd_capture(&g_h[0], "H1");
        log_line("hunt: === press 1 of 5: BASELINE captured. ===");
        log_line("hunt: NEXT: press again WITHOUT touching anything. That "
                 "control is what separates real state from the ~1500 pages "
                 "that change every frame on their own.");
        speech_say(L"Baseline. Touch nothing, then press again.", FALSE);
        g_hstage = 1;
        return;

    case 1:
        hd_capture(&g_h[1], "H2");
        /* Everything that moved with nothing touched is noise -- but we still
         * need its CONTENT, to subtract noisy FIELDS later, not just pages. */
        /* Nothing to store yet: the interesting pages are the ones that will
         * react at press 3, and reversibility is judged from HASHES, so no
         * baseline bytes are needed. */
        {
            hd_snapshot *a = &g_h[0], *b = &g_h[1];
            log_line("hunt: === press 2 of 5: CONTROL captured. %d page(s) "
                     "changed on their own (that is the noise floor). ===",
                     hd_changed(a, b, NULL, 0));
        }
        log_line("hunt: NEXT: move the cursor DOWN exactly ONE item, then "
                 "press.");
        speech_say(L"Control done. Move down one item, then press.", FALSE);
        g_hstage = 2;
        return;

    case 2: {
        hd_capture(&g_h[2], "H3");
        /* The pages that REACTED to the move are the only ones that can hold
         * the answer, and they are knowable now. Store their current bytes:
         * this is the "after the move" state. */
        g_hn = 0;
        hunt_collect(1, 2);
        /* Drop pages that also changed during the CONTROL. Those tick on
         * their own, so anything they show across a move is unattributable.
         * This is the page-level half of the control; the field-level half is
         * the before/after comparison at the verdict. */
        {
            int w = 0;
            for (int i = 0; i < g_hn; i++) {
                unsigned h0 = 0, h1 = 0;
                for (int k = 0; k < g_h[0].nblocks; k++)
                    if (g_h[0].block[k].addr == g_haddr[i])
                        { h0 = g_h[0].block[k].hash; break; }
                for (int k = 0; k < g_h[1].nblocks; k++)
                    if (g_h[1].block[k].addr == g_haddr[i])
                        { h1 = g_h[1].block[k].hash; break; }
                if (h0 && h1 && h0 == h1)
                    g_haddr[w++] = g_haddr[i];
            }
            log_line("hunt: %d page(s) reacted to the move; %d of them were "
                     "quiet in the control and are kept.", g_hn, w);
            g_hn = w;
        }
        log_line("hunt: === press 3 of 5: MOVE captured, tracking %d page(s). "
                 "===", g_hn);
        log_line("hunt: NEXT: move the cursor BACK UP one item, then press. "
                 "State returns to its old value; a counter keeps climbing.");
        speech_say(L"Move captured. Move back up one item, then press.",
                   FALSE);
        g_hstage = 3;
        return;
    }

    case 3: {
        /* ⚠⚠ WHY THERE IS A FIFTH PRESS.
         *
         * Page-level reversibility (hash at press 2 == hash at press 4) is far
         * too strict: a page holding the selection index will almost certainly
         * also hold something that ticks, and one noisy neighbour then throws
         * the whole page away. The harness proved it -- a planted index and a
         * planted counter on the SAME page gave "0 returned, 176 rejected".
         * Reversibility has to be judged per FIELD, which needs real bytes
         * from before and after a move.
         *
         * But at press 3 the pre-move bytes are already gone, and storing
         * baselines for all ~400k pages is 1.5 GB.
         *
         * The way out: the pages that react to a cursor move are the SAME
         * pages every time. Press 3 identifies them; presses 4 and 5 then
         * bracket a SECOND move with real content. Five presses, and the test
         * is exact rather than approximate. */
        hunt_fill_content(3);          /* bytes BEFORE the second move */
        log_line("hunt: === press 4 of 5: pre-move bytes captured for %d "
                 "tracked page(s). ===", g_hn);
        log_line("hunt: NEXT: move the cursor ONE item again (either way), "
                 "then press. That second move is what gets measured, field "
                 "by field.");
        speech_say(L"Ready. Move the cursor one item, then press.", FALSE);
        g_hstage = 4;
        return;
    }

    case 4: {
        /* Content AFTER the second move, in slot 2 (reused). */
        hunt_fill_content(2);

        /* A page that changed on the move and came back is where state lives.
         * Compare CONTENT between stage 2 (after the move) and stage 3 (after
         * moving back) at field granularity. */
        int idx = 0, counters = 0, returned = 0;
        g_hunt_no++;
        log_line("hunt: === press 5 of 5: VERDICT (hunt %d) ===", g_hunt_no);
        for (int i = 0; i < g_hn; i++) {
            /* Per-FIELD comparison across a real move: slot 3 holds the bytes
             * before it, slot 2 the bytes after. No page-level gate -- a page
             * is not disqualified because something else on it also moved. */
            const BYTE *b = hunt_page(3, i);   /* BEFORE the move */
            const BYTE *m = hunt_page(2, i);   /* AFTER the move  */
            for (SIZE_T off = 0; off + 4 <= HD_BLOCK; off += 4) {
                unsigned vb, vm;
                memcpy(&vb, b + off, 4);
                memcpy(&vm, m + off, 4);
                if (vb == vm) continue;

                returned++;
                int d = (int)(vm - vb);
                int looks_float = (vb >= 0x38000000u && vb <= 0x4A000000u) ||
                                  (vb >= 0xB8000000u && vb <= 0xCA000000u);
                if ((d == 1 || d == -1) && vb < 4096 && vm < 4096 &&
                    !looks_float) {
                    idx++;
                    seen_record(g_haddr[i], (unsigned)off, vb, vm);
                    if (idx <= 32) {
                        /* The page's hash proved it returned, so vb is both
                         * the before and the after-back value. */
                        log_line("hunt:   ⭐⭐ %p +0x%03llx  %u -> %u -> %u  "
                                 "(delta %+d, RETURNED) -- INDEX CANDIDATE",
                                 (void *)g_haddr[i],
                                 (unsigned long long)off, vb, vm, vb, d);
                        if (g_ntrack < HD_TRACK_MAX) {
                            g_track[g_ntrack].addr = g_haddr[i] + off;
                            g_track[g_ntrack].n = 0;
                            g_ntrack++;
                        }
                    }
                } else {
                    counters++;
                }
            }
        }

        log_line("hunt: %d field(s) changed across the move on pages that were "
                 "QUIET in the control; %d are small and stepped by ONE "
                 "(index candidates), %d are floats, pointers or larger "
                 "jumps.", returned, idx, counters);

        /* The cross-hunt view: this is what actually identifies the field. */
        seen_report();

        /* Arm the span scan on this hunt's pages. It watches EVERY field in
         * them, which only became affordable once the hunt narrowed the space
         * -- sampling all of memory covers 256 KB of 1.5 GB and is useless. */
        hd_span_arm();
        if (idx == 0)
            log_line("hunt: ⚠ no index candidate. Either the cursor moved "
                     "more than one item, or the index is not a small u32 "
                     "here. Repeat the hunt.");
        else
            log_line("hunt: the candidates are latched -- press the TRACK key "
                     "after each cursor move to confirm one walks by one and "
                     "reverses.");

        {
            wchar_t msg[160];
            /* Count fields that have now recurred -- the number that matters. */
            int recur = 0;
            for (int i = 0; i < g_nseen; i++)
                if (g_seen[i].hits >= 2) recur++;
            if (g_hunt_no >= 2)
                _snwprintf(msg, 159, L"Hunt %d done. %d candidates, %d seen in "
                                     L"more than one hunt. See the log.",
                           g_hunt_no, idx, recur);
            else
                _snwprintf(msg, 159, L"Hunt 1 done. %d candidates. Run the "
                                     L"hunt again to see which recur.", idx);
            msg[159] = L'\0';
            speech_say(msg, FALSE);
        }
        g_hstage = 0;
        return;
    }
    }
}

/* --- Finding a stable path to a candidate --------------------------------
 * See heapdiff.h: the address changes every launch, so a chain rooted in the
 * exe image (ASLR off, no relocations) is what makes the field re-findable. */

#define PATH_MAX_HITS 64

/* Is `v` inside the loaded exe image? Those addresses are stable forever. */
static BOOL in_image(uintptr_t v, uintptr_t *base_out, SIZE_T *size_out)
{
    static uintptr_t base;
    static SIZE_T    size;
    if (!base) {
        HMODULE m = GetModuleHandleW(NULL);
        if (!m) return FALSE;
        IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)m;
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)m + dos->e_lfanew);
        base = (uintptr_t)m;
        size = nt->OptionalHeader.SizeOfImage;
    }
    if (base_out) *base_out = base;
    if (size_out) *size_out = size;
    return v >= base && v < base + size;
}

/* Find addresses holding a pointer into [lo, hi). Returns how many were found;
 * fills `out` up to `cap`. */
static int find_refs(uintptr_t lo, uintptr_t hi, uintptr_t *out, int cap,
                     int *in_image_count)
{
    static BYTE buf[HD_CHUNK];
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t maxa = (uintptr_t)si.lpMaximumApplicationAddress;
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    uintptr_t stack_lo = (uintptr_t)tib->StackLimit;
    uintptr_t stack_hi = (uintptr_t)tib->StackBase;
    int n = 0;

    while (addr < maxa) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) break;
        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        SIZE_T rsize = mbi.RegionSize;
        uintptr_t next = base + rsize;
        if (next <= addr) break;

        /* Unlike the hunt, READ-ONLY regions matter here: a static pointer in
         * the exe's .rdata is exactly what we are hoping to find. */
        BOOL ok = mbi.State == MEM_COMMIT &&
                  !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                  (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE |
                                  PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                  PAGE_EXECUTE_READWRITE));
        if (!ok || (base < stack_hi && next > stack_lo)) {
            addr = next;
            continue;
        }

        for (volatile SIZE_T off = 0; off + 8 <= rsize; off += HD_CHUNK) {
            SIZE_T want = rsize - off;
            if (want > HD_CHUNK) want = HD_CHUNK;
            want &= ~(SIZE_T)7;
            if (!want) break;
            volatile BOOL faulted;
            MEM_GUARDED(faulted, memcpy(buf, (const void *)(base + off), want));
            if (faulted) continue;
            for (SIZE_T k = 0; k + 8 <= want; k += 8) {
                uintptr_t v;
                memcpy(&v, buf + k, 8);
                if (v < lo || v >= hi) continue;
                uintptr_t site = base + off + k;
                /* ⚠ KEEP THE IN-IMAGE HITS, NOT JUST THE FIRST N.
                 *
                 * The first version counted every in-image hit but stored only
                 * the first `cap` hits OVERALL. In-game that meant 137 hits of
                 * which 6 were static -- and all 6 sat past index 64, so the
                 * summary said "6 inside the exe image" while the detail loop
                 * printed nothing. The count and the array disagreed, which is
                 * the worst kind of bug: the headline was right and the
                 * actionable part was missing.
                 *
                 * Static hits are the whole point, so they get priority: they
                 * are always stored, displacing an ordinary hit if need be. */
                BOOL stat = in_image(site, NULL, NULL);
                if (stat && in_image_count)
                    (*in_image_count)++;
                if (n < cap) {
                    out[n] = site;
                } else if (stat) {
                    /* Full: overwrite a non-static entry rather than drop a
                     * static one. */
                    for (int r = 0; r < cap; r++) {
                        if (!in_image(out[r], NULL, NULL)) {
                            out[r] = site;
                            break;
                        }
                    }
                }
                n++;
            }
        }
        addr = next;
    }
    return n;
}

void hd_find_path(void)
{
    /* Use the strongest cross-hunt candidate: most hits, index-shaped. */
    int best = -1, best_hits = 0;
    for (int i = 0; i < g_nseen; i++) {
        int boolish = g_seen[i].last_from <= 1 && g_seen[i].last_to <= 1;
        if (boolish) continue;
        if (g_seen[i].hits > best_hits) { best_hits = g_seen[i].hits; best = i; }
    }
    if (best < 0) {
        log_line("path: no index-shaped candidate yet -- run the hunt first.");
        speech_say(L"No candidate yet. Run the hunt first.", FALSE);
        return;
    }
    if (!mem_guard_install()) return;

    uintptr_t page = g_seen[best].page;
    uintptr_t field = page + g_seen[best].off;
    uintptr_t ibase = 0; SIZE_T isize = 0;
    in_image(0, &ibase, &isize);

    log_line("path: hunting a stable route to %p +0x%03x (seen in %d hunts). "
             "Exe image is %p..%p -- ASLR is off and there are no "
             "relocations, so anything in there is valid on EVERY launch.",
             (void *)page, g_seen[best].off, g_seen[best].hits,
             (void *)ibase, (void *)(ibase + isize));

    /* Level 1: who points at this page? */
    static uintptr_t lvl1[PATH_MAX_HITS];
    int in_img = 0;
    DWORD t0 = GetTickCount();
    int n1 = find_refs(page, page + HD_BLOCK, lvl1, PATH_MAX_HITS, &in_img);
    log_line("path: level 1 -- %d location(s) hold a pointer into the "
             "candidate page (%d of them inside the exe image), in %lu ms.",
             n1, in_img, (unsigned long)(GetTickCount() - t0));

    if (in_img > 0) {
        /* Iterate the STORED entries, not the total hit count: n1 counts every
         * hit found, which can far exceed what the array holds. */
        int stored = n1 < PATH_MAX_HITS ? n1 : PATH_MAX_HITS;
        int printed = 0;
        for (int i = 0; i < stored; i++) {
            if (!in_image(lvl1[i], NULL, NULL)) continue;
            printed++;
            uintptr_t v = 0;
            mem_copy(&v, (const void *)lvl1[i], 8);
            log_line("path:   ⭐⭐ STATIC %p (image+0x%llx) -> %p   "
                     "field would be [that] + 0x%llx",
                     (void *)lvl1[i],
                     (unsigned long long)(lvl1[i] - ibase), (void *)v,
                     (unsigned long long)(field - v));
        }
        /* Never let the headline claim more than the detail showed. */
        if (printed < in_img)
            log_line("path: ⚠ %d static hit(s) counted but only %d could be "
                     "listed -- the capture array is full.", in_img, printed);
        log_line("path: a STATIC location points at the candidate. That is a "
                 "one-hop path and it is stable across launches -- read the "
                 "pointer at image+offset, add the field offset, done.");
        speech_say(L"Found a static path. See the log.", FALSE);
        return;
    }

    if (n1 == 0) {
        log_line("path: nothing points at this page at all. It may be reached "
                 "by computation rather than by a stored pointer, in which "
                 "case a struct signature or a code hook is needed instead.");
        speech_say(L"No pointer to the candidate found.", FALSE);
        return;
    }

    /* Level 2: who points at the pointers? */
    int in_img2 = 0, total2 = 0;
    for (int i = 0; i < n1 && i < 16; i++) {
        static uintptr_t lvl2[PATH_MAX_HITS];
        int hits = find_refs(lvl1[i] & ~(uintptr_t)0xFFF,
                             (lvl1[i] & ~(uintptr_t)0xFFF) + HD_BLOCK,
                             lvl2, PATH_MAX_HITS, &in_img2);
        total2 += hits;
        for (int j = 0; j < hits && j < PATH_MAX_HITS; j++) {
            if (!in_image(lvl2[j], NULL, NULL)) continue;
            log_line("path:   ⭐⭐ STATIC %p (image+0x%llx) -> %p -> page %p",
                     (void *)lvl2[j],
                     (unsigned long long)(lvl2[j] - ibase),
                     (void *)lvl1[i], (void *)page);
        }
    }
    log_line("path: level 2 -- %d location(s) point at those pointers, %d "
             "inside the image.", total2, in_img2);
    if (in_img2 == 0)
        log_line("path: no static root within two hops. Either the chain is "
                 "longer, or the struct is reached from a register/TLS and a "
                 "signature scan is the better route.");

    {
        wchar_t msg[128];
        _snwprintf(msg, 127, L"Path search done. %d static roots found.",
                   in_img + in_img2);
        msg[127] = L'\0';
        speech_say(msg, FALSE);
    }
}

/* --- Verifying a static path --------------------------------------------
 * See heapdiff.h. These are the roots the path search found in-game. */

/* image-relative offsets of static pointers, and the field offset from each
 * pointer's TARGET. Measured 2026-09-02. */
/* ⚠ All four of these resolved to the SAME address in-game, so they are four
 * copies of one pointer, not four independent confirmations. One is kept as
 * the primary; the others are checked only to notice if they ever diverge. */
static const struct { unsigned long long site_rva; long long field_off;
                      const char *note; } g_paths[] = {
    { 0x1208bd0, 0x128, "primary (lone site, outside the table)" },
    { 0x221ed98, 0x128, "duplicate -- same target in-game"       },
};

/* ⭐ VERIFIED ACROSS LAUNCHES 2026-09-02:
 *     session A: image+0x1208bd0 -> 0x048206A0
 *     session B: image+0x1208bd0 -> 0x04882270   (target moved, path held)
 * 4 of 4 paths resolved to a readable field on a fresh launch. The per-launch
 * reallocation problem is solved: a fixed image-relative offset finds the
 * object on any run. What remains is whether +0x128 is the SELECTION. */

void hd_verify_path(void)
{
    static unsigned prev[4];
    static int      have_prev;
    uintptr_t ibase = 0;
    SIZE_T isize = 0;
    if (!in_image(0, &ibase, &isize) && !ibase) {
        log_line("verify: cannot locate the exe image.");
        return;
    }
    if (!mem_guard_install()) return;

    log_line("verify: === following the recorded static paths ===");
    int ok = 0, moved = 0;
    for (int i = 0; i < (int)(sizeof(g_paths) / sizeof(g_paths[0])); i++) {
        uintptr_t site = ibase + (uintptr_t)g_paths[i].site_rva;
        uintptr_t target = 0;
        if (!mem_copy(&target, (const void *)site, 8)) {
            log_line("verify:   image+0x%llx  UNREADABLE",
                     g_paths[i].site_rva);
            continue;
        }
        if (target < 0x10000) {
            log_line("verify:   image+0x%llx -> %p  (null/!valid) [%s]",
                     g_paths[i].site_rva, (void *)target, g_paths[i].note);
            continue;
        }
        uintptr_t field = target + (uintptr_t)g_paths[i].field_off;
        unsigned v = 0;
        if (!mem_copy(&v, (const void *)field, 4)) {
            log_line("verify:   image+0x%llx -> %p, field %p UNREADABLE",
                     g_paths[i].site_rva, (void *)target, (void *)field);
            continue;
        }
        ok++;
        /* Report the value AND whether it changed since the last press --
         * "the pointer resolved" and "this is the selection" are different
         * claims, and only the second one matters. */
        const char *delta = "";
        char dbuf[48];
        if (have_prev && prev[i] != v) {
            _snprintf(dbuf, sizeof(dbuf), "   CHANGED %u -> %u (%+d)",
                      prev[i], v, (int)(v - prev[i]));
            delta = dbuf;
            moved++;
        } else if (have_prev) {
            delta = "   (unchanged)";
        }
        prev[i] = v;
        log_line("verify:   image+0x%llx -> %p + 0x%llx = %p  value %u%s  [%s]",
                 g_paths[i].site_rva, (void *)target,
                 (unsigned long long)g_paths[i].field_off,
                 (void *)field, v, delta, g_paths[i].note);
    }
    have_prev = 1;

    log_line("verify: %d of %d path(s) resolved to a readable field%s. "
             "⚠ Resolving is NOT proof -- press again after moving the cursor "
             "one item and require the value to STEP BY ONE.",
             ok, (int)(sizeof(g_paths) / sizeof(g_paths[0])),
             moved ? "" : "");
    {
        wchar_t msg[128];
        _snwprintf(msg, 127, L"%d of %d paths resolved. Move the cursor and "
                             L"press again.", ok,
                   (int)(sizeof(g_paths) / sizeof(g_paths[0])));
        msg[127] = L'\0';
        speech_say(msg, FALSE);
    }
}

/* ⚠ WATCH A WINDOW, NOT ONE FIELD.
 *
 * The field at +0x128 stepped by +-1 and reversed -- real state, not a counter.
 * But it only ever held 0, 1 or 2, and CFC2's menus have more entries than
 * that. So it is either the item cursor (and the player only used three items)
 * or a 3-state field next to it: a tab, a page, a sub-menu selector.
 *
 * Rather than ask the user to interpret that, watch the whole object and
 * report each field's RANGE. The one whose range matches the item count is the
 * item cursor; a neighbour that tops out at 2 is a tab. This also finds the
 * index if +0x128 turns out to be the wrong offset by a few bytes. */
#define POLL_WIN_OFF   0x00      /* from the target pointer          */
#define POLL_WIN_LEN   0x200     /* 512 bytes around the known field */
#define POLL_NFIELDS   (POLL_WIN_LEN / 4)

/* Per-run range state, at file scope so the idle path can flush a finished
 * run -- otherwise the LAST run of a session is never reported. */
static DWORD    g_run_tick;
static unsigned g_run_min, g_run_max;
static int      g_run_n;

void hd_poll_path(void)
{
    static unsigned last;
    static int      have;
    static int      logged;
    static int      changes;

    /* Per-field history across the watched window. */
    static unsigned wlast[POLL_NFIELDS];
    static unsigned wmin[POLL_NFIELDS], wmax[POLL_NFIELDS];
    static int      wmoves[POLL_NFIELDS];
    static int      whave;

    uintptr_t ibase = 0;
    SIZE_T isize = 0;
    in_image(0, &ibase, &isize);
    if (!ibase) return;

    uintptr_t target = 0;
    if (!mem_copy(&target, (const void *)(ibase + g_paths[0].site_rva), 8))
        return;
    if (target < 0x10000) {
        /* The pointer is null before the shell builds its menu; that is not a
         * failure, so say nothing and try again next tick. */
        return;
    }
    unsigned v = 0;
    if (!mem_copy(&v, (const void *)(target + g_paths[0].field_off), 4))
        return;

    if (!have) {
        have = 1;
        last = v;
        log_line("poll: watching [image+0x%llx] + 0x%llx = %p, value %u. "
                 "Navigate the menus -- every CHANGE is logged, so no "
                 "keypresses are needed to answer whether this is the "
                 "selection.",
                 g_paths[0].site_rva,
                 (unsigned long long)g_paths[0].field_off,
                 (void *)(target + g_paths[0].field_off), v);
        return;
    }
    if (v == last) {
        /* ⚠ Flush a finished run from the IDLE path too. Reporting only on the
         * next change means the LAST run of a session -- often the informative
         * one -- is never reported at all. */
        if (g_run_n && GetTickCount() - g_run_tick > 4000) {
            log_line("poll: RUN ENDED -- %d change(s), range %u..%u "
                     "(span %u). A run is one uninterrupted navigation "
                     "session, so this span should match that screen's item "
                     "count.",
                     g_run_n, g_run_min, g_run_max,
                     g_run_max - g_run_min + 1);
            g_run_n = 0;
        }
        return;
    }

    changes++;
    int d = (int)(v - last);

    /* ⭐⭐ SPEAK EVERY CHANGE.
     *
     * Asking the user to stand on a known item and press a key kept failing --
     * not through carelessness, but because the protocol asks a blind player
     * to hold "which item am I on" and "which key answers that" in mind while
     * navigating, and to remember it afterwards for me. That is my design
     * fault, not theirs.
     *
     * Speaking the value as it changes removes all of it: they just move, and
     * hear the number alongside the item. If the numbers track the menu, we
     * both know at once; if they do not, that is equally obvious. Speech is
     * non-blocking and interruptible, so a fast walk simply overwrites. */
    {
        wchar_t s[32];
        _snwprintf(s, 31, L"%u", v);
        s[31] = L'\0';
        speech_say(s, TRUE);       /* TRUE = interrupt: keep up with the cursor */
    }
    /* Cap the log: if this turns out to be a fast-changing field we do not
     * want it filling the file, and the first few dozen changes settle the
     * question either way. */
    if (logged < 60) {
        logged++;
        log_line("poll: value %u -> %u  (%+d)%s", last, v, d,
                 (d == 1 || d == -1) ? "   <- stepped by ONE" : "");
    } else if (logged == 60) {
        logged++;
        log_line("poll: (further changes not logged)");
    }
    last = v;

    /* --- The window: track every field's range and how often it moves ---- */
    static BYTE win[POLL_WIN_LEN];
    if (mem_readable_run((const void *)(target + POLL_WIN_OFF),
                         POLL_WIN_LEN) >= POLL_WIN_LEN) {
        volatile BOOL faulted;
        MEM_GUARDED(faulted, memcpy(win, (const void *)(target + POLL_WIN_OFF),
                                    POLL_WIN_LEN));
        if (!faulted) {
            for (int i = 0; i < POLL_NFIELDS; i++) {
                unsigned x;
                memcpy(&x, win + i * 4, 4);
                if (!whave) {
                    wlast[i] = wmin[i] = wmax[i] = x;
                    continue;
                }
                if (x != wlast[i]) {
                    wmoves[i]++;
                    wlast[i] = x;
                }
                if (x < wmin[i]) wmin[i] = x;
                if (x > wmax[i]) wmax[i] = x;
            }
            whave = 1;
        }
    }

    /* ⭐ PER-RUN RANGE, so the screen need not be identified.
     *
     * What matters is the range a field reaches WHILE THE PLAYER IS ON ONE
     * SCREEN. Naming the screen is impossible (route 2 is closed: all 25
     * layouts stay resident), but a "run" is derivable without it: a burst of
     * changes with no long pause is one navigation session. An 8-item list
     * should span ~0..7 in a run; a 3-item dialog only 0..2.
     *
     * This removes the last thing the user had to report. */
    {
        DWORD now = GetTickCount();
        if (g_run_n && now - g_run_tick > 4000) {
            log_line("poll: RUN ENDED -- %d change(s), range %u..%u (span %u). "
                     "A run is one uninterrupted navigation session, so this "
                     "span should match that screen's item count.",
                     g_run_n, g_run_min, g_run_max,
                     g_run_max - g_run_min + 1);
            g_run_n = 0;
        }
        if (!g_run_n) { g_run_min = g_run_max = v; }
        if (v < g_run_min) g_run_min = v;
        if (v > g_run_max) g_run_max = v;
        g_run_n++;
        g_run_tick = now;
    }

    /* Summarise periodically: the field whose RANGE matches the menu's item
     * count is the item cursor. A neighbour topping out at 2 is a tab. */
    if (changes % 12 == 0) {
        log_line("poll: --- fields that moved, by range (window %p +0x%x) ---",
                 (void *)(target + POLL_WIN_OFF), POLL_WIN_LEN);
        int shown = 0;
        for (int i = 0; i < POLL_NFIELDS && shown < 12; i++) {
            /* Small, bounded, and actually moving: index-shaped. */
            if (!wmoves[i] || wmax[i] > 64)
                continue;
            shown++;
            log_line("poll:    +0x%03x  range %u..%u  (%d change%s)%s",
                     POLL_WIN_OFF + i * 4, wmin[i], wmax[i], wmoves[i],
                     wmoves[i] == 1 ? "" : "s",
                     (POLL_WIN_OFF + i * 4) == (int)g_paths[0].field_off
                         ? "   <- the known field" : "");
        }
        if (!shown)
            log_line("poll:    (no small bounded field has moved yet)");
    }
}

void hd_say_value(void)
{
    uintptr_t ibase = 0;
    SIZE_T isize = 0;
    in_image(0, &ibase, &isize);
    if (!ibase || !mem_guard_install()) return;

    uintptr_t target = 0;
    if (!mem_copy(&target, (const void *)(ibase + g_paths[0].site_rva), 8) ||
        target < 0x10000) {
        speech_say(L"Pointer not ready.", FALSE);
        log_line("say: pointer at image+0x%llx is not usable yet.",
                 g_paths[0].site_rva);
        return;
    }

    /* Speak the primary field plus the other movers, so one press covers all
     * the candidates rather than needing a key each. */
    static const int offs[] = { 0x128, 0x0d0, 0x140, 0x1d0, 0x1d4, 0x1e8 };
    unsigned v[6];
    int n = 0;
    for (int i = 0; i < 6; i++) {
        unsigned x = 0;
        if (mem_copy(&x, (const void *)(target + offs[i]), 4))
            v[n++] = x;
        else
            v[n++] = 0xFFFFFFFFu;
    }

    log_line("say: +0x128=%u  +0x0d0=%u  +0x140=%u  +0x1d0=%u  +0x1d4=%u  "
             "+0x1e8=%u   (target %p)",
             v[0], v[1], v[2], v[3], v[4], v[5], (void *)target);

    /* Speak only the primary and the wide-range one: a spoken list of six
     * numbers is not usable, and these two are the live hypotheses. */
    wchar_t msg[128];
    _snwprintf(msg, 127, L"%u, and %u", v[0], v[4]);
    msg[127] = L'\0';
    speech_say(msg, TRUE);
}

/* --- Span scan ----------------------------------------------------------
 * See heapdiff.h: rank by DISTINCT VALUE COUNT, which is the one property a
 * blink flag cannot fake and a selection index must have. */

/* 2048 pages at 20 bytes of state each = 40 MB, which covers every hunt
 * observed so far (273..2262 pages) exhaustively. The earlier 256-page budget
 * reached only 13% and said so in the log. */
/* 3072 pages: the last run covered 2048 of 2084 and just missed full
 * coverage. 60 MB, and the sweep is page-bulk now so the cost is in reads,
 * not syscalls. */
#define SPAN_SLOTS    3145728   /* addresses watched */
/* The bitmap covers values 0..31, which is where a menu index lives; anything
 * outside disqualifies the field outright. */
#define SPAN_DISTINCT 32

/* ⭐ SHRUNK FROM 64 TO 20 BYTES so every hunt page fits.
 *
 * The first version remembered 12 exact values per slot (64 B), which covered
 * only 256 of 1898 hunt pages -- 13%, and the log said so. But the report only
 * ever prints COUNT, MIN and MAX, so storing the values themselves bought
 * nothing. A tiny bitmap over 0..31 gives the distinct count for exactly the
 * range an index lives in, and anything outside that is disqualifying anyway.
 *
 * 20 bytes x 1898 pages x 1024 fields = 37 MB: the whole hunt, exhaustively. */
typedef struct {
    uintptr_t addr;
    unsigned  mask;             /* bit v set for each value v in 0..31 seen */
    unsigned short mn, mx;
    unsigned char  nseen;       /* popcount(mask) */
    unsigned char  overflow;    /* saw a value outside 0..31: not an index */
    /* ⚠ THE SAME, FOR THE LOW BYTE. A u8 index of value 3 whose neighbouring
     * bytes are nonzero reads as a u32 far above 31, so the u32 view
     * disqualifies it on the first sample and never looks again. A u16 index
     * has the same problem. Since a little-endian u8/u16 index occupies the
     * low bits of the same dword, tracking the low byte in parallel covers
     * both widths for one extra compare and no extra memory read. */
    unsigned  bmask;
    unsigned char bmn, bmx, bnseen, bover;
} span_slot;

/* 16 MB: allocated, not static, so it does not bloat the DLL's BSS. */
static span_slot *g_span;
static int   g_nspan;
static DWORD g_span_last;
/* When the scan armed, so the first report waits for real sampling rather
 * than firing instantly against a zero timestamp. */
static DWORD g_span_armed_tick;
static int   g_span_reports;

/* Watch the object the verified static path leads to, but WIDE: the index may
 * be well outside the 0x200 window that only ever showed 2-state fields. */
/* ⛔ THE STATIC-PATH OBJECT HOLDS RENDERING DATA, NOT THE SELECTION.
 *
 * The first span scan watched target±0x2000 and the top fields were
 * 225..274, 368..752, 544..736, 71..128 -- pixel coordinates and sizes. A menu
 * index looks like 0..N-1 and STARTS AT ZERO; not one of these came near 0.
 * The only 0-adjacent fields were the same 1..3 chrome already rejected.
 *
 * So the search must widen beyond that object, and gain the constraint that
 * separates an index from a coordinate: **an index starts at or near 0.**
 * Combined with span >= 4 that is a very tight filter.
 *
 * Sampling every writable page at 10 Hz is far too much, so this samples a
 * STRIDED subset -- one field per 64 bytes. A selection index is a naturally
 * aligned int in a small struct, so a 64-byte grid has a good chance of
 * landing on it, and the coverage is stated honestly in the log rather than
 * implied. */
/* ⛔⛔ SAMPLING ALL OF MEMORY CANNOT WORK -- and the first attempt did not
 * even do that. 4096 slots at one per 64 bytes covers 256 KB, so the scan
 * watched the first 256 KB it met (CRT/loader heap, not game state) and
 * nothing else, while the log claimed to be sampling "writable memory" and
 * printed "0 MB". Deriving the stride from the total instead would put samples
 * ~384 KB apart -- far too sparse to land on a 4-byte index.
 *
 * ⭐ The answer is not to sample thinner but to NARROW THE SPACE FIRST, and
 * the hunt already does exactly that: it reports the PAGES that react to a
 * cursor move and are quiet in the control. A few hundred pages is small
 * enough to watch EVERY field in, with no stride and no coverage caveat.
 *
 * So the span scan now watches the hunt's surviving pages. Run a hunt first;
 * without one there is nothing to narrow to, and it says so. */
void hd_span_arm(void)
{
    if (!mem_guard_install()) return;

    if (!g_span) {
        g_span = (span_slot *)VirtualAlloc(
            NULL, (SIZE_T)SPAN_SLOTS * sizeof(span_slot),
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!g_span) {
            log_line("span: could not allocate %llu MB of slot state.",
                     (unsigned long long)((SIZE_T)SPAN_SLOTS *
                                          sizeof(span_slot) / 1048576));
            return;
        }
    }

    if (g_hn <= 0) {
        log_line("span: no hunt pages to watch. Run the hunt (F6 x5) first -- "
                 "it reports the pages that react to a cursor move, which is "
                 "what makes an exhaustive field scan affordable. Sampling all "
                 "of memory instead cannot work: 4096 slots covers 256 KB of "
                 "1.5 GB.");
        g_nspan = 0;
        return;
    }

    g_nspan = 0;
    int pages = 0;
    for (int p = 0; p < g_hn && g_nspan < SPAN_SLOTS; p++) {
        if (mem_readable_run((const void *)g_haddr[p], HD_BLOCK) < HD_BLOCK)
            continue;
        pages++;
        for (SIZE_T off = 0; off + 4 <= HD_BLOCK && g_nspan < SPAN_SLOTS;
             off += 4) {
            g_span[g_nspan].addr = g_haddr[p] + off;
            g_span[g_nspan].mask = 0;
            g_span[g_nspan].mn = g_span[g_nspan].mx = 0;
            g_span[g_nspan].nseen = 0;
            g_span[g_nspan].overflow = 0;
            g_span[g_nspan].bmask = 0;
            g_span[g_nspan].bmn = g_span[g_nspan].bmx = 0;
            g_span[g_nspan].bnseen = 0;
            g_span[g_nspan].bover = 0;
            g_nspan++;
        }
    }

    g_span_armed_tick = GetTickCount();
    log_line("span: watching EVERY field in %d hunt page(s) -- %d field(s), "
             "no stride, so a miss here IS meaningful. Ranking by distinct "
             "value count, requiring the field to reach 0 or 1 and to be "
             "DENSE (span ~ count): the previous scan's top hits were "
             "225..274 and 368..752, i.e. pixel coordinates.",
             pages, g_nspan);
    if (g_nspan >= SPAN_SLOTS)
        log_line("span: ⚠ slot limit reached at %d fields (%d of %d pages "
                 "covered) -- raise SPAN_SLOTS or narrow the hunt further.",
                 SPAN_SLOTS, pages, g_hn);
}

void hd_span_tick(void)
{
    if (!g_span || !g_nspan) return;
    DWORD now = GetTickCount();
    if (now - g_span_last < 100) return;      /* 10 Hz is ample */
    g_span_last = now;

    /* ⛔⛔ ONE mem_copy PER FIELD WAS THE BUG. `mem_copy` calls
     * `mem_readable_run`, i.e. a VirtualQuery SYSCALL, so 2,097,152 fields
     * meant ~2M syscalls per tick -- seconds per pass at a 100 ms target
     * rate. The worker sat inside a single tick for the whole session and the
     * report after the loop was NEVER reached, which is why the log showed
     * "span armed" and then nothing however long the user played.
     *
     * This is the identical mistake already fixed in hd_capture (a per-block
     * VirtualQuery, 406k syscalls) and not carried over here.
     *
     * Fix: read a whole PAGE at a time under one guard -- 2048 reads instead
     * of 2M -- and index the fields out of the local copy. */
    static BYTE pbuf[HD_BLOCK];
    uintptr_t cur_page = 0;
    BOOL page_ok = FALSE;

    for (int i = 0; i < g_nspan; i++) {
        if (g_span[i].overflow && g_span[i].bover)
            continue;                      /* both views disqualified */

        uintptr_t page = g_span[i].addr & ~(uintptr_t)(HD_BLOCK - 1);
        if (page != cur_page) {
            cur_page = page;
            page_ok = FALSE;
            if (mem_readable_run((const void *)page, HD_BLOCK) >= HD_BLOCK) {
                volatile BOOL faulted;
                MEM_GUARDED(faulted,
                            memcpy(pbuf, (const void *)page, HD_BLOCK));
                page_ok = !faulted;
            }
        }
        if (!page_ok)
            continue;

        unsigned v;
        memcpy(&v, pbuf + (g_span[i].addr - page), 4);

        /* The low-byte view, which is where a u8 or u16 index lives. */
        if (!g_span[i].bover) {
            unsigned b = v & 0xFF;
            if (b > 31) {
                g_span[i].bover = 1;
            } else {
                unsigned bit = 1u << b;
                if (!(g_span[i].bmask & bit)) {
                    int first = (g_span[i].bmask == 0);
                    g_span[i].bmask |= bit;
                    g_span[i].bnseen++;
                    if (first) { g_span[i].bmn = g_span[i].bmx = (unsigned char)b; }
                    else {
                        if (b < g_span[i].bmn) g_span[i].bmn = (unsigned char)b;
                        if (b > g_span[i].bmx) g_span[i].bmx = (unsigned char)b;
                    }
                }
            }
        }
        if (g_span[i].overflow)
            continue;
        /* A menu index lives in 0..31 in practice. Anything outside is a
         * pointer, float, coordinate or counter -- disqualify it once and
         * never look at it again, which is also what keeps this cheap. */
        if (v > 31) { g_span[i].overflow = 1; continue; }
        unsigned bit = 1u << v;
        if (g_span[i].mask & bit)
            continue;                      /* value already seen */
        /* ⚠ Use the MASK, not the values, to detect the first sample: a
         * first value of 0 would leave mn==mx==0 and a "!mn && !mx" test
         * would then re-initialise on every later sample, pinning mn at 0. */
        int first = (g_span[i].mask == 0);
        g_span[i].mask |= bit;
        g_span[i].nseen++;
        if (first) {
            g_span[i].mn = g_span[i].mx = (unsigned short)v;
        } else {
            if (v < g_span[i].mn) g_span[i].mn = (unsigned short)v;
            if (v > g_span[i].mx) g_span[i].mx = (unsigned short)v;
        }
    }

    /* ⚠ REPORT EARLY AND OFTEN, NOT ON A LONG TIMER.
     *
     * A 15 s interval meant a run that ended shortly after the hunt produced
     * NO report at all -- the log stopped one line after "span armed" and the
     * whole run was wasted. The user was told "then play", never that the
     * duration mattered. A tool must not depend on the user knowing how long
     * to keep it open.
     *
     * First report after 3 s, then every 10 s. */
    /* g_span_armed_tick is set when the scan arms, so the first report waits
     * 3 s of actual sampling rather than firing instantly on a zero
     * timestamp. */
    static DWORD last_report;
    DWORD since = now - (last_report ? last_report : g_span_armed_tick);
    if (since < (DWORD)(last_report ? 10000 : 3000)) return;
    last_report = now;
    g_span_reports++;

    /* ⭐ AN INDEX STARTS AT ZERO AND IS DENSE.
     *
     * The discriminator the previous scan lacked. Its top hits were 225..274,
     * 368..752, 544..736 -- perfectly good multi-valued fields, and every one
     * a pixel coordinate. A selection index over N items takes values
     * 0..N-1: it reaches 0 (or 1), and its span is about equal to its count,
     * because it visits every value rather than jumping. */
    /* Log the tick cost. A scan that silently takes seconds per pass never
     * reaches this report at all -- which is exactly what happened -- and a
     * duration in the log is the only way that regression is visible. */
    log_line("span: --- report %d: INDEX-SHAPED fields (start near 0, dense); "
             "last sweep %lu ms over %d field(s) ---",
             g_span_reports, (unsigned long)(GetTickCount() - now), g_nspan);
    int shown = 0;
    /* Consider BOTH widths: the u32 view and the low-byte view (u8/u16). */
    for (int want = SPAN_DISTINCT; want >= 3 && shown < 12; want--) {
        for (int i = 0; i < g_nspan && shown < 12; i++) {
            for (int w = 0; w < 2 && shown < 12; w++) {
                int over  = w ? g_span[i].bover  : g_span[i].overflow;
                int n     = w ? g_span[i].bnseen : g_span[i].nseen;
                unsigned mn = w ? g_span[i].bmn : g_span[i].mn;
                unsigned mx = w ? g_span[i].bmx : g_span[i].mx;
                if (over || n != want) continue;
                if (mn > 1) continue;             /* must reach 0 or 1 */
                /* Dense: an index visits every value, so span ~= count. */
                if (mx - mn > (unsigned)n + 2) continue;
                shown++;
                log_line("span:   ⭐⭐ %p  %d distinct, range %u..%u  -- INDEX "
                         "CANDIDATE (%s, starts at %u, dense)",
                         (void *)g_span[i].addr, n, mn, mx,
                         w ? "low byte / u8-u16" : "u32", mn);
            }
        }
    }
    if (!shown) {
        log_line("span:   (no field starts near 0 with a dense multi-value "
                 "range yet -- navigate a list with several items)");
        /* ⚠⚠ REPORT THE POPULATION, NOT JUST NEAR MISSES.
         *
         * The first version only listed fields with >= 4 distinct values --
         * so when NOTHING reached 4 it printed nothing at all, exactly when a
         * diagnostic was most needed. "No candidates" and "no field moved at
         * all" then look identical, which is the failure this project keeps
         * paying for.
         *
         * Print the whole histogram instead: how many fields took 1, 2, 3, 4+
         * distinct values, and how many were disqualified. That distinguishes
         * "the index is not here" from "nothing was sampled" from "everything
         * is 2-state chrome". */
        int hist[6] = { 0, 0, 0, 0, 0, 0 };
        int bhist[6] = { 0, 0, 0, 0, 0, 0 };
        int over = 0, quiet = 0, bover = 0;
        for (int i = 0; i < g_nspan; i++) {
            if (g_span[i].overflow) over++;
            else {
                int n = g_span[i].nseen;
                if (!n) quiet++; else hist[n < 5 ? n : 5]++;
            }
            if (g_span[i].bover) bover++;
            else {
                int n = g_span[i].bnseen;
                if (n) bhist[n < 5 ? n : 5]++;
            }
        }
        log_line("span:   u32 view: %d never in 0..31, %d disqualified; "
                 "1 value: %d, 2: %d, 3: %d, 4: %d, 5+: %d",
                 quiet, over, hist[1], hist[2], hist[3], hist[4], hist[5]);
        log_line("span:   low-byte view (u8/u16): %d disqualified; "
                 "1 value: %d, 2: %d, 3: %d, 4: %d, 5+: %d",
                 bover, bhist[1], bhist[2], bhist[3], bhist[4], bhist[5]);
        if (hist[4] + hist[5] == 0)
            log_line("span:   ⚠ NOTHING took 4+ distinct small values. Either "
                     "no long list was traversed, or the index is not a u32 "
                     "in these pages (try u16/u8), or it lives outside the "
                     "hunt's pages entirely.");

        int nmiss = 0;
        for (int i = 0; i < g_nspan && nmiss < 5; i++) {
            if (g_span[i].overflow || g_span[i].nseen < 3) continue;
            unsigned mn = g_span[i].mn, mx = g_span[i].mx;
            nmiss++;
            log_line("span:     (near miss: %p  %d distinct, range %u..%u)",
                     (void *)g_span[i].addr, g_span[i].nseen, mn, mx);
        }
    }
}

/* --- The trial protocol -------------------------------------------------- */

static hd_snapshot g_a1, g_a2, g_b;
static hd_stage    g_stage;

/* --- Candidate persistence ----------------------------------------------
 *
 * ⚠ Candidates lived only in process memory, so every game restart threw away
 * the narrowing. Six trials got the set to 8 pages; the next session began
 * again from 369 and the watch reported noise. Narrowing is the expensive part
 * (each trial costs three captures and careful navigation) -- losing it on
 * exit makes the whole method impractical.
 *
 * Stored as text next to the exe so it can be inspected and deleted by hand.
 * ASLR is off and there are no relocations, so addresses are valid across runs
 * -- that is exactly why this is sound here and would not be elsewhere. Each
 * address is still re-validated on use by the capture itself. */
static void cand_path(char *out, size_t cap)
{
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
    if (!n) { out[0] = 0; return; }
    char *slash = strrchr(out, '\\');
    if (slash) slash[1] = 0;
    strncat(out, "cfc2_candidates.txt", cap - strlen(out) - 1);
}

static void cand_save(void)
{
    char p[MAX_PATH];
    cand_path(p, sizeof(p));
    if (!p[0]) return;
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "# CFC2 heapdiff candidates -- delete this file to start over\n");
    fprintf(f, "trials %d\n", g_trial);
    for (int i = 0; i < g_ncand; i++)
        fprintf(f, "%llx\n", (unsigned long long)g_cand[i]);
    fclose(f);
    log_line("heapdiff: saved %d candidate(s) and trial count %d to "
             "cfc2_candidates.txt -- narrowing now survives a restart.",
             g_ncand, g_trial);
}

static void cand_load(void)
{
    char p[MAX_PATH];
    cand_path(p, sizeof(p));
    if (!p[0]) return;
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[64];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        if (!strncmp(line, "trials ", 7)) { g_trial = atoi(line + 7); continue; }
        unsigned long long v = 0;
        if (sscanf(line, "%llx", &v) == 1 && v &&
            n < (int)(sizeof(g_cand) / sizeof(g_cand[0])))
            g_cand[n++] = (uintptr_t)v;
    }
    fclose(f);
    if (n > 0) {
        g_ncand = n;
        g_have_cand = 1;
        log_line("heapdiff: resumed %d candidate(s) from %d previous trial(s) "
                 "(cfc2_candidates.txt). Further trials INTERSECT with these; "
                 "delete that file to start a fresh experiment.", n, g_trial);
    }
}

void hd_reset(void)
{
    g_stage = HD_WANT_A1;
    g_trial = 0;
    g_ncand = 0;
    g_have_cand = 0;
    /* Reset the WATCH stage too. Leaving it mid-cycle means the next three
     * F7 presses land on the wrong stages -- the user's cursor move happens
     * during what the watch believes is its control window, and the field is
     * then filtered away as noise. Caught offline, but it would have been
     * invisible and maddening in-game. */
    g_wstage = HD_W_WANT_1;
    {   /* Drop the saved set too, or the next run would resume what was
         * just discarded. */
        char p[MAX_PATH];
        cand_path(p, sizeof(p));
        if (p[0]) DeleteFileA(p);
    }
    log_line("heapdiff: experiment reset (saved candidates deleted).");
}

hd_stage hd_stage_now(void) { return g_stage; }

/* Keep only candidates that also appear in `found`. */
static void intersect(const uintptr_t *found, int nfound)
{
    int w = 0;
    for (int i = 0; i < g_ncand; i++) {
        for (int j = 0; j < nfound; j++) {
            if (g_cand[i] == found[j]) { g_cand[w++] = g_cand[i]; break; }
        }
    }
    g_ncand = w;
}

hd_stage hd_step(void)
{
    /* Pick up any saved candidate set on first use, so trials accumulate
     * across game restarts instead of starting over every launch. */
    static int loaded;
    if (!loaded) { loaded = 1; cand_load(); }

    if (!alloc_pools()) {
        log_line("heapdiff: could not allocate snapshot pools.");
        return g_stage;
    }
    g_a1.block = g_pool_a1;
    g_a2.block = g_pool_a2;
    g_b.block  = g_pool_b;

    switch (g_stage) {
    case HD_WANT_A1:
        g_trial++;
        log_line("heapdiff: === trial %d, capture A1 (control baseline); "
                 "this trial's contrast will be a %s ===",
                 g_trial, (g_trial % 2) ? "SCREEN CHANGE" : "CURSOR MOVE");
        if (!hd_capture(&g_a1, "A1"))
            return g_stage;
        g_stage = HD_WANT_A2;
        speech_say(L"Baseline captured. Do not touch anything. "
                   L"Press F6 again.", FALSE);
        log_line("heapdiff: NEXT press = A2, the control repeat. Change "
                 "NOTHING before it -- whatever differs between A1 and A2 is "
                 "noise that must be subtracted.");
        break;

    case HD_WANT_A2: {
        log_line("heapdiff: === trial %d, capture A2 (control repeat) ===",
                 g_trial);
        if (!hd_capture(&g_a2, "A2"))
            return g_stage;
        int noise = hd_changed(&g_a1, &g_a2, NULL, 0);
        log_line("heapdiff: control: %d block(s) changed with NOTHING touched "
                 "-- that is the per-capture noise floor (timers, animation, "
                 "audio, RNG). These are excluded from the result.", noise);
        g_stage = HD_WANT_B;
        /* ⭐ ALTERNATE THE CONTRAST: odd trials change SCREEN, even trials
         * move the CURSOR one item on the SAME screen.
         *
         * Why: intersecting more screen-change trials hits a floor. A page
         * that changes on every screen switch for a reason unrelated to focus
         * -- a render target, an input queue, a scroll animation -- is a TRUE
         * positive for the question asked and useless for the question wanted.
         * More of the same trial cannot remove it.
         *
         * A cursor move is the discriminator: it changes the SELECTION while
         * the screen stays put. State that tracks focus must change for both;
         * state that is merely per-screen changes only on the switch. */
        if (g_trial % 2) {
            speech_say(L"Control captured. Now change to a different screen, "
                       L"then press F6.", FALSE);
            log_line("heapdiff: NEXT press = B. Change to a DIFFERENT SCREEN "
                     "first, then press.");
        } else {
            speech_say(L"Control captured. Now move the cursor one item, "
                       L"staying on this screen, then press F6.", FALSE);
            log_line("heapdiff: NEXT press = B. Move the CURSOR one item on "
                     "the SAME screen, then press. This trial separates "
                     "selection state from per-screen state: anything that "
                     "changes on a screen switch but NOT on a cursor move is "
                     "layout or render state, not focus.");
        }
        break;
    }

    case HD_WANT_B: {
        log_line("heapdiff: === trial %d, capture B (after screen change) ===",
                 g_trial);
        if (!hd_capture(&g_b, "B"))
            return g_stage;

        /* signal = A2 -> B, noise = A1 -> A2, candidates = signal minus noise */
        static uintptr_t sig[HD_MAX_REPORT * 64];
        static uintptr_t noi[HD_MAX_REPORT * 64];
        int nsig_all = hd_changed(&g_a2, &g_b, sig,
                                  (int)(sizeof(sig) / sizeof(sig[0])));
        int nnoi_all = hd_changed(&g_a1, &g_a2, noi,
                                  (int)(sizeof(noi) / sizeof(noi[0])));
        int nsig = nsig_all < (int)(sizeof(sig) / sizeof(sig[0]))
                 ? nsig_all : (int)(sizeof(sig) / sizeof(sig[0]));
        int nnoi = nnoi_all < (int)(sizeof(noi) / sizeof(noi[0]))
                 ? nnoi_all : (int)(sizeof(noi) / sizeof(noi[0]));

        static uintptr_t keep[HD_MAX_REPORT * 64];
        int nkeep = 0;
        for (int i = 0; i < nsig; i++) {
            int noisy = 0;
            for (int j = 0; j < nnoi; j++)
                if (sig[i] == noi[j]) { noisy = 1; break; }
            if (!noisy && nkeep < (int)(sizeof(keep) / sizeof(keep[0])))
                keep[nkeep++] = sig[i];
        }

        log_line("heapdiff: trial %d (%s): %d changed, %d of those also "
                 "changed in the control -> %d candidate(s).",
                 g_trial, (g_trial % 2) ? "SCREEN CHANGE" : "CURSOR MOVE",
                 nsig_all, nsig - nkeep, nkeep);
        if (nsig_all > nsig || nnoi_all > nnoi)
            log_line("heapdiff: ⚠ change lists were capped (%d/%d signal, "
                     "%d/%d noise) -- candidates may be incomplete.",
                     nsig, nsig_all, nnoi, nnoi_all);

        if (!g_have_cand) {
            memcpy(g_cand, keep, (SIZE_T)nkeep * sizeof(keep[0]));
            g_ncand = nkeep;
            g_have_cand = 1;
        } else {
            int before = g_ncand;
            intersect(keep, nkeep);
            log_line("heapdiff: intersected with previous trials: %d -> %d "
                     "survivor(s).", before, g_ncand);
        }

        /* Report CONTIGUOUS RUNS, not one line per page. Adjacent pages
         * almost certainly belong to one structure, so runs are both far
         * shorter to read and more informative than a flat address list --
         * and the whole list is written, never truncated at HD_MAX_REPORT,
         * because a candidate silently dropped from the report is a candidate
         * lost. */
        log_line("heapdiff: === %d candidate page(s) after %d trial(s) ===",
                 g_ncand, g_trial);
        {
            int i = 0, runs = 0;
            while (i < g_ncand) {
                int j = i;
                while (j + 1 < g_ncand &&
                       g_cand[j + 1] == g_cand[j] + HD_BLOCK)
                    j++;
                int npg = j - i + 1;
                if (npg > 1)
                    log_line("heapdiff:   %p .. %p  (%d pages)",
                             (void *)g_cand[i], (void *)g_cand[j], npg);
                else
                    log_line("heapdiff:   %p", (void *)g_cand[i]);
                runs++;
                i = j + 1;
            }
            log_line("heapdiff: (%d page(s) in %d contiguous run(s))",
                     g_ncand, runs);
        }

        if (g_ncand == 0) {
            log_line("heapdiff: no page changed on the screen change that did "
                     "not also change on its own. Either the screen state "
                     "lives in a page that ticks anyway, or more trials are "
                     "needed to separate them.");
        } else if (g_trial < 2) {
            log_line("heapdiff: ⚠ ONE trial only. A slow timer that happened "
                     "not to tick during the control window is still in this "
                     "list. Run more trials -- survivors intersect.");
        }

        cand_save();

        {
            wchar_t msg[160];
            /* Say whether the set is ready to WATCH, so the next action is
             * obvious without reading the log. */
            if (g_trial >= 3 && g_ncand <= HD_WATCH_PAGES)
                _snwprintf(msg, 159, L"Trial %d done. %d candidates. "
                                     L"Narrow enough: press F7 to watch bytes.",
                           g_trial, g_ncand);
            else
                _snwprintf(msg, 159, L"Trial %d done. %d candidates. "
                                     L"Press F6 to start another trial.",
                           g_trial, g_ncand);
            msg[159] = L'\0';
            speech_say(msg, FALSE);
        }
        g_stage = HD_WANT_A1;
        break;
    }
    }
    return g_stage;
}
