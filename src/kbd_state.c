/*
 * The shell's decoded keyboard state -- see kbd_state.h for the offline
 * derivation this file rests on.
 */

#include "kbd_state.h"
#include "log.h"
#include "memsafe.h"

#include "../libs/minhook/include/MinHook.h"

#include <stdio.h>
#include <string.h>

/* ⭐ The builder's real signature, as far as we need it.
 *
 * On x64 there is only one calling convention, so rcx/rdx arrive as the first
 * two parameters whatever the function's true arity is. Declaring only the two
 * we care about is safe HERE specifically because we pass them straight through
 * to the original and never return early: extra arguments the game passes stay
 * untouched in their registers and on the caller's stack, and the return value
 * is whatever the original produced.
 *
 * ⚠ The prologue is `mov [rsp+0x10],rbx` -- a single 5-byte instruction, which
 * is exactly MinHook's minimum and has no branch target inside it. Verified
 * against the dump before this hook was written; a 4-byte prologue would have
 * needed a different technique. */
typedef void (*pfn_builder)(void *rcx, void *rdx);
static pfn_builder g_real_builder;

static volatile LONG  g_calls;
static void * volatile g_manager;
static void * volatile g_state;
static BOOL  g_installed;
static kbd_install_status g_why = KBD_INSTALL_NOT_TRIED;

kbd_install_status kbd_install_why(void) { return g_why; }

/* The hook. Records the two arguments and gets out of the way.
 *
 * ⚠ It does NOT read through either pointer. Inside a hook, on a thread we do
 * not control, a caller's pointer is untrusted -- and this project has already
 * killed the game once by dereferencing one. Capturing the VALUE is safe;
 * reading through it happens later, guarded, on our own thread. */
static void hook_builder(void *rcx, void *rdx)
{
    InterlockedIncrement(&g_calls);
    /* Publish once. These are singleton pointers and do not change, so writing
     * every frame would be pointless cache traffic; but a plain compare-and-set
     * is cheaper than a branch mispredict either way. */
    if (rcx) InterlockedExchangePointer((void * volatile *)&g_manager, rcx);
    if (rdx) InterlockedExchangePointer((void * volatile *)&g_state, rdx);
    g_real_builder(rcx, rdx);
}

BOOL kbd_install(void)
{
    if (g_installed)
        return TRUE;

    void *target = (void *)(ULONG_PTR)KBD_BUILDER_ADDR;

    /* ⭐ Verify the target is what the offline work says it is BEFORE writing
     * a detour into it. The exe is Enigma-packed, so this address is only code
     * after unpack; hooking a still-encrypted page would patch ciphertext and
     * the game would execute our trampoline into garbage. VirtualQuery is the
     * check that distinguishes "unpacked and executable" from "looks fine". */
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(target, &mbi, sizeof(mbi)) != sizeof(mbi) ||
        mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        log_line("kbd: 0x%llx is not committed executable memory -- REFUSING "
                 "to hook. Nothing was written.", KBD_BUILDER_ADDR);
        g_why = KBD_INSTALL_BAD_PROTECT;
        return FALSE;
    }

    /* ⭐⭐ And verify the IDENTITY, not just the protection. The vtable slot at
     * 0x14104e718+0x30 must still hold the update function, and the class name
     * must still read "sKeyboard". If either fails, the offline model does not
     * match this build and hooking would be acting on a stale claim -- exactly
     * the failure this project has paid for repeatedly. */
    ULONGLONG slot = 0;
    if (!mem_copy(&slot, (void *)(ULONG_PTR)(KBD_VTABLE_ADDR + 0x30),
                  sizeof(slot)) || slot != KBD_UPDATE_ADDR) {
        log_line("kbd: vtable slot 0x%llx holds 0x%llx, expected 0x%llx -- "
                 "the offline model does NOT match this build. REFUSING.",
                 KBD_VTABLE_ADDR + 0x30, slot, KBD_UPDATE_ADDR);
        g_why = KBD_INSTALL_BAD_VTABLE;
        return FALSE;
    }
    char name[10];
    if (!mem_copy(name, (void *)(ULONG_PTR)KBD_NAME_ADDR, sizeof(name)) ||
        memcmp(name, "sKeyboard", 9) != 0) {
        log_line("kbd: the class name at 0x%llx is not \"sKeyboard\" -- "
                 "REFUSING to hook.", KBD_NAME_ADDR);
        g_why = KBD_INSTALL_BAD_NAME;
        return FALSE;
    }
    log_line("kbd: identity CONFIRMED in this process -- vtable+0x30 -> "
             "0x%llx and the class name reads \"sKeyboard\".", KBD_UPDATE_ADDR);

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        log_line("kbd: MH_Initialize failed (%d).", (int)st);
        g_why = KBD_INSTALL_MINHOOK_FAILED;
        return FALSE;
    }
    st = MH_CreateHook(target, (void *)&hook_builder, (void **)&g_real_builder);
    if (st != MH_OK) {
        log_line("kbd: MH_CreateHook failed (%d).", (int)st);
        g_why = KBD_INSTALL_MINHOOK_FAILED;
        return FALSE;
    }
    st = MH_EnableHook(target);
    if (st != MH_OK) {
        log_line("kbd: MH_EnableHook failed (%d); removing.", (int)st);
        g_why = KBD_INSTALL_MINHOOK_FAILED;
        MH_RemoveHook(target);
        return FALSE;
    }

    g_installed = TRUE;
    g_why = KBD_INSTALL_OK;
    log_line("kbd: hooked the sKeyboard state builder at 0x%llx. It receives "
             "rcx=the instance and rdx=the state bitmap, so the instance "
             "pointer arrives as an ARGUMENT -- no memory search.",
             KBD_BUILDER_ADDR);
    return TRUE;
}

void kbd_remove(void)
{
    if (!g_installed)
        return;
    MH_DisableHook((void *)(ULONG_PTR)KBD_BUILDER_ADDR);
    MH_RemoveHook((void *)(ULONG_PTR)KBD_BUILDER_ADDR);
    g_installed = FALSE;
    log_line("kbd: hook removed.");
}

void kbd_snapshot(kbd_capture *out)
{
    memset(out, 0, sizeof(*out));
    out->installed = g_installed;
    out->calls     = InterlockedCompareExchange(&g_calls, 0, 0);
    out->manager   = InterlockedCompareExchangePointer(
                         (void * volatile *)&g_manager, NULL, NULL);
    out->state     = InterlockedCompareExchangePointer(
                         (void * volatile *)&g_state, NULL, NULL);
    /* ⭐ THE FALSIFIABLE CHECK on the whole offline chain. The disassembly says
     * `lea rdi,[rcx+0x138]`, so the two captured pointers MUST differ by
     * exactly 0x138. If they do not, the model is wrong and every offset below
     * is wrong with it -- so this is reported, not assumed. */
    if (out->manager && out->state)
        out->offset_ok = ((BYTE *)out->state - (BYTE *)out->manager)
                             == KBD_STATE_OFFSET;
}

BOOL kbd_read(DWORD words[5][KBD_BANK_WORDS])
{
    void *state = InterlockedCompareExchangePointer(
                      (void * volatile *)&g_state, NULL, NULL);
    if (!state)
        return FALSE;

    static const int banks[5] = {
        KBD_BANK_CUR, KBD_BANK_OLD, KBD_BANK_TRG, KBD_BANK_REL, KBD_BANK_CHG
    };
    for (int b = 0; b < 5; b++) {
        if (!mem_copy(words[b], (BYTE *)state + banks[b],
                      KBD_BANK_WORDS * sizeof(DWORD)))
            return FALSE;   /* ⭐ a return value: "could not measure" must never
                             * look like "no keys are down". */
    }
    return TRUE;
}

BOOL kbd_is_set(const DWORD bank[KBD_BANK_WORDS], int code)
{
    if (code < 0 || code >= KBD_BANK_WORDS * 32)
        return FALSE;
    return (bank[code >> 5] & (1u << (code & 31))) != 0;
}

const DWORD *g_kbd_test_cur;

static DWORD g_prev[KBD_BANK_WORDS];
static BOOL  g_have_prev;

void kbd_reset_edges(void)
{
    memset(g_prev, 0, sizeof(g_prev));
    g_have_prev = FALSE;
}

BOOL kbd_poll_edges(kbd_edges *out)
{
    DWORD w[5][KBD_BANK_WORDS];
    memset(out, 0, sizeof(*out));
    if (g_kbd_test_cur) {
        memcpy(w[0], g_kbd_test_cur, sizeof(w[0]));
    } else if (!kbd_read(w)) {
        return FALSE;   /* could not measure -- NOT "nothing pressed" */
    }

    /* ⭐ Edges from successive `cur` reads, NOT from the engine's Trg bank.
     * Trg lives one frame; this poller runs every 100 ms and caught it about
     * 1 press in 6 when measured in-game. Differencing `cur` is exact at any
     * poll rate: a key that went down between two reads shows up in exactly
     * one of them. */
    for (int i = 0; i < KBD_BANK_WORDS; i++) {
        DWORD cur = w[0][i];
        out->down[i]     = cur;
        out->pressed[i]  = cur & ~g_prev[i];
        out->released[i] = g_prev[i] & ~cur;
        g_prev[i] = cur;
    }

    /* ⚠ On the very first call every held key looks "newly pressed", because
     * there is no previous sample to difference against. Say so rather than
     * reporting phantom presses -- a caller that acts on them would fire an
     * action the user never requested. */
    out->first = !g_have_prev;
    if (!g_have_prev) {
        g_have_prev = TRUE;
        memset(out->pressed, 0, sizeof(out->pressed));
        memset(out->released, 0, sizeof(out->released));
    }
    return TRUE;
}

/* Format the set bits of a bank as a list of internal codes. */
static void fmt_bits(char *buf, size_t cap, const DWORD *bank)
{
    size_t used = 0;
    buf[0] = '\0';
    int n = 0;
    for (int w = 0; w < KBD_BANK_WORDS; w++) {
        for (int b = 0; b < 32; b++) {
            if (!(bank[w] & (1u << b)))
                continue;
            int code = w * 32 + b;
            int k = _snprintf(buf + used, cap - used, "%s0x%02x",
                              n ? " " : "", code);
            if (k < 0 || (size_t)k >= cap - used) { used = cap - 1; goto done; }
            used += (size_t)k;
            if (++n >= 12) { _snprintf(buf + used, cap - used, " ..."); goto done; }
        }
    }
done:
    if (!n)
        _snprintf(buf, cap, "(none)");
}

void kbd_poll_and_log(void)
{
    kbd_capture cap;
    kbd_snapshot(&cap);

    if (!cap.installed)
        return;

    /* ⭐ Liveness before findings, same rule as the caller trace: if the
     * builder never ran, "no keys down" is a measurement that never happened. */
    static BOOL warned_idle;
    if (cap.calls == 0) {
        if (!warned_idle) {
            log_line("kbd: ⛔ the builder has not run yet -- no state has been "
                     "captured, so nothing can be concluded about key state.");
            warned_idle = TRUE;
        }
        return;
    }

    static BOOL announced;
    if (!announced && cap.state) {
        announced = TRUE;
        log_line("kbd: CAPTURED instance=%p state=%p after %ld builder calls.",
                 cap.manager, cap.state, cap.calls);
        if (cap.offset_ok) {
            log_line("kbd: ⭐⭐ state - instance == 0x%x, exactly as the "
                     "disassembly predicted. The offline model is CONFIRMED in "
                     "live memory.", KBD_STATE_OFFSET);
        } else {
            log_line("kbd: ⚠⚠ state - instance == 0x%llx but the disassembly "
                     "said 0x%x. The model is WRONG; do not trust the bank "
                     "offsets below.",
                     (unsigned long long)((BYTE *)cap.state -
                                          (BYTE *)cap.manager),
                     KBD_STATE_OFFSET);
        }
    }

    DWORD w[5][KBD_BANK_WORDS];
    if (!kbd_read(w)) {
        static BOOL warned_read;
        if (!warned_read) {
            warned_read = TRUE;
            log_line("kbd: ⛔ could not READ the state block (fault or no "
                     "pointer). Nothing is concluded.");
        }
        return;
    }

    /* Edges DERIVED from successive `cur` reads. This is the reliable source;
     * see the note on kbd_poll_edges(). */
    kbd_edges e;
    if (!kbd_poll_edges(&e))
        return;

    /* Log only on change, so the log shows navigation rather than a per-frame
     * flood -- a flood is not evidence, and it would bury the signal. */
    static DWORD last_cur[KBD_BANK_WORDS];
    static BOOL  have_last;
    if (have_last && memcmp(last_cur, e.down, sizeof(last_cur)) == 0)
        return;
    memcpy(last_cur, e.down, sizeof(last_cur));
    have_last = TRUE;

    char cur[160], press[160], rel[160], engine_trg[160];
    fmt_bits(cur,   sizeof(cur),   e.down);
    fmt_bits(press, sizeof(press), e.pressed);
    fmt_bits(rel,   sizeof(rel),   e.released);
    /* ⭐ The engine's own Trg is logged ALONGSIDE ours, not instead of it.
     * In-game it was empty for ~5 of every 6 presses because it lives one
     * frame and we sample at 100 ms. Showing both keeps that visible as
     * evidence rather than a remembered caveat -- if engine_trg ever starts
     * agreeing with pressed, the sampling assumption has changed. */
    fmt_bits(engine_trg, sizeof(engine_trg), w[2]);
    log_line("kbd: down=[%s]  pressed=[%s]  released=[%s]  (engine Trg=[%s])",
             cur, press, rel, engine_trg);
}
