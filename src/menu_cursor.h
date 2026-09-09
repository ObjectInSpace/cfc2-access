/*
 * The shell's menu cursor -- the chokepoint every menu moves its selection
 * through.
 *
 * WHAT WAS FOUND OFFLINE (memory: cfc2-menu-cursor-chokepoint)
 * -----------------------------------------------------------
 * Route 7 asked which code REACTS to Down. Two generic helpers, called by every
 * shell menu:
 *
 *   0x14007cdc0  vertical    (input masks 0x10010 up   / 0xa00a0 down)
 *   0x14007c9f0  horizontal  (input masks 0x80080 left / 0x50050 right)
 *
 *   int move(void *screen, int *cursor, int min, int max,
 *            int sfx_sel, int wrap);        // rcx, rdx, r8d, r9d, stack, stack
 *
 * Body, read off the machine code: load the current index through `rdx`, query
 * input with `edx = [screen+0x2ec]` (a player/pad index), clamp or wrap against
 * min/max, WRITE THE NEW INDEX BACK through `rdx`, compare it against the old
 * value and -- only if it changed -- play a cursor-move SFX and return the
 * delta. 34 vertical and 34 horizontal call sites, all inside
 * 0x140079000-0x1400b5700.
 *
 * ⭐⭐ WHY THIS MATTERS MORE THAN IT LOOKS
 * The four closed scan routes concluded "there is no flat 0..N-1 selection
 * index in this game -- proven, not assumed" (1,914,880 fields, nothing over 4
 * distinct values). An index DOES exist. It is a field of the PER-SCREEN
 * object, passed by pointer, at a different address in every menu -- so a span
 * scan for one global integer could not have found it, and that negative was
 * sound for exactly the thing it tested. The generalisation was the error, not
 * the measurement.
 *
 * ⭐ Many call sites also pass max as [screen+0x334]-1 beside cursor
 * [screen+0x330], so the item COUNT is available too -- that is what "item 3 of
 * 8" needs. ⚠ Those offsets are NOT universal (other sites index arrays at
 * +0x358/+0x378). We never rely on them: the pointer arrives as an ARGUMENT.
 *
 * WHY A HOOK AND NOT A SCAN
 * -------------------------
 * Same reason as kbd_state: both the screen and the cursor pointer arrive as
 * arguments, so hooking one known code address hands them over directly.
 * ⭐ Read the answer out of the instructions; never scan memory for a
 * recognisable shape. That method is 4 for 4 on this project; scanning is 0
 * for 4.
 *
 * ⚠⚠ SAFETY -- THE ORDER MATTERS HERE, UNLIKE kbd_state
 * The NEW index does not exist until the original has run, so this detour must
 * call through FIRST and read afterwards. That means it reads through a
 * caller-supplied pointer on a thread we do not control, which this project has
 * already killed the game by doing once. So:
 *   - `cursor` is VirtualQuery-checked (committed, readable, not guarded)
 *     before any dereference, every call -- not once and cached.
 *   - `screen` is never dereferenced at all; only its value is recorded.
 *   - the return value is passed through untouched, so the game's own control
 *     flow is unchanged whatever we conclude.
 * Off switch: cfc2_hook.off next to the exe, shared with the other hooks.
 */
#ifndef CFC2_MENU_CURSOR_H
#define CFC2_MENU_CURSOR_H

#include <windows.h>

/* Live addresses. ASLR off, no relocations -- valid in every future run. */
#define MC_VERT_ADDR  0x14007cdc0ULL
#define MC_HORZ_ADDR  0x14007c9f0ULL

/* One observed cursor movement. */
typedef struct {
    void *screen;      /* rcx -- recorded only, never dereferenced */
    int  *cursor;      /* rdx -- the index variable this menu uses */
    int   old_index;
    int   new_index;
    int   min_index;
    int   max_index;   /* r9d: the LAST valid index, so count == max - min + 1 */
    /* ⚠ The helper's return value is the DIRECTION THE USER PRESSED (+1/-1),
     * not the change in the index. It is computed before any index arithmetic
     * and then used as the step. The two differ exactly when the cursor WRAPS:
     * in-game, 0 -> 5 on a 6-item menu reports step -1. That is the more useful
     * semantics -- it tells a wrap apart from a jump. */
    int   step;        /* +1/-1: the direction, NOT new_index - old_index */
    BOOL  vertical;    /* which helper: up/down vs left/right */

    /* ⭐⭐ WHICH SCREEN. The 68 call sites sit in 37 distinct functions -- one
     * per menu, each a state machine dispatching on [this+8]. So the CALLER
     * identifies the screen, and the enclosing function identifies it more
     * coarsely than the exact call site (19 functions contain several cursor
     * calls, for different lists or axes within one screen).
     *
     * ⭐ A code address, valid forever: ASLR is off and the exe has no
     * relocations. Recording it needs NO dereference, which is what makes this
     * safe -- see the warning on `screen` above.
     *
     * ⚠ `caller` is the raw return address from OUR stack frame, so it is
     * trustworthy in a way a caller-SUPPLIED pointer is not. It is still only
     * recorded and compared, never used as a pointer. */
    void *caller;      /* return address: which call site invoked the helper */
} mc_move;

/* Why an install attempt ended the way it did.
 *
 * ⭐⭐ Same reason kbd_state has one: a test asserting only "install returned
 * FALSE" passed with every guard deleted, because MinHook independently refuses
 * an unmapped address. The outcome was right and the assertion was worthless.
 * Reporting WHICH layer refused makes the guards observable. */
typedef enum {
    MC_INSTALL_NOT_TRIED = 0,
    MC_INSTALL_OK,
    MC_INSTALL_BAD_PROTECT,     /* a target is not committed executable memory */
    MC_INSTALL_BAD_PROLOGUE,    /* the first bytes are not the expected pushes */
    MC_INSTALL_MINHOOK_FAILED   /* guards passed; MinHook itself refused */
} mc_install_status;

BOOL mc_install(void);
mc_install_status mc_install_why(void);
void mc_remove(void);

/* How many times each helper has run, and how many times the cursor actually
 * MOVED. ⭐ Both counts, not just the second: "0 moves" is only meaningful
 * beside a large call count. With calls == 0 the hook is not intercepting and
 * nothing at all can be concluded -- see the project's rule that "could not
 * measure" must be distinguishable from "measured nothing". */
typedef struct {
    BOOL installed;
    LONG calls;        /* helper invocations seen */
    LONG moves;        /* invocations where the index changed */
    LONG rejected;     /* invocations where `cursor` failed its safety check */
} mc_stats;

void mc_get_stats(mc_stats *out);

/* Pop the most recent movement, if one has been recorded since the last call.
 * Returns FALSE when nothing new has happened -- a return value, so "no
 * movement" cannot be confused with a stale repeat of the previous one. */
BOOL mc_take_move(mc_move *out);

/* Log any movement, plus a one-time report of the first capture. Called from
 * the worker thread. */
void mc_poll_and_log(void);

/* Test-only seam: drive the shipping detour body with synthetic arguments,
 * instead of a real game call. Returns the delta the body computed.
 * ⭐ This exists so the offline test exercises the REAL function rather than a
 * copy of its logic -- a test against a reimplementation proves nothing about
 * what ships.
 *
 * `writable` tells the stand-in "original" whether it may write through
 * `cursor`. ⛔ The test passes FALSE for the deliberately-unmapped pointers of
 * cases 5 and 6. It must NOT be derived by calling cursor_is_safe() inside the
 * stand-in: that was tried, and it made those two cases pass even with the
 * hook's own pointer check sabotaged away -- the harness was supplying the
 * mechanism under test. */
/* Test-only: the sfx_sel and wrap the stand-in "original" was handed on the
 * last call.
 *
 * ⛔⛔ These exist because the detour declared only FOUR arguments while the
 * real helper takes SIX, so the original read its 5th and 6th out of stack
 * slots our frame never filled. The 5th is the cursor SFX SELECTOR -- the user
 * heard the menu sound change. Ten passing tests missed it, because every one
 * of them asked what the hook REPORTED and none asked what it PASSED ON. */
void mc_test_last_passthrough(int *sfx_sel, int *wrap);

int mc_test_body(void *screen, int *cursor, int min, int max,
                 BOOL vertical, int simulated_new_index, BOOL writable,
                 int simulated_step, void *simulated_caller);

#endif /* CFC2_MENU_CURSOR_H */
