/*
 * The shell's menu cursor -- see menu_cursor.h for the offline derivation this
 * file rests on.
 */

#include "menu_cursor.h"
#include "log.h"
#include "memsafe.h"
#include "menu_speak.h"

#include "../libs/minhook/include/MinHook.h"

#include <stdio.h>
#include <string.h>

/* ⭐ Only the arguments we need are declared. On x64 there is one calling
 * convention, so rcx/rdx/r8d/r9d arrive as the first four parameters whatever
 * the function's true arity is; the two stack arguments (sfx_sel, wrap) stay
 * untouched in the caller's frame and reach the original unchanged, because we
 * pass the register arguments straight through and never return early.
 *
 * ⚠ This works only because we do not touch the stack arguments. If this hook
 * ever needs `wrap`, the declaration must grow to the full six -- reading a
 * stack argument that the prototype does not mention is undefined, not merely
 * untidy. */
typedef int (*pfn_move)(void *screen, int *cursor, int min, int max);

static pfn_move g_real_vert;
static pfn_move g_real_horz;

static volatile LONG g_calls;
static volatile LONG g_moves;
static volatile LONG g_rejected;

/* The most recent movement, published for the worker thread.
 *
 * ⚠ A sequence counter, not a flag: the hook runs on the render thread and the
 * reader on ours, so a plain "new data" boolean could be cleared between the
 * hook's write and the reader's read and lose a movement. The reader takes a
 * snapshot and compares sequence numbers either side of it -- if they differ,
 * the hook wrote during the copy and the snapshot is discarded rather than
 * reported torn. */
static volatile LONG g_seq;
static mc_move       g_last;
static volatile LONG g_taken_seq;

static BOOL              g_installed;
static mc_install_status g_why = MC_INSTALL_NOT_TRIED;

mc_install_status mc_install_why(void) { return g_why; }

/* Is this pointer safe to dereference as an int, right now?
 *
 * ⚠⚠ Checked on EVERY call, never cached. The cursor belongs to a screen
 * object the game can free and reallocate between menus; a check done once at
 * first sight would still be "passing" long after the memory went away. The
 * cost is one VirtualQuery per keypress-driven call, which is nothing.
 *
 * ⚠ PAGE_GUARD is excluded explicitly: reading a guard page does not fault
 * harmlessly, it consumes the guard and changes the process's behaviour. This
 * project has hit that before. */
static BOOL cursor_is_safe(const int *p)
{
    if (!p || ((ULONG_PTR)p & 3u))     /* misaligned cannot be a live int* */
        return FALSE;

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi))
        return FALSE;
    if (mbi.State != MEM_COMMIT)
        return FALSE;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
        return FALSE;
    if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                         PAGE_EXECUTE_WRITECOPY)))
        return FALSE;

    /* The whole int must be inside this region, not straddling its end. */
    ULONG_PTR end = (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;
    return (ULONG_PTR)p + sizeof(int) <= end;
}

/* The shared detour body.
 *
 * ⚠⚠ THE ORDER IS THE WHOLE POINT, and it differs from kbd_state's hook. The
 * new index does not exist until the original has run, so we must read `cursor`
 * BEFORE (for the old value) and AFTER (for the new one). Both reads go through
 * cursor_is_safe(); a caller's pointer is untrusted even when it looked fine a
 * microsecond earlier.
 *
 * ⚠ The original is called UNCONDITIONALLY and its return value passed through
 * untouched, whatever we manage or fail to observe. The game's control flow
 * must not depend on our success. */
static int detour_body(pfn_move real, void *screen, int *cursor,
                       int min, int max, BOOL vertical, void *caller)
{
    /* ⚠⚠ EVERYTHING LIVE ACROSS A setjmp MUST BE volatile. GCC's -Wclobbered is
     * not pedantry here: longjmp may restore a stale register copy, and a stale
     * `delta` would be RETURNED TO THE GAME -- the hook would tell a menu that
     * its cursor moved when it did not, or vice versa. This project's rule is
     * to fix these, never to wave them through. */
    volatile BOOL safe = cursor_is_safe(cursor);
    volatile int  old  = 0;
    volatile int  delta;
    BOOL faulted = FALSE;

    if (safe)
        MEM_GUARDED(faulted, old = *(volatile int *)cursor);
    if (faulted)
        safe = FALSE;

    delta = real(screen, cursor, min, max);

    InterlockedIncrement(&g_calls);
    if (!safe) {
        InterlockedIncrement(&g_rejected);
        return delta;
    }

    volatile int neu = old;
    MEM_GUARDED(faulted, neu = *(volatile int *)cursor);
    if (faulted) {
        InterlockedIncrement(&g_rejected);
        return delta;
    }

    if (neu == old)
        return delta;   /* the helper ran but the cursor did not move */

    /* Publish. Bump the sequence, write, bump again: a reader that sees the
     * same even value either side of its copy knows the copy was clean. */
    InterlockedIncrement(&g_seq);
    g_last.screen    = screen;
    g_last.cursor    = cursor;
    g_last.old_index = old;
    g_last.new_index = neu;
    g_last.min_index = min;
    g_last.max_index = max;
    g_last.step      = delta;
    g_last.vertical  = vertical;
    g_last.caller    = caller;
    InterlockedIncrement(&g_seq);

    InterlockedIncrement(&g_moves);
    return delta;
}

/* ⚠ __builtin_return_address(0) is the GAME's return address, not MinHook's:
 * MinHook patches the target with a jmp and pushes nothing, so the address the
 * CPU pushed for `call 0x14007cdc0` is still the one on the stack. The caller
 * trace established this; GCC has no _ReturnAddress().
 *
 * ⭐⭐ BUT A RETURN ADDRESS IS NOT A CALL SITE -- it points at the instruction
 * AFTER the call, 5 bytes on for `call rel32`. Shipping the raw value made
 * every screen unknown: the table held call sites read off the disassembly
 * (0x14009ba21) while the hook reported returns (0x14009ba26), so nothing ever
 * matched and every menu fell back to position-only. Measured on all six
 * screens in the 2026-09-08 run: ret - 5 == a known call site, every time,
 * and no raw return address was itself a call site.
 *
 * So normalise HERE, once, and let `caller` mean the call site -- the thing
 * that can be looked up in a disassembler. ⚠ Verified, not assumed: the byte
 * 5 back must actually be 0xE8 (call rel32). If it is not, the frame is not
 * what we think and the raw value is kept, so a wrong normalisation can never
 * silently masquerade as a real call site. */
static void *call_site_of(void *ret)
{
    BYTE *p = (BYTE *)ret - 5;
    BYTE op;
    if (!mem_copy(&op, p, 1) || op != 0xE8)
        return ret;                    /* not a call rel32 -- do not rewrite */
    return p;
}

static int hook_vert(void *screen, int *cursor, int min, int max)
{
    return detour_body(g_real_vert, screen, cursor, min, max, TRUE,
                       call_site_of(__builtin_return_address(0)));
}

static int hook_horz(void *screen, int *cursor, int min, int max)
{
    return detour_body(g_real_horz, screen, cursor, min, max, FALSE,
                       call_site_of(__builtin_return_address(0)));
}

/* The stand-in "original" used only by mc_test_body(). It writes the new index
 * through the caller's pointer and returns the STEP the caller asked to
 * simulate.
 *
 * ⛔⛔ IT USED TO RETURN `new - old`, AND THAT TAUGHT ME A FALSE FACT.
 * The real helper's return value is the DIRECTION THE USER PRESSED (+1/-1),
 * computed at 0x14007ce96-0x14007ceb6 BEFORE any index arithmetic and then used
 * as the step: `lea eax,[r15 + rbx]`. It is not the change in the index. The
 * two only agree when the cursor does not wrap -- so the in-game log showed
 * `0 -> 5` with `delta -1`, which looked like a sign bug in the mod and was
 * actually the stand-in having encoded my assumption instead of the game's
 * behaviour. Case 1 passed because the harness supplied the answer.
 * ⭐ Same trap as the `cursor_is_safe()` one, in a different place: a stand-in
 * must be TOLD what to do, never compute it plausibly.
 * ⭐ And the real semantics are the more useful ones -- a wrap is
 * distinguishable from a jump, which an index delta cannot express.
 *
 * ⚠ The real helper writes through rdx unconditionally -- in the game that
 * pointer is always the game's own. The test deliberately passes an unmapped
 * one to exercise the HOOK's rejection path, and a stand-in that faulted there
 * would take the test process down before the hook's behaviour could be seen.
 *
 * ⛔⛔ SO WHY NOT JUST CALL cursor_is_safe() HERE? Tried, and it made cases 5
 * and 6 WORTHLESS: with the hook's own check sabotaged to `safe = TRUE`, the
 * whole suite still passed, because the stand-in's copy of the check refused
 * instead. The harness was silently supplying the mechanism under test.
 * ⭐ The stand-in must therefore be told what to do by the TEST, never derive
 * it the same way the shipping code does -- otherwise a broken guard and a
 * working one are indistinguishable. */
static int  g_sim_new;
static int  g_sim_step;       /* what the real helper returns: the direction */
static BOOL g_sim_writable;   /* set by mc_test_body: may the stand-in write? */

static int fake_original(void *screen, int *cursor, int min, int max)
{
    (void)screen; (void)min; (void)max;
    if (!g_sim_writable)
        return 0;
    *cursor = g_sim_new;
    return g_sim_step;
}

int mc_test_body(void *screen, int *cursor, int min, int max,
                 BOOL vertical, int simulated_new_index, BOOL writable,
                 int simulated_step, void *simulated_caller)
{
    /* ⭐ The seam drives the SHIPPING detour_body(), not a copy of it. A test
     * written against a reimplementation proves nothing about what ships. */
    g_sim_new      = simulated_new_index;
    g_sim_step     = simulated_step;
    g_sim_writable = writable;
    return detour_body(fake_original, screen, cursor, min, max, vertical,
                       simulated_caller);
}

/* --- install -------------------------------------------------------------- */

/* The expected first bytes of both helpers:
 *   push rbx; push rbp; push rsi; push rdi; push r14
 * = 40 53 55 56 57 41 56 -- SEVEN bytes (push rbx and push r14 are two each,
 * the rest one), spanning five whole instructions. That clears MinHook's
 * five-byte minimum with a clean boundary at byte 5.
 *
 * ⭐ And nothing branches INTO those bytes: tools/find_callers.py was run
 * against each of addr+1..addr+4 for both helpers, with --jmp, and found zero
 * sites. A branch landing mid-prologue would jump into the middle of our
 * trampoline patch. Checked rather than assumed.
 *
 * ⭐ Verified offline against the dump BEFORE this hook was written, and
 * verified again here at runtime: the exe is Enigma-packed, so these addresses
 * hold ciphertext until unpack completes. Hooking too early would patch
 * encrypted bytes and send the game through a trampoline into garbage. */
static const BYTE MC_PROLOGUE[] = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56 };

static BOOL check_target(ULONGLONG addr)
{
    void *p = (void *)(ULONG_PTR)addr;

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi) ||
        mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        log_line("mc: 0x%llx is not committed executable memory -- REFUSING "
                 "to hook. Nothing was written.", addr);
        g_why = MC_INSTALL_BAD_PROTECT;
        return FALSE;
    }

    BYTE got[sizeof(MC_PROLOGUE)];
    if (!mem_copy(got, p, sizeof(got)) ||
        memcmp(got, MC_PROLOGUE, sizeof(got)) != 0) {
        log_line("mc: the bytes at 0x%llx are not the expected prologue "
                 "(%02x %02x %02x %02x %02x %02x %02x) -- the offline model "
                 "does NOT match this build. REFUSING.",
                 addr, got[0], got[1], got[2], got[3], got[4], got[5], got[6]);
        g_why = MC_INSTALL_BAD_PROLOGUE;
        return FALSE;
    }
    return TRUE;
}

BOOL mc_install(void)
{
    if (g_installed)
        return TRUE;

    if (!check_target(MC_VERT_ADDR) || !check_target(MC_HORZ_ADDR))
        return FALSE;

    log_line("mc: identity CONFIRMED in this process -- both cursor helpers "
             "carry the expected prologue.");

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        log_line("mc: MH_Initialize failed (%d).", (int)st);
        g_why = MC_INSTALL_MINHOOK_FAILED;
        return FALSE;
    }

    void *v = (void *)(ULONG_PTR)MC_VERT_ADDR;
    void *h = (void *)(ULONG_PTR)MC_HORZ_ADDR;

    if (MH_CreateHook(v, (void *)&hook_vert, (void **)&g_real_vert) != MH_OK ||
        MH_CreateHook(h, (void *)&hook_horz, (void **)&g_real_horz) != MH_OK) {
        log_line("mc: MH_CreateHook failed; removing both.");
        MH_RemoveHook(v);
        MH_RemoveHook(h);
        g_why = MC_INSTALL_MINHOOK_FAILED;
        return FALSE;
    }

    /* ⚠ Enable BOTH or NEITHER. A half-installed pair would report vertical
     * movement only, and "the user never pressed Left" and "we never hooked
     * Left" would be indistinguishable in the log. */
    if (MH_EnableHook(v) != MH_OK || MH_EnableHook(h) != MH_OK) {
        log_line("mc: MH_EnableHook failed; removing both so the result "
                 "cannot be half-measured.");
        MH_DisableHook(v);
        MH_DisableHook(h);
        MH_RemoveHook(v);
        MH_RemoveHook(h);
        g_why = MC_INSTALL_MINHOOK_FAILED;
        return FALSE;
    }

    g_installed = TRUE;
    g_why = MC_INSTALL_OK;
    log_line("mc: hooked both menu cursor helpers -- vertical 0x%llx, "
             "horizontal 0x%llx. The screen object and the cursor pointer "
             "arrive as ARGUMENTS, so there is no memory search.",
             MC_VERT_ADDR, MC_HORZ_ADDR);
    return TRUE;
}

void mc_remove(void)
{
    if (!g_installed)
        return;
    void *v = (void *)(ULONG_PTR)MC_VERT_ADDR;
    void *h = (void *)(ULONG_PTR)MC_HORZ_ADDR;
    MH_DisableHook(v);
    MH_DisableHook(h);
    MH_RemoveHook(v);
    MH_RemoveHook(h);
    g_installed = FALSE;
    log_line("mc: hooks removed.");
}

void mc_get_stats(mc_stats *out)
{
    memset(out, 0, sizeof(*out));
    out->installed = g_installed;
    out->calls     = InterlockedCompareExchange(&g_calls, 0, 0);
    out->moves     = InterlockedCompareExchange(&g_moves, 0, 0);
    out->rejected  = InterlockedCompareExchange(&g_rejected, 0, 0);
}

BOOL mc_take_move(mc_move *out)
{
    LONG before = InterlockedCompareExchange(&g_seq, 0, 0);
    if (before == InterlockedCompareExchange(&g_taken_seq, 0, 0))
        return FALSE;                       /* nothing new since last time */
    if (before & 1)
        return FALSE;                       /* a write is in flight; next poll */

    mc_move snap = g_last;

    LONG after = InterlockedCompareExchange(&g_seq, 0, 0);
    if (after != before)
        return FALSE;                       /* torn -- discard, never report */

    InterlockedExchange(&g_taken_seq, after);
    *out = snap;
    return TRUE;
}

void mc_poll_and_log(void)
{
    mc_stats st;
    mc_get_stats(&st);
    if (!st.installed)
        return;

    /* ⭐ Liveness before findings. If the helpers have never run, "no movement"
     * is a measurement that never happened, not a negative result. */
    static BOOL warned_idle;
    if (st.calls == 0) {
        if (!warned_idle) {
            warned_idle = TRUE;
            log_line("mc: the cursor helpers have not run yet -- nothing can "
                     "be concluded about menu movement.");
        }
        return;
    }

    static BOOL announced;
    if (!announced) {
        announced = TRUE;
        log_line("mc: the helpers ARE running (%ld calls so far). The hook is "
                 "live.", st.calls);
    }

    /* ⚠ `if`, not `while`: only the most recent movement is kept, so a loop
     * would suggest a queue that does not exist. */
    mc_move m;
    if (mc_take_move(&m)) {
        /* ⭐ Speak first, log second: the user is waiting on the readout, and
         * the log is for us. menu_speak() falls back to position-only whenever
         * the screen is unknown or the text cannot be read -- it never guesses
         * a name. */
        menu_speak(&m);
        if (!menu_speak_lookup((ULONGLONG)(ULONG_PTR)m.caller))
            menu_speak_log_unknown(&m);

        int count = m.max_index - m.min_index + 1;
        log_line("mc: %s cursor %d -> %d  (item %d of %d, range %d..%d, "
                 "step %+d)  caller=%p screen=%p cursor=%p",
                 m.vertical ? "vertical  " : "horizontal",
                 m.old_index, m.new_index,
                 m.new_index - m.min_index + 1, count,
                 m.min_index, m.max_index, m.step, m.caller, m.screen, m.cursor);
    }

    /* ⚠ A rejected pointer is REPORTED, not silently skipped: it would mean the
     * argument model is wrong somewhere, and a quiet skip would look exactly
     * like a menu the user never entered. */
    static LONG last_rejected;
    if (st.rejected != last_rejected) {
        last_rejected = st.rejected;
        log_line("mc: %ld call(s) had an unreadable cursor pointer and were "
                 "skipped. Those movements are MISSING from the log above.",
                 st.rejected);
    }
}
