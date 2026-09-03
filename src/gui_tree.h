/*
 * Walk the INSTANTIATED GUI tree -- route 3, and the only route left.
 *
 * Routes 1 and 2 are closed and both failed the SAME way: they measured a
 * static property of the shipped FILE and it looked like live state.
 *
 *  - hashwatch (text hook): 381 calls, 5 game callers, 0 hashing text, with a
 *    passing positive control. Label hashes are computed at LOAD time; there is
 *    no text chokepoint to hook.
 *  - name-pool residency: 24-25 of 25 layouts resident AT ONCE, every one at
 *    6/6 corroboration. Residency is a CONSTANT. The focus-state counts it
 *    ranked by matched each layout's static count parsed from the shipped file
 *    -- the scan was re-reading file content out of memory, never observing
 *    live state.
 *
 * So the bar for this route is set by how those failed. It is NOT enough to
 * find a field that looks like focus. A field only counts as live state once it
 * has been shown to CHANGE while the player navigates, and to change in a way
 * that tracks the screen. This header therefore describes a DUMPER, not a
 * detector: it reports what it sees and makes no claim about what is focused.
 *
 * ⭐ THE ENTRY POINT: pointers INTO the name pool.
 *
 * Established offline against the shipped menu.arc (main_menu_2, and the same
 * shape across all 25 layouts): a layout does NOT reference its names by
 * offset. Searching the whole file for every deep pool offset -- as u32 and
 * u64, both pool-relative and file-absolute -- yields ZERO hits. Names are not
 * addressed numerically on disk.
 *
 * That is what makes the runtime interesting rather than a dead end. In memory
 * the loaded pool sits at a real address, so anything referring to a node by
 * name must hold a `char *` INTO that pool. Such a pointer cannot come from the
 * file -- the file has no pointers -- so its existence is evidence of an
 * INSTANTIATED object. This is the precise opposite of the residency scan: that
 * one found bytes the loader copied out of a file, whereas a pointer into the
 * pool can only have been written by the runtime.
 *
 * Finding those pointers gives node candidates. Dumping the words around each
 * one and DIFFING two dumps taken on different screens is what identifies which
 * field is live state. Nothing is announced until a field is proven to vary.
 *
 * Read-only throughout: VirtualQuery, then guarded reads. No writes, no hooks,
 * no calls into game code. Deliberate -- the hooking route killed the game once
 * (see hash_watch.c and memory cfc2-seed-pointer-stack-smash) and a scanner
 * cannot.
 */
#ifndef CFC2_GUI_TREE_H
#define CFC2_GUI_TREE_H

#include <windows.h>
#include "gui_find.h"

/* Words captured either side of a pool pointer. The node struct layout is
 * UNKNOWN -- that is the thing being measured -- so a window is dumped rather
 * than fields being parsed out of a guessed layout. 16 QWORDs back and forward
 * covers a generously sized node without assuming anything about it. */
#define GTREE_BACK      16
#define GTREE_FWD       16
#define GTREE_WINDOW    (GTREE_BACK + 1 + GTREE_FWD)

/* How many node candidates to record per pass.
 *
 * ⚠ Measured in-game: 3117 pool pointers, of which ~192 survive validation as
 * real pool entries -- and the first run hit this cap EXACTLY, three passes
 * running. A cap that binds is a SAMPLE taken in address order, which is
 * arbitrary with respect to whatever holds the live state, so a "nothing
 * changed" result drawn from it proves nothing.
 *
 * 4096 sits far above the observed population, so the cap should no longer
 * bind. The log says explicitly when it does. Cost is only memory:
 * sizeof(gtree_node) is ~350 bytes, so ~1.4 MB per pass, and two passes are
 * held at once. */
#define GTREE_MAX_NODES 4096

/* Hard bound on candidate hits INSPECTED per pass, regardless of how many are
 * kept. A hit rejected as "not a real pool entry" does not grow the node
 * count, so capping on kept nodes alone leaves the work unbounded -- that is
 * precisely what hung the second in-game run. */
#define GTREE_MAX_SEEN  2000000

/* A location holding a pointer into the name pool. */
typedef struct {
    uintptr_t site;                  /* address of the pointer itself */
    uintptr_t target;                /* the char* it holds */
    char      name[64];              /* the pooled name it points at */
    int       name_trunc;            /* name was longer than the buffer */

    /* The raw window around the site. This is the measurement. */
    uintptr_t word[GTREE_WINDOW];
    int       word_ok[GTREE_WINDOW]; /* that slot was readable */

    /* ⭐ ONE HOP OUT: a hash of what each pointer word POINTS AT.
     *
     * Measured 2026-09-02: 0 of 3097 nodes changed a single word across three
     * DIFFERENT screens, with full coverage and no sampling. So the objects
     * holding name pointers are static -- they are the loaded layout TEMPLATE
     * (parsed menu.arc), which matches all 25 layouts staying permanently
     * resident.
     *
     * But those objects are dense with pointers to OTHER objects
     * (0x3c7ecXXX / 0x3c7edXXX in the dump). Per-instance state would live
     * there, one hop away, not in the bytes beside the name pointer. Hashing
     * the target keeps the dump small while still detecting any change in it.
     * Zero means "not a followable pointer". */
    unsigned  deref_hash[GTREE_WINDOW];

    /* How many words in the window point into the pool. Descriptive only -- it
     * does NOT claim any word is a state or a focus flag.
     *
     * ⚠ There was also an `nptr_heap` tally ("does this word point at
     * committed memory"). REMOVED: it cost a VirtualQuery syscall per word,
     * which is what stalled the first in-game pass, and it was never evidence
     * of anything. The RAW WORDS below are the measurement. */
    int       nptr_pool;             /* words pointing into the pool */
} gtree_node;

typedef struct {
    BOOL      ok;
    const char *layout;              /* layout whose pool was used */
    uintptr_t pool_base;
    SIZE_T    pool_span;

    gtree_node node[GTREE_MAX_NODES];
    int        nnodes;
    int        nnodes_seen;          /* found before the cap; may exceed nnodes */

    /* Positive counts of what was examined. "Nothing found" and "never looked"
     * must never be indistinguishable in the log -- this project has been
     * bitten by exactly that (see memory probe-zero-checked-looks-like-zero-
     * failed). */
    int       regions_walked;
    int       regions_scanned;
    SIZE_T    bytes_scanned;
    DWORD     ms;
    LONG      faults_absorbed;
    /* The scan stopped at the deadline: results are PARTIAL. The differ must
     * say so, otherwise a truncated pass yields a confident-looking
     * "0 changed" -- which is how this project's earlier false negatives were
     * produced. */
    BOOL      incomplete;
} gtree_result;

/* One pass: locate a resident pool, then find pointers into it and dump the
 * window around each.
 *
 * Returns TRUE if any pool pointer was found. A miss is a legitimate transient
 * result, not an error.
 *
 * `tag` labels the pass in the log so two passes can be told apart when
 * diffing (e.g. "beat8" vs "beat16"). */
BOOL gui_tree_scan(gtree_result *out, const char *tag);

/* Seed the pool cache from a gui_find result the caller already has.
 *
 * gui_find walks 1.6 GB (~15 s in-game). The mod runs it once for the layout
 * inventory, so handing that result here spares the first capture from
 * repeating it -- which otherwise leaves the log silent for 15 s after the
 * hotkey, indistinguishable from a stall. Safe: the seeded address is
 * re-validated on use, so a stale seed costs a fresh search, never a wrong
 * answer. */
void gui_tree_seed(const gui_result *g);

/* Dump a pass to the log in a form that diffs cleanly line by line. */
void gui_tree_report(const gtree_result *r, const char *tag);

/* Compare two passes and log ONLY the words that changed.
 *
 * This is the actual deliverable of route 3. A word that is identical across
 * two different screens is file-derived or structural; a word that CHANGES is
 * the live state we have been unable to find twice over. Matching is by node
 * NAME (not by index or address), since the set and order of nodes may differ
 * between passes.
 *
 * Logs a positive count of nodes compared, so "no changes" is distinguishable
 * from "nothing was comparable". */
void gui_tree_diff(const gtree_result *a, const gtree_result *b);

/* Is `p` a plausible pointer into the pool? Exposed for the offline harness,
 * so the mechanism can be tested directly rather than only end-to-end --
 * an end-to-end trap here passed once with the mechanism sabotaged (see memory
 * test-trap-must-be-proven-to-fail). */
BOOL gui_tree_points_into_pool(uintptr_t v, uintptr_t base, SIZE_T span);

#endif /* CFC2_GUI_TREE_H */
