/*
 * Locate the live GUI layout's name pool. See gui_find.h for the rationale.
 *
 * Read-only: VirtualQuery, then guarded reads. No writes, no hooks, no calls
 * into game code. The hooking route crashed the game once (see hash_watch.c);
 * a scanner cannot, and that is why this exists.
 */
#include "gui_find.h"
#include "gui_probes.h"
#include "memsafe.h"
#include "log.h"

#include <string.h>
#include <stdio.h>

/* The pool of one layout is a few KB. Read the heap in chunks large enough to
 * hold a whole pool plus overlap, so a name is never split across two reads
 * and thereby missed. */
#define CHUNK      (256 * 1024)
#define OVERLAP    256          /* > longest name we search for */

/* How far either side of the anchor a corroborating name may sit and still
 * count as "the same pool". Pools observed in the shipped files run to ~9 KB
 * (result: 497 names); allow generous slack without spanning the whole heap. */
#define POOL_WINDOW (64 * 1024)

/* Search a buffer for the NUL-DELIMITED form of `name`.
 *
 * ⚠ Searching bare would be WRONG. "icon_status_btl_BM_NOMIP" is a name only
 * in lobby_2, but the same bytes end a texture path in `result`; a bare search
 * would report the lobby while the results screen was up. A pool entry is
 * always \0name\0, so requiring the delimiters rejects substring hits by
 * construction. Returns an offset into buf, or -1. */
SIZE_T gui_find_delimited(const BYTE *buf, SIZE_T len, const char *name)
{
    SIZE_T n = strlen(name);
    if (n == 0 || len < n + 2)
        return (SIZE_T)-1;

    /* Anchor the search on the name's FIRST BYTE via memchr rather than
     * stepping one byte at a time.
     *
     * Measured in-game: the naive loop scanned 827 MB in ~38 s -- 40x slower
     * per byte than gmd_find's 1.6 GB in 1.9 s -- because every 256 KB chunk
     * was walked once per probe string (25 layouts x 7 names). At that speed
     * the scan is still running while the player navigates, so it reports a
     * screen they have already left. memchr is vectorised in the CRT and skips
     * to plausible positions instead of testing every offset.
     *
     * The delimiting is unchanged: still \0name\0, still rejects substrings.
     * See test_gui_find.c, which asserts that directly. */
    const BYTE *p = buf;
    SIZE_T remain = len;
    while (remain >= n + 2) {
        const BYTE *hit = (const BYTE *)memchr(p, name[0], remain - (n + 1));
        if (!hit)
            return (SIZE_T)-1;
        SIZE_T i = (SIZE_T)(hit - buf);
        /* i is the candidate START of the name, so the NUL sits at i-1. */
        if (i > 0 && buf[i - 1] == 0 && buf[i + n] == 0 &&
            memcmp(hit, name, n) == 0)
            return i;
        p = hit + 1;
        remain = len - (SIZE_T)(p - buf);
    }
    return (SIZE_T)-1;
}

int gui_identify_pool(const BYTE *pool, SIZE_T span, int *corroborating,
                      int *looked_for)
{
    int best = -1, best_score = 0, best_corr = 0, best_of = 0;

    for (int i = 0; i < GUI_NLAYOUTS; i++) {
        const gui_layout_probe *p = &g_gui_layouts[i];
        const char *key = p->anchor ? p->anchor : p->discrim;
        if (!key)
            continue;
        if (gui_find_delimited(pool, span, key) == (SIZE_T)-1)
            continue;

        /* The anchor matched. Corroborate with other names from the SAME
         * layout: one string in a large heap is not evidence. */
        int corr = 0;
        for (int c = 0; c < p->ncorrob; c++) {
            if (p->corrob[c] &&
                gui_find_delimited(pool, span, p->corrob[c]) != (SIZE_T)-1)
                corr++;
        }

        /* A discriminator-based match must ALSO be checked against the layout
         * it is meant to separate from -- `versus` shares nearly every name,
         * so the discriminator's ABSENCE there is the whole signal. */
        if (!p->anchor && p->versus) {
            /* Nothing to do beyond having matched `discrim`, which by
             * construction does not occur in `versus`. Recorded for clarity. */
        }

        int score = corr * 2 + 1;
        if (score > best_score) {
            best_score = score;
            best = i;
            best_corr = corr;
            best_of = p->ncorrob;
        }
    }

    if (corroborating) *corroborating = best_corr;
    if (looked_for)    *looked_for = best_of;
    return best;
}

/* Should this region be examined at all? */
static BOOL want_region(const MEMORY_BASIC_INFORMATION *mbi)
{
    if (mbi->State != MEM_COMMIT)
        return FALSE;
    /* PAGE_GUARD is 0x100 -- do NOT mask Protect to a byte, that silently
     * clears it and makes this test dead code (learned in gmd_find). */
    if (mbi->Protect & (PAGE_NOACCESS | PAGE_GUARD))
        return FALSE;
    DWORD w = mbi->Protect & (PAGE_READWRITE | PAGE_READONLY |
                              PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                              PAGE_EXECUTE_READWRITE);
    if (!w)
        return FALSE;
    /* The pool lives in heap the loader filled, not in the mapped image. */
    return mbi->Type == MEM_PRIVATE || mbi->Type == MEM_MAPPED;
}

BOOL gui_find(gui_result *out)
{
    memset(out, 0, sizeof(*out));
    out->layout_index = -1;

    if (!mem_guard_install()) {
        log_line("guifind: fault guard self-test FAILED -- not scanning.");
        return FALSE;
    }

    LONG faults0 = mem_guard_fault_count();
    /* Time the scan: the first in-game run took ~38 s, slow enough that the
     * player had moved on before it answered. Regressions here are silent
     * unless the duration is in the log. */
    DWORD t0 = GetTickCount();

    /* Exclude our own stack: reading a stack guard page raises
     * STATUS_GUARD_PAGE_VIOLATION, and longjmp-ing out of it leaves the guard
     * unreset and the thread broken. Excluding beats catching. */
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    uintptr_t stack_lo = (uintptr_t)tib->StackLimit;
    uintptr_t stack_hi = (uintptr_t)tib->StackBase;

    static BYTE buf[CHUNK + OVERLAP];
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t maxa = (uintptr_t)si.lpMaximumApplicationAddress;
    int regions = 0, walked = 0;
    SIZE_T scanned = 0;

    while (addr < maxa) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)))
            break;
        walked++;
        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        SIZE_T rsize = mbi.RegionSize;
        uintptr_t next = base + rsize;
        if (next <= addr)
            break;                       /* no forward progress: stop */

        if (!want_region(&mbi) ||
            (base < stack_hi && next > stack_lo)) {
            addr = next;
            continue;
        }

        regions++;
        /* volatile: `off` is live across the MEM_GUARDED longjmp. A stale
         * register copy restored by longjmp could re-scan or loop forever --
         * this project has twice found -Wclobbered to be a real defect here,
         * never noise. */
        for (volatile SIZE_T off = 0; off < rsize; off += CHUNK) {
            SIZE_T want = rsize - off;
            if (want > CHUNK) want = CHUNK;
            SIZE_T tail = (off + want + OVERLAP <= rsize) ? OVERLAP : 0;

            SIZE_T got = mem_readable_run((const void *)(base + off),
                                          want + tail);
            if (got < 16)
                continue;

            volatile BOOL faulted;
            MEM_GUARDED(faulted,
                        memcpy(buf, (const void *)(base + off), got));
            if (faulted)
                continue;
            scanned += got;

            /* Collect EVERY layout resident in this chunk rather than taking
             * the first and stopping. The shell keeps many layouts loaded, so
             * a first hit answers "something is resident", not "this is the
             * screen" -- see the note in gui_find.h. */
            for (int i = 0; i < GUI_NLAYOUTS &&
                            out->nresident < GUI_MAX_RESIDENT; i++) {
                const gui_layout_probe *p = &g_gui_layouts[i];
                const char *key = p->anchor ? p->anchor : p->discrim;
                if (!key)
                    continue;
                if (gui_find_delimited(buf, got, key) == (SIZE_T)-1)
                    continue;

                int corr = 0;
                for (int c = 0; c < p->ncorrob; c++) {
                    if (p->corrob[c] &&
                        gui_find_delimited(buf, got, p->corrob[c])
                            != (SIZE_T)-1)
                        corr++;
                }
                /* Require corroboration: a lone anchor hit could be a stale
                 * freed buffer or a loader scratch copy. */
                if (corr < 2)
                    continue;

                /* Already recorded from an earlier chunk? */
                BOOL dup = FALSE;
                for (int d = 0; d < out->nresident; d++)
                    if (out->resident[d].index == i) { dup = TRUE; break; }
                if (dup)
                    continue;

                gui_resident *R = &out->resident[out->nresident++];
                R->index = i;
                R->name = p->layout;
                R->pool_base = (BYTE *)(base + off);
                R->corroborating = corr;
                R->corroborating_of = p->ncorrob;
                R->interactive = p->interactive;
                R->states_found = 0;
                for (int s = 0; s < GUI_NSTATES; s++) {
                    if (gui_find_delimited(buf, got, g_gui_states[s].name)
                            != (SIZE_T)-1)
                        R->states_found++;
                }
                if (p->interactive)
                    out->ninteractive++;

                /* Best candidate = most focus states among interactive
                 * layouts. A screen you can actually navigate carries more
                 * SELECT/DECIDE machinery than a passive one. This is a
                 * HEURISTIC for ranking, not a claim about what is on
                 * screen -- see gui_find.h. */
                if (p->interactive &&
                    (out->layout_index < 0 ||
                     R->states_found > out->states_found)) {
                    out->found = TRUE;
                    out->layout_index = i;
                    out->layout_name = p->layout;
                    out->pool_base = R->pool_base;
                    out->pool_span = got;
                    out->corroborating = corr;
                    out->corroborating_of = p->ncorrob;
                    out->states_found = R->states_found;
                    out->region_base = base;
                    out->region_size = rsize;
                }
            }
        }
        addr = next;
    }

    /* Report a POSITIVE count of what was examined. "No layout resident" and
     * "the scanner never looked" are different findings and must never look
     * the same in the log. */
    out->regions_scanned = regions;
    out->bytes_scanned = scanned;
    out->faults_absorbed = mem_guard_fault_count() - faults0;

    log_line("guifind: walked %d regions; scanned %d candidate regions "
             "(%llu KB) in %lu ms; guard absorbed %ld fault(s); %d layout(s) "
             "resident (%d interactive)",
             walked, regions, (unsigned long long)(scanned / 1024),
             (unsigned long)(GetTickCount() - t0), out->faults_absorbed,
             out->nresident, out->ninteractive);

    if (out->nresident > 0) {
        /* List them all. Naming only the best one would hide that several are
         * loaded at once, which is the single most important thing this scan
         * has established about how the shell works. */
        for (int i = 0; i < out->nresident; i++) {
            const gui_resident *R = &out->resident[i];
            log_line("guifind:   resident: %-16s %d/%d corroborating, "
                     "%2d focus states%s%s",
                     R->name, R->corroborating, R->corroborating_of,
                     R->states_found,
                     R->interactive ? " (interactive)" : "",
                     R->index == out->layout_index ? "  <- most states" : "");
        }
        if (out->nresident > 1)
            log_line("guifind: ⚠ %d layouts are resident AT ONCE -- being "
                     "resident does NOT mean being on screen. The pick above "
                     "is ranked by focus-state count, which is a heuristic, "
                     "not evidence of what is displayed.", out->nresident);
        return out->found;
    }

    if (out->faults_absorbed > 0)
        log_line("guifind: ⚠ the scan absorbed faults -- it was PARTIAL, so "
                 "'not found' here is not proof of absence.");
    else
        log_line("guifind: complete scan, NO layout identified. Most likely "
                 "none is resident yet (they load with the screen). Retry "
                 "after navigating to a menu.");
    return FALSE;
}
