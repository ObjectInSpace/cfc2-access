/*
 * Caller tracer for GetAsyncKeyState -- see caller_trace.h for why.
 */

#include "caller_trace.h"
#include "log.h"
#include "memsafe.h"

#include "../libs/minhook/include/MinHook.h"

#include <stdio.h>
#include <string.h>

/* --- State ---------------------------------------------------------------
 * All of it is touched from the game's threads inside the hook, so every
 * mutation is an interlocked op and the table is append-only. Nothing is ever
 * removed, so a pointer once published stays valid for the process lifetime. */

typedef SHORT (WINAPI *pfn_GetAsyncKeyState)(int);
static pfn_GetAsyncKeyState g_real;

static volatile LONG g_total;
static volatile LONG g_self;
static volatile LONG g_foreign;
static volatile LONG g_unreadable;
static volatile LONG g_overflow;

static volatile LONG g_n_sites;           /* published count */
static ct_site       g_sites[CT_MAX_SITES];
/* Per-site bitmap of which VKs that site asked about, 256 bits. Kept out of
 * ct_site so the reported struct stays small; only the popcount is exported. */
static volatile LONG g_vkmap[CT_MAX_SITES][8];

static BOOL  g_installed;

/* Our own module's address range, so we can exclude ourselves. Computed once
 * at install time: comparing a base address is cheap enough for a hook that
 * runs every frame, and it is the whole reason this trace can be believed. */
static ULONG_PTR g_self_lo, g_self_hi;

/* --- The hook ------------------------------------------------------------
 * Runs on the game's threads, every frame, potentially several times per frame.
 * Constraints, all load-bearing:
 *   - no allocation, no locks, no I/O, no speech;
 *   - no dereference of the return address, ever -- it is only compared and,
 *     once, VirtualQuery'd;
 *   - the real function is called unconditionally and its value returned
 *     unchanged, so the game's behaviour is identical whether or not we are
 *     here. This hook OBSERVES; it must not become a participant.
 */
static SHORT WINAPI hook_GetAsyncKeyState(int vk)
{
    /* ⭐ Why __builtin_return_address(0) is the CALLER, not MinHook.
     * MinHook patches the target's first bytes with a `jmp` straight to this
     * function -- it does not call us, and it pushes nothing. So when the game
     * executes `call [GetAsyncKeyState]`, the return address the CPU pushed is
     * the game's, and this frame's return address IS that same value. The
     * trampoline (g_real) holds the displaced original bytes plus a jump back,
     * and is only entered when WE call it below.
     * (MSVC's _ReturnAddress() intrinsic does not exist in GCC; this is the
     * equivalent, and the toolchain here is MinGW GCC 16.1.) */
    void *ret = __builtin_return_address(0);

    InterlockedIncrement(&g_total);

    ULONG_PTR r = (ULONG_PTR)ret;
    if (r >= g_self_lo && r < g_self_hi) {
        /* ⭐ Us. Counted, not discarded: we call this nine times a second, so a
         * zero here would mean the hook is not intercepting at all, and every
         * "no game callers" conclusion would be worthless. */
        InterlockedIncrement(&g_self);
        return g_real(vk);
    }

    InterlockedIncrement(&g_foreign);

    /* Find or add the site. Linear scan over a table that stays tiny; a hash
     * would be more code and more failure modes for no measurable gain at this
     * size, and the scan reads only published entries. */
    LONG n = InterlockedCompareExchange(&g_n_sites, 0, 0);
    for (LONG i = 0; i < n; i++) {
        if (g_sites[i].ret == ret) {
            InterlockedIncrement(&g_sites[i].hits);
            g_sites[i].last_ms = GetTickCount();
            g_sites[i].last_vk = vk;
            /* Record which VK this site asked about. A genuine input reader
             * sweeps many keys; a site that only ever asks about one is far
             * more likely a single hotkey test. This distinguishes them
             * without us having to guess in advance which is which. */
            LONG word = (vk >> 5) & 7;
            LONG bit  = 1L << (vk & 31);
            LONG prev = InterlockedOr(&g_vkmap[i][word], bit);
            if (!(prev & bit))
                InterlockedIncrement(&g_sites[i].distinct_vks);
            return g_real(vk);
        }
    }

    /* New site. VirtualQuery it ONCE, here, before it is recorded -- a return
     * address that does not point at committed executable memory is not a call
     * site, it is a corrupted or unwound stack read, and recording it would
     * put a garbage address into the findings. */
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(ret, &mbi, sizeof(mbi)) != sizeof(mbi) ||
        mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        InterlockedIncrement(&g_unreadable);
        return g_real(vk);
    }

    /* Claim a slot. InterlockedIncrement returns the NEW value, so the claimed
     * index is one less; if it is past the end we have overflowed. */
    LONG idx = InterlockedIncrement(&g_n_sites) - 1;
    if (idx >= CT_MAX_SITES) {
        /* Put the count back so it never reads past the array, and count the
         * overflow: a full table means the caller set is not small, which
         * would contradict the model and must be visible, not silent. */
        InterlockedExchange(&g_n_sites, CT_MAX_SITES);
        InterlockedIncrement(&g_overflow);
        return g_real(vk);
    }

    DWORD now = GetTickCount();
    g_sites[idx].hits     = 1;
    g_sites[idx].first_ms = now;
    g_sites[idx].last_ms  = now;
    g_sites[idx].last_vk  = vk;
    g_sites[idx].distinct_vks = 1;
    g_vkmap[idx][(vk >> 5) & 7] = 1L << (vk & 31);
    /* Publish the pointer LAST: a reader that sees a non-NULL ret is then
     * guaranteed to see the fields beside it. */
    InterlockedExchangePointer((void * volatile *)&g_sites[idx].ret, ret);

    return g_real(vk);
}

/* --- Install / remove ---------------------------------------------------- */

BOOL ct_install(void)
{
    if (g_installed)
        return TRUE;

    /* Our own bounds, for the self-filter. GetModuleHandleEx with the ADDRESS
     * flag asks "which module is this code in?", which is exactly the question
     * -- no name matching, no assumptions about our own filename. */
    HMODULE self = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)(void *)&ct_install, &self) || !self) {
        log_line("ct: cannot determine our own module bounds -- ABORTING. "
                 "Without the self-filter the first 'input reader' found would "
                 "be us, and the whole result would be a false positive.");
        return FALSE;
    }
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)self;
    IMAGE_NT_HEADERS *nt  = (IMAGE_NT_HEADERS *)((BYTE *)self + dos->e_lfanew);
    g_self_lo = (ULONG_PTR)self;
    g_self_hi = g_self_lo + nt->OptionalHeader.SizeOfImage;
    log_line("ct: our module is %p-%p; callers in that range are OURS and are "
             "counted separately.", (void *)g_self_lo, (void *)g_self_hi);

    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (!u32) {
        log_line("ct: user32.dll not loaded -- cannot hook. Nothing written.");
        return FALSE;
    }
    void *target = (void *)GetProcAddress(u32, "GetAsyncKeyState");
    if (!target) {
        log_line("ct: GetAsyncKeyState not found in user32 -- nothing written.");
        return FALSE;
    }

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        log_line("ct: MH_Initialize failed (%d) -- nothing written.", (int)st);
        return FALSE;
    }
    st = MH_CreateHook(target, (void *)&hook_GetAsyncKeyState,
                       (void **)&g_real);
    if (st != MH_OK) {
        log_line("ct: MH_CreateHook failed (%d) -- nothing written.", (int)st);
        return FALSE;
    }
    st = MH_EnableHook(target);
    if (st != MH_OK) {
        log_line("ct: MH_EnableHook failed (%d) -- trampoline built but NOT "
                 "enabled; removing.", (int)st);
        MH_RemoveHook(target);
        return FALSE;
    }

    g_installed = TRUE;
    log_line("ct: hooked user32!GetAsyncKeyState at %p (trampoline %p). "
             "This is the first code in this project that WRITES to game "
             "memory. Off switch: cfc2_hook.off next to the exe.",
             target, (void *)g_real);
    return TRUE;
}

void ct_remove(void)
{
    if (!g_installed)
        return;
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        void *target = (void *)GetProcAddress(u32, "GetAsyncKeyState");
        if (target)
            MH_DisableHook(target);
    }
    MH_Uninitialize();
    g_installed = FALSE;
    log_line("ct: hook removed.");
}

void ct_snapshot(ct_report *out)
{
    memset(out, 0, sizeof(*out));
    out->installed     = g_installed;
    out->total_calls   = InterlockedCompareExchange(&g_total, 0, 0);
    out->self_calls    = InterlockedCompareExchange(&g_self, 0, 0);
    out->foreign_calls = InterlockedCompareExchange(&g_foreign, 0, 0);
    out->unreadable    = InterlockedCompareExchange(&g_unreadable, 0, 0);
    out->overflow      = InterlockedCompareExchange(&g_overflow, 0, 0);

    LONG n = InterlockedCompareExchange(&g_n_sites, 0, 0);
    if (n > CT_MAX_SITES) n = CT_MAX_SITES;
    int k = 0;
    for (LONG i = 0; i < n; i++) {
        /* Skip a slot claimed but not yet published -- its ret is still NULL.
         * It will be there next snapshot. */
        if (!g_sites[i].ret)
            continue;
        out->sites[k++] = g_sites[i];
    }
    out->n_sites = k;
}

void ct_report_log(void)
{
    ct_report r;
    ct_snapshot(&r);

    if (!r.installed) {
        log_line("ct: NOT INSTALLED -- no conclusion of any kind can be drawn "
                 "from the absence of callers below.");
        return;
    }

    /* ⭐⭐ The liveness check, stated before the findings rather than buried
     * after them. We call GetAsyncKeyState nine times per 100 ms tick, so if
     * self_calls is zero the detour is not intercepting and "0 game callers"
     * would be a measurement that never happened -- not a negative result.
     * See memory inconclusive-must-be-a-return-value. */
    if (r.self_calls == 0) {
        log_line("ct: ⛔ INCONCLUSIVE -- self_calls=0. We call this function "
                 "9x per 100ms from mod.c, so zero of our own calls means the "
                 "hook is NOT intercepting. total=%ld. Everything below is "
                 "meaningless; do not read it as 'the shell does not poll'.",
                 r.total_calls);
        return;
    }

    log_line("ct: total=%ld  ours=%ld  game=%ld  unreadable=%ld  overflow=%ld  "
             "distinct game sites=%d",
             r.total_calls, r.self_calls, r.foreign_calls,
             r.unreadable, r.overflow, r.n_sites);

    if (r.overflow > 0)
        log_line("ct: ⚠ the site table OVERFLOWED (%ld dropped). More than %d "
                 "distinct callers contradicts the model that a small number of "
                 "routines read input; treat the list below as partial.",
                 r.overflow, CT_MAX_SITES);

    if (r.n_sites == 0) {
        log_line("ct: ⚠ the hook IS live (%ld of our own calls seen) but NO "
                 "call came from outside our module. The shell imports "
                 "GetAsyncKeyState but has not called it yet, or calls it only "
                 "in a state not reached during this run.", r.self_calls);
        return;
    }

    /* Sort by hits, descending. Insertion sort over <=64 entries, done on the
     * snapshot so the live table is untouched. */
    for (int i = 1; i < r.n_sites; i++) {
        ct_site key = r.sites[i];
        int j = i - 1;
        while (j >= 0 && r.sites[j].hits < key.hits) {
            r.sites[j + 1] = r.sites[j];
            j--;
        }
        r.sites[j + 1] = key;
    }

    ULONG_PTR image = (ULONG_PTR)GetModuleHandleW(NULL);
    for (int i = 0; i < r.n_sites; i++) {
        ct_site *s = &r.sites[i];
        ULONG_PTR a = (ULONG_PTR)s->ret;
        /* Report the RVA too: ASLR is off and the exe has no relocations, so
         * the absolute address is stable forever, but the RVA is what goes
         * into the offline riprefs index. */
        log_line("ct:   site %2d  ret=%p  rva=0x%-8llx  hits=%-7ld  "
                 "vks=%-3ld  last_vk=0x%02x  window=%lu ms",
                 i, s->ret,
                 (unsigned long long)(a >= image ? a - image : 0),
                 s->hits, s->distinct_vks, (unsigned)s->last_vk,
                 (unsigned long)(s->last_ms - s->first_ms));
    }

    /* ⚠ Interpretation belongs in the log next to the numbers, because the
     * count alone is ambiguous: the site that polls the MOST KEYS is the input
     * reader, not necessarily the site with the most hits (a tight one-key
     * check in a hot loop can easily out-hit a 70-key sweep). */
    log_line("ct: ⭐ read this by DISTINCT VKS, not by hits. The shell's input "
             "reader sweeps the whole key table (the game's KB_* map has ~70 "
             "entries); a site with vks=1 is a single hotkey test. Feed the "
             "top site's rva to tools/build_riprefs.py and disassemble around "
             "it -- the selection state is written near there.");
}
