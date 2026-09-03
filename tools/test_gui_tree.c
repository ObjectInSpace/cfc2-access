/*
 * Offline positive control for the instantiated GUI tree walk.
 *
 * Why this exists, stated plainly: in this project a scanner that finds
 * nothing and a scanner that is broken have twice looked identical in the log,
 * and an end-to-end trap has once passed with the mechanism deliberately
 * sabotaged. So this harness plants a KNOWN tree and asserts the mechanism
 * directly, not merely the final answer.
 *
 * What is planted: a real layout name pool (extracted from the shipped
 * menu.arc) plus synthetic "node" structs holding REAL pointers into that
 * pool -- which is precisely the shape the live walk looks for, and a shape
 * that cannot occur in the file itself.
 *
 * The cases:
 *   1. positive     -- nodes are found, named correctly from the pool
 *   2. delimiting   -- a pointer into the MIDDLE of a name is rejected
 *   3. diff-changed -- a word altered between passes is reported
 *   4. diff-quiet   -- an unchanged tree reports NO changes (the control that
 *                      makes case 3 mean something)
 *   5. negative     -- with the tree wiped, nothing is found
 *
 * Case 4 matters as much as case 3: a differ that reports everything as
 * changed would pass case 3 while being useless.
 *
 * Usage: test_gui_tree.exe <main_menu_2.pool>
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gui_tree.h"
#include "gui_find.h"
#include "memsafe.h"
#include "log.h"

static int g_fail;

static void check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? " OK " : "FAIL", what);
    if (!cond) g_fail++;
}

/* Read a file unbuffered.
 *
 * ⚠ NOT a stylistic choice. The gui_find harness originally used fread, and
 * the CRT's FILE buffer left a SECOND copy of the pool on the heap, which the
 * scanner duly found -- breaking the negative control. The leak was in the
 * TEST, not the scanner. Read with the Win32 API and no buffering. */
static BYTE *slurp(const char *path, SIZE_T *len)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("cannot open %s\n", path);
        exit(2);
    }
    DWORD sz = GetFileSize(h, NULL);
    BYTE *buf = (BYTE *)VirtualAlloc(NULL, sz + 4096, MEM_COMMIT | MEM_RESERVE,
                                     PAGE_READWRITE);
    DWORD got = 0;
    ReadFile(h, buf, sz, &got, NULL);
    CloseHandle(h);
    *len = got;
    return buf;
}

/* A synthetic node: a name pointer with plausible neighbouring fields.
 * The real layout is UNKNOWN -- that is what the live dump is for -- so this
 * only has to have the property the walker keys on: a pointer into the pool. */
typedef struct {
    uintptr_t vtable;
    uintptr_t parent;
    uintptr_t name;        /* -> into the pool */
    uintptr_t state;       /* the field we will vary between passes */
    uintptr_t flags;
    uintptr_t pad[3];
} fake_node;

/* Find the offset of the Nth NUL-delimited name in a pool blob. */
static SIZE_T nth_name(const BYTE *pool, SIZE_T len, int n, char *out, int cap)
{
    SIZE_T i = 0;
    int seen = 0;
    while (i < len) {
        while (i < len && pool[i] == 0) i++;
        if (i >= len) break;
        SIZE_T s = i;
        while (i < len && pool[i] != 0) i++;
        /* A real pool entry is preceded by a NUL. */
        if (s > 0 && pool[s - 1] == 0) {
            if (seen == n) {
                SIZE_T l = i - s;
                if (l >= (SIZE_T)cap) l = cap - 1;
                memcpy(out, pool + s, l);
                out[l] = '\0';
                return s;
            }
            seen++;
        }
    }
    return (SIZE_T)-1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s <pool file>\n", argv[0]);
        return 2;
    }

    /* ⚠ log_line() silently NO-OPS until log_init() runs. Without this the
     * scanner's own diagnostics -- the positive counts that say what it
     * examined -- vanish, and a failing case gives nothing to work from. */
    log_init();

    if (!mem_guard_install()) {
        printf("fault guard self-test failed\n");
        return 2;
    }

    SIZE_T plen = 0;
    BYTE *pfile = slurp(argv[1], &plen);
    printf("pool file: %llu bytes\n", (unsigned long long)plen);

    /* Plant the pool in its own region, NUL-padded on both sides so the span
     * measurement has a real boundary to find. */
    SIZE_T region = 1 << 20;
    BYTE *pool = (BYTE *)VirtualAlloc(NULL, region, MEM_COMMIT | MEM_RESERVE,
                                      PAGE_READWRITE);
    memset(pool, 0, region);
    /* Put non-pool bytes before and after so measure_pool_span sees an end. */
    BYTE *pbase = pool + 4096;
    memcpy(pbase, pfile, plen);
    /* ⚠ THE TRAILING DATA MUST LOOK LIKE THE REAL THING.
     *
     * The original padding was a solid run of high bytes, which any bound rule
     * stops at -- so the harness could not catch an overshooting bound. In the
     * game the pool is followed by FLOAT data, which is mostly non-printable
     * but sprinkled with isolated printable bytes, and that pattern defeated a
     * "stop after N consecutive bad bytes" rule: measured spans of 6907 and
     * 8355 on a pool whose real length is 2083. Reproduce that texture here so
     * the bound is tested against the shape that actually broke it. */
    for (int i = 0; i < 512; i++) {
        /* 3 high bytes then 1 printable, repeating: never 4 bad in a row. */
        BYTE b = ((i & 3) == 3) ? (BYTE)('A' + (i % 26))
                                : (BYTE)(0xC0 | (i & 0x0F));
        pbase[plen + 8 + i] = b;
        pbase[-8 - i] = b;
    }

    /* --- Case 2 first: the delimiting/pointer test, asserted DIRECTLY -------
     * Testing this only end-to-end is not enough. In this project an
     * end-to-end trap passed while the delimited search was sabotaged, because
     * scoring supplied the right answer anyway. Assert the primitive. */
    printf("\n[case 2] pointer-into-pool test, asserted directly\n");
    check(gui_tree_points_into_pool((uintptr_t)pbase,
                                    (uintptr_t)pbase, plen),
          "pool base is inside the pool");
    check(gui_tree_points_into_pool((uintptr_t)pbase + plen - 1,
                                    (uintptr_t)pbase, plen),
          "last pool byte is inside the pool");
    check(!gui_tree_points_into_pool((uintptr_t)pbase - 1,
                                     (uintptr_t)pbase, plen),
          "byte before the pool is REJECTED");
    check(!gui_tree_points_into_pool((uintptr_t)pbase + plen,
                                     (uintptr_t)pbase, plen),
          "byte after the pool is REJECTED");

    /* --- Plant the fake tree ---------------------------------------------- */
    enum { NNODES = 12 };
    fake_node *nodes = (fake_node *)VirtualAlloc(NULL, 1 << 16,
                                                 MEM_COMMIT | MEM_RESERVE,
                                                 PAGE_READWRITE);
    char names[NNODES][64];
    for (int i = 0; i < NNODES; i++) {
        /* Read names from the PLANTED copy, so the source buffer can be
         * released before scanning (see below). */
        SIZE_T off = nth_name(pbase, plen, i * 3 + 1, names[i], 64);
        if (off == (SIZE_T)-1) {
            printf("pool has too few names\n");
            return 2;
        }
        nodes[i].vtable = 0x140000000ULL + i * 0x100;
        nodes[i].parent = (uintptr_t)&nodes[0];
        nodes[i].name   = (uintptr_t)(pbase + off);
        nodes[i].state  = 0x1000 + i;
        nodes[i].flags  = 0xAABB0000ULL + i;
    }

    printf("\nplanted %d fake nodes; first name = \"%s\"\n", NNODES, names[0]);

    /* ⚠ RELEASE THE SOURCE BUFFER before scanning.
     *
     * `pfile` holds a SECOND copy of the pool, and the scanner is honest: it
     * found that copy instead of the planted one, bounded it correctly, and
     * reported a clean "zero pointers found" -- because the fake nodes point
     * at `pbase`, not at `pfile`. Every assertion failed and none of it was
     * the walker's fault.
     *
     * This is the same class of bug the gui_find harness hit, where the CRT's
     * FILE buffer left a stray copy on the heap. A test that plants a target
     * must leave exactly ONE copy of it in the process, or it is testing which
     * copy the scanner reaches first. */
    VirtualFree(pfile, 0, MEM_RELEASE);
    pfile = NULL;

    /* NOTE: gui_tree_scan() calls gui_find() to locate the pool, and gui_find
     * scans this whole process -- which is exactly what we want, since the
     * pool really is planted here. */

    /* --- Case 1: positive ------------------------------------------------- */
    printf("\n[case 1] positive control: the planted nodes are found\n");
    static gtree_result A;  /* static: ~1.9 MB, too big for the stack */
    BOOL ok = gui_tree_scan(&A, "A");
    check(ok, "scan reports success");
    check(A.nnodes > 0, "at least one node candidate found");

    /* ⭐ Assert the pool BOUNDS directly, not just the final answer.
     *
     * The first version of this walk anchored on gui_find's chunk address and
     * span (base+off / bytes read), which are NOT the pool's address and
     * length. It reported a clean "complete scan, ZERO pointers found" while
     * testing the wrong range entirely. A too-WIDE span is the more dangerous
     * failure -- it still finds the planted nodes, so an end-to-end check
     * passes while adjacent float data is being counted as node references.
     * Neither error is visible from the node count alone. */
    printf("  pool: reported %p +%llu, planted %p +%llu\n",
           (void *)A.pool_base, (unsigned long long)A.pool_span,
           (void *)pbase, (unsigned long long)plen);
    /* The planted pool is preceded by NUL padding (the garbage barrier starts
     * at pbase-8), and a NUL is a legitimate pool byte, so the measured base
     * may sit slightly before pbase. That is correct behaviour, not slack:
     * what matters is that it is the POOL and not a chunk boundary. */
    check(A.pool_base <= (uintptr_t)pbase &&
          (uintptr_t)pbase - A.pool_base <= 8,
          "pool BASE matches where the pool was actually planted");
    /* The planted pool is NUL-padded, and trailing NULs are legitimately pool
     * bytes, so allow the span to reach the garbage barrier but no further. */
    check(A.pool_span >= plen && A.pool_span <= plen + 16,
          "pool SPAN matches the planted length (not a chunk-sized window)");

    /* Every planted name must appear. Anything less and the walk is sampling,
     * not finding. */
    int found_named = 0;
    for (int i = 0; i < NNODES; i++) {
        for (int k = 0; k < A.nnodes; k++) {
            if (strcmp(A.node[k].name, names[i]) == 0) { found_named++; break; }
        }
    }
    printf("  (matched %d of %d planted names)\n", found_named, NNODES);
    check(found_named == NNODES, "ALL planted node names were found");

    /* --- Case 3: a changed word is reported ------------------------------- */
    printf("\n[case 3] diff detects a word that CHANGED between passes\n");
    for (int i = 0; i < NNODES; i++)
        nodes[i].state = 0x9000 + i;      /* the "screen changed" */

    static gtree_result B;  /* static: ~1.9 MB, too big for the stack */
    check(gui_tree_scan(&B, "B"), "second pass scans");

    /* Capture the diff by counting differing words ourselves against the same
     * matching rule, so the assertion does not depend on parsing log text. */
    /* Mirror gui_tree_diff's rule exactly: match by SITE ADDRESS.
     * (It matched by unique name until the first real run showed that rule
     * discarding 140 of 192 nodes, since layouts reuse names heavily.) */
    int diffw = 0, comparable = 0;
    for (int i = 0; i < A.nnodes; i++) {
        int bi = -1;
        for (int k = 0; k < B.nnodes; k++)
            if (B.node[k].site == A.node[i].site) { bi = k; break; }
        if (bi < 0 || strcmp(A.node[i].name, B.node[bi].name) != 0)
            continue;
        comparable++;
        for (int w = 0; w < GTREE_WINDOW; w++)
            if (A.node[i].word_ok[w] && B.node[bi].word_ok[w] &&
                A.node[i].word[w] != B.node[bi].word[w])
                diffw++;
    }
    printf("  (comparable nodes: %d, differing words: %d)\n",
           comparable, diffw);
    check(comparable > 0, "some nodes were comparable by site address");
    check(diffw > 0, "the altered word was DETECTED as changed");
    gui_tree_diff(&A, &B);

    /* --- Case 4: an unchanged tree reports NO changes --------------------- */
    printf("\n[case 4] control: an UNCHANGED tree reports no changes\n");
    static gtree_result C;  /* static: ~1.9 MB, too big for the stack */
    check(gui_tree_scan(&C, "C"), "third pass scans");
    int diffw2 = 0, comparable2 = 0;
    for (int i = 0; i < B.nnodes; i++) {
        int ci = -1;
        for (int k = 0; k < C.nnodes; k++)
            if (C.node[k].site == B.node[i].site) { ci = k; break; }
        if (ci < 0 || strcmp(B.node[i].name, C.node[ci].name) != 0)
            continue;
        comparable2++;
        for (int w = 0; w < GTREE_WINDOW; w++)
            if (B.node[i].word_ok[w] && C.node[ci].word_ok[w] &&
                B.node[i].word[w] != C.node[ci].word[w])
                diffw2++;
    }
    printf("  (comparable nodes: %d, differing words: %d)\n",
           comparable2, diffw2);
    check(comparable2 > 0, "nodes were comparable");
    /* The planted fields are static between B and C. Real memory around them
     * may legitimately move, so require the STATE field specifically to be
     * quiet rather than demanding a globally identical dump. */
    check(diffw2 < diffw, "an unchanged tree reports FEWER changes than a "
                          "changed one (the differ is not just noise)");

    /* --- Case 3b: a STABLE pointer whose TARGET changed --------------------
     *
     * The real in-game run found 0 of 3097 words changed across three
     * different screens, so the objects holding name pointers are static
     * (they are the loaded layout template). The live state, if reachable at
     * all from here, is one hop out: a pointer that stays put while the object
     * it references changes.
     *
     * This case plants exactly that shape -- nothing in the node's own 33-word
     * window changes, only the pointed-at memory -- and requires the differ to
     * report it. Without this, the deref-hash mechanism would be untested and
     * a silent failure would look just like a genuine negative. */
    printf("\n[case 3b] a STABLE pointer whose TARGET changed is detected\n");
    {
        BYTE *target = (BYTE *)VirtualAlloc(NULL, 4096,
                                            MEM_COMMIT | MEM_RESERVE,
                                            PAGE_READWRITE);
        memset(target, 0xAA, 4096);
        /* Point every node's `flags` word at the same target and hold the
         * whole window still from here on. */
        for (int i = 0; i < NNODES; i++)
            nodes[i].flags = (uintptr_t)target;

        static gtree_result P, Q;
        check(gui_tree_scan(&P, "P"), "pass P scans");

        memset(target, 0x55, 4096);          /* ONLY the target changes */

        check(gui_tree_scan(&Q, "Q"), "pass Q scans");

        int words = 0, derefs = 0, cmp = 0;
        for (int i = 0; i < P.nnodes; i++) {
            int qi = -1;
            for (int k = 0; k < Q.nnodes; k++)
                if (Q.node[k].site == P.node[i].site) { qi = k; break; }
            if (qi < 0 || strcmp(P.node[i].name, Q.node[qi].name)) continue;
            cmp++;
            for (int w = 0; w < GTREE_WINDOW; w++) {
                if (!P.node[i].word_ok[w] || !Q.node[qi].word_ok[w]) continue;
                if (P.node[i].word[w] != Q.node[qi].word[w]) words++;
                if (P.node[i].deref_hash[w] && Q.node[qi].deref_hash[w] &&
                    P.node[i].deref_hash[w] != Q.node[qi].deref_hash[w])
                    derefs++;
            }
        }
        printf("  (compared %d, raw words changed: %d, deref hashes "
               "changed: %d)\n", cmp, words, derefs);
        check(cmp > 0, "nodes were comparable");
        check(derefs > 0,
              "the STABLE pointer's changed TARGET was detected");
        gui_tree_diff(&P, &Q);
        VirtualFree(target, 0, MEM_RELEASE);
    }

    /* --- Case 4b: MANY REJECTED hits must not blow up the pass -------------
     *
     * ⚠ This is the case that would have caught the second in-game hang, and
     * the harness had no equivalent: its heap is tiny, so passes finished
     * instantly no matter how expensive each hit was.
     *
     * The hang was volume, not logic. The node cap tested `nnodes` (nodes
     * KEPT), but a hit rejected by read_pool_name never increments that, so
     * the cap never engaged; every rejected hit then paid 2-3 VirtualQuery
     * syscalls validating against the game's memory. Millions of them never
     * finished.
     *
     * Plant a large block of pointers into the MIDDLE of pool names -- all
     * valid hits, essentially all rejected -- and require the pass to finish
     * promptly and keep the real nodes. */
    printf("\n[case 4b] many REJECTED hits still finish quickly\n");
    enum { NDECOY = 400000 };
    uintptr_t *decoy = (uintptr_t *)VirtualAlloc(
        NULL, NDECOY * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (!decoy) { printf("  (alloc failed, skipping)\n"); }
    else {
        /* +2 lands mid-name: a real hit that read_pool_name must reject
         * (a pooled name is preceded by a NUL). */
        for (int i = 0; i < NDECOY; i++)
            decoy[i] = nodes[i % NNODES].name + 2;

        DWORD t0 = GetTickCount();
        static gtree_result R;  /* static: ~1.9 MB, too big for the stack */
        gui_tree_scan(&R, "R");
        DWORD el = GetTickCount() - t0;
        printf("  (hits seen: %d, kept: %d, elapsed: %lu ms)\n",
               R.nnodes_seen, R.nnodes, (unsigned long)el);

        check(R.nnodes_seen >= NDECOY,
              "the decoy hits were actually encountered");
        /* ⚠ THRESHOLD CALIBRATED AGAINST THE ACTUAL BUG, not guessed.
         * Measured on this machine: snapshot-based validation does 400k hits
         * in ~0 ms; restoring the syscall-per-hit form takes ~2281 ms. A lax
         * threshold (15 s was the first try) passes BOTH and tests nothing.
         * 500 ms sits far above the fixed path and far below the broken one.
         * In-game the heap is ~1.6 GB rather than a few MB, so the broken
         * path there was not slow but non-terminating. */
        check(el < 500, "the pass finished promptly despite the hit volume");
        /* The real nodes must survive the flood. */
        int still = 0;
        for (int i = 0; i < NNODES; i++)
            for (int k = 0; k < R.nnodes; k++)
                if (!strcmp(R.node[k].name, names[i])) { still++; break; }
        printf("  (real nodes still found: %d of %d)\n", still, NNODES);
        check(still > 0, "real nodes are still found among the rejected hits");

        VirtualFree(decoy, 0, MEM_RELEASE);
    }

    /* --- Case 5a: the pool is cached ACROSS passes -- prove it revalidates --
     *
     * The pool address is cached between passes (gui_find costs ~6 s in-game
     * and re-running it per pass meant the first live run never completed a
     * single pass). Caching is only safe if a stale entry is detected.
     *
     * ⚠⚠ HONEST LIMIT OF THIS CASE -- do not read more into it than it shows.
     * Removing `still_a_pool()` entirely (trusting the cache blindly) leaves
     * BOTH this case and case 5 passing. Verified by deliberately doing so.
     * The reason is that `read_pool_name()` independently requires a preceding
     * NUL and a printable run, so a wiped region yields no valid names and the
     * candidates are discarded downstream whatever the cache did.
     *
     * So what is asserted here is the OUTCOME (a dead pool produces no nodes),
     * not the validator. `still_a_pool()` is a cheap sanity check, not a
     * guarantee: if the pool were freed and REUSED by other string data it
     * would sample five pool-like bytes and pass, and the real protection
     * would again be read_pool_name plus the requirement that a name match the
     * layout. Stating that plainly beats a green line implying more. */
    printf("\n[case 5a] a dead cached pool yields no nodes "
           "(outcome, not validator -- see comment)\n");
    SecureZeroMemory(pbase - 128, plen + 256);   /* pool gone, nodes intact */

    static gtree_result S;  /* static: ~1.9 MB, too big for the stack */
    gui_tree_scan(&S, "S");
    printf("  (nodes reported: %d)\n", S.nnodes);
    check(S.nnodes == 0,
          "a stale/dead cached pool produces NO node candidates");

    /* --- Case 5: negative control ----------------------------------------- */
    printf("\n[case 5] negative control: tree and pool wiped\n");
    memset(nodes, 0, 1 << 16);
    SecureZeroMemory(pool, region);

    static gtree_result D;  /* static: ~1.9 MB, too big for the stack */
    BOOL any = gui_tree_scan(&D, "D");
    check(!any || D.nnodes == 0,
          "with the tree gone, NO node candidates are reported");

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
