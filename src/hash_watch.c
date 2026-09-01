#include "hash_watch.h"
#include "log.h"

#include "MinHook.h"

#include <stdio.h>
#include <string.h>
#include <psapi.h>

/* _ReturnAddress() is an MSVC intrinsic; this project builds with MinGW GCC. */
#ifndef _MSC_VER
#define _ReturnAddress() __builtin_return_address(0)
#endif

/* The confirmed GMD label hash primitive (probe_hash.c reproduced known
 * hashes with it in-process). Signature established by the same experiment. */
#define CFC2_CRC32_A 0x1402ef1e0   /* u32(u32 *seed, const void *buf, u32 len) */
/* The slicing-by-8 CRC over the second table; no direct callers, reached via a
 * pointer. Shape says zlib crc32(crc, buf, len). */
#define CFC2_CRC32_B 0x1405a4a10

typedef unsigned (__fastcall *crc_a_t)(unsigned *seed, const void *buf,
                                       unsigned len);
typedef unsigned (__fastcall *crc_b_t)(unsigned crc, const void *buf,
                                       unsigned len);

static crc_a_t g_orig_a;
static crc_b_t g_orig_b;

/* --- Observation buffer --------------------------------------------------
 * This runs on a hot path (every render-state hash, potentially per frame), so
 * recording must be cheap and must never allocate or take a lock that the game
 * could already hold. A fixed table of distinct callers with a small sample
 * string each, updated with interlocked ops only, satisfies that.
 *
 * Sampling only DISTINCT return addresses is what keeps the cost bounded: a
 * caller in a per-frame loop is recorded once, not 60 times a second. */
#define MAX_SITES 64
#define SAMPLE_LEN 48

typedef struct {
    volatile uintptr_t ret;          /* return address = the caller */
    volatile LONG      count;
    volatile LONG      printable;    /* calls whose buffer looked like text */
    char               sample[SAMPLE_LEN];
    volatile LONG      have_sample;
} site;

static site  g_sites[MAX_SITES];
static volatile LONG g_nsites;
static volatile LONG g_total;
static volatile LONG g_installed;

/* Is this buffer plausibly TEXT of any kind?
 *
 * Deliberately broader than "matches a GMD label". GMD labels are uppercase
 * (MENU_TITLE_00), but a lookup might equally be handed a resource path, a
 * mixed-case name, or a UI node id -- and a filter tuned to exactly what I
 * expect would report "no text" for all of those, producing a confident
 * negative that is really just a too-narrow filter. Accept any mostly-
 * printable ASCII run and let the SAMPLE in the log show what it actually was.
 *
 * This still cleanly separates text from the float/bitfield structs the
 * graphics callers hash, which is the distinction that matters. */
static BOOL looks_like_text(const char *p, unsigned len)
{
    if (len < 3 || len > 128)
        return FALSE;
    unsigned alnum = 0;
    for (unsigned i = 0; i < len; i++) {
        unsigned char c = (unsigned char)p[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9')) { alnum++; continue; }
        if (c == '_' || c == '.' || c == '/' || c == '\\' || c == '-')
            continue;
        return FALSE;
    }
    /* Mostly alphanumeric, not just a run of separators. */
    return alnum * 2 >= len;
}

/* Record one observation. Lock-free: races can only lose a sample, never
 * corrupt the table, and losing a sample does not change a conclusion. */
static void record(uintptr_t ret, const void *buf, unsigned len)
{
    InterlockedIncrement(&g_total);

    int n = (int)InterlockedCompareExchange(&g_nsites, 0, 0);
    for (int i = 0; i < n && i < MAX_SITES; i++) {
        if (g_sites[i].ret == ret) {
            InterlockedIncrement(&g_sites[i].count);
            /* Upgrade the sample if this call carries a label-looking buffer
             * and we have not stored one yet. */
            if (buf && looks_like_text((const char *)buf, len)) {
                InterlockedIncrement(&g_sites[i].printable);
                if (InterlockedCompareExchange(&g_sites[i].have_sample, 1, 0)
                        == 0) {
                    unsigned c = len < SAMPLE_LEN - 1 ? len : SAMPLE_LEN - 1;
                    memcpy(g_sites[i].sample, buf, c);
                    g_sites[i].sample[c] = '\0';
                }
            }
            return;
        }
    }

    /* New caller. */
    LONG idx = InterlockedIncrement(&g_nsites) - 1;
    if (idx < 0 || idx >= MAX_SITES)
        return;                      /* table full: stop recording, do not wrap */
    g_sites[idx].ret = ret;
    g_sites[idx].count = 1;
    if (buf && looks_like_text((const char *)buf, len)) {
        g_sites[idx].printable = 1;
        unsigned c = len < SAMPLE_LEN - 1 ? len : SAMPLE_LEN - 1;
        memcpy(g_sites[idx].sample, buf, c);
        g_sites[idx].sample[c] = '\0';
        g_sites[idx].have_sample = 1;
    }
}

/* noinline + no tail-call: __builtin_return_address(0) is only meaningful in a
 * real frame. If the compiler inlined these or turned the trampoline call into
 * a jump, the "caller" we record would be wrong -- and a wrong address here
 * would look exactly like a correct one in the log. */
__attribute__((noinline, optimize("no-optimize-sibling-calls")))
static unsigned __fastcall hook_a(unsigned *seed, const void *buf,
                                  unsigned len)
{
    record((uintptr_t)_ReturnAddress(), buf, len);
    return g_orig_a(seed, buf, len);
}

__attribute__((noinline, optimize("no-optimize-sibling-calls")))
static unsigned __fastcall hook_b(unsigned crc, const void *buf, unsigned len)
{
    record((uintptr_t)_ReturnAddress(), buf, len);
    return g_orig_b(crc, buf, len);
}

BOOL hash_watch_install(void)
{
    if (MH_Initialize() != MH_OK) {
        log_line("hashwatch: MH_Initialize failed");
        return FALSE;
    }

    int ok = 0;
    if (MH_CreateHook((LPVOID)CFC2_CRC32_A, (LPVOID)hook_a,
                      (LPVOID *)&g_orig_a) == MH_OK &&
        MH_EnableHook((LPVOID)CFC2_CRC32_A) == MH_OK) {
        log_line("hashwatch: hooked crc32_A at 0x%llx",
                 (unsigned long long)CFC2_CRC32_A);
        ok++;
    } else {
        log_line("hashwatch: FAILED to hook crc32_A at 0x%llx",
                 (unsigned long long)CFC2_CRC32_A);
    }

    if (MH_CreateHook((LPVOID)CFC2_CRC32_B, (LPVOID)hook_b,
                      (LPVOID *)&g_orig_b) == MH_OK &&
        MH_EnableHook((LPVOID)CFC2_CRC32_B) == MH_OK) {
        log_line("hashwatch: hooked crc32_B at 0x%llx",
                 (unsigned long long)CFC2_CRC32_B);
        ok++;
    } else {
        log_line("hashwatch: FAILED to hook crc32_B at 0x%llx",
                 (unsigned long long)CFC2_CRC32_B);
    }

    InterlockedExchange(&g_installed, ok);

    /* POSITIVE CONTROL. Call the hooked primitive ourselves with a real GMD
     * label. If the detour and the text filter both work, this shows up in the
     * report as a caller inside our own DLL with sample="MENU_TITLE_00".
     *
     * This is what makes a later "0 callers hashed text" trustworthy. Without
     * it, a broken detour, a wrong signature, or a too-narrow filter would all
     * produce the same empty result as a genuine absence -- and I would have no
     * way to tell which I was looking at. */
    if (ok > 0 && g_orig_a) {
        unsigned seed = 0xFFFFFFFFu;
        static const char probe[] = "MENU_TITLE_00";
        unsigned h = ((crc_a_t)hook_a)(&seed, probe, (unsigned)sizeof(probe) - 1);
        log_line("hashwatch: positive control -- hashed \"%s\" through the "
                 "hook, got 0x%08x (expect 0xec548ce8) %s",
                 probe, h, h == 0xec548ce8u ? "OK" : "MISMATCH");
    }

    return ok > 0;
}

void hash_watch_report(void)
{
    if (!InterlockedCompareExchange(&g_installed, 0, 0)) {
        log_line("hashwatch: no hooks installed -- nothing to report");
        return;
    }

    int n = (int)InterlockedCompareExchange(&g_nsites, 0, 0);
    if (n > MAX_SITES) n = MAX_SITES;
    LONG total = InterlockedCompareExchange(&g_total, 0, 0);

    /* Report a POSITIVE count of what was examined. "0 label-like callers"
     * and "the hook never fired" are completely different findings and must
     * never look the same in the log. */
    log_line("hashwatch: %ld calls observed across %d distinct callers",
             total, n);
    if (total == 0) {
        log_line("hashwatch: the hooks are installed but NEVER FIRED -- "
                 "these primitives are not on the menu path at all.");
        return;
    }

    /* Our own positive-control call returns into this DLL, so exclude that
     * range when counting GAME callers -- otherwise the control would inflate
     * the very number it exists to validate. */
    HMODULE self = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void *)hash_watch_report, &self);
    MODULEINFO mi;
    uintptr_t self_lo = 0, self_hi = 0;
    if (self && GetModuleInformation(GetCurrentProcess(), self, &mi,
                                     sizeof(mi))) {
        self_lo = (uintptr_t)mi.lpBaseOfDll;
        self_hi = self_lo + mi.SizeOfImage;
    }

    int labelish = 0, control = 0;
    for (int i = 0; i < n; i++) {
        if (g_sites[i].printable > 0) {
            BOOL is_self = self_lo && g_sites[i].ret >= self_lo &&
                           g_sites[i].ret < self_hi;
            if (is_self) {
                control++;
                log_line("hashwatch:  [control] our own probe call, "
                         "sample=\"%s\" -- detour+filter WORK",
                         g_sites[i].sample);
                continue;
            }
            labelish++;
            log_line("hashwatch:  ⭐ GAME caller 0x%llx  calls=%ld  text=%ld  "
                     "sample=\"%s\"",
                     (unsigned long long)g_sites[i].ret,
                     g_sites[i].count, g_sites[i].printable,
                     g_sites[i].sample);
        }
    }
    for (int i = 0; i < n; i++) {
        if (g_sites[i].printable == 0)
            log_line("hashwatch:     caller 0x%llx  calls=%ld  (no text)",
                     (unsigned long long)g_sites[i].ret, g_sites[i].count);
    }
    /* State the control's verdict alongside the count, so the negative is
     * always read with the evidence that the detector was working. */
    log_line("hashwatch: %d/%d GAME callers hashed text "
             "(positive control: %s)",
             labelish, n - control,
             control > 0 ? "PASSED -- a text hash WOULD have been caught"
                         : "MISSING -- treat any zero here as INCONCLUSIVE");
}

void hash_watch_shutdown(void)
{
    if (InterlockedCompareExchange(&g_installed, 0, 0)) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        InterlockedExchange(&g_installed, 0);
    }
}
