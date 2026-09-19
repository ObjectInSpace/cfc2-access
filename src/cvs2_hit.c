/* See cvs2_hit.h for the offline derivation this file rests on. */

#include "cvs2_hit.h"
#include "log.h"
#include "memsafe.h"

#include "../libs/minhook/include/MinHook.h"

#include <stdio.h>
#include <string.h>

#define HIT_FN 0x1406C0060ull
/* The byte pattern at the entry, so we refuse to patch a different build.
 * mov [rsp+0x10],rbx ; mov [rsp+0x18],rbp ; push rsi ; push rdi ; push r14 */
static const unsigned char HIT_SIG[] = {
    0x48,0x89,0x5C,0x24,0x10, 0x48,0x89,0x6C,0x24,0x18, 0x56,0x57,0x41,0x56
};

typedef void (*pfn_hit)(void *a, void *b);
static pfn_hit g_real_hit;

/* A hit record. Captured in the detour, which must do NO work: it runs on the
 * game's own thread, inside frame processing. Copy words, return. */
typedef struct {
    ULONGLONG self;      /* rdx: the player struct B                      */
    ULONGLONG other;     /* rcx: the attacker-side argument               */
    ULONGLONG p3f0;      /* [B+0x3F0] -- the mid-hit pointer we are after */
    unsigned  player;    /* [B+0x5EC]                                     */
    /* A window of whatever p3f0 points at, copied later by the worker.   */
} hit_rec;

#define HIT_RING 16
static hit_rec        g_ring[HIT_RING];
static volatile LONG  g_write;     /* total records produced             */
static volatile LONG  g_read;      /* total consumed by the worker       */
static volatile LONG  g_calls;     /* detour entries                     */
static volatile LONG  g_nullptr;   /* entries whose [B+0x3F0] was NULL   */

/* ⚠ The detour reads [B+0x3F0] directly. That is a dereference of a pointer
 * from a live game thread, which this project has killed the process doing
 * before ([[hooked-function-pointers-are-untrusted]]). It is guarded: B came
 * to us as an argument the game itself is about to use, and we read ONE
 * aligned qword at a fixed offset inside a structure the game is mid-call on.
 * We do not follow it here -- the worker does that later, with the full
 * mem_readable_run() + fault-guard treatment. */
static void hit_detour(void *a, void *b)
{
    InterlockedIncrement(&g_calls);

    if (b) {
        const unsigned char *B = (const unsigned char *)b;
        ULONGLONG p = 0;
        unsigned idx = 0xFF;
        /* One guarded read of the two fields we want. */
        if (mem_readable(B + 0x3F0, 8) && mem_readable(B + 0x5EC, 1)) {
            memcpy(&p, B + 0x3F0, 8);
            idx = B[0x5EC];
        }
        if (!p) {
            InterlockedIncrement(&g_nullptr);
        } else {
            LONG slot = InterlockedIncrement(&g_write) - 1;
            hit_rec *r = &g_ring[slot % HIT_RING];
            r->self   = (ULONGLONG)(ULONG_PTR)b;
            r->other  = (ULONGLONG)(ULONG_PTR)a;
            r->p3f0   = p;
            r->player = idx;
        }
    }

    g_real_hit(a, b);   /* pass through untouched */
}

/* ⚠ Resolve switches against the EXE's directory, never the process working
 * directory. Steam does not guarantee the CWD is the game folder, and a switch
 * that silently reads as "absent" would make this probe look like it ran and
 * found nothing -- the exact failure mode the liveness counters exist to
 * prevent. This mirrors mod.c's opt_in_file_present(). */
static BOOL beside_exe(const char *leaf)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return FALSE;
    char *slash = strrchr(path, 0x5C);
    if (!slash) return FALSE;
    slash[1] = 0;
    strncat(path, leaf, MAX_PATH - strlen(path) - 1);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}
static BOOL switched_off(void) { return beside_exe("cfc2_hook.off"); }
static BOOL switched_on(void)  { return beside_exe("cfc2_hit.on"); }

BOOL cvs2_hit_install(void)
{
    if (!switched_on()) { log_line("hit: off (no cfc2_hit.on)"); return FALSE; }
    if (switched_off())  { log_line("hit: disabled by cfc2_hook.off"); return FALSE; }

    const void *fn = (const void *)(ULONG_PTR)HIT_FN;
    if (!mem_readable(fn, sizeof HIT_SIG)) {
        log_line("hit: ⚠ COULD NOT read 0x%llx -- concluding nothing", HIT_FN);
        return FALSE;
    }
    if (memcmp(fn, HIT_SIG, sizeof HIT_SIG) != 0) {
        log_line("hit: ⛔ entry bytes differ from the audited build; refusing to patch");
        return FALSE;
    }
    if (MH_CreateHook((LPVOID)(ULONG_PTR)HIT_FN, (LPVOID)hit_detour,
                      (LPVOID *)&g_real_hit) != MH_OK) {
        log_line("hit: ⛔ MH_CreateHook failed"); return FALSE;
    }
    if (MH_EnableHook((LPVOID)(ULONG_PTR)HIT_FN) != MH_OK) {
        log_line("hit: ⛔ MH_EnableHook failed"); return FALSE;
    }
    log_line("hit: installed on 0x%llx", HIT_FN);
    return TRUE;
}

void cvs2_hit_remove(void)
{
    MH_DisableHook((LPVOID)(ULONG_PTR)HIT_FN);
}

/* ⭐ Liveness before findings: "0 records" means nothing unless we know the
 * detour ran at all. g_calls says that; g_nullptr says the pointer really is
 * NULL outside hits, which is the claim the whole snapshot negative rests on. */
void cvs2_hit_poll_and_log(void)
{
    static LONG last_calls = -1;
    LONG calls = g_calls, w = g_write, nul = g_nullptr;

    if (calls != last_calls) {
        log_line("hit: calls=%ld records=%ld nullptr=%ld", calls, w, nul);
        last_calls = calls;
    }

    while (g_read < w) {
        LONG slot = g_read;
        if (w - slot > HIT_RING) {      /* lapped: say so, never pretend */
            log_line("hit: ⚠ ring lapped, skipped %ld record(s)", w - slot - HIT_RING);
            slot = w - HIT_RING;
        }
        hit_rec r = g_ring[slot % HIT_RING];
        g_read = slot + 1;

        log_line("hit: P%u B=0x%llx arg=0x%llx [B+0x3F0]=0x%llx",
                 r.player + 1, r.self, r.other, r.p3f0);

        /* Dump a window at the target, the thing no snapshot could reach. */
        const unsigned char *t = (const unsigned char *)(ULONG_PTR)r.p3f0;
        SIZE_T run = mem_readable_run(t, 0x80);
        if (!run) { log_line("hit:   target unreadable now"); continue; }
        char line[3 * 32 + 8];
        for (SIZE_T base = 0; base < run && base < 0x80; base += 16) {
            int n = 0;
            for (SIZE_T i = base; i < base + 16 && i < run; i++)
                n += snprintf(line + n, sizeof line - n, "%02x ", t[i]);
            log_line("hit:   +%02llx  %s", (ULONGLONG)base, line);
        }
    }
}
