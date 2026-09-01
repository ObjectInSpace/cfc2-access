#include "hash_watch.h"
#include "log.h"

#include "MinHook.h"

#include <stdio.h>
#include <string.h>

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

/* Is this buffer plausibly a GMD label? Labels are uppercase ASCII with
 * underscores and digits (MENU_TITLE_00, STATS_CVS2_39_NAME). Being strict
 * here is deliberate: it is what separates "hashing a string" from "hashing a
 * struct of floats", which is exactly the distinction static analysis could
 * not make. */
static BOOL looks_like_label(const char *p, unsigned len)
{
    if (len < 3 || len > 64)
        return FALSE;
    unsigned upper = 0;
    for (unsigned i = 0; i < len; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c >= 'A' && c <= 'Z') { upper++; continue; }
        if (c == '_' || (c >= '0' && c <= '9')) continue;
        return FALSE;       /* anything else disqualifies it */
    }
    return upper >= 2;
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
            if (buf && looks_like_label((const char *)buf, len)) {
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
    if (buf && looks_like_label((const char *)buf, len)) {
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

    int labelish = 0;
    for (int i = 0; i < n; i++) {
        if (g_sites[i].printable > 0) {
            labelish++;
            log_line("hashwatch:  ⭐ caller 0x%llx  calls=%ld  label-like=%ld  "
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
    log_line("hashwatch: %d/%d callers hashed label-like strings", labelish, n);
}

void hash_watch_shutdown(void)
{
    if (InterlockedCompareExchange(&g_installed, 0, 0)) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        InterlockedExchange(&g_installed, 0);
    }
}
