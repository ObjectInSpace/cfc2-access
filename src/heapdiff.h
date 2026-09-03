/*
 * Differential heap scan -- find memory that CHANGES when the screen changes.
 *
 * ⭐ WHY THIS EXISTS, AND WHY IT IS SHAPED THIS WAY.
 *
 * Three routes to "which menu item is focused" have been closed, and all three
 * failed identically: each anchored on something that could be FOUND rather
 * than something that CHANGES.
 *
 *   1. text hook   -- the CRC primitive never hashes text (load-time only)
 *   2. name pool   -- all 25 layouts permanently resident; residency is a
 *                     constant, and the "focus state counts" were the shipped
 *                     file's own properties read back out of memory
 *   3. tree walk   -- 1789 objects holding pointers into the pool, their full
 *                     +-128 byte windows AND one hop out: 0 changes across two
 *                     COMPLETE passes on different screens
 *
 * Every one of those anchors derived from shipped FILE content, and file
 * content does not vary. Locatability was never the property wanted. Variance
 * is. So this inverts the search: assume NOTHING about what the state looks
 * like, find memory that differs between screens, and identify it afterwards.
 *
 * ⚠⚠ THE SAME-SCREEN CONTROL IS NOT OPTIONAL -- IT IS THE WHOLE METHOD.
 *
 * A running game rewrites a great deal of memory every frame: animation phase,
 * frame counters, timers, RNG, audio buffers, particle state. All of that
 * changes between two captures no matter what the player did. Without a
 * control, every clock in the process looks like a focus signal, and the
 * result would be a third confident false positive to match the three false
 * negatives.
 *
 * So the unit of work is a TRIAL, not a capture:
 *
 *      A1 ---- (wait, touch nothing) ---- A2 ---- (change screen) ---- B
 *
 *   noise  = pages differing between A1 and A2   (same screen: pure noise)
 *   signal = pages differing between A2 and B    (screen changed)
 *   candidates = signal MINUS noise
 *
 * A page that changes when nothing happened cannot be evidence of a screen
 * change, whatever else it does. Subtracting the control is what separates
 * "state that tracks the screen" from "state that merely ticks".
 *
 * Multiple trials intersect: a page must survive EVERY trial to stay a
 * candidate. One trial cannot distinguish a screen-tracking page from a slow
 * timer that happened not to tick during the control window.
 *
 * Read-only throughout: VirtualQuery, then guarded reads. No writes, no hooks,
 * no calls into game code -- the same discipline that let the tree walk run
 * repeatedly without ever destabilising the game.
 */
#ifndef CFC2_HEAPDIFF_H
#define CFC2_HEAPDIFF_H

#include <windows.h>

/* Hash granularity. 4 KB is the natural page size and costs ~1.6 MB of hashes
 * for the ~1.55 GB this process exposes -- cheap enough to hold several
 * snapshots at once. Finer blocks would localise a hit better but cost
 * proportionally more; the right move is to re-scan surviving pages finer,
 * not to pay 16x up front for memory that is 99.9% uninteresting. */
#define HD_BLOCK      4096

/* Upper bound on blocks tracked. ~1.55 GB / 4 KB is ~400k; allow headroom.
 * A snapshot that would exceed this is TRUNCATED and says so -- silently
 * covering less than the caller believes is how false negatives are made. */
#define HD_MAX_BLOCKS 600000

/* How many candidate pages to report in detail. */
#define HD_MAX_REPORT 64

typedef struct {
    uintptr_t addr;           /* block start */
    unsigned  hash;
} hd_block;

typedef struct {
    hd_block *block;          /* sorted by addr */
    int       nblocks;
    int       truncated;      /* hit HD_MAX_BLOCKS: coverage is PARTIAL */
    SIZE_T    bytes;
    int       regions;
    /* Chunks whose bulk copy faulted and had to be retried block by block.
     * Normally 0. Reported because the fallback is what stops a single bad
     * page from silently costing a whole chunk's worth of coverage. */
    int       fallback_chunks;
    DWORD     ms;
    LONG      faults;
} hd_snapshot;

/* Where a trial has got to. The state machine is explicit so the log can
 * always say what the next keypress will do -- a user driving this from inside
 * the game cannot see internal state, and "what did that press just do?" has
 * already cost this project several runs. */
typedef enum {
    HD_WANT_A1 = 0,   /* next capture is the control baseline */
    HD_WANT_A2,       /* next is the control repeat -- DO NOT touch anything */
    HD_WANT_B         /* next is after changing screen */
} hd_stage;

/* Take a snapshot into `out`. Returns FALSE if nothing could be captured. */
BOOL hd_capture(hd_snapshot *out, const char *tag);

/* Drive one step of the trial protocol, using `s` as scratch for the capture.
 * Returns the stage that will come NEXT, and logs what the user should do.
 * When a trial completes it logs the candidate set and intersects it with
 * previous trials. */
hd_stage hd_step(void);

/* Current stage, for the caller's prompts. */
hd_stage hd_stage_now(void);

/* Reset the whole experiment (drops accumulated candidates). */
void hd_reset(void);

/* --- Byte-level watch on the surviving candidates ------------------------
 *
 * Once the candidate set is small, narrowing further stops being the useful
 * move: 8 surviving pages out of 403,800 already say WHERE to look, and the
 * open question becomes WHAT inside them changes. A page is 4096 bytes and the
 * field of interest may be 4 of them.
 *
 * So this keeps full CONTENT (not a hash) for up to HD_WATCH_PAGES candidate
 * pages and reports the exact offsets that differ, with old and new values.
 * If moving the cursor down one item flips a single 4-byte field from N to
 * N+1, that is the selection index and the search is over.
 *
 * ⚠ This is a MEASUREMENT, not a detector. A changed dword is a candidate
 * field, not proof of focus -- the same discipline the whole project runs on.
 * Cheap enough to be unconditional: 32 pages is 128 KB. */
#define HD_WATCH_PAGES 32

/* ⚠⚠ THE WATCH NEEDS ITS OWN CONTROL, at FIELD granularity.
 *
 * The trial protocol's control subtracts noisy PAGES. Within a page that
 * survives, individual FIELDS were never noise-tested -- and a page can
 * qualify because ONE field tracks the cursor while forty others on the same
 * page churn every frame. Measured: 47 fields changed in every watch event,
 * none of them index-like.
 *
 * So the watch runs the same A1/A2/B shape as the trials, one level down:
 *
 *    W1 --(wait, touch NOTHING)-- W2 --(move the cursor)-- W3
 *
 *    field noise  = fields differing W1 -> W2
 *    field signal = fields differing W2 -> W3
 *    candidates   = signal MINUS noise
 *
 * Same principle that made the page-level scan work, applied where it was
 * missing. */
/* ⭐⭐ A FOURTH CAPTURE: REVERSIBILITY.
 *
 * Paid for with a false positive. A pointer pair passed every filter here --
 * 13 page-level trials, zero field-level control noise, a constant stride, and
 * it persisted between rounds -- and was still a monotonic allocation counter.
 * It advanced +1 element on an UP move, and the record it pointed at was 144
 * zero bytes.
 *
 * The controls prove a field is not NOISE. They cannot prove it is STATE.
 * A counter is perfectly correlated with input and represents nothing.
 *
 * So the protocol UNDOES the action: move the cursor one item, then move it
 * BACK. Real state returns to its earlier value; a counter keeps climbing.
 * One extra keypress, and it is the single most discriminating test available.
 */
typedef enum {
    HD_W_WANT_1 = 0,   /* baseline */
    HD_W_WANT_2,       /* control repeat -- touch NOTHING */
    HD_W_WANT_3,       /* after moving the cursor ONE item */
    HD_W_WANT_4        /* after moving it BACK -- the reversibility test */
} hd_wstage;

/* Drive one step of the watch protocol. Logs what to do next, and on the
 * third capture reports only fields that changed on the cursor move and NOT
 * in the control window. */
void hd_watch_step(void);

/* Current watch stage, for prompts. */
hd_wstage hd_watch_stage(void);

/* Capture the watched pages' contents. Uses the current candidate set.
 * Exposed for the offline harness. */
BOOL hd_watch_capture(const char *tag);

/* Compare the last two captures and log the changed bytes, with no control
 * subtraction. Exposed for the harness; hd_watch_step is what the game uses. */
void hd_watch_report(void);

/* --- Follow a candidate pointer pair -------------------------------------
 *
 * Found 2026-09-02: at page 0x2884D000, the aligned u64s at +0x978 and +0x980
 * both advance by exactly 0x90 (144) per one-item cursor move, persist between
 * rounds, and did not move at all in a clean control window. That is the shape
 * of a pointer into a 144-byte-per-element array -- i.e. a pointer AT the
 * selected item's record.
 *
 * ⚠ Not yet proven to BE the selection. Both observed moves were in the same
 * direction, so a counter that advances on ANY input is still consistent with
 * the data. The decisive test is moving the cursor the OTHER way: a selection
 * pointer steps back by 0x90, a counter keeps climbing. This probe reports the
 * signed delta and the stride in elements so that is immediately readable.
 *
 * It also dumps the 144-byte record pointed at, because if that record holds a
 * GMD label hash or a string pointer then the selected item can be NAMED,
 * which is the entire goal. */
void hd_probe_ptr(void);

/* --- Track the index candidates over repeated cursor moves ---------------
 *
 * ⚠ WHY THIS IS THE ONLY WAY TO CONFIRM ONE. Heap addresses differ between
 * launches, so a candidate found in one session cannot be re-found in the
 * next: run A produced 0x048FA000 +0xb88 (3->4->3), run B produced five
 * candidates on entirely different pages, with ZERO overlap. Both runs had
 * clean controls. Confirmation across runs is therefore impossible by address,
 * and a single 3->4->3 observation cannot distinguish an index from a
 * coincidence.
 *
 * So confirm WITHIN one run: latch the current index candidates, then sample
 * them after each cursor press. A real selection index walks 3,4,5,6 with
 * successive presses and reverses when you go back. A coincidence does not.
 *
 * ⚠ And note a shipping mod needs a STABLE PATH to the field anyway (a static
 * address, or a signature in the surrounding struct). Confirming the field is
 * step one; making it re-findable every launch is a separate problem. */
void hd_track_latch(void);   /* remember the current index candidates */
void hd_track_sample(void);  /* record their values now, and log the series */

/* --- ONE-KEY HUNT: the whole method in four presses ----------------------
 *
 * ⚠ THE OLD WORKFLOW WAS UNREASONABLE. It asked for F6 x3 per trial x6+
 * trials, then F7 x4, then F9 x4 -- around 30 keypresses with a different
 * cursor instruction at each stage, all of which the user had to keep straight
 * while playing. That is a tool built around its own implementation history
 * rather than around the person using it.
 *
 * The page-level trials (F6) existed ONLY to shrink what the field-level watch
 * had to examine. But the field-level pass has the SAME control and a STRONGER
 * test (reversibility), so it can do the whole job in one sweep:
 *
 *   press 1  baseline
 *   press 2  control      -- touch NOTHING
 *   press 3  after moving the cursor ONE item
 *   press 4  after moving it BACK
 *
 * Page hashes cover all ~404k pages (1.6 MB per snapshot). Full CONTENT is
 * kept only for pages whose hash moved in the control or the move window --
 * a few thousand pages, ~6-15 MB, and that set is known by press 3, before it
 * is needed. So one sweep yields both the page-level filtering and the
 * field-level verdict.
 *
 * Anything that changes when nothing was touched is noise; anything that moves
 * on the cursor and does not come back is a counter; what is left, small and
 * stepping by one, is the index. */
void hd_hunt(void);          /* drive one press of the four-press hunt */

/* --- Find a STABLE PATH to a candidate ----------------------------------
 *
 * ⚠⚠ THIS IS NOW THE ONLY BLOCKER. Reading the shell's text is solved, and
 * the hunt finds a cursor-tracking field in minutes -- but the address is
 * different every launch (session A: 0x4A7F000; session B: 0x698000,
 * 0x2D5F0000). A mod cannot ship a number that changes every run, and MORE
 * HUNTS DO NOT HELP: they raise confidence in an address that dies with the
 * process.
 *
 * The exe has **ASLR off and no relocations**, so anything INSIDE the image is
 * stable forever. If a static global points -- directly, or through a short
 * chain -- at the page the hunt identified, that chain IS the answer: follow
 * it at startup and the field is found again on any launch.
 *
 * So: search memory for pointers to the candidate page, then search for
 * pointers to THOSE, up to a few levels, and report any chain that starts
 * inside the exe image. Read-only; no hooks. */
void hd_find_path(void);

/* --- Verify a static path across launches -------------------------------
 *
 * The path search found EIGHT static roots for the menu-state page, four of
 * them pointing at one object with the field at +0x128:
 *
 *     image+0x1208bd0 -> 0x048206A0,  field = that + 0x128
 *     image+0x221ed98 -> 0x048206A0,  field = that + 0x128
 *
 * That is only a *claim* until it survives a restart. This reads the recorded
 * root on any launch, follows it, and reports the value it lands on -- so the
 * chain can be checked directly instead of re-deriving a candidate first.
 *
 * ⚠ A pointer that resolves is not proof: it must land on a field that TRACKS
 * THE CURSOR. So this also samples across presses, exactly like hd_track,
 * because "the address resolved" and "the address is the selection" are
 * different claims and this project has already conflated them once. */
void hd_verify_path(void);

/* Poll the primary static path and log the value whenever it CHANGES.
 *
 * ⚠ The last open question is whether the field at [image+0x1208bd0] + 0x128
 * is the selection index. Asking the user to press a key, move the cursor and
 * press again works, but it is fragile and this project has already lost runs
 * to mis-driven manual protocols. Polling instead means simply NAVIGATING the
 * menus answers it: if the value walks with the cursor, the log shows it.
 *
 * Cheap enough to run continuously: two pointer reads and a compare. */
void hd_poll_path(void);

/* Speak the watched field's value on demand.
 *
 * ⚠ Why this beats any amount of further log analysis: the range a field shows
 * depends entirely on where the player navigated, and mixed navigation across
 * screens makes it ambiguous by construction (observed: 1..3 across several
 * screens, which proves nothing either way). The decisive question is simply
 * "does the value equal the position of the item I am sitting on?" -- and only
 * the player can see which item that is.
 *
 * So: speak the number. The player stands on a known entry (say the 5th game
 * in the list), presses the key, and hears it. One press settles what hours of
 * range analysis cannot. */
void hd_say_value(void);

/* --- Span scan: find fields whose RANGE matches an item count -------------
 *
 * ⛔ WHY THE HUNT KEEPS FINDING CHROME. It asks "what changed when the cursor
 * moved and came back when it moved back?" -- and a blink phase, a highlight
 * animation and a focus flag all satisfy that perfectly. So does the index,
 * but the chrome vastly outnumbers it, so it dominates every candidate list.
 * Measured: the best candidate by every earlier criterion (`+0x128`, verified
 * across four launches, ±1 steps, reversing) turned out to hold only 0/1/2
 * over 40 changes on several screens. A 2-state field.
 *
 * THE MISSING CONSTRAINT IS SPAN. A selection index must take as many distinct
 * values as the menu has items. A blink flag never will, however perfectly it
 * reverses.
 *
 * So: sample many candidate addresses repeatedly while the player navigates
 * normally, and rank by how many DISTINCT values each has taken. A field that
 * reaches 6-8 distinct values stands out instantly from the 2-state crowd --
 * and needs no protocol, no key discipline, and no memory from the user.
 *
 * Call hd_span_arm() once to choose what to watch, then hd_span_tick() from
 * the poll loop. */
void hd_span_arm(void);
void hd_span_tick(void);

/* Hash one block. Exposed so the harness can assert the primitive directly
 * rather than only end-to-end -- an end-to-end trap in this project has
 * already passed once with the mechanism deliberately sabotaged. */
unsigned hd_hash_block(const BYTE *p, SIZE_T n);

/* Compare two snapshots: fill `out` with blocks present in BOTH whose hash
 * differs. Returns the count. `out` may be NULL to count only.
 *
 * Blocks present in only one snapshot are NOT differences -- memory was
 * allocated or freed, which says nothing about the screen. Counting those as
 * changes would swamp the real signal. */
int hd_changed(const hd_snapshot *a, const hd_snapshot *b,
               uintptr_t *out, int cap);

#endif /* CFC2_HEAPDIFF_H */
