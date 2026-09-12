/*
 * cGUIObjText -- capture a live instance and find where its string lives.
 * See gui_text.h for the offline derivation this file rests on.
 */

#include "gui_text.h"
#include "log.h"
#include "memsafe.h"

#include "../libs/minhook/include/MinHook.h"

#include <stdio.h>
#include <string.h>

/* The update method's signature as far as we can tell: a this-call taking one
 * further argument.
 *
 * ⛔⛔ THE SIGNATURE TRAP THIS PROJECT ALREADY PAID FOR. menu_cursor's detour
 * declared FOUR arguments where the real helper took SIX, so the original read
 * its 5th and 6th out of stack slots our frame never filled -- and the 5th was
 * the cursor SFX selector, which the user HEARD change. Ten passing tests
 * missed it, because each asked what the hook REPORTED and none asked what it
 * PASSED ON.
 *
 * ⭐ Here the reading is different, and safer: 0x1402f9720 uses only rcx and
 * rdx before its first call (mov rcx,rdx / mov rbx,rdx / mov rdi,rcx at
 * 0x1402f9750-0x1402f9765), and it never touches r8/r9 or any inbound stack
 * slot. Two arguments is the whole signature. Verified by reading the
 * instructions, not by assuming the common case.
 *
 * ⚠ The return value is passed through untouched whatever we conclude, so even
 * if this reading were wrong in some way we have not seen, the game's control
 * flow does not depend on us. */
typedef void *(*pfn_update)(void *self, void *arg);

static pfn_update g_real_update;

/* The constructor: `cGUIObjText *ctor(cGUIObjText *this)` -- one argument, and
 * it returns `this` (`mov rax,rbx` before the epilogue). */
typedef void *(*pfn_ctor)(void *self);
static pfn_ctor g_real_ctor;

static volatile LONG g_calls;
static volatile LONG g_distinct;
static volatile LONG g_faults;
/* Instances whose vptr was not this class's -- see gt_poll_and_log(). A
 * nonzero count means the hook target is wrong, not that the game misbehaved. */
static volatile LONG g_wrong_class;
/* Calls whose object was a DIFFERENT class -- expected and normal for a shared
 * helper, so counted separately from g_wrong_class (which means the filter let
 * one through and is a BUG). */
static volatile LONG g_other_class;
/* Calls whose object WAS ours. ⭐ The number that says the filter is working:
 * a large g_other_class with g_ours == 0 means we are hooked to something this
 * class never reaches. */
static volatile LONG g_ours;
/* ⭐⭐ Constructions seen. THE decisive number: cGUIObjText objects that were
 * actually built. It answers a question no update-method hook can, because
 * every such hook confounds "wrong method" with "no objects exist". */
static volatile LONG g_ctors;

/* A ring of recently constructed instances, re-inspected by the worker on
 * later polls -- see hook_ctor() for why capture-time inspection cannot work.
 * ⚠ Plain pointers, never owned and never freed by us; every use re-validates. */
#define GT_RING_SIZE 32
static void *volatile g_ring[GT_RING_SIZE];
static volatile LONG  g_ring_next;
/* Which ring entry the worker sweeps next, and how many objects it has found
 * carrying text. ⭐ The second is reported: "swept N, found text in M" is
 * interpretable where a bare "no text" is not. */
static LONG           g_sweep_at;
static volatile LONG  g_with_text;

/* ⭐ The text setter's channel. `g_settext_calls` beside `g_with_text` is what
 * makes a quiet log interpretable: calls with no text means the filter is
 * rejecting everything, which is a different fault from the hook never firing. */
static volatile LONG  g_settext_calls;
#define GT_TEXT_MAX 256
static char           g_text_buf[GT_TEXT_MAX];
static void          *g_text_self;
static volatile LONG  g_text_ready;   /* mailbox full */
static volatile LONG  g_text_busy;    /* claim flag over g_text_buf */
static char           g_text_last[GT_TEXT_MAX];  /* dedupe: last logged */

/* ⭐⭐ THE FOCUS RECORDER -- item 4's measurement, and the caption channel.
 *
 * THE QUESTION IT EXISTS TO ANSWER: does the shell push a per-ROW caption
 * through setText when the cursor moves? The corpus proves such strings EXIST
 * and are distinct (docs/screen-id-from-prompt-text.md); it does NOT prove the
 * game sets one on every row change. The 21:19 run saw 6 distinct strings from
 * 324 calls, which is far fewer than one per row -- so this is genuinely open,
 * and nothing downstream may assume its outcome.
 *
 * ⛔ WHY THE EXISTING MAILBOX CANNOT ANSWER IT. g_text_buf is a ONE-SLOT
 * mailbox that deliberately drops intermediate strings: it is built for
 * "distinct sightings", not a transcript. Whether a caption arrives per row
 * change is exactly a question about the strings it discards.
 *
 * ⛔ AND WHY THIS IS NOT A TIMING JOIN. docs/rethink-after-gui-text.md forbids
 * joining captured text to cursor indices BY TIMING -- three of six captures in
 * that run coincided with no cursor movement at all, so a time-window
 * correlation would fabricate relationships. This records a MOVEMENT SEQUENCE
 * NUMBER stamped by menu_cursor itself, so a reader can see which strings
 * arrived between one movement and the next. That is an ordering fact, not a
 * correlation: if captions do NOT track the cursor, the recording shows strings
 * with no movement between them and the answer is a clean NO.
 *
 * ⚠ Bounded and lossy by design: it stops recording when full rather than
 * wrapping, so the earliest evidence after a movement is what survives, and
 * `g_focus_dropped` counts what did not fit. A recorder that silently wraps
 * would show a full buffer whether or not anything was missed. */
/* ⭐⭐ THE INSTANCE SET -- step 1 of the GUI-instance route
 * (docs/gui-instance-route.md).
 *
 * THE QUESTION: does the set of LIVE cGUIObjText objects contain one text per
 * menu row, and can the FOCUSED one be told apart? If yes, item names come
 * from the instance tree for ANY screen -- including Gallery, the music list
 * and the consent dialog, none of which the GMD or the caption channel can
 * reach.
 *
 * ⛔ WHY THIS IS NOT ROUTE 3 AGAIN. Route 3 anchored on the name POOL, which is
 * file data parsed out of menu.arc; everything within one hop of it was static
 * by design and it closed on a clean negative (1789 objects, 0 changes). This
 * anchors on a pointer the game hands us in setText -- an object it
 * CONSTRUCTED. Route 3's own closing note says the root must come from "the
 * GUI system's own instance list, not the layout data". This is that list.
 *
 * ⚠ LOGGED ONLY, NOTHING SPOKEN. A set of live objects is not yet a mapping
 * from row to text: 126 objects are alive in an ordinary session and only some
 * belong to the current screen. Route 3's lesson is that a reachable,
 * identifiable structure is not automatically live state, so this measures
 * before anything is built on it.
 *
 * ⚠ Pointers are NOT owned. A GUI object is freed on a screen change, so every
 * read re-validates the pointer AND the vptr -- the same discipline that caught
 * a freed-and-reallocated object during the setText hunt. */
#define GT_INST_MAX 64
typedef struct {
    void *obj;
    char  text[GT_TEXT_MAX];
    LONG  move_seq;      /* movement current when this text was last set */
    LONG  sets;          /* how many times setText hit this object */
} gt_instance;
static gt_instance   g_inst[GT_INST_MAX];
static volatile LONG g_inst_count;
static volatile LONG g_inst_busy;
/* ⭐ EVICTIONS, not drops. The table now replaces its oldest entry rather than
 * refusing new ones, so nothing is lost silently -- a high count just means the
 * session has visited many screens, which is expected. */
static volatile LONG g_inst_evicted;

#define GT_FOCUS_MAX 48
/* The header promises callers this size; a mismatch would silently truncate
 * captions. Caught at COMPILE time rather than by a comment asking nicely. */
typedef char gt_caption_size_check[(GT_CAPTION_MAX == GT_TEXT_MAX) ? 1 : -1];
typedef struct {
    char      text[GT_TEXT_MAX];
    void     *self;
    LONG      move_seq;     /* value of g_move_seq when this arrived */
    int       which;        /* which of the four overloads fired */
} gt_focus_rec;
static gt_focus_rec  g_focus[GT_FOCUS_MAX];
static volatile LONG g_focus_count;
static volatile LONG g_focus_dropped;
static volatile LONG g_focus_busy;
/* Stamped by gt_note_focus_move() from the cursor hook. Never read as a
 * timestamp -- only compared for equality/order. */
static volatile LONG g_move_seq;
/* ⭐ Liveness: if this is 0, the cursor side never called us and EVERY record
 * carries seq 0. Then "no caption tracked the cursor" would be unfalsifiable,
 * so the report must refuse to conclude. */
static volatile LONG g_move_notifications;

/* The most recently seen instance, published for the worker thread.
 *
 * ⚠ A sequence counter, not a flag: the hook runs on the game's render thread
 * and the reader on ours. A plain "new data" boolean could be cleared between
 * the hook's write and the reader's read and lose the capture. The reader
 * snapshots and compares the sequence either side of its copy; if they differ
 * the hook wrote during the copy and the snapshot is discarded rather than
 * reported torn. Same discipline as menu_cursor. */
static volatile LONG  g_seq;
static void          *g_last_self;
static void          *g_last_caller;
static LONG           g_taken_seq;

static BOOL              g_installed;
static gt_install_status g_why = GT_INSTALL_NOT_TRIED;

/* ⚠ Test-only, declared here because BOTH the caption path and the numbered
 * lookup below gate on it. The SHIPPING guard (`g_installed`) is load-bearing --
 * without it the mod reads a recorder no hook is filling -- so it cannot be
 * dropped, and a suite that cannot reach the code cannot test it either.
 * Defined once, near the state it guards, rather than mid-file. */
static BOOL g_focus_test_mode;

gt_install_status gt_install_why(void) { return g_why; }

/* --- the detour ----------------------------------------------------------
 *
 * ⚠⚠ IT DEREFERENCES NOTHING. It records a pointer VALUE and calls the
 * original. That is the entire body, and it is deliberate: this runs on the
 * game's threads, and this project has already taken the game down by reading
 * through a caller-supplied pointer inside a hook. Recording a value cannot
 * fault. All inspection happens later, on our worker thread, where a fault is
 * caught by the guard and costs us a log line instead of the process.
 *
 * ⚠ The original is called UNCONDITIONALLY and its result returned untouched.
 */
static void *detour_body(pfn_update real, void *self, void *arg, void *caller)
{
    InterlockedIncrement(&g_calls);

    /* ⭐⭐ THE FILTER THAT MAKES A SHARED HELPER USABLE.
     *
     * This helper runs for several GUI classes -- that is WHY it runs per
     * element per frame, and why it is the right hook. But it means most calls
     * are not ours. Publishing every one would fill the channel with
     * OutlineFonts and the worker would rarely see a cGUIObjText.
     *
     * So read ONE qword -- the vptr -- and publish only our class's objects.
     *
     * ⚠⚠ THIS IS THE ONLY DEREFERENCE IN THE DETOUR, and it is the narrowest
     * possible one: 8 bytes at offset 0 of a pointer the game is CURRENTLY
     * making a virtual call through. If [self+0] were unreadable the game
     * would already have crashed reaching us. It is still guarded, because
     * "the game would have crashed" is an argument, and this project has been
     * wrong about exactly this kind of argument before -- a caller's pointer
     * is untrusted even when the reasoning says otherwise.
     *
     * ⭐ Everything wider than these 8 bytes stays where it was: on the worker
     * thread, under the guard, off the game's threads. */
    /* ⚠⚠ TWO LAYERS, NOT ONE. The fault guard alone is not enough and this was
     * proven here, not reasoned about: with only MEM_GUARDED around the read,
     * the offline suite died with 0xC0000005 on the test's synthetic pointer.
     * The guard is a vectored handler, and a fault taken on a thread whose
     * frame it cannot unwind still takes the process down.
     *
     * ⭐ So ask the memory manager FIRST (mem_readable_run), and keep the guard
     * as the second layer for the TOCTOU window -- another thread can free the
     * region between the query and the read. This is exactly the pairing
     * menu_cursor's cursor_is_safe() + MEM_GUARDED uses, and the same reason.
     *
     * ⚠ Checked every call, never cached: a GUI object is freed on a screen
     * change and a pointer that passed a moment ago is not a pointer that
     * still passes. */
    ULONGLONG vptr = 0;
    BOOL faulted = FALSE;
    if (!self || ((ULONG_PTR)self & 7u) ||
        mem_readable_run(self, sizeof(vptr)) < sizeof(vptr)) {
        InterlockedIncrement(&g_other_class);
        return real(self, arg);
    }
    MEM_GUARDED(faulted, vptr = *(volatile ULONGLONG *)self);
    if (faulted || vptr != GT_VTABLE_ADDR) {
        if (faulted)
            InterlockedIncrement(&g_faults);
        else
            InterlockedIncrement(&g_other_class);
        return real(self, arg);
    }

    InterlockedIncrement(&g_ours);

    /* Publish. Bump, write, bump: a reader seeing the same even value either
     * side of its copy knows the copy was clean. */
    InterlockedIncrement(&g_seq);
    g_last_self   = self;
    g_last_caller = caller;
    InterlockedIncrement(&g_seq);

    return real(self, arg);
}

/* ⚠ __builtin_return_address(0) is the GAME's return address: MinHook patches
 * the target with a jmp and pushes nothing, so the address the CPU pushed for
 * the call is still the one on the stack. GCC has no _ReturnAddress().
 *
 * ⚠ Recorded and compared only, never dereferenced -- unlike a caller-SUPPLIED
 * pointer, our own return address is trustworthy, but it still buys nothing to
 * treat it as memory.
 *
 * ⛔ NOT normalised to a call site here, unlike menu_cursor. This method is
 * VTABLE-dispatched (call qword ptr [rax+0x28]), so the byte five back is not
 * 0xE8 and the "ret - 5" rule menu_cursor uses does not apply. Applying it
 * anyway would manufacture a plausible-looking address that is not a call
 * site. The raw value is logged as what it is. */
static void *hook_update(void *self, void *arg)
{
    return detour_body(g_real_update, self, arg,
                       __builtin_return_address(0));
}

/* The constructor detour -- the BIRTH probe.
 *
 * ⚠⚠ NO VPTR FILTER HERE, and that is deliberate rather than an oversight.
 * At ctor entry the object is raw allocation: the factory writes the vptr only
 * AFTER this returns. Filtering on it would reject every object and manufacture
 * exactly the false zero this probe exists to escape.
 *
 * ⭐ Safety is unchanged in the part that matters: the detour still
 * DEREFERENCES NOTHING. It counts, records the pointer, and calls the original.
 * The worker re-checks the vptr later -- by which time the factory has written
 * it -- so the identity guarantee is deferred, not dropped.
 *
 * ⚠ We do NOT publish to the capture channel before the original runs: at that
 * moment the object's fields are uninitialised, and a worker poll landing in
 * between would inspect garbage and report it as a finding. Call through
 * first, then publish. */
/* ⭐⭐⭐ THE TEXT SETTER -- setText(this [rcx], const char *s [rdx], ...).
 *
 * This is the target the previous six runs were groping for. It does not need
 * an object to be "populated later": the TEXT ARRIVES AS AN ARGUMENT, which is
 * the shape that has worked every time on this project and that scanning has
 * failed at every time.
 *
 * ⚠⚠ SIGNATURE. The real function takes more than two arguments -- the body
 * reads r8d and r9d (`mov esi,r9d`, `mov ebp,r8d` at 0x1402f0b6a/6d) and a
 * fifth from [rsp+0x130]. menu_cursor's SFX bug was exactly this: a detour that
 * declares too few arguments makes the original read junk from stack slots our
 * frame never filled, and the user HEARD it. So declare six and pass them all
 * through untouched.
 *
 * ⚠ Slot 26 is shared with other classes, and at least one caller passes a
 * 32-BIT value in edx rather than a pointer. Reading rdx as a char* without
 * checking the vptr would dereference an integer. The filter is not optional
 * here even though the target is right. */
typedef void *(*pfn_settext)(void *self, const char *s, int a, int b,
                             int c, int d);
static pfn_settext g_real_settext_n[GT_SETTEXT_COUNT];   /* the four reals */
/* ⭐ Per-overload counts. One number per hook, so the log names WHICH of the
 * four the shell uses instead of leaving it to another run to find out. The
 * previous run could not answer that, because only one of four was hooked. */
static volatile LONG g_settext_each[GT_SETTEXT_COUNT];

static void *settext_body(int which, pfn_settext real, void *self,
                          const char *s, int a, int b, int c, int d)
{
    InterlockedIncrement(&g_settext_calls);
    InterlockedIncrement(&g_settext_each[which]);

    /* Identify the object before trusting `s`. Two layers, as everywhere in
     * this file: ask the memory manager, then guard the read.
     *
     * ⭐ BOTH classes are accepted. cGUIObjText and cGUIObjMessage each have
     * these two overloads, and cGUIObjMessage is if anything the more likely
     * holder of menu label text. Accepting one and not the other would repeat
     * the mistake that produced "setText ran 0 times". */
    ULONGLONG vptr = 0;
    BOOL faulted = FALSE;
    BOOL ours = FALSE;
    if (self && !((ULONG_PTR)self & 7u) &&
        mem_readable_run(self, sizeof(vptr)) >= sizeof(vptr)) {
        MEM_GUARDED(faulted, vptr = *(volatile ULONGLONG *)self);
        ours = !faulted &&
               (vptr == GT_VTABLE_ADDR || vptr == GT_VTABLE_MSG);
    }

    if (ours && s) {
        /* Copy the string out on the spot -- it is the caller's buffer and may
         * not outlive this call. Bounded, guarded, and published for the
         * worker to log; NO logging on the game's thread. */
        char buf[GT_TEXT_MAX];
        SIZE_T run = mem_readable_run(s, sizeof(buf) - 1);
        if (run > 0) {
            BOOL f2 = FALSE;
            MEM_GUARDED(f2, memcpy(buf, s, run));
            if (!f2) {
                buf[run < sizeof(buf) ? run : sizeof(buf) - 1] = '\0';

                /* ⛔⛔ A SEQ-BRACKET IS THE WRONG TOOL AT THIS FREQUENCY, and
                 * the first live run proved it: the setter fires ~650 times a
                 * SECOND (167,081 calls in ~4 minutes), while the worker polls
                 * every 100 ms. The sequence therefore advanced by hundreds
                 * between polls and the reader almost always sampled it
                 * mid-write (odd), so it discarded every snapshot. 616 strings
                 * were captured and NOT ONE was ever logged.
                 *
                 * ⭐ The bracket works for menu_cursor because that channel
                 * ticks a few times per keypress. Copying a pattern across a
                 * frequency difference of four orders of magnitude is what
                 * broke it. A publisher this hot needs a lock the reader can
                 * actually win, not an optimistic retry.
                 *
                 * So: a one-slot mailbox with a claim flag. The hook fills it
                 * only when it is EMPTY and never blocks; the worker drains it.
                 * Dropping intermediate strings is correct here -- the same
                 * label is set hundreds of times a second and we want distinct
                 * sightings, not a transcript. */
                if (InterlockedCompareExchange(&g_text_busy, 1, 0) == 0) {
                    g_text_self = self;
                    strncpy(g_text_buf, buf, sizeof(g_text_buf) - 1);
                    g_text_buf[sizeof(g_text_buf) - 1] = 0;
                    /* Publish LAST: the reader tests this, so it must not see
                     * a full flag over a half-written buffer. */
                    InterlockedExchange(&g_text_ready, 1);
                    InterlockedExchange(&g_text_busy, 0);
                }
                InterlockedIncrement(&g_with_text);

                /* ⭐⭐ THE INSTANCE SET: one entry per OBJECT, latest text.
                 *
                 * Distinct from the focus record below, which is an ordered
                 * TRANSCRIPT of distinct strings. This is keyed by object, so
                 * it answers "what does each live element currently say?" --
                 * the question a screen reader needs and the transcript cannot
                 * answer, because it collapses repeats and keeps no identity.
                 *
                 * ⚠ Same try-lock discipline as everything else in this hook:
                 * never block the render thread, drop rather than wait. */
                if (InterlockedCompareExchange(&g_inst_busy, 1, 0) == 0) {
                    LONG n = g_inst_count, i;
                    for (i = 0; i < n; i++) {
                        if (g_inst[i].obj == self)
                            break;
                    }
                    if (i == n && n < GT_INST_MAX) {
                        g_inst[n].obj = self;
                        g_inst[n].sets = 0;
                        InterlockedExchange(&g_inst_count, n + 1);
                        i = n;
                    } else if (i == n) {
                        /* ⛔⛔ THE TABLE MUST FORGET. The first run filled all
                         * 64 slots and then dropped 3600 objects -- and the
                         * ones dropped were the NEWEST, i.e. the screen the
                         * user is actually on. 30 of 64 entries were STALE
                         * objects from screens left long ago.
                         *
                         * ⭐⭐ PREFER A DEAD SLOT. Age alone was not enough, and
                         * the 13:43 run says why in two numbers:
                         *
                         *  - `move_seq` only advances when the CURSOR MOVES, so
                         *    entries arrive in ties -- 15 at move 16, 15 at
                         *    move 10, 12 at move 20. A minimum-by-move_seq scan
                         *    cannot order within a tie and always takes the
                         *    lowest index, so it evicted the same early slots
                         *    repeatedly.
                         *  - meanwhile every `move 0` entry was STALE (freed
                         *    title-screen and main-menu objects) and still held
                         *    a slot ~40 minutes in. The table could already SEE
                         *    they were dead -- gt_instance_report labels them --
                         *    and never acted on it.
                         *
                         * So: find a slot whose object is no longer a live
                         * cGUIObjText and take that first. Only if every entry
                         * is live does age decide. A dead entry costs nothing to
                         * lose; a live one may be the screen the user is on.
                         *
                         * ⚠ THE VALIDATION IS THE SAME ONE THE REPORT USES, and
                         * it must be: a slot this considers dead is exactly a
                         * slot the report would print STALE. Two different
                         * notions of "dead" would make the log stop describing
                         * the table's behaviour.
                         *
                         * ⚠ Cost: one guarded pointer read per entry, on a hook
                         * that already walks the table. Bounded at 64 and only
                         * on the insert path for an object not already present
                         * -- not on the common case, which is a re-set of an
                         * object the table already holds. */
                        LONG victim = -1;
                        for (LONG k = 0; k < GT_INST_MAX; k++) {
                            ULONGLONG vptr = 0;
                            volatile BOOL faulted = FALSE;
                            void *o = g_inst[k].obj;
                            if (!o || ((ULONG_PTR)o & 7u) ||
                                mem_readable_run(o, sizeof(vptr)) <
                                    sizeof(vptr)) {
                                victim = k;          /* unreadable => dead */
                                break;
                            }
                            MEM_GUARDED(faulted,
                                        vptr = *(volatile ULONGLONG *)o);
                            if (faulted || (vptr != GT_VTABLE_ADDR &&
                                            vptr != GT_VTABLE_MSG)) {
                                victim = k;          /* not ours any more */
                                break;
                            }
                        }
                        if (victim < 0) {
                            /* Every entry is still live -- fall back to age. */
                            victim = 0;
                            for (LONG k = 1; k < GT_INST_MAX; k++) {
                                if (g_inst[k].move_seq < g_inst[victim].move_seq)
                                    victim = k;
                            }
                        }
                        g_inst[victim].obj = self;
                        g_inst[victim].sets = 0;
                        g_inst[victim].text[0] = 0;
                        i = victim;
                        InterlockedIncrement(&g_inst_evicted);
                    }
                    if (i < GT_INST_MAX && g_inst[i].obj == self) {
                        memcpy(g_inst[i].text, buf, GT_TEXT_MAX - 1);
                        g_inst[i].text[GT_TEXT_MAX - 1] = 0;
                        g_inst[i].move_seq = g_move_seq;
                        g_inst[i].sets++;
                    }
                    InterlockedExchange(&g_inst_busy, 0);
                }

                /* ⭐ THE FOCUS RECORD -- a separate, ordered channel. See the
                 * block comment at g_focus. Same discipline as the mailbox: a
                 * bounded try-lock that never blocks the render thread, and no
                 * logging or formatting here.
                 *
                 * ⚠ Records DISTINCT strings only. The setter fires ~650/s and
                 * a transcript of repeats would fill 48 slots in under a tenth
                 * of a second, burying the very transition being measured. */
                if (InterlockedCompareExchange(&g_focus_busy, 1, 0) == 0) {
                    LONG n = g_focus_count;
                    BOOL dup = (n > 0 &&
                                strcmp(g_focus[n - 1].text, buf) == 0);
                    if (!dup) {
                        if (n < GT_FOCUS_MAX) {
                            /* memcpy + explicit terminator: `buf` is already
                             * NUL-terminated and the same size, so strncpy's
                             * truncation warning is about a case that cannot
                             * arise -- but silencing a warning by ignoring it
                             * is how a real one gets missed later. */
                            memcpy(g_focus[n].text, buf, GT_TEXT_MAX - 1);
                            g_focus[n].text[GT_TEXT_MAX - 1] = 0;
                            g_focus[n].self = self;
                            g_focus[n].move_seq = g_move_seq;
                            g_focus[n].which = which;
                            /* Publish the count last. */
                            InterlockedExchange(&g_focus_count, n + 1);
                        } else {
                            InterlockedIncrement(&g_focus_dropped);
                        }
                    }
                    InterlockedExchange(&g_focus_busy, 0);
                }
            } else {
                InterlockedIncrement(&g_faults);
            }
        }
    } else if (!ours) {
        InterlockedIncrement(&g_other_class);
    }

    return real(self, s, a, b, c, d);
}

/* Four thin detours over one body. ⭐ Each passes its own index so the log can
 * name WHICH overload the shell actually uses -- the question the previous run
 * could not answer, because only one of the four was hooked. */
static void *hook_settext_0(void *self, const char *s, int a, int b, int c, int d)
{ return settext_body(0, g_real_settext_n[0], self, s, a, b, c, d); }
static void *hook_settext_1(void *self, const char *s, int a, int b, int c, int d)
{ return settext_body(1, g_real_settext_n[1], self, s, a, b, c, d); }
static void *hook_settext_2(void *self, const char *s, int a, int b, int c, int d)
{ return settext_body(2, g_real_settext_n[2], self, s, a, b, c, d); }
static void *hook_settext_3(void *self, const char *s, int a, int b, int c, int d)
{ return settext_body(3, g_real_settext_n[3], self, s, a, b, c, d); }

static void *const GT_SETTEXT_HOOKS[GT_SETTEXT_COUNT] = {
    (void *)&hook_settext_0, (void *)&hook_settext_1,
    (void *)&hook_settext_2, (void *)&hook_settext_3
};
static const ULONGLONG GT_SETTEXT_ADDRS[GT_SETTEXT_COUNT] = {
    GT_SETTEXT_A, GT_SETTEXT_B, GT_SETTEXT_C, GT_SETTEXT_D
};
static const char *const GT_SETTEXT_NAMES[GT_SETTEXT_COUNT] = {
    "cGUIObjText.25", "cGUIObjText.26",
    "cGUIObjMessage.25", "cGUIObjMessage.26"
};

static void *hook_ctor(void *self)
{
    InterlockedIncrement(&g_ctors);

    void *r = g_real_ctor(self);

    /* ⭐⭐ REMEMBER IT FOR LATER, don't just publish it once.
     *
     * ⛔ THE TIMING TRAP, and the reason the first ctor run found no text: at
     * ctor exit the object is CONSTRUCTED BUT EMPTY. The ctor itself zeroes
     * +0x1f8 and +0x200 -- the very fields we are hunting. Inspecting at
     * capture time is guaranteed to find nothing, and would produce a
     * confident "no text at any offset" about objects that simply had not been
     * filled yet.
     *
     * So keep a small ring of live instances and let the WORKER re-read them on
     * later polls, by which time the shell has assigned their labels. The ring
     * is fixed-size and overwrites oldest-first: bounded work, no allocation,
     * and nothing to free on the game's thread.
     *
     * ⚠ Storing a pointer is not keeping the object alive. A GUI object is
     * freed on a screen change, so every later read re-validates the pointer
     * and the vptr -- which is exactly what caught the freed-and-reallocated
     * object in run 4. */
    LONG slot = InterlockedIncrement(&g_ring_next) - 1;
    g_ring[(SIZE_T)slot % GT_RING_SIZE] = self;

    InterlockedIncrement(&g_seq);
    g_last_self   = self;
    g_last_caller = __builtin_return_address(0);
    InterlockedIncrement(&g_seq);

    return r;
}

/* --- the inspector -------------------------------------------------------
 *
 * Given the bytes of one instance, say where the string is.
 *
 * ⭐⭐ IT REPORTS WHAT IT FOUND, AND SEPARATELY WHETHER IT CONCLUDED ANYTHING.
 * "no conclusion" is -1, a distinct return value -- not offset 0, and not an
 * all-zero struct. Those must never look alike (memory:
 * inconclusive-must-be-a-return-value).
 *
 * ⚠ The two candidate offsets are checked FIRST and named as candidates, but
 * they get no special authority: a candidate that does not resolve to text
 * loses to any other offset that does. The offline accessors made them worth
 * looking at; only the live bytes decide.
 *
 * `resolve` turns a pointer field's value into readable bytes. In the game it
 * is a guarded read of foreign memory; in a test it reads the harness's own
 * buffer. Passing it in is what lets the SHIPPING inspector run in the test
 * without the test re-deriving what the inspector does. */
typedef SIZE_T (*pfn_resolve)(void *ctx, ULONGLONG addr, char *out, SIZE_T max);

/* Does this look like text a human would read? Deliberately strict: a run of
 * printable ASCII, at least 2 characters, NUL-terminated within the window.
 *
 * ⚠ Strictness here is safety, not taste. A loose test turns every pointer to
 * arbitrary bytes into a "string" and buries the real field in noise -- and
 * this project has twice mistaken a 4-byte coincidence for evidence. */
static BOOL looks_like_text(const char *s, SIZE_T n, int *out_len)
{
    SIZE_T i = 0;
    while (i < n && s[i] != '\0') {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c > 0x7e)
            return FALSE;
        i++;
    }
    if (i < 2 || i >= n)        /* too short, or no terminator in the window */
        return FALSE;
    if (out_len)
        *out_len = (int)i;
    return TRUE;
}

/* UTF-16 is the other real possibility: the shell is a Japanese-authored
 * engine and the GMD holds UTF-8, but a widened copy in the element would be
 * unsurprising. Checked explicitly rather than assumed absent. */
static BOOL looks_like_wide_text(const char *s, SIZE_T n, int *out_len)
{
    SIZE_T i = 0;
    while (i + 1 < n) {
        unsigned char lo = (unsigned char)s[i], hi = (unsigned char)s[i + 1];
        if (lo == 0 && hi == 0)
            break;
        if (hi != 0)            /* non-Latin-1: stop claiming, stay strict */
            return FALSE;
        if (lo < 0x20 || lo > 0x7e)
            return FALSE;
        i += 2;
    }
    if (i < 4 || i + 1 >= n)
        return FALSE;
    if (out_len)
        *out_len = (int)(i / 2);
    return TRUE;
}

/* The shared inspector body. Returns TRUE if it examined the whole object;
 * *text_offset gets the winning offset or -1.
 *
 * ⭐⭐ IT PRINTS A POSITIVE COUNT OF WHAT IT EXAMINED. "0 candidates" from a
 * sweep that examined 0 fields and "0 candidates" from one that examined 64
 * are completely different findings, and a log that cannot tell them apart is
 * how "0 failed" gets read as "0 checked". */
static BOOL inspect_body(const void *obj, SIZE_T len,
                         pfn_resolve resolve, void *ctx,
                         int *text_offset, BOOL log_it)
{
    const unsigned char *b = (const unsigned char *)obj;
    int    best        = -1;
    int    best_len    = 0;
    BOOL   best_wide   = FALSE;
    BOOL   best_is_cand = FALSE;
    int    examined    = 0;      /* pointer-shaped fields actually resolved */
    int    hits        = 0;

    if (text_offset)
        *text_offset = -1;
    if (len < 8)
        return FALSE;

    for (SIZE_T off = 0; off + 8 <= len; off += 8) {
        ULONGLONG v;
        memcpy(&v, b + off, 8);

        /* A pointer field worth resolving. Zero and small integers are not
         * addresses; neither is anything above the user-mode ceiling. */
        if (v < 0x10000 || v > 0x7fffffffffffULL)
            continue;

        char buf[128];
        SIZE_T got = resolve(ctx, v, buf, sizeof(buf));
        examined++;
        if (got == 0)
            continue;

        int  slen = 0;
        BOOL wide = FALSE;
        if (looks_like_text(buf, got, &slen)) {
            wide = FALSE;
        } else if (looks_like_wide_text(buf, got, &slen)) {
            wide = TRUE;
        } else {
            continue;
        }

        hits++;
        BOOL is_cand = (off == GT_CAND_A || off == GT_CAND_B);
        if (log_it)
            log_line("gt:   +0x%03x -> 0x%llx  %s\"%.60s\" (%d chars)%s",
                     (unsigned)off, v, wide ? "L" : "", buf, slen,
                     is_cand ? "   <- OFFLINE CANDIDATE" : "");

        /* ⭐ A candidate offset wins ties, but never beats a longer string at
         * another offset. The offline accessors earned these two offsets a
         * look, not a verdict. */
        if (best < 0 ||
            (is_cand && !best_is_cand && slen >= best_len) ||
            (slen > best_len && (is_cand || !best_is_cand))) {
            best = (int)off; best_len = slen;
            best_wide = wide; best_is_cand = is_cand;
        }
    }

    /* Also look for the string stored INLINE rather than behind a pointer --
     * a fixed char array in the object is just as likely as a pointer, and a
     * sweep that only followed pointers would miss it completely. */
    for (SIZE_T off = 0; off + 4 <= len; off += 4) {
        int slen = 0;
        if (looks_like_text((const char *)b + off, len - off, &slen) &&
            slen >= 3) {
            if (log_it)
                log_line("gt:   +0x%03x INLINE \"%.60s\" (%d chars)",
                         (unsigned)off, (const char *)b + off, slen);
            hits++;
            if (best < 0) { best = (int)off; best_len = slen; }
            /* Skip past what we just read, so one string is not reported
             * again at every offset inside itself. */
            off += (SIZE_T)slen & ~3u;
        }
    }

    if (log_it)
        log_line("gt: examined %d pointer-shaped field(s) in %u bytes; "
                 "%d resolved to text; conclusion: %s",
                 examined, (unsigned)len, hits,
                 best < 0 ? "NONE -- no offset holds readable text"
                          : "see the winning offset below");
    if (log_it && best >= 0)
        log_line("gt: ⭐ best candidate is +0x%03x (%s%d chars)%s",
                 (unsigned)best, best_wide ? "wide, " : "", best_len,
                 best_is_cand ? " -- and it is one of the two offsets the "
                                "offline accessors pointed at"
                              : " -- ⚠ NOT one of the offline candidates");

    if (text_offset)
        *text_offset = best;
    return TRUE;
}

/* --- resolving a pointer in the live game -------------------------------- */

/* ⚠⚠ EVERY read is VirtualQuery-checked and guarded, EVERY time. A GUI object
 * is freed when its screen changes; a pointer that passed a check a moment ago
 * is not a pointer that is still valid. Caching a check here would be exactly
 * the mistake that killed the game once already. */
/* ⚠ Now referenced only by the test seam: the shipping code no longer searches
 * an object for its string, because setText hands it to us. Kept because
 * gt_test_inspect() still exercises the inspector, and because if a FUTURE
 * class has to be cracked the same way, this is the machinery to reuse -- with
 * the caveat that six runs showed searching is the slow route and finding the
 * writer is the fast one. */
__attribute__((unused))
static SIZE_T resolve_live(void *ctx, ULONGLONG addr, char *out, SIZE_T max)
{
    (void)ctx;
    const void *p = (const void *)(ULONG_PTR)addr;

    SIZE_T run = mem_readable_run(p, max);
    if (run == 0)
        return 0;
    if (run > max)
        run = max;

    /* ⚠ mem_copy is itself guarded, but count a fault so a partial sweep can
     * never be reported as a complete one. A scan that says "found nothing"
     * while faults are nonzero was a PARTIAL scan and must say so. */
    if (!mem_copy(out, p, run)) {
        InterlockedIncrement(&g_faults);
        return 0;
    }
    return run;
}

void gt_note_focus_move(void)
{
    InterlockedIncrement(&g_move_seq);
    InterlockedIncrement(&g_move_notifications);
}

/* See the declaration of g_focus_test_mode above for why this exists. */
void gt_test_focus_enable(void) { g_focus_test_mode = TRUE; }

/* Test-only: the window clear that gt_focus_report() performs, without needing
 * a real install to reach it. ⚠ It clears the same field by the same rule, so
 * the test drives the shipping behaviour rather than a copy of it. */
void gt_test_focus_clear(void) { g_focus_count = 0; }

/* ⭐⭐ The most recent PROSE string the game set, with NO movement stamp.
 *
 * ⛔⛔ THIS IS DELIBERATELY THE TIMING JOIN gt_take_caption() REFUSES, and it is
 * safe here for one reason only: the caller matches the text against a KNOWN
 * SET of strings before acting on it. A modal prompt identifies itself by its
 * own content, so "which movement did this belong to?" -- the question the
 * stamp exists to answer -- does not arise.
 *
 * ⚠ DO NOT use this to name a menu ITEM. That is exactly the join that speaks
 * one row's caption on another row, and the stamp exists because 3 of 6 early
 * captures coincided with no cursor movement at all.
 *
 * ⚠ Needed because the quit dialogs produce ZERO cursor movements, so every
 * movement-stamped channel is structurally blind to them. */
BOOL gt_prose_at(int nth, char *out, int outlen)
{
    BOOL got = FALSE;
    int  seen = 0;

    if (!out || outlen <= 0 || nth < 0)
        return FALSE;
    out[0] = '\0';
    if (!g_installed && !g_focus_test_mode)
        return FALSE;
    if (InterlockedCompareExchange(&g_focus_busy, 1, 0) != 0)
        return FALSE;
    /* ⛔⛔ NEWEST-FIRST, BUT ENUMERABLE. Returning ONLY the newest prose was the
     * bug that made the modal announcer do nothing in the game: the quit dialog
     * sets the prompt, then "Yes", then "No" -- all prose -- so "the newest" is
     * always "No" and the prompt is never examined. The caller has to be able to
     * look PAST the most recent string. */
    for (LONG i = g_focus_count - 1; i >= 0; i--) {
        /* Prose only -- a markup strip is a button hint, never a prompt. */
        if (g_focus[i].text[0] && strchr(g_focus[i].text, '<') == NULL) {
            if (seen++ < nth)
                continue;
            strncpy(out, g_focus[i].text, (size_t)outlen - 1);
            out[outlen - 1] = '\0';
            got = TRUE;
            break;
        }
    }
    InterlockedExchange(&g_focus_busy, 0);
    return got;
}

BOOL gt_take_caption(char *out, int outlen)
{
    BOOL got = FALSE;

    if (!out || outlen <= 0)
        return FALSE;
    out[0] = '\0';
    if (!g_installed && !g_focus_test_mode)
        return FALSE;

    /* ⭐ The NEWEST record stamped with the CURRENT movement -- i.e. text the
     * game set after the cursor last moved. A record from an earlier movement
     * is stale by construction, and returning it is how a caption for the
     * previous row gets spoken for this one.
     *
     * ⛔ This is deliberately NOT "the most recent string": that is the timing
     * join the rethink note forbids. The stamp is set by the cursor hook, so
     * "belongs to this movement" is an ordering fact, not a time window. */
    if (InterlockedCompareExchange(&g_focus_busy, 1, 0) != 0)
        return FALSE;               /* never block the caller; try again later */
    {
        LONG seq = g_move_seq;
        for (LONG i = g_focus_count - 1; i >= 0; i--) {
            if (g_focus[i].move_seq != seq)
                break;              /* records are appended in order */
            /* Prefer a PROSE caption over a hint strip: the strip describes the
             * screen and does not change per row, so speaking it on every
             * movement would be noise. A caption has no markup. */
            if (strchr(g_focus[i].text, '<') == NULL) {
                strncpy(out, g_focus[i].text, (size_t)outlen - 1);
                out[outlen - 1] = '\0';
                got = TRUE;
                break;
            }
        }
    }
    InterlockedExchange(&g_focus_busy, 0);
    return got;
}

BOOL gt_focus_conclusive(long *records, long *moves)
{
    if (records) *records = (long)g_focus_count;
    if (moves)   *moves   = (long)g_move_notifications;
    /* ⭐ Conclusive only if the cursor side actually notified us. Records
     * without movements cannot answer "do captions track the cursor?" at all. */
    return g_move_notifications > 0;
}

void gt_instance_report(void)
{
    LONG n, dropped;

    /* ⚠ Test mode reaches this too -- the report now RECLAIMS dead slots, which
     * is behaviour a suite must be able to exercise, not just a printout. */
    if (!g_installed && !g_focus_test_mode) {
        log_line("gt: instance set: the hook is not installed; NOTHING was "
                 "measured.");
        return;
    }
    if (InterlockedCompareExchange(&g_inst_busy, 1, 0) != 0) {
        log_line("gt: instance set busy; report skipped this poll.");
        return;
    }
    n = g_inst_count;
    dropped = g_inst_evicted;

    log_line("gt: === INSTANCE SET: %ld live text object(s), %ld dropped "
             "(table full) ===", n, dropped);

    {
        LONG live = 0, stale = 0, reclaimed = 0;
        LONG seq = g_move_seq;
        for (LONG i = 0; i < n; i++) {
            /* ⚠ Re-validate every time. The pointer is not owned and a GUI
             * object is freed on a screen change; a vptr check is what catches
             * a freed-and-reallocated slot. */
            ULONGLONG vptr = 0;
            BOOL faulted = FALSE;
            BOOL ours = FALSE;
            void *o = g_inst[i].obj;
            if (o && !((ULONG_PTR)o & 7u) &&
                mem_readable_run(o, sizeof(vptr)) >= sizeof(vptr)) {
                MEM_GUARDED(faulted, vptr = *(volatile ULONGLONG *)o);
                ours = !faulted &&
                       (vptr == GT_VTABLE_ADDR || vptr == GT_VTABLE_MSG);
            }
            if (ours) live++; else stale++;

            log_line("gt:   %p %s [move %ld, %ld set(s)] \"%.70s\"",
                     o, ours ? "LIVE " : "STALE",
                     g_inst[i].move_seq, g_inst[i].sets, g_inst[i].text);

            /* ⭐⭐ RECLAIM IT HERE, not merely count it. Eviction only runs when
             * a NEW object needs a slot, so on a screen that keeps re-setting
             * its resident objects nothing triggers it and dead entries
             * accumulate. Measured on the lobby settings screen: 64 objects,
             * 31 LIVE, 33 STALE -- and that screen needs 16 slots of its own,
             * so a label it DID draw ("Passcode") may have been squeezed out.
             *
             * ⚠ This pass has already proven the entry dead by the same test
             * eviction uses, and holds the same lock, so freeing the slot costs
             * one store and cannot race. */
            if (!ours) {
                g_inst[i].obj = NULL;
                g_inst[i].text[0] = '\0';
                g_inst[i].sets = 0;
                reclaimed++;
            }
        }
        /* ⭐ A POSITIVE count of what was examined, and of what is still a
         * valid object -- "0 live" from 64 entries and from none are entirely
         * different findings. */
        /* ⭐ STALE COUNT IS NOW A HEALTH READING, not just a description. The
         * table reclaims dead slots on insert, so a full table still carrying
         * many stale entries means reclaim is not keeping up (or is broken) --
         * and the numbered-item lookup depends on this table holding the
         * CURRENT screen. Saying so in the log is what makes the next run able
         * to answer it without another code change. */
        log_line("gt: of %ld object(s): %ld still LIVE, %ld stale; current "
                 "move seq %ld.", n, live, stale, seq);
        if (reclaimed)
            log_line("gt: ⭐ reclaimed %ld dead slot(s) this pass -- eviction "
                     "alone only fires when a NEW object needs a slot, so a "
                     "screen that re-sets its resident objects never triggers "
                     "it and the dead pile up.", reclaimed);
        if (n >= GT_INST_MAX)
            log_line("gt: ⚠ the table is FULL (%ld slots, %ld eviction(s) so "
                     "far). %ld stale entr%s present -- dead slots are "
                     "reclaimed before live ones, so a large stale count here "
                     "means reclaim is not keeping up.",
                     n, dropped, stale, stale == 1 ? "y" : "ies");
        log_line("gt: ▶ READ IT LIKE THIS: if the LIVE objects include one per "
                 "menu row of the screen you are on, the instance route works "
                 "and item names can come from here for ANY screen -- Gallery, "
                 "the music list and the consent dialog included. If the live "
                 "set is dominated by objects from OTHER screens, the next "
                 "question is how the game marks the current one. ⚠ Nothing "
                 "here is spoken; this is a measurement.");
    }
    InterlockedExchange(&g_inst_busy, 0);
}

/* Does `s` begin with the decimal number `n` followed by '.'?
 *
 * ⚠ Exact-width comparison, not atoi(): atoi("0123") and atoi("12") would both
 * satisfy a numeric test for values they should not, and a prefix compare
 * against "1." would match "1." inside "11.". Formatting the expected prefix and
 * comparing the whole thing is the form with no such gap.
 *
 * ⭐ Both widths are accepted ("1." and "01.") because the padding is the
 * game's presentation choice, not part of the identity. The music list pads to
 * two digits; nothing guarantees another numbered list will. */
static BOOL text_has_number_prefix(const char *s, int n)
{
    char pad[16], bare[16];
    if (!s || n < 0) return FALSE;
    _snprintf(pad,  sizeof(pad),  "%02d.", n);
    _snprintf(bare, sizeof(bare), "%d.",   n);
    pad[sizeof(pad) - 1] = 0; bare[sizeof(bare) - 1] = 0;
    return strncmp(s, pad,  strlen(pad))  == 0 ||
           strncmp(s, bare, strlen(bare)) == 0;
}

/* Is this object's text a plausible VALUE, or screen furniture to skip?
 * ⚠ Deliberately strict: help captions end in punctuation or run long, and hint
 * strips carry Confirm/Back. Everything else is a candidate. */
static BOOL rv_usable(const char *s)
{
    size_t n;
    if (!s || !s[0])
        return FALSE;
    n = strlen(s);
    if (n > 40)
        return FALSE;
    if (s[n - 1] == '.' || s[n - 1] == '!' || s[n - 1] == '?')
        return FALSE;
    if (strstr(s, "Confirm") || strstr(s, "Back"))
        return FALSE;
    return TRUE;
}

BOOL gt_row_value(const char *const *labels, int nlabels, int row,
                  char *out, int outlen)
{
    if (out && outlen > 0) out[0] = 0;
    if ((!g_installed && !g_focus_test_mode) || !labels || nlabels <= 0 ||
        row < 0 || row >= nlabels || !out || outlen <= 0)
        return FALSE;
    if (InterlockedCompareExchange(&g_inst_busy, 1, 0) != 0)
        return FALSE;

    volatile int found = 0;
    LONG n = g_inst_count;
    /* Find a CONSECUTIVE run of the row labels. See the header for why merely
     * "in order" is not enough. */
    for (LONG start = 0; start + nlabels <= n && !found; start++) {
        int k;
        for (k = 0; k < nlabels; k++) {
            if (strcmp(g_inst[start + k].text, labels[k]) != 0)
                break;
        }
        if (k != nlabels)
            continue;
        /* The values follow, skipping prose and hint strips. */
        int want = row, i;
        for (i = start + nlabels; i < n; i++) {
            if (!rv_usable(g_inst[i].text))
                continue;
            if (want-- == 0) {
                strncpy(out, g_inst[i].text, (size_t)outlen - 1);
                out[outlen - 1] = 0;
                found = 1;
                break;
            }
        }
    }
    InterlockedExchange(&g_inst_busy, 0);
    if (!found) out[0] = 0;
    return found ? TRUE : FALSE;
}

BOOL gt_numbered_item(int n, char *out, int outlen)
{
    if (out && outlen > 0) out[0] = 0;
    if ((!g_installed && !g_focus_test_mode) || !out || outlen <= 0 || n < 0)
        return FALSE;
    /* Never block the render thread's hook; a missed lookup is silence. */
    if (InterlockedCompareExchange(&g_inst_busy, 1, 0) != 0)
        return FALSE;

    /* ⚠⚠ volatile because MEM_GUARDED is setjmp/longjmp: a fault inside the
     * loop unwinds to the guard, and a non-volatile local held in a register
     * across that jump is restored to an INDETERMINATE value. `matches` deciding
     * whether a name is spoken and `i` driving the loop are the two worst
     * variables in this function to have quietly rewound.
     *
     * ⭐ -Wclobbered caught this on the first code written after it was added to
     * build.ps1 -- the flag was kept from a reverted change for exactly this
     * reason, and it earned its place immediately. */
    volatile int matches = 0;
    LONG n_ent = g_inst_count;
    for (volatile LONG i = 0; i < n_ent; i++) {
        if (!text_has_number_prefix(g_inst[i].text, n))
            continue;
        /* ⚠ Re-validate the pointer exactly as gt_instance_report does. An
         * entry survives until eviction, so a stale object from a screen left
         * long ago can still hold matching text -- and 30 of 64 entries were
         * stale in the very run this was built from. */
        ULONGLONG vptr = 0;
        BOOL faulted = FALSE;
        void *o = g_inst[i].obj;
        if (!o || ((ULONG_PTR)o & 7u) ||
            mem_readable_run(o, sizeof(vptr)) < sizeof(vptr))
            continue;
        MEM_GUARDED(faulted, vptr = *(volatile ULONGLONG *)o);
        if (faulted || (vptr != GT_VTABLE_ADDR && vptr != GT_VTABLE_MSG))
            continue;
        matches++;
        if (matches == 1) {
            strncpy(out, g_inst[i].text, (size_t)outlen - 1);
            out[outlen - 1] = 0;
        }
    }
    InterlockedExchange(&g_inst_busy, 0);

    /* ⛔ TWO LIVE OBJECTS CLAIMING THE SAME NUMBER IS A CONTRADICTION. It means
     * the table holds another screen's numbered list as well as this one's, and
     * there is no evidence saying which is on screen. Refuse both. */
    if (matches != 1) {
        if (matches > 1)
            log_line("gt: ⚠ %d live objects claim item number %d -- ambiguous, "
                     "naming none.", matches, n);
        out[0] = 0;
        return FALSE;
    }
    return TRUE;
}

/* ⭐⭐⭐ POSITION -> LIVE OBJECT, BY THE OBJECT'S OWN SCREEN COORDINATES.
 *
 * The join two screens were blocked on. A cGUIObjText stores where it is:
 *     +0x040  float X        +0x044  float Y
 * verified in run-2026-09-11-0317 against screens whose row order was ALREADY
 * established independently (Online Play 6 of 6, Lobby 3 of 3, 0 contradictions),
 * and again in ...-0322 on the lobby-ID field itself.
 *
 * ⛔⛔ WHY NOTHING ELSE WILL DO. On the lobby field's six slots, +0x050 reads
 * 0a, 0c, 0b, 0d, 0e, 0f -- slots 2 and 3 INVERTED -- and the instance set's
 * insertion order puts the edited slot FIRST in one run and LAST in another.
 * Either key silently swaps two characters of a code the user is spelling out.
 * The coordinate is the only one that orders them correctly.
 *
 * ⭐ THE AXIS IS DERIVED, NOT ASSUMED. A menu column varies in Y at constant X;
 * a strip varies in X at constant Y. This measures the spread of both across
 * the candidates and sorts by whichever actually varies, so the same function
 * serves the vertical menus and the horizontal lobby field. Assuming Y would
 * have been right for every screen that motivated this and wrong for the one
 * it was built for.
 *
 * ⚠ Candidates are filtered by `match`, supplied by the caller, because the
 * instance set holds several screens at once (30 of 64 entries were stale in
 * the run this was built from). Coordinates order a set; they do not decide
 * membership of it.
 */
/* ⚠ Named, not anonymous: two anonymous struct declarations are DISTINCT types
 * in C, so the swap below cannot assign one to the other. */
typedef struct { float x, y; const char *text; } gt_cand;

BOOL gt_item_at_position(int position, int total, gt_cand_fn match, void *ctx,
                         char *out, int outlen)
{
    gt_cand cand[GT_INST_MAX];
    volatile int n_cand = 0;

    if (out && outlen > 0) out[0] = 0;
    if ((!g_installed && !g_focus_test_mode) || !out || outlen <= 0)
        return FALSE;
    if (position < 1 || total < 1 || position > total)
        return FALSE;
    if (InterlockedCompareExchange(&g_inst_busy, 1, 0) != 0)
        return FALSE;

    LONG n_ent = g_inst_count;
    for (volatile LONG i = 0; i < n_ent && n_cand < GT_INST_MAX; i++) {
        void *o = g_inst[i].obj;
        ULONGLONG vptr = 0;
        BOOL faulted = FALSE;
        unsigned char buf[GT_CLASS_SIZE];

        if (!o || ((ULONG_PTR)o & 7u))
            continue;
        if (!g_inst[i].text[0])
            continue;
        if (match && !match(g_inst[i].text, ctx))
            continue;
        /* ⚠ Re-validate: an entry survives until eviction, so a stale object
         * from a screen left long ago can still hold matching text. */
        if (mem_readable_run(o, sizeof(vptr)) < sizeof(vptr))
            continue;
        MEM_GUARDED(faulted, vptr = *(volatile ULONGLONG *)o);
        if (faulted || (vptr != GT_VTABLE_ADDR && vptr != GT_VTABLE_MSG))
            continue;
        /* ⛔⛔ mem_readable_run FIRST, then the guard. A guarded memcpy across
         * a bad page is NOT reliably caught -- case C of run_span_test. */
        if (mem_readable_run(o, GT_CLASS_SIZE) < GT_CLASS_SIZE)
            continue;
        if (!mem_copy(buf, o, GT_CLASS_SIZE))
            continue;

        memcpy((void *)&cand[n_cand].x, buf + 0x40, sizeof(float));
        memcpy((void *)&cand[n_cand].y, buf + 0x44, sizeof(float));
        /* ⚠ Reject NaN/inf outright: they make a comparison sort inconsistent,
         * which is undefined behaviour, not merely a wrong order. */
        if (!(cand[n_cand].x == cand[n_cand].x) ||
            !(cand[n_cand].y == cand[n_cand].y))
            continue;
        cand[n_cand].text = g_inst[i].text;
        n_cand++;
    }

    /* ⛔ THE COUNT MUST MATCH THE SCREEN. Fewer candidates than the cursor
     * reports means some rows are missing from the set and position N is not
     * the Nth candidate; more means another screen's objects are mixed in.
     * Either way the ordering is not this screen's, so refuse. */
    if (n_cand != total) {
        if (n_cand)
            log_line("gt: position %d of %d -- %d candidate object(s), "
                     "count MISMATCH, naming none.", position, total, n_cand);
        InterlockedExchange(&g_inst_busy, 0);
        return FALSE;
    }

    /* ⭐ Which axis is this screen laid out on? Sort by the one that VARIES. */
    {
        float xmin = cand[0].x, xmax = cand[0].x;
        float ymin = cand[0].y, ymax = cand[0].y;
        for (int i = 1; i < n_cand; i++) {
            if (cand[i].x < xmin) xmin = cand[i].x;
            if (cand[i].x > xmax) xmax = cand[i].x;
            if (cand[i].y < ymin) ymin = cand[i].y;
            if (cand[i].y > ymax) ymax = cand[i].y;
        }
        BOOL by_x = (xmax - xmin) > (ymax - ymin);

        /* Insertion sort: n is at most 64 and typically under 10. */
        for (int i = 1; i < n_cand; i++) {
            for (int j = i; j > 0; j--) {
                float a = by_x ? cand[j].x     : cand[j].y;
                float b = by_x ? cand[j - 1].x : cand[j - 1].y;
                if (a >= b) break;
                gt_cand t = cand[j];
                cand[j] = cand[j - 1];
                cand[j - 1] = t;
            }
        }

        /* ⛔ TWO OBJECTS AT THE SAME COORDINATE CANNOT BE ORDERED. Speaking
         * either would be a coin flip on which row the user is standing on. */
        for (int i = 1; i < n_cand; i++) {
            float a = by_x ? cand[i].x     : cand[i].y;
            float b = by_x ? cand[i - 1].x : cand[i - 1].y;
            if (a == b) {
                log_line("gt: position %d of %d -- two objects share a "
                         "coordinate, order is UNDEFINED, naming none.",
                         position, total);
                InterlockedExchange(&g_inst_busy, 0);
                return FALSE;
            }
        }

        strncpy(out, cand[position - 1].text, (size_t)outlen - 1);
        out[outlen - 1] = 0;
    }

    InterlockedExchange(&g_inst_busy, 0);
    return out[0] != 0;
}

/* --- the focus differ (see gui_text.h) ----------------------------------- */

#define FD_MAX 16          /* objects tracked per screen; a strip has 8-9 */

typedef struct {
    void *obj;
    unsigned char snap[GT_CLASS_SIZE];
    BOOL  have;
} fd_entry;

static fd_entry     g_fd[FD_MAX];
static volatile LONG g_fd_on;
/* -1 = the differ has not run. See gt_test_focus_diff_changed(). */
static volatile LONG g_fd_last = -1;

/* ⚠ The opt-in FILE is checked by mod.c, which owns that helper, and the answer
 * is handed in here -- the same shape as the gt hook's own switch. Duplicating
 * the file check in this translation unit would give two places for the switch
 * to disagree. */
void gt_focus_diff_enable(BOOL on) { InterlockedExchange(&g_fd_on, on ? 1 : 0); }

static BOOL fd_enabled(void) { return g_fd_on == 1; }

/* ⚠⚠ CALLED FROM THE WORKER THREAD, never the render thread. The cursor hook
 * runs on the game's render thread and its contract is "two atomics and nothing
 * else"; this does guarded reads of 528 bytes per object plus logging, which has
 * no business there. gt_note_focus_move() already advances g_move_seq from the
 * hook, so the worker can tell a movement happened by watching that counter --
 * which is exactly what mc_poll_and_log's caption path already relies on.
 *
 * ⚠ A movement missed because two happened inside one poll costs one sample,
 * not a wrong one: the snapshot is always compared against the previous
 * snapshot, never against an assumed position. */
/* The diff itself, with no gate of its own. ⭐ Split out so the SAME comparison
 * can be driven by a cursor movement OR by a screen redraw -- the quit dialogs
 * produce no movements at all, so a movement-only differ was blind to exactly
 * the screen it was most needed on. */
static void fd_run(void)
{
    if (InterlockedCompareExchange(&g_inst_busy, 1, 0) != 0)
        return;

    /* ⭐ Report BEFORE re-snapshotting, so each line describes the movement
     * that just happened rather than the one before it. */
    /* ⚠ volatile across MEM_GUARDED's longjmp -- see gt_numbered_item for the
     * same hazard; -Wclobbered caught both. */
    volatile int changed_objs = 0;
    LONG n = g_inst_count;
    for (volatile LONG i = 0; i < n && i < FD_MAX; i++) {
        void *o = g_inst[i].obj;
        if (!o || ((ULONG_PTR)o & 7u))
            continue;
        /* ⚠ Validate exactly as everything else here does: an entry survives
         * until eviction, and a freed object's bytes are not this screen's. */
        volatile ULONGLONG vptr = 0;
        volatile BOOL faulted = FALSE;
        if (mem_readable_run(o, sizeof(ULONGLONG)) < sizeof(ULONGLONG))
            continue;
        MEM_GUARDED(faulted, vptr = *(volatile ULONGLONG *)o);
        if (faulted || (vptr != GT_VTABLE_ADDR && vptr != GT_VTABLE_MSG))
            continue;
        if (mem_readable_run(o, GT_CLASS_SIZE) < GT_CLASS_SIZE)
            continue;

        unsigned char cur[GT_CLASS_SIZE];
        if (!mem_copy(cur, o, GT_CLASS_SIZE))
            continue;

        /* Find this object's previous snapshot. */
        fd_entry *e = NULL;
        for (int k = 0; k < FD_MAX; k++) {
            if (g_fd[k].have && g_fd[k].obj == o) { e = &g_fd[k]; break; }
        }
        if (!e) {
            for (int k = 0; k < FD_MAX; k++) {
                if (!g_fd[k].have) { e = &g_fd[k]; break; }
            }
            if (!e) continue;                 /* table full: skip, never evict */
            e->obj = o; e->have = TRUE;
            memcpy(e->snap, cur, GT_CLASS_SIZE);
            continue;                         /* nothing to diff yet */
        }

        /* Report the changed offsets for this object, as OFFSETS -- the raw
         * measurement. Naming a field would be a claim; this is evidence. */
        char line[400];
        int used = 0, nch = 0;
        for (int off = 0; off < GT_CLASS_SIZE && used < (int)sizeof(line) - 24;
             off += 4) {
            if (memcmp(e->snap + off, cur + off, 4) == 0)
                continue;
            nch++;
            used += _snprintf(line + used, sizeof(line) - used - 1,
                              "%s+0x%03x:%08x->%08x", used ? " " : "",
                              off, *(unsigned *)(e->snap + off),
                              *(unsigned *)(cur + off));
        }
        line[sizeof(line) - 1] = '\0';
        if (nch) {
            changed_objs++;
            log_line("fdiff:   %p \"%.28s\" %d dword(s): %s",
                     o, g_inst[i].text, nch, line);
        }
        memcpy(e->snap, cur, GT_CLASS_SIZE);
    }

    /* ⭐⭐ THE CONTROL, stated every time. If EVERY object changed, what
     * changed is a ticker (animation phase, a frame counter) and says nothing
     * about focus. A selection marker moves on ONE object -- or on two, the one
     * leaving focus and the one entering it. */
    InterlockedExchange(&g_fd_last, changed_objs);
    log_line("fdiff: move %ld -- %d of %ld tracked object(s) changed. ▶ READ IT "
             "LIKE THIS: 1-2 changed = a focus candidate; ALL changed = a "
             "ticker, not focus; 0 changed = selection is not a field of these "
             "objects and lives on a parent or in the animation system.",
             g_move_seq, changed_objs, n < FD_MAX ? n : FD_MAX);

    InterlockedExchange(&g_inst_busy, 0);
}

void gt_focus_diff_note_move(void)
{
    if (!fd_enabled() || (!g_installed && !g_focus_test_mode))
        return;
    /* Only when the cursor actually moved since the last look. */
    static LONG last_seen = -1;
    LONG seq = g_move_seq;
    if (seq == last_seen)
        return;
    last_seen = seq;
    fd_run();
}

/* ⭐⭐ Sample because the SCREEN REDREW, not because the cursor moved.
 *
 * ⛔⛔ The quit dialogs move their selection without going through the cursor
 * helpers -- measured: 3 and 8 arrow presses, ZERO movements logged -- so
 * gt_focus_diff_note_move() can never fire on them, however long they are open.
 * A differ that cannot observe the one screen it is needed for is not a
 * measurement tool, and the run that would have proved that was nearly spent
 * before this was noticed.
 *
 * ⚠ The caller rate-limits this to once per beat; a screen that redraws
 * constantly (the music player's timer) would otherwise fill the log. */
void gt_focus_diff_note_text(void)
{
    if (!fd_enabled() || (!g_installed && !g_focus_test_mode))
        return;
    fd_run();
}

void gt_focus_diff_report(void)
{
    if (!fd_enabled())
        return;
    log_line("fdiff: the focus differ is ON (cfc2_focusdiff.on). It logs which "
             "bytes of each live text object change per cursor movement. "
             "⚠ Nothing here is spoken; it is a measurement.");
}

/* ⭐⭐ THE LAYOUT PROBE: find a field that orders objects by SCREEN POSITION.
 *
 * ⛔⛔ THE BUG IT EXISTS TO KILL. Two screens are blocked on the same thing --
 * the Lobby ID field (six single-character slots) and the Music chooser's
 * 9-item game strip. Both have their text on screen; on both, the caption
 * channel is silent (24 movements, ZERO captions on the music strip, confirmed
 * across every log). So the text is reachable and the POSITION is not.
 *
 * The tempting join is the instance set's insertion order. It is WRONG: in the
 * 1913 run the slot being edited is LAST in the list, in the 1945 run it is
 * FIRST -- whichever slot the user touched decides. A lookup by list position
 * passes each run individually and is wrong in general, which is exactly the
 * sabotage the music-track fixture exists to catch.
 *
 * ⭐ SO: SCREEN POSITION MUST COME FROM THE OBJECT, not from arrival order. A
 * cGUIObjText is a GUI widget and almost certainly stores its own coordinates.
 *
 * ⚠⚠ WHY THIS IS A DUMP AND NOT A DETECTOR. I do not know which offset holds a
 * coordinate, and "find the field that looks like an X" is precisely the
 * shape-matching that went 0 for 4 on this project (see the route 1-4 notes:
 * look-alikes are abundant and indistinguishable as data). This prints the
 * RAW FIELDS of each candidate object and leaves the identification to a human
 * comparing them against a screen whose left-to-right order is KNOWN. The
 * measurement is here; the claim is not.
 *
 * ⭐ THE CONTROL THAT MAKES IT READABLE: the lobby field's six slots are all
 * on screen at once, in a row. So the right offset is one holding six DISTINCT,
 * MONOTONIC values. A field that is constant across all six is chrome; a field
 * that changes every frame is a ticker. Both are visible here and neither can
 * masquerade as the answer.
 *
 * ⚠ The focus differ cannot do this job: it reports fields that CHANGE, and a
 * slot's position is STATIC. That is why this is a separate probe rather than
 * another mode of an existing one.
 *
 * Switch: cfc2_layout.on next to the exe. Absent = off, like every other aid
 * here -- a diagnostic that runs during play is a mod that stalls.
 */
static BOOL layout_enabled(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = (GetFileAttributesA("cfc2_layout.on") != INVALID_FILE_ATTRIBUTES);
    return cached != 0;
}

/* ⭐⭐⭐ THE MODAL SELECTION HUNT -- a TARGETED heap diff.
 *
 * ⛔ WHAT IS ALREADY RULED OUT, so this does not repeat it: the selection is not
 * in the "Yes"/"No" TEXT OBJECTS. Their differing fields (+0x124, +0x13c) are
 * byte-identical across four arrow presses, and the focus differ -- reporting
 * freely on other objects in the same run -- saw no change on either. Closed in
 * run-2026-09-11-1322.
 *
 * ⭐ THE LEAD THIS FOLLOWS: both option objects carry the SAME pointer at
 * +0x088, stable for the life of a dialog appearance and different between
 * appearances. A value shared by exactly the two widgets that form one control
 * is the control itself -- the natural place for "which one is selected".
 *
 * ⭐⭐ AND WHY THIS IS NOT THE BLIND SCAN THAT FAILED FOUR TIMES ON THIS
 * PROJECT. Those searched 1.4 GB for something selection-SHAPED, and look-alikes
 * are abundant and indistinguishable as data. This has an ANCHOR the game gave
 * us and a CONTROL the user performs: snapshot the parent, the user presses a
 * direction, snapshot again, and report only what CHANGED. A field that moves
 * when and only when the selection moves is not a look-alike.
 *
 * ⚠ It reports OFFSETS AND VALUES, never a conclusion. Naming a field "the
 * selection" from one movement is the mistake this file exists to avoid; two
 * presses in opposite directions, with the value returning, is what would
 * settle it -- and that is a judgement for a human reading the log.
 *
 * Switch: cfc2_modal.on. Absent = off.
 */
#define MODAL_PARENT_OFF 0x088
#define MODAL_SEL_OFF    0x38c          /* the selected index, in the parent */
#define MODAL_SNAP       0x400          /* bytes of the parent to watch */

static BOOL modal_enabled(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = (GetFileAttributesA("cfc2_modal.on") != INVALID_FILE_ATTRIBUTES);
    return cached != 0;
}

/* ⭐⭐⭐ THE MODAL SELECTION -- shipping, not a probe.
 *
 * Reads the selected index out of the parent both option widgets share.
 *
 *     parent = <either option object> + 0x088
 *     index  = *(int *)(parent + 0x38c)      0 = the TOP option, 1 = the next
 *
 * ⭐ ESTABLISHED BY TWO INDEPENDENT SOURCES, which is the standard every other
 * row in this project meets:
 *   1. LAYOUT -- "Yes" sits above "No" at y=540/600 in every run, so a
 *      top-to-bottom index makes 0=Yes and 1=No.
 *   2. OUTCOME -- on both confirms in run-...-1450 the index read 1 and the
 *      game KEPT RUNNING, i.e. the choice taken was No. 1=No from the game's
 *      own behaviour, agreeing with the layout.
 *
 * ⚠ AND THE MODEL IS A TOGGLE, NOT A DIRECTION. My first reading was
 * "UP gives 0, DOWN gives 1" from two samples, and the next run reversed it.
 * On a two-item list either direction wraps, so every press flips the value --
 * 6 of 6 samples across both runs fit that and none fit the direction model.
 * ⛔ Do not reintroduce a direction rule here.
 *
 * ⚠ Returns FALSE when no such dialog is on screen, which is every other
 * screen: the lookup is keyed on a live object whose text is "Yes".
 */
BOOL gt_modal_selection(int *out_index)
{
    void *parent = NULL;
    LONG n;
    BOOL got = FALSE;

    if (out_index)
        *out_index = -1;
    if (!out_index || (!g_installed && !g_focus_test_mode))
        return FALSE;
    if (InterlockedCompareExchange(&g_inst_busy, 1, 0) != 0)
        return FALSE;

    n = g_inst_count;
    for (LONG i = 0; i < n && !parent; i++) {
        void *o = g_inst[i].obj;
        ULONGLONG vptr = 0;
        BOOL faulted = FALSE;
        unsigned char buf[MODAL_PARENT_OFF + sizeof(void *)];

        if (!o || strcmp(g_inst[i].text, "Yes") != 0)
            continue;
        if (mem_readable_run(o, sizeof(vptr)) < sizeof(vptr))
            continue;
        MEM_GUARDED(faulted, vptr = *(volatile ULONGLONG *)o);
        if (faulted || (vptr != GT_VTABLE_ADDR && vptr != GT_VTABLE_MSG))
            continue;
        if (mem_readable_run(o, sizeof(buf)) < sizeof(buf))
            continue;
        if (!mem_copy(buf, o, sizeof(buf)))
            continue;
        memcpy(&parent, buf + MODAL_PARENT_OFF, sizeof(parent));
    }

    if (parent && !((ULONG_PTR)parent & 7u)) {
        unsigned char sel[4];
        const unsigned char *at = (const unsigned char *)parent + MODAL_SEL_OFF;
        /* ⛔⛔ mem_readable_run FIRST, then the guarded copy. */
        if (mem_readable_run(at, sizeof(sel)) >= sizeof(sel) &&
            mem_copy(sel, at, sizeof(sel))) {
            unsigned v = *(unsigned *)sel;
            /* ⚠ A two-option dialog. Anything else means this is not the
             * control we think it is, and a bare refusal beats a wrong word. */
            if (v <= 1) {
                *out_index = (int)v;
                got = TRUE;
            }
        }
    }

    InterlockedExchange(&g_inst_busy, 0);
    return got;
}

void gt_modal_probe(void)
{
    static void         *g_parent;
    static unsigned char g_snap[MODAL_SNAP];
    static BOOL          g_have;

    void *parent = NULL;
    unsigned char cur[MODAL_SNAP];
    LONG n;

    if (!modal_enabled() || (!g_installed && !g_focus_test_mode))
        return;
    if (InterlockedCompareExchange(&g_inst_busy, 1, 0) != 0)
        return;

    /* Find a live object whose text is "Yes" and read its +0x088. ⚠ Keyed on
     * the TEXT, so this probe is inert on every screen that has no such
     * widget -- it cannot wander onto another menu's objects. */
    n = g_inst_count;
    for (LONG i = 0; i < n && !parent; i++) {
        void *o = g_inst[i].obj;
        ULONGLONG vptr = 0;
        BOOL faulted = FALSE;
        unsigned char buf[MODAL_PARENT_OFF + sizeof(void *)];

        if (!o || strcmp(g_inst[i].text, "Yes") != 0)
            continue;
        if (mem_readable_run(o, sizeof(vptr)) < sizeof(vptr))
            continue;
        MEM_GUARDED(faulted, vptr = *(volatile ULONGLONG *)o);
        if (faulted || (vptr != GT_VTABLE_ADDR && vptr != GT_VTABLE_MSG))
            continue;
        /* ⛔⛔ mem_readable_run FIRST, then the guard -- a guarded memcpy over a
         * bad page is NOT reliably caught (case C of run_span_test). */
        if (mem_readable_run(o, sizeof(buf)) < sizeof(buf))
            continue;
        if (!mem_copy(buf, o, sizeof(buf)))
            continue;
        memcpy(&parent, buf + MODAL_PARENT_OFF, sizeof(parent));
    }

    if (!parent) {
        /* No dialog on screen. Drop the baseline so the NEXT appearance starts
         * clean rather than diffing against a dead object's bytes. */
        g_have = FALSE;
        g_parent = NULL;
        InterlockedExchange(&g_inst_busy, 0);
        return;
    }

    if (((ULONG_PTR)parent & 7u) ||
        mem_readable_run(parent, MODAL_SNAP) < MODAL_SNAP ||
        !mem_copy(cur, parent, MODAL_SNAP)) {
        log_line("modal: parent %p is not readable for %d bytes -- NOTHING was "
                 "measured (this is not a negative result).",
                 parent, MODAL_SNAP);
        InterlockedExchange(&g_inst_busy, 0);
        return;
    }

    if (!g_have || parent != g_parent) {
        memcpy(g_snap, cur, MODAL_SNAP);
        g_parent = parent;
        g_have = TRUE;
        log_line("modal: watching parent %p (%d bytes). ▶ Press a direction on "
                 "the dialog; each change is logged as offset:old->new. A field "
                 "that moves ONLY with the selection, and RETURNS when you move "
                 "back, is the candidate.", parent, MODAL_SNAP);
        InterlockedExchange(&g_inst_busy, 0);
        return;
    }

    {
        char line[600];
        int used = 0, nch = 0;
        for (int off = 0; off < MODAL_SNAP && used < (int)sizeof(line) - 28;
             off += 4) {
            if (memcmp(g_snap + off, cur + off, 4) == 0)
                continue;
            nch++;
            used += _snprintf(line + used, sizeof(line) - used - 1,
                              "%s+0x%03x:%08x->%08x", used ? " " : "",
                              off, *(unsigned *)(g_snap + off),
                              *(unsigned *)(cur + off));
        }
        line[sizeof(line) - 1] = '\0';
        if (nch)
            log_line("modal: parent %p -- %d dword(s) changed: %s",
                     parent, nch, line);

        /* ⭐⭐ THE CANDIDATE, REPORTED EVERY SAMPLE whether it moved or not.
         *
         * +0x38c held 1 and went to 0 on UP, then back to 1 on DOWN -- the
         * ROUND TRIP that distinguishes a selection index from a counter or an
         * animation ramp (the +0x13c..+0x148 block climbs monotonically as
         * floats and never returns). It is 0/1 as an INTEGER, which as a float
         * would be a denormal: index values, not geometry.
         *
         * ⛔ But that is TWO transitions, and a two-sample finding is how this
         * project has gone wrong before. Logging it unconditionally means the
         * next run reports its value at every sample, including the presses
         * that fall between diffs -- which is what turns two samples into a
         * count. ⚠ Reported, never spoken: nothing acts on this yet. */
        log_line("modal: +0x38c = %u  (candidate selection index; UP was 0, "
                 "DOWN was 1)", *(unsigned *)(cur + 0x38c));

        memcpy(g_snap, cur, MODAL_SNAP);
    }

    InterlockedExchange(&g_inst_busy, 0);
}

void gt_layout_report(void)
{
    LONG n;
    int shown = 0;

    if (!layout_enabled())
        return;

    if (!g_installed) {
        log_line("layout: the hook is not installed; NOTHING was measured.");
        return;
    }
    if (InterlockedCompareExchange(&g_inst_busy, 1, 0) != 0) {
        log_line("layout: instance set busy; report skipped this poll.");
        return;
    }

    n = g_inst_count;
    log_line("layout: === FIELD DUMP of live text objects (%ld in set) ===", n);
    log_line("layout: ▶ READ IT LIKE THIS: on a screen whose items sit in a "
             "known left-to-right (or top-to-bottom) order, look for an offset "
             "holding DISTINCT values that follow that order. Constant across "
             "all = chrome. Different every poll = a ticker. Neither is a "
             "coordinate.");

    for (LONG i = 0; i < n; i++) {
        void *o = g_inst[i].obj;
        ULONGLONG vptr = 0;
        BOOL faulted = FALSE;
        unsigned char cur[GT_CLASS_SIZE];
        char line[600];
        int used = 0;

        if (!o)
            continue;
        /* ⚠ The pointer is not ours and a GUI object is freed on a screen
         * change. Re-validate exactly as every other reader here does. */
        if (mem_readable_run(o, sizeof(ULONGLONG)) < sizeof(ULONGLONG))
            continue;
        MEM_GUARDED(faulted, vptr = *(volatile ULONGLONG *)o);
        if (faulted || (vptr != GT_VTABLE_ADDR && vptr != GT_VTABLE_MSG))
            continue;
        /* ⛔⛔ mem_readable_run FIRST, always. A guarded memcpy across a bad
         * page is NOT reliably caught -- measured in this project, case C of
         * run_span_test. The guard is a net for the race, never the check. */
        if (mem_readable_run(o, GT_CLASS_SIZE) < GT_CLASS_SIZE)
            continue;
        if (!mem_copy(cur, o, GT_CLASS_SIZE))
            continue;

        /* ⭐ Only SHORT texts. The slots and the strip entries are short; the
         * prose blurbs and markup strips that share the set are not, and they
         * would bury the rows being compared. */
        if (!g_inst[i].text[0] || strlen(g_inst[i].text) > 48)
            continue;

        /* Dump the object's dwords as raw offsets. Naming a field would be a
         * claim; an offset and its value are the measurement. Offsets 0x00-0x0f
         * are the vptr and are skipped. */
        for (int off = 0x10; off < GT_CLASS_SIZE &&
                             used < (int)sizeof(line) - 24; off += 4) {
            unsigned v = *(unsigned *)(cur + off);
            if (!v)
                continue;               /* zeros carry no ordering information */
            used += _snprintf(line + used, sizeof(line) - used - 1,
                              "%s+0x%03x:%08x", used ? " " : "", off, v);
        }
        line[sizeof(line) - 1] = '\0';
        log_line("layout:   %p \"%.24s\" %s", o, g_inst[i].text, line);
        shown++;
    }

    /* ⭐ A POSITIVE count of what was examined. "0 failed" must never be
     * mistakable for "0 checked" -- this project has shipped that once. */
    log_line("layout: %d object(s) dumped of %ld in the set (short text only).",
             shown, n);
    if (!shown)
        log_line("layout: ⚠ NOTHING was dumped -- no conclusion is available "
                 "from this poll, which is NOT the same as a negative result.");

    InterlockedExchange(&g_inst_busy, 0);
}

void gt_focus_report(void)
{
    LONG n, dropped, notes;

    if (!g_installed) {
        log_line("gt: focus recorder: the hook is not installed; "
                 "NOTHING was measured.");
        return;
    }

    if (InterlockedCompareExchange(&g_focus_busy, 1, 0) != 0) {
        log_line("gt: focus recorder busy; report skipped this poll.");
        return;
    }
    n       = g_focus_count;
    dropped = g_focus_dropped;
    notes   = g_move_notifications;

    /* ⭐⭐ LIVENESS BEFORE FINDINGS. If the cursor side never notified us,
     * every record carries seq 0 and "no caption tracked the cursor" is
     * unfalsifiable -- indistinguishable from "the cursor never moved". Refuse
     * to conclude, exactly as iat_probe and kbd_state do.
     * (memory: inconclusive-must-be-a-return-value) */
    /* ⭐ One implementation of the verdict, shared with the tests. A report
     * that re-derived the rule could drift from the function callers trust --
     * a harness must not supply the mechanism under test, and neither must a
     * log line. (memory: harness-supplies-the-mechanism) */
    if (!gt_focus_conclusive(NULL, NULL)) {
        log_line("gt: ⚠ focus recorder INCONCLUSIVE -- %ld string(s) recorded "
                 "but the cursor hook never notified us (%ld movements). "
                 "Nothing can be concluded about per-row captions.", n, notes);
        InterlockedExchange(&g_focus_busy, 0);
        return;
    }

    log_line("gt: === FOCUS RECORD: %ld distinct string(s) across %ld cursor "
             "movement(s), %ld dropped (buffer full) ===", n, notes, dropped);

    {
        LONG with_move = 0;      /* strings that arrived after a movement */
        LONG prose     = 0;      /* strings with no markup = captions      */
        for (LONG i = 0; i < n; i++) {
            BOOL is_prose = (strchr(g_focus[i].text, '<') == NULL);
            if (g_focus[i].move_seq > 0) with_move++;
            if (is_prose) prose++;
            log_line("gt:   [move %ld] %s %p: \"%.90s\"",
                     g_focus[i].move_seq, is_prose ? "PROSE" : "strip",
                     g_focus[i].self, g_focus[i].text);
        }
        /* ⭐ A POSITIVE count of what was examined, so "0 captions" from a run
         * that saw 40 strings is distinguishable from one that saw none. */
        log_line("gt: of %ld string(s): %ld arrived after a cursor movement, "
                 "%ld were prose captions.", n, with_move, prose);
        /* ⭐ CLEAR THE WINDOW after reporting it.
         *
         * ⛔ Without this the recorder fills to GT_FOCUS_MAX and goes blind for
         * the rest of the session -- every later screen silently missing, with
         * only `dropped` to hint at it. The 22:50 run did not hit the cap, but
         * only because it ended after 40 s; case 19 shows the recorder returns
         * NOTHING once full. Each report therefore covers the window SINCE THE
         * LAST ONE, which also reads better than an ever-growing transcript.
         *
         * ⚠ The move sequence and the notification COUNT are deliberately NOT
         * reset: the sequence must keep advancing or a stale caption from
         * before the clear could match the reader's seq, and the count is the
         * liveness evidence for the whole session. */
        g_focus_count = 0;
        log_line("gt: ▶ READ IT LIKE THIS: a DIFFERENT prose string stamped "
                 "with each successive move number = per-row captions DO flow "
                 "through setText. The same few strings repeating across many "
                 "move numbers = they do NOT, and the existing cursor+GMD "
                 "channel stays the only item-name source.");
    }
    InterlockedExchange(&g_focus_busy, 0);
}

BOOL gt_poll_and_log(void)
{
    if (!g_installed)
        return FALSE;

    /* ⭐⭐ THE TEXT CHANNEL FIRST -- this is the product, everything below is
     * the old exploratory sweep.
     *
     * The setter hands us the string directly, so there is no object to
     * inspect, no offset to guess and no waiting for a field to be filled.
     * Snapshot it with the same seq-bracket discipline as every other channel
     * here: equal even values either side of the copy mean the copy was clean.
     *
     * ⚠ Logged on OUR thread, never in the detour. The hook copies bytes and
     * returns; formatting and file I/O on the game's render thread is how the
     * discovery sweep starved the mod three times. */
    if (InterlockedCompareExchange(&g_text_ready, 0, 1) == 1) {
        char  copy[GT_TEXT_MAX];
        void *who;
        /* Take the same claim flag the writer uses, so a hook running right now
         * cannot overwrite the buffer mid-copy.
         *
         * ⛔⛔ BOUNDED, NEVER AN UNBOUNDED SPIN. This was `while (cas != 0);`
         * with no limit. The writer is the GAME'S render thread: if it is
         * descheduled while holding the flag, this loop spins on OUR worker
         * forever and the whole mod freezes -- the same failure mode that
         * starved this mod three times before. It surfaced when a sabotage test
         * left the flag held and HUNG the suite; a hang is how an unbounded
         * wait announces itself, and I would rather it announced itself here.
         *
         * ⭐ So bound it and give up. Losing one sighting costs nothing -- the
         * same label is set hundreds of times a second and the next poll gets
         * it. A missed log line is invisible; a frozen mod is not. */
        int spins = 0;
        while (InterlockedCompareExchange(&g_text_busy, 1, 0) != 0) {
            if (++spins > 1000) {
                /* Put the flag back so the value is not lost for good. */
                InterlockedExchange(&g_text_ready, 1);
                return FALSE;
            }
        }
        who = g_text_self;
        strncpy(copy, g_text_buf, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = 0;
        InterlockedExchange(&g_text_busy, 0);

        /* ⭐ Log only DISTINCT strings. The same label is set hundreds of
         * times a second; printing every sighting would bury the log and tell
         * us nothing a single line does not. The count in the stats line is
         * what carries the volume. */
        if (strcmp(copy, g_text_last) != 0) {
            strncpy(g_text_last, copy, sizeof(g_text_last) - 1);
            g_text_last[sizeof(g_text_last) - 1] = 0;
            log_line("gt: * TEXT SET on %p: \"%s\"", who, copy);
            return TRUE;
        }
    }

    /* ⛔⛔ THE RING SWEEP IS GONE -- the setter replaced it.
     *
     * It existed to guess WHICH OFFSET held the string, by re-reading captured
     * objects until one looked populated. That question is answered: the text
     * arrives as an ARGUMENT to setText, so there is nothing to search for.
     *
     * ⛔ And it was actively harmful in the run of 2026-09-09 19:35. It printed
     * a REFUSAL line for every foreign object it swept -- about ten a second,
     * 1235 in all -- burying the log it was meant to inform. A diagnostic that
     * fires continuously is noise, not evidence.
     *
     * ⚠ Its counters (g_distinct, g_with_text, g_wrong_class) are kept and
     * still reported, so a reader comparing runs does not find them silently
     * missing. They simply stop moving. */
    return FALSE;
}

/* --- install -------------------------------------------------------------
 *
 * The expected first bytes of 0x1402fd6d0:
 *     push rbx; sub rsp,0x30   = 40 53 48 83 ec 30
 * SIX bytes across two whole instructions, so MinHook's five-byte patch ends
 * on a clean boundary. ⭐ And find_callers.py --jmp found zero branches into
 * addr+1..+4, so nothing can land inside the patch.
 *
 * ⭐⭐ THE PROPERTY THAT MATTERS MOST is not visible in these bytes: this
 * function has ZERO direct callers, so the vtable is the only way to reach it.
 * The previous target had a perfectly good prologue and was still wrong,
 * because it was a shared helper three sites called directly. A prologue check
 * confirms WHICH CODE we patched; it says nothing about WHOSE OBJECTS arrive.
 *
 * The prologue is re-checked here at runtime because the exe is Enigma-packed
 * and these addresses hold ciphertext until unpack completes; hooking early
 * would patch encrypted bytes. */
/* ⛔⛔ THREE TARGETS NOW SHARE THIS SHAPE AND DIFFER ONLY IN THE FRAME SIZE:
 * `push rbx; sub rsp,N`. Reusing one constant across two of them already cost
 * a run (0x20 vs 0x30), and correcting the vtable then changed this one from
 * 0x30 to 0x20 as well. So each target gets its OWN bytes, spelled out, and
 * case 9c pins them against the dump.
 *
 * ⭐ The general lesson: a prologue constant belongs to an ADDRESS, not to a
 * shape. Two functions starting with the same two instructions are still two
 * functions. */
static const BYTE GT_PROLOGUE[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20 };

/* ⛔⛔ THE CTOR NEEDS ITS OWN. It is `push rbx; sub rsp,0x20` -- the SAME shape
 * as the update method but a different frame size, so the last byte is 0x20,
 * not 0x30.
 *
 * The first version reused GT_PROLOGUE for both, and the ctor hook silently
 * declined to install for exactly that one byte. It cost a run. ⭐ It cost
 * only ONE run because the refusal was loud and the stats carried
 * `ctor_hooked`, so the log said "this zero measures nothing" instead of
 * "0 constructed" -- the difference between a wasted run and a wrong
 * conclusion. Two functions never share a prologue constant just because they
 * start with the same two instructions. */
static const BYTE GT_CTOR_PROLOGUE[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20 };

static BOOL check_target(void)
{
    void *p = (void *)(ULONG_PTR)GT_UPDATE_ADDR;

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi) ||
        mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        log_line("gt: 0x%llx is not committed executable memory -- REFUSING "
                 "to hook. Nothing was written.", GT_UPDATE_ADDR);
        g_why = GT_INSTALL_BAD_PROTECT;
        return FALSE;
    }

    BYTE got[sizeof(GT_PROLOGUE)];
    if (!mem_copy(got, p, sizeof(got)) ||
        memcmp(got, GT_PROLOGUE, sizeof(got)) != 0) {
        log_line("gt: the bytes at 0x%llx are not the expected prologue "
                 "(%02x %02x %02x %02x %02x) -- the offline model does NOT "
                 "match this build. REFUSING.",
                 GT_UPDATE_ADDR, got[0], got[1], got[2], got[3], got[4]);
        g_why = GT_INSTALL_BAD_PROLOGUE;
        return FALSE;
    }

    /* ⭐⭐ THE UNIQUENESS CLAIM, RE-CHECKED IN THIS PROCESS. The whole reason
     * no type check is needed in the detour is that this function address sits
     * in exactly one vtable slot -- cGUIObjText's. That was established from a
     * dump. Confirm the half of it we can confirm cheaply here: the class's
     * own vtable really does hold this address in the expected slot. If the
     * build moved, we would otherwise hook something and attribute its
     * instances to the wrong class. */
    /* ⭐ Checked against the INSTANCE vtable -- the one an object actually
     * carries, and the one the detour's filter tests. The DTI's descriptor
     * vtable (0x141056350) is a different, two-slot table; confusing the two
     * is half of what went wrong in the first four runs. */
    ULONGLONG slot = 0;
    const void *sp = (const void *)(ULONG_PTR)
                     (GT_VTABLE_ADDR + GT_VTABLE_SLOT * 8);
    if (!mem_copy(&slot, sp, sizeof(slot)) || slot != GT_UPDATE_ADDR) {
        log_line("gt: cGUIObjText's instance vtable slot %d holds 0x%llx, not "
                 "the "
                 "expected 0x%llx -- the class layout does not match the "
                 "offline model. REFUSING.",
                 GT_VTABLE_SLOT, slot, GT_UPDATE_ADDR);
        g_why = GT_INSTALL_BAD_VTABLE;
        return FALSE;
    }
    return TRUE;
}

BOOL gt_install(void)
{
    if (g_installed)
        return TRUE;

    /* ⛔⛔ THE DEPENDENCY WAS INVERTED, AND IT COST A RUN.
     *
     * The CTOR probe is the load-bearing hook: it is what proved the class is
     * instantiated (36 objects) and it is what captures the instances we
     * inspect. The update hook is now OPTIONAL -- five runs established that no
     * slot of this vtable is a usable per-frame method, and we no longer need
     * one.
     *
     * But install() checked the update target FIRST and returned FALSE on
     * mismatch, so when GT_VTABLE_ADDR was corrected to the real instance
     * vtable (0x141056b40) and GT_UPDATE_ADDR still named a slot of the OLD
     * one, the whole module refused to install -- ctor probe included. The run
     * produced nothing.
     *
     * ⭐ The guard was right to refuse: slot 13 of the new vtable is
     * 0x1402fd6a0, not 0x1402fd6d0, and hooking the stale address would have
     * patched a function belonging to another class. The bug was that a
     * FAILED OPTIONAL check aborted a MANDATORY one. Order the install by what
     * it depends on, not by what was written first. */
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        log_line("gt: MH_Initialize failed (%d).", (int)st);
        g_why = GT_INSTALL_MINHOOK_FAILED;
        return FALSE;
    }

    /* The update hook: attempted, never required. */
    void *t = (void *)(ULONG_PTR)GT_UPDATE_ADDR;
    if (!check_target()) {
        log_line("gt: ⚠ the update-method hook was NOT installed (see above). "
                 "That is survivable: it has never fired in five runs, and the "
                 "ctor probe below is what actually captures instances.");
        t = NULL;
    } else {
        log_line("gt: identity CONFIRMED -- 0x%llx carries the expected "
                 "prologue and sits in the instance vtable's slot %d.",
                 GT_UPDATE_ADDR, GT_VTABLE_SLOT);
    }
    if (t && MH_CreateHook(t, (void *)&hook_update,
                      (void **)&g_real_update) != MH_OK) {
        log_line("gt: ⚠ MH_CreateHook failed for the update method; "
                 "continuing without it.");
        t = NULL;
    }
    if (t && MH_EnableHook(t) != MH_OK) {
        log_line("gt: ⚠ MH_EnableHook failed for the update method; "
                 "continuing without it.");
        MH_RemoveHook(t);
        g_real_update = NULL;
    }

    /* ⭐⭐ The CONSTRUCTOR hook -- the birth probe, and THE hook this module
     * now rests on. It proved the class is instantiated (36 objects in the
     * 17:19 run) and it is what captures the instances the worker inspects.
     *
     * ⚠ If THIS fails, the module has nothing: no construction count and no
     * instances. Unlike the update hook above, a failure here is worth
     * reporting as a failed install -- see below. */
    void *c = (void *)(ULONG_PTR)GT_CTOR_ADDR;

    /* ⭐ The ctor gets the SAME protection guard as the update target. It was
     * missing, and the omission was only visible because case 10 asserts WHICH
     * layer refuses: with the game unmapped, install fell through to MinHook
     * (reason 5) instead of reporting BAD_PROTECT (reason 2). A bare
     * "install failed" assertion would have shown nothing.
     *
     * ⚠ This is the guard that matters most for the ctor: it is now the
     * load-bearing hook, so patching a bad address here breaks everything. */
    MEMORY_BASIC_INFORMATION cmbi;
    if (VirtualQuery(c, &cmbi, sizeof(cmbi)) != sizeof(cmbi) ||
        cmbi.State != MEM_COMMIT ||
        !(cmbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                          PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        log_line("gt: the ctor at 0x%llx is not committed executable memory -- "
                 "REFUSING to hook. Nothing was written.", GT_CTOR_ADDR);
        g_why = GT_INSTALL_BAD_PROTECT;
        return FALSE;
    }

    BYTE cpro[sizeof(GT_CTOR_PROLOGUE)];
    if (!mem_copy(cpro, c, sizeof(cpro)) ||
        memcmp(cpro, GT_CTOR_PROLOGUE, sizeof(cpro)) != 0) {
        /* ⭐ PRINT WHAT WE SAW, not just that it mismatched. The first version
         * of this line said only "does not carry the expected prologue", and
         * the cause was a single byte (0x20 vs 0x30) that the message could
         * have shown. A refusal that does not say what it saw makes the reader
         * re-derive the disassembly to find a one-byte bug. */
        log_line("gt: ⚠ the ctor at 0x%llx carries %02x %02x %02x %02x %02x "
                 "%02x, not the expected %02x %02x %02x %02x %02x %02x -- NOT "
                 "hooking it. The construction count will be absent, so a zero "
                 "update count cannot be told apart from 'no objects exist'.",
                 GT_CTOR_ADDR,
                 cpro[0], cpro[1], cpro[2], cpro[3], cpro[4], cpro[5],
                 GT_CTOR_PROLOGUE[0], GT_CTOR_PROLOGUE[1], GT_CTOR_PROLOGUE[2],
                 GT_CTOR_PROLOGUE[3], GT_CTOR_PROLOGUE[4],
                 GT_CTOR_PROLOGUE[5]);
    } else if (MH_CreateHook(c, (void *)&hook_ctor,
                             (void **)&g_real_ctor) != MH_OK ||
               MH_EnableHook(c) != MH_OK) {
        MH_RemoveHook(c);
        g_real_ctor = NULL;
        log_line("gt: ⚠ could not hook the ctor at 0x%llx; construction count "
                 "will be absent.", GT_CTOR_ADDR);
    } else {
        log_line("gt: hooked the cGUIObjText CONSTRUCTOR at 0x%llx. It has "
                 "exactly ONE caller (the factory 0x%llx), so this counts "
                 "EVERY object of this class that is ever built -- which is "
                 "what tells a wrong hook target apart from a class the shell "
                 "never instantiates.", GT_CTOR_ADDR, GT_FACTORY_ADDR);
    }

    /* ⭐⭐⭐ THE TEXT SETTER -- the hook this module now exists for.
     * `setText(this, const char *s, ...)`: the string arrives as an ARGUMENT,
     * so there is nothing to guess and nothing to wait for. See the header for
     * how it was derived, and why the vptr filter is still required. */
    /* ⭐⭐ ALL FOUR OVERLOADS. The previous run hooked one and reported
     * "setText ran 0 times" -- the class was right, the overload was not. All
     * four write +0xd0 and share this prologue; two belong to cGUIObjText and
     * two to cGUIObjMessage. Hooking one and guessing is what cost that run. */
    static const BYTE SETTEXT_PROLOGUE[] = { 0x48, 0x89, 0x5c, 0x24, 0x08 };
    int settext_installed = 0;
    for (int i = 0; i < GT_SETTEXT_COUNT; i++) {
        void *x = (void *)(ULONG_PTR)GT_SETTEXT_ADDRS[i];
        MEMORY_BASIC_INFORMATION xmbi;
        BYTE xpro[sizeof(SETTEXT_PROLOGUE)];

        if (VirtualQuery(x, &xmbi, sizeof(xmbi)) != sizeof(xmbi) ||
            xmbi.State != MEM_COMMIT ||
            !(xmbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                              PAGE_EXECUTE_READWRITE |
                              PAGE_EXECUTE_WRITECOPY))) {
            log_line("gt: ⚠ %s at 0x%llx is not committed executable memory -- "
                     "skipped.", GT_SETTEXT_NAMES[i], GT_SETTEXT_ADDRS[i]);
            continue;
        }
        if (!mem_copy(xpro, x, sizeof(xpro)) ||
            memcmp(xpro, SETTEXT_PROLOGUE, sizeof(xpro)) != 0) {
            log_line("gt: ⚠ %s at 0x%llx carries %02x %02x %02x %02x %02x, not "
                     "the expected %02x %02x %02x %02x %02x -- skipped.",
                     GT_SETTEXT_NAMES[i], GT_SETTEXT_ADDRS[i],
                     xpro[0], xpro[1], xpro[2], xpro[3], xpro[4],
                     SETTEXT_PROLOGUE[0], SETTEXT_PROLOGUE[1],
                     SETTEXT_PROLOGUE[2], SETTEXT_PROLOGUE[3],
                     SETTEXT_PROLOGUE[4]);
            continue;
        }
        if (MH_CreateHook(x, GT_SETTEXT_HOOKS[i],
                          (void **)&g_real_settext_n[i]) != MH_OK ||
            MH_EnableHook(x) != MH_OK) {
            MH_RemoveHook(x);
            g_real_settext_n[i] = NULL;
            log_line("gt: ⚠ could not hook %s at 0x%llx -- skipped.",
                     GT_SETTEXT_NAMES[i], GT_SETTEXT_ADDRS[i]);
            continue;
        }
        settext_installed++;
    }

    /* ⚠ A PARTIAL install is reported, not silently accepted: if only some of
     * the four are live, a zero from the others means nothing. */
    if (settext_installed == 0) {
        log_line("gt: ⛔ none of the %d setText overloads could be hooked.",
                 GT_SETTEXT_COUNT);
        g_why = GT_INSTALL_MINHOOK_FAILED;
        return FALSE;
    }
    log_line("gt: hooked %d of %d setText overloads (cGUIObjText slots 25/26, "
             "cGUIObjMessage slots 25/26). Each carries its own counter, so "
             "the log will name WHICH one the shell uses.%s",
             settext_installed, GT_SETTEXT_COUNT,
             settext_installed < GT_SETTEXT_COUNT
                 ? "  ⚠ PARTIAL -- a zero from a skipped overload measures "
                   "nothing."
                 : "");

    /* ⭐ The ctor is the one that must work. Without it there is no census and
     * no instances, so reporting a successful install would be a lie that
     * produces a silent, uninterpretable run -- exactly what this project's
     * rule about "could not measure" exists to prevent. */
    if (!g_real_ctor) {
        if (t) { MH_DisableHook(t); MH_RemoveHook(t); g_real_update = NULL; }
        log_line("gt: ⛔ the ctor probe could not be installed, so this module "
                 "has no way to reach an instance. Reporting FAILURE rather "
                 "than a run that would look quiet and mean nothing.");
        g_why = GT_INSTALL_MINHOOK_FAILED;
        return FALSE;
    }

    g_installed = TRUE;
    g_why = GT_INSTALL_OK;
    log_line("gt: ready -- ctor probe live%s. Detours DEREFERENCE NOTHING "
             "beyond a single guarded vptr read; all inspection happens on the "
             "worker thread under the fault guard.",
             t ? " and update hook installed" : " (update hook absent)");
    return TRUE;
}

void gt_remove(void)
{
    if (!g_installed)
        return;
    void *t = (void *)(ULONG_PTR)GT_UPDATE_ADDR;
    MH_DisableHook(t);
    MH_RemoveHook(t);

    /* ⚠ Only if it was actually installed. g_real_ctor is the record of that:
     * the ctor hook is allowed to fail without failing the install, so
     * unhooking unconditionally would ask MinHook to remove a hook that was
     * never created. */
    if (g_real_ctor) {
        void *c = (void *)(ULONG_PTR)GT_CTOR_ADDR;
        MH_DisableHook(c);
        MH_RemoveHook(c);
        g_real_ctor = NULL;
    }
    for (int i = 0; i < GT_SETTEXT_COUNT; i++) {
        if (!g_real_settext_n[i])
            continue;
        void *x = (void *)(ULONG_PTR)GT_SETTEXT_ADDRS[i];
        MH_DisableHook(x);
        MH_RemoveHook(x);
        g_real_settext_n[i] = NULL;
    }

    g_installed = FALSE;
    log_line("gt: hooks removed.");
}

void gt_get_stats(gt_stats *out)
{
    memset(out, 0, sizeof(*out));
    out->installed = g_installed;
    out->calls     = InterlockedCompareExchange(&g_calls, 0, 0);
    out->distinct  = InterlockedCompareExchange(&g_distinct, 0, 0);
    out->faults    = InterlockedCompareExchange(&g_faults, 0, 0);
    out->wrong_class = InterlockedCompareExchange(&g_wrong_class, 0, 0);
    out->other_class = InterlockedCompareExchange(&g_other_class, 0, 0);
    out->ours        = InterlockedCompareExchange(&g_ours, 0, 0);
    out->ctors       = InterlockedCompareExchange(&g_ctors, 0, 0);
    out->ctor_hooked = (g_real_ctor != NULL);
    out->with_text   = InterlockedCompareExchange(&g_with_text, 0, 0);
    out->settext_calls = InterlockedCompareExchange(&g_settext_calls, 0, 0);
    for (int i = 0; i < GT_SETTEXT_COUNT; i++)
        out->settext_each[i] =
            InterlockedCompareExchange(&g_settext_each[i], 0, 0);
}

/* --- test seams ----------------------------------------------------------
 *
 * ⛔⛔ The stand-in returns a value the TEST chose; it does not compute one
 * plausibly. A stand-in that re-derives what the shipping code derives makes a
 * broken mechanism and a working one indistinguishable -- this project has
 * shipped that mistake twice. */
static void *g_sim_ret;

static void *fake_original(void *self, void *arg)
{
    (void)self; (void)arg;
    return g_sim_ret;
}

void *gt_test_body(void *self, void *arg)
{
    /* A distinctive value the test can recognise, so "passed through" is
     * provable rather than merely plausible. */
    g_sim_ret = (void *)(ULONG_PTR)0xABCDEF0012345678ULL;
    return detour_body(fake_original, self, arg, (void *)0x140001234ULL);
}

void *gt_test_last_captured(void)
{
    return g_last_self;
}

/* Expose the prologue constants so a test can pin them. `which`: 0 = the
 * update method, 1 = the ctor.
 *
 * ⭐ Returns the SHIPPING constants, not copies -- a test against a duplicate
 * of the values would pass while the values the install actually compares
 * against were wrong, which is precisely the bug this exists to catch. */
const BYTE *gt_test_prologue(int which, int *len)
{
    if (which == 0) {
        if (len) *len = (int)sizeof(GT_PROLOGUE);
        return GT_PROLOGUE;
    }
    if (len) *len = (int)sizeof(GT_CTOR_PROLOGUE);
    return GT_CTOR_PROLOGUE;
}

/* The stand-in "original" ctor. ⛔ It returns a value the TEST recognises
 * rather than computing a plausible one -- a stand-in that re-derives what the
 * shipping code derives leaves a broken mechanism indistinguishable from a
 * working one. */
/* ⛔ Returns a value the TEST chose, and records nothing itself -- a stand-in
 * that re-derived what the detour derives would leave a broken detour and a
 * working one indistinguishable. */
static void *fake_settext(void *self, const char *s, int a, int b, int c, int d)
{
    (void)self; (void)s; (void)a; (void)b; (void)c; (void)d;
    return g_sim_ret;
}

static void *fake_ctor(void *self)
{
    (void)self;
    return g_sim_ret;
}

void *gt_test_settext_body(void *self, const char *s, char *out, int outlen)
{
    /* ⭐ Drives the SHIPPING shared body via overload 1 (cGUIObjText slot 26).
     * All four detours are one-line wrappers over settext_body(), so exercising
     * one exercises the mechanism; the per-overload counters are what the LIVE
     * run distinguishes, and no offline test can tell which the game calls. */
    g_sim_ret = (void *)(ULONG_PTR)0xFEEDFACE00112233ULL;
    pfn_settext saved = g_real_settext_n[1];
    g_real_settext_n[1] = (pfn_settext)fake_settext;
    void *r = settext_body(1, g_real_settext_n[1], self, s, 1, 2, 3, 4);
    g_real_settext_n[1] = saved;
    if (out && outlen > 0) {
        strncpy(out, g_text_buf, (size_t)outlen - 1);
        out[outlen - 1] = 0;
    }
    return r;
}

BOOL gt_test_take_text(char *out, int outlen)
{
    if (InterlockedCompareExchange(&g_text_ready, 0, 1) != 1)
        return FALSE;
    /* ⚠ Bounded, for the same reason as the shipping reader above: a test that
     * hangs looks exactly like a test that is slow, and one already did. */
    int spins = 0;
    while (InterlockedCompareExchange(&g_text_busy, 1, 0) != 0) {
        if (++spins > 1000) {
            InterlockedExchange(&g_text_ready, 1);
            return FALSE;
        }
    }
    if (out && outlen > 0) {
        strncpy(out, g_text_buf, (size_t)outlen - 1);
        out[outlen - 1] = 0;
    }
    InterlockedExchange(&g_text_busy, 0);
    return TRUE;
}

void *gt_test_ctor_body(void *self)
{
    g_sim_ret = (void *)(ULONG_PTR)0xC0FFEE0011223344ULL;
    pfn_ctor saved = g_real_ctor;
    g_real_ctor = fake_ctor;
    void *r = hook_ctor(self);
    g_real_ctor = saved;
    return r;
}

/* Resolve a pointer inside the harness's own buffer. The test builds an object
 * whose "pointer" fields hold addresses within a buffer it owns, so the
 * SHIPPING inspector walks real pointer fields without touching foreign
 * memory. */
typedef struct { const unsigned char *base; ULONGLONG lo, hi; } test_ctx;

static SIZE_T resolve_test(void *ctx, ULONGLONG addr, char *out, SIZE_T max)
{
    test_ctx *t = (test_ctx *)ctx;
    if (addr < t->lo || addr >= t->hi)
        return 0;
    SIZE_T avail = (SIZE_T)(t->hi - addr);
    if (avail > max) avail = max;
    memcpy(out, t->base + (addr - t->lo), avail);
    return avail;
}

BOOL gt_test_inspect(const void *buf, SIZE_T len, int *text_offset)
{
    /* The harness's buffer stands in for the process's address space: a
     * pointer field holding `lo + k` resolves to byte k of the buffer. */
    test_ctx ctx;
    ctx.base = (const unsigned char *)buf;
    ctx.lo   = 0x100000ULL;
    ctx.hi   = ctx.lo + len;
    return inspect_body(buf, len, resolve_test, &ctx, text_offset, FALSE);
}

/* Test seams for the instance set. The table is static and object-keyed, so a
 * test must be able to ask "how many objects?" and "what does THIS one say?" --
 * asserting the log text instead would test the formatter, not the table. */
int gt_test_focus_diff_changed(void) { return (int)g_fd_last; }

int gt_test_instance_count(void)
{
    return (int)g_inst_count;
}

int gt_test_instance_text(void *obj, char *out, int outlen)
{
    if (out && outlen > 0) out[0] = 0;
    for (LONG i = 0; i < g_inst_count; i++) {
        if (g_inst[i].obj != obj)
            continue;
        if (out && outlen > 0) {
            strncpy(out, g_inst[i].text, (size_t)outlen - 1);
            out[outlen - 1] = 0;
        }
        return (int)g_inst[i].sets;
    }
    return 0;
}

/* ⭐⭐ Walk the instance set for widgets belonging to ONE screen, identified by
 * the parent pointer the game stamps at +0x088. See gui_text.h.
 *
 * ⚠ Both entry points share this so the validation sequence exists once: the
 * pointer checks, the vptr re-validation and the mem_readable_run-before-copy
 * ordering are the same five lines gt_item_at_position() uses, and a second
 * copy of them would be a second place to get them wrong. */
typedef struct { float x, y; const char *text; } gt_widget;

static int widgets_of(const void *screen, float want_x, gt_widget *out, int cap)
{
    int n = 0;
    LONG n_ent = g_inst_count;

    if (!screen)
        return 0;
    for (volatile LONG i = 0; i < n_ent && n < cap; i++) {
        void *o = g_inst[i].obj;
        ULONGLONG vptr = 0;
        BOOL faulted = FALSE;
        unsigned char buf[GT_CLASS_SIZE];
        void *parent = NULL;
        float x = 0.f, y = 0.f;

        if (!o || ((ULONG_PTR)o & 7u) || !g_inst[i].text[0])
            continue;
        if (mem_readable_run(o, sizeof(vptr)) < sizeof(vptr))
            continue;
        MEM_GUARDED(faulted, vptr = *(volatile ULONGLONG *)o);
        if (faulted || (vptr != GT_VTABLE_ADDR && vptr != GT_VTABLE_MSG))
            continue;
        /* ⛔⛔ mem_readable_run FIRST, then the guarded copy -- case C of
         * run_span_test measured a guarded memcpy across a bad page going
         * UNCAUGHT. The guard is a net for the race, never the check. */
        if (mem_readable_run(o, GT_CLASS_SIZE) < GT_CLASS_SIZE)
            continue;
        if (!mem_copy(buf, o, GT_CLASS_SIZE))
            continue;

        memcpy(&parent, buf + 0x88, sizeof(parent));
        if (parent != screen)
            continue;
        memcpy(&x, buf + 0x40, sizeof(float));
        memcpy(&y, buf + 0x44, sizeof(float));
        if (!(x == x) || !(y == y))         /* reject NaN: breaks every compare */
            continue;
        if (want_x >= 0.f && !(x > want_x - GT_COORD_EPS &&
                               x < want_x + GT_COORD_EPS))
            continue;
        out[n].x = x;
        out[n].y = y;
        out[n].text = g_inst[i].text;
        n++;
    }
    return n;
}

/* Sort a widget set by Y, top to bottom, and refuse on a shared Y.
 * ⚠ Two rows at one coordinate means the ordering is not this screen's -- and
 * ONE copy of this rule, shared by every caller, so it cannot drift. */
static BOOL sort_by_y(gt_widget *w, int n)
{
    /* n <= 64 on the worker thread, so an insertion sort is the right amount
     * of machinery. */
    for (int i = 1; i < n; i++) {
        gt_widget t = w[i];
        int j = i - 1;
        while (j >= 0 && w[j].y > t.y) { w[j + 1] = w[j]; j--; }
        w[j + 1] = t;
    }
    for (int i = 1; i < n; i++)
        if (w[i].y > w[i-1].y - GT_COORD_EPS && w[i].y < w[i-1].y + GT_COORD_EPS)
            return FALSE;
    return TRUE;
}

BOOL gt_widget_of(const void *screen, float x, float y, char *out, int outlen)
{
    gt_widget w[GT_INST_MAX];
    int n, hit = -1;

    if (out && outlen > 0) out[0] = 0;
    if ((!g_installed && !g_focus_test_mode) || !out || outlen <= 0)
        return FALSE;
    if (InterlockedCompareExchange(&g_inst_busy, 1, 0) != 0)
        return FALSE;
    n = widgets_of(screen, x, w, GT_INST_MAX);
    InterlockedExchange(&g_inst_busy, 0);

    for (int i = 0; i < n; i++) {
        if (y >= 0.f && !(w[i].y > y - GT_COORD_EPS && w[i].y < y + GT_COORD_EPS))
            continue;
        if (hit >= 0)
            return FALSE;                   /* two matches is a contradiction */
        hit = i;
    }
    if (hit < 0)
        return FALSE;
    _snprintf(out, (size_t)outlen, "%s", w[hit].text);
    out[outlen - 1] = 0;
    return out[0] != 0;
}

BOOL gt_column_y(const void *screen, float x, int position, float *out_y)
{
    gt_widget w[GT_INST_MAX];
    int n;

    if (!out_y || position < 1)
        return FALSE;
    if ((!g_installed && !g_focus_test_mode))
        return FALSE;
    if (InterlockedCompareExchange(&g_inst_busy, 1, 0) != 0)
        return FALSE;
    n = widgets_of(screen, x, w, GT_INST_MAX);
    InterlockedExchange(&g_inst_busy, 0);

    /* ⚠ ONE sort, shared with gt_column_item() -- two copies of an ordering
     * rule is two places for it to drift. */
    if (position > n || !sort_by_y(w, n))
        return FALSE;
    *out_y = w[position - 1].y;
    return TRUE;
}

int gt_column_count(const void *screen, float x)
{
    gt_widget w[GT_INST_MAX];
    int n;

    if (!g_installed && !g_focus_test_mode)
        return 0;
    if (InterlockedCompareExchange(&g_inst_busy, 1, 0) != 0)
        return 0;
    n = widgets_of(screen, x, w, GT_INST_MAX);
    InterlockedExchange(&g_inst_busy, 0);
    return n;
}

BOOL gt_column_item(const void *screen, float x, float pair_x, int position,
                    char *out, int outlen)
{
    gt_widget w[GT_INST_MAX], p[GT_INST_MAX];
    int n, np = 0;

    if (out && outlen > 0) out[0] = 0;
    if ((!g_installed && !g_focus_test_mode) || !out || outlen <= 0 ||
        position < 1)
        return FALSE;
    if (InterlockedCompareExchange(&g_inst_busy, 1, 0) != 0)
        return FALSE;
    n = widgets_of(screen, x, w, GT_INST_MAX);
    if (pair_x >= 0.f)
        np = widgets_of(screen, pair_x, p, GT_INST_MAX);
    InterlockedExchange(&g_inst_busy, 0);

    if (position > n || !sort_by_y(w, n))
        return FALSE;

    const gt_widget *me = &w[position - 1];
    /* ⚠ Paired by Y, never by ordinal -- the columns can differ in length. */
    const char *mate = NULL;
    for (int i = 0; i < np; i++)
        if (p[i].y > me->y - GT_COORD_EPS && p[i].y < me->y + GT_COORD_EPS) {
            if (mate) { mate = NULL; break; }   /* two partners: refuse */
            mate = p[i].text;
        }

    /* ⭐⭐ A BLANKED MATE IS NOT A MATE. The game POOLS one pair object per row
     * and BLANKS it -- a single space -- when the row has nothing to show,
     * instead of destroying it. Measured: every x=633 object ever seen holding
     * " " was also seen holding real text, so it is a live, resident,
     * Y-aligned widget that passes widgets_of()'s `text[0]` filter (which
     * rejects "" but not " ").
     *
     * ⛔ Without this, row 4 reads " Jinchu Watari,  " -- a name, a comma and a
     * pause the player cannot interpret. Case 54 is exactly that string, and it
     * was seen to FAIL with this trim removed while case 53 stayed green. */
    if (mate) {
        const char *q = mate;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')
            q++;
        if (!*q)
            mate = NULL;
    }

    if (mate)
        _snprintf(out, (size_t)outlen, "%s, %s", me->text, mate);
    else
        _snprintf(out, (size_t)outlen, "%s", me->text);
    out[outlen - 1] = 0;
    return out[0] != 0;
}

/* ===================== THE MOVE LIST MEMBERSHIP PROBE =====================
 *
 * See gui_text.h for what this answers and why no log can answer it. Read-only
 * throughout; nothing here is ever spoken.
 *
 * ⭐⭐ AGGREGATE, DO NOT TRANSCRIBE. The user moves through HUNDREDS of entries
 * quickly (measured median 0.6 s), and log_line() does a full _wfopen/fclose
 * PER LINE -- so one line per movement is hundreds of file opens on the worker
 * thread. A repeated outcome increments `hits` instead of taking a slot, so 400
 * identical movements occupy ONE bucket.
 *
 * ⭐ That aggregation is a STRONGER dedupe than g_focus's consecutive-duplicate
 * rule, and it is also why this report does NOT clear its window. g_focus must
 * clear or it fills to 48 and goes blind (case 19); g_ml cannot fill by
 * repetition, only by 24 DISTINCT outcomes -- and g_ml_overflow then says so.
 * Clearing would destroy the session-wide distribution that IS the verdict. */
#define GT_ML_MAX 24
typedef struct {
    int  total;        /* the vertical cursor's reported item total */
    int  names;        /* live members in the derived name column */
    int  members;      /* live objects whose +0x088 == this screen */
    LONG hits;
    LONG first_seq, last_seq;
} gt_ml_bucket;
static gt_ml_bucket g_ml[GT_ML_MAX];
static volatile LONG g_ml_used = -1;   /* -1 = never ran; 0 = ran, found none */
static volatile LONG g_ml_overflow;
static volatile LONG g_ml_samples;
static volatile LONG g_ml_missed;      /* movements that passed between polls */
static volatile LONG g_ml_busy_skips;
static volatile LONG g_ml_faults;
static volatile LONG g_ml_on;
static volatile LONG g_ml_last_seq;
static float         g_ml_col_x;       /* the DERIVED name-column X */

void gt_movelist_enable(BOOL on) { InterlockedExchange(&g_ml_on, on ? 1 : 0); }
static BOOL ml_enabled(void) { return g_ml_on == 1; }

/* Record one outcome. ⛔ Overflow STOPS AND COUNTS rather than wrapping: a
 * wrapping ring would discard the RARE outcome, which is precisely the evidence
 * being sought, and would show a full table whether or not anything was lost
 * (the reasoning at g_focus's declaration). */
static void ml_record(int total, int names, int members, LONG seq)
{
    LONG used = g_ml_used < 0 ? 0 : g_ml_used;
    for (LONG i = 0; i < used; i++) {
        if (g_ml[i].total == total && g_ml[i].names == names &&
            g_ml[i].members == members) {
            g_ml[i].hits++;
            g_ml[i].last_seq = seq;
            g_ml_used = used;
            return;
        }
    }
    if (used >= GT_ML_MAX) {
        InterlockedIncrement(&g_ml_overflow);
        g_ml_used = used;
        return;
    }
    g_ml[used].total = total;
    g_ml[used].names = names;
    g_ml[used].members = members;
    g_ml[used].hits = 1;
    g_ml[used].first_seq = g_ml[used].last_seq = seq;
    g_ml_used = used + 1;
}

/* The census itself, given the screen pointer that defines membership.
 *
 * ⚠ `screen` is COMPARED as a value against +0x088 read out of our own guarded
 * copy -- never dereferenced. mc_move.screen is documented "recorded only,
 * never dereferenced" and that contract is kept. */
static void ml_census(void *screen, int *out_members, int *out_names)
{
    float xs[GT_INST_MAX];
    /* ⚠ VOLATILE because these live across MEM_GUARDED's setjmp. longjmp may
     * restore a stale register copy, which here would silently under- or
     * over-count the census -- and -Wclobbered flagged exactly this. This
     * project already had one real setjmp hazard caught by that warning; it is
     * not a warning to wave through. */
    volatile int nx = 0;
    volatile int members = 0;

    *out_members = *out_names = 0;
    if (!screen)
        return;

    LONG n_ent = g_inst_count;
    for (volatile LONG i = 0; i < n_ent && nx < GT_INST_MAX; i++) {
        void *o = g_inst[i].obj;
        ULONGLONG vptr = 0;
        BOOL faulted = FALSE;
        unsigned char buf[GT_CLASS_SIZE];
        void *parent = NULL;
        float x = 0.f, y = 0.f;

        if (!o || ((ULONG_PTR)o & 7u))
            continue;
        if (!g_inst[i].text[0])
            continue;
        /* ⚠ Re-validate EVERY sample. An entry survives until eviction, so a
         * stale object from a screen left long ago can still hold matching
         * text -- and counting it would answer YES when the truth is NO, the
         * worst direction for this particular question. */
        if (mem_readable_run(o, sizeof(vptr)) < sizeof(vptr))
            continue;
        MEM_GUARDED(faulted, vptr = *(volatile ULONGLONG *)o);
        if (faulted) { InterlockedIncrement(&g_ml_faults); continue; }
        if (vptr != GT_VTABLE_ADDR && vptr != GT_VTABLE_MSG)
            continue;
        /* ⛔⛔ mem_readable_run FIRST, then the guarded copy. A guarded memcpy
         * across a bad page is NOT reliably caught -- case C of run_span_test. */
        if (mem_readable_run(o, GT_CLASS_SIZE) < GT_CLASS_SIZE)
            continue;
        if (!mem_copy(buf, o, GT_CLASS_SIZE))
            continue;

        memcpy(&parent, buf + 0x88, sizeof(parent));
        if (parent != screen)
            continue;                       /* not this screen's widget */
        members++;

        memcpy(&x, buf + 0x40, sizeof(float));
        memcpy(&y, buf + 0x44, sizeof(float));
        /* ⚠ Reject NaN outright: it makes every comparison false and would
         * silently shrink the column census. */
        if (!(x == x) || !(y == y))
            continue;
        xs[nx++] = x;
    }

    /* ⭐ DERIVE the name column rather than hardcoding it: that constant has
     * already been written down as 1265, then 1268, and measured as 633.0 for
     * the condition column. A derived value also survives a resolution change
     * or a per-game layout difference.
     *
     * ⛔⛔ THE LEFTMOST COLUMN, NOT THE MOST POPULATED. Picking the mode was
     * the first implementation and case 47 caught it: with one of three name
     * objects gone stale, 2 names against 3 notation objects made the MODE the
     * NOTATION column (x=1565), so the census reported names=3 -- counting the
     * wrong column entirely. In-game that is a character with few moves, which
     * is common (measured per-character counts run 2..23).
     *
     * Leftmost is structural: the move list draws names at x=84, conditions at
     * 633, the heading at 960 and notation at 1565. The name column is the
     * left-hand one by layout, not by happening to be the busiest. */
    /* ⚠ volatile for the same reason as above: this runs after the guarded
     * reads and -Wclobbered covers the whole function. */
    volatile float best_x = 0.f;
    volatile int   best_n = 0;
    for (int i = 0; i < (int)nx; i++) {
        if (best_n == 0 || xs[i] < best_x) {
            best_x = xs[i];
            best_n = 0;
            for (int j = 0; j < (int)nx; j++)
                if (xs[j] == xs[i]) best_n++;
        }
    }
    g_ml_col_x   = best_n ? best_x : 0.f;
    *out_members = (int)members;
    *out_names   = (int)best_n;
}

/* ⚠⚠ THE CURSOR STATE IS PASSED IN, not fetched.
 *
 * ⛔ The first version called mc_peek_rest() from here, which made gui_text.c
 * depend on menu_cursor.c -- and linking that into this suite dragged in
 * menu_speak, the GMD reader and their dependencies, turning a focused test
 * binary into most of the mod. The module boundary was the problem, not the
 * link line.
 *
 * ⭐ mod.c already owns both modules and already calls them in sequence, so it
 * hands the screen pointer and the total over. gui_text stays self-contained
 * and the suite stays honest about what it covers. */
void gt_movelist_probe(void *screen, int total, BOOL is_movelist)
{
    if (!ml_enabled())
        return;
    if (!g_installed && !g_focus_test_mode)
        return;
    if (g_ml_used < 0)
        g_ml_used = 0;                      /* it ran: 0 now means "found none" */
    if (!is_movelist || !screen || total < 1)
        return;

    LONG seq = g_move_seq;
    LONG prev = g_ml_last_seq;
    if (seq == prev)
        return;                             /* no movement since last sample */
    /* ⚠ A jump of more than one means movements passed between polls. Counted,
     * never hidden: a run whose `missed` approaches `samples` is untrustworthy
     * and the report must say so rather than present a tidy histogram built on
     * a fraction of the data. */
    if (prev && seq > prev + 1)
        InterlockedExchangeAdd(&g_ml_missed, seq - prev - 1);
    g_ml_last_seq = seq;

    if (InterlockedCompareExchange(&g_inst_busy, 1, 0) != 0) {
        InterlockedIncrement(&g_ml_busy_skips);
        return;
    }
    int members = 0, names = 0;
    ml_census(screen, &members, &names);
    InterlockedExchange(&g_inst_busy, 0);

    ml_record(total, names, members, seq);
    InterlockedIncrement(&g_ml_samples);
}

BOOL gt_movelist_conclusive(long *samples, long *missed)
{
    if (samples) *samples = (long)g_ml_samples;
    if (missed)  *missed  = (long)g_ml_missed;
    return g_ml_samples > 0;
}

void gt_movelist_report(void)
{
    long samples = 0, missed = 0;

    if (!ml_enabled())
        return;
    if (!gt_movelist_conclusive(&samples, &missed)) {
        log_line("gt: ⚠ MOVE LIST PROBE INCONCLUSIVE -- 0 movement(s) "
                 "sampled on a move-list screen (%ld poll(s) lost the lock, "
                 "%ld fault(s)). Nothing can be concluded about membership. "
                 "This is NOT a negative result.",
                 (long)g_ml_busy_skips, (long)g_ml_faults);
        return;
    }

    LONG used = g_ml_used < 0 ? 0 : g_ml_used;
    log_line("gt: === MOVE LIST MEMBERSHIP: %ld sample(s), %ld missed ===",
             samples, missed);
    log_line("gt:   name column derived at X=%.1f", (double)g_ml_col_x);
    LONG agree = 0, disagree = 0;
    for (LONG i = 0; i < used; i++) {
        log_line("gt:   [total=%-3d names=%-3d members=%-3d] x%-4ld seq %ld..%ld",
                 g_ml[i].total, g_ml[i].names, g_ml[i].members,
                 (long)g_ml[i].hits, (long)g_ml[i].first_seq,
                 (long)g_ml[i].last_seq);
        if (g_ml[i].total == g_ml[i].names) agree += g_ml[i].hits;
        else                                disagree += g_ml[i].hits;
    }
    /* ⭐ POSITIVE counts throughout: "0 failed" must never be mistakable for
     * "0 checked". */
    log_line("gt:   of %ld sample(s): %ld had names == total, %ld DISAGREED; "
             "%ld outcome(s) did not fit the table; %ld lock-loss; %ld fault(s).",
             samples, (long)agree, (long)disagree, (long)g_ml_overflow,
             (long)g_ml_busy_skips, (long)g_ml_faults);
    log_line("gt: ▶ READ IT LIKE THIS:");
    log_line("gt:   names == total on EVERY sample -> the coordinate join can "
             "name the rows; the reader's refusals are the thing to fix.");
    log_line("gt:   names CONSTANT while total VARIES -> the vertical cursor is "
             "NOT a row cursor over this column. It selects a CATEGORY or PAGE, "
             "and a row reader needs the per-category count first.");
    log_line("gt:   names > total always -> the whole list is resident and the "
             "cursor counts a WINDOW. Find the scroll offset before position N "
             "means anything.");
    log_line("gt:   names == 0 with members > 0 -> the column X was mis-derived; "
             "compare the derived X above against a cfc2_layout.on dump.");
    log_line("gt:   names == 0 AND members == 0 -> the +0x088 == screen "
             "assumption is WRONG here. Re-measure before building anything.");
}

int gt_test_ml_buckets(void)
{
    return g_ml_used < 0 ? -1 : (int)g_ml_used;
}

BOOL gt_test_ml_bucket_at(int i, int *total, int *names, int *members,
                          long *hits)
{
    if (g_ml_used <= 0 || i < 0 || i >= (int)g_ml_used)
        return FALSE;
    if (total)   *total   = g_ml[i].total;
    if (names)   *names   = g_ml[i].names;
    if (members) *members = g_ml[i].members;
    if (hits)    *hits    = (long)g_ml[i].hits;
    return TRUE;
}

long gt_test_ml_overflow(void) { return (long)g_ml_overflow; }
long gt_test_ml_missed(void)   { return (long)g_ml_missed; }

/* ⭐ Hands in only the INPUTS. The harness never says which objects are
 * members or what the column X is -- those are the mechanism under test. */
void gt_test_ml_feed(int total, void *screen)
{
    if (!ml_enabled())
        return;
    if (g_ml_used < 0)
        g_ml_used = 0;
    int members = 0, names = 0;
    ml_census(screen, &members, &names);
    ml_record(total, names, members, g_move_seq);
    InterlockedIncrement(&g_ml_samples);
}

void gt_test_ml_note_move(int n)
{
    for (int i = 0; i < n; i++)
        InterlockedIncrement(&g_move_seq);
}

void gt_test_reset(void)
{
    g_ml_used = -1; g_ml_overflow = 0; g_ml_samples = 0; g_ml_missed = 0;
    g_ml_busy_skips = 0; g_ml_faults = 0; g_ml_on = 0;
    g_ml_last_seq = 0; g_ml_col_x = 0.f;
    for (int i = 0; i < GT_ML_MAX; i++) {
        g_ml[i].total = g_ml[i].names = g_ml[i].members = 0;
        g_ml[i].hits = g_ml[i].first_seq = g_ml[i].last_seq = 0;
    }
    g_calls = g_distinct = g_faults = g_wrong_class = 0;
    g_other_class = g_ours = g_ctors = g_with_text = 0;
    g_settext_calls = 0; g_text_ready = 0; g_text_busy = 0;
    for (int i = 0; i < GT_SETTEXT_COUNT; i++) g_settext_each[i] = 0;
    g_text_last[0] = 0;
    g_text_self = NULL; g_text_buf[0] = 0;
    g_ring_next = 0; g_sweep_at = 0;
    for (int i = 0; i < GT_RING_SIZE; i++) g_ring[i] = NULL;
    g_seq = 0; g_taken_seq = 0;
    g_last_self = NULL; g_last_caller = NULL;
    /* ⚠ The focus channel too. A case that inherits the previous case's
     * records would assert against evidence it did not produce -- and would
     * PASS while testing nothing, which is the worst shape of green. */
    /* ⚠ The differ's state too: a case inheriting the previous case's snapshots
     * would diff against another fixture's bytes and report changes nobody
     * made. Same reasoning as the focus channel's reset below. */
    g_fd_last = -1; g_fd_on = 0;
    for (int i = 0; i < FD_MAX; i++) { g_fd[i].obj = NULL; g_fd[i].have = FALSE; }
    g_inst_count = 0; g_inst_evicted = 0; g_inst_busy = 0;
    for (int i = 0; i < GT_INST_MAX; i++) {
        g_inst[i].obj = NULL; g_inst[i].text[0] = 0;
        g_inst[i].move_seq = 0; g_inst[i].sets = 0;
    }
    g_focus_count = 0; g_focus_dropped = 0; g_focus_busy = 0;
    g_move_seq = 0; g_move_notifications = 0;
    for (int i = 0; i < GT_FOCUS_MAX; i++) {
        g_focus[i].text[0] = 0;
        g_focus[i].self = NULL;
        g_focus[i].move_seq = 0;
        g_focus[i].which = 0;
    }
}
