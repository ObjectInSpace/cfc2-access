/*
 * CFC2 Access — mod core.
 *
 * STAGE 0 (the gate): prove that an injected DLL survives Enigma Protector in
 * this process, and that speech works from inside it. Everything else in the
 * project is blocked on this answer, so this file is deliberately instrumented
 * to produce evidence rather than just to work.
 *
 * What we are watching for:
 *  - Does DllMain even run? (log line 1)
 *  - Does the worker thread survive past Enigma's unpacking? (heartbeats)
 *  - Is the image unpacked by the time we look? (entropy probe at the OEP
 *    region — packed sections read as garbage, unpacked ones as real code)
 *  - Can we change page protection? (the specific thing Enigma blocks by
 *    hooking NtProtectVirtualMemory)
 */

#include "mod.h"
#include "log.h"
#include "speech.h"

#include <stdio.h>

static HANDLE g_thread;
static volatile LONG g_quit;

/* --- Probe: can we change .text page protection? -------------------------
 * This is the capability Enigma specifically takes away, and the one every
 * later stage depends on (MinHook needs it to write trampolines). Test it
 * directly rather than inferring it from whether a hook "seemed" to work.
 * We flip a page to RWX and immediately restore it — no bytes are written. */
static BOOL probe_can_protect(void)
{
    HMODULE base = GetModuleHandleW(NULL);
    if (!base) {
        log_line("probe: GetModuleHandleW(NULL) failed");
        return FALSE;
    }

    /* Walk the PE headers to find the first executable section. */
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt =
        (IMAGE_NT_HEADERS *)((BYTE *)base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);

    void *target = NULL;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            target = (BYTE *)base + sec[i].VirtualAddress;
            break;
        }
    }
    if (!target) {
        log_line("probe: no executable section found");
        return FALSE;
    }

    DWORD old = 0;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &old)) {
        log_line("probe: VirtualProtect DENIED (err %lu) -- Enigma is "
                 "blocking page protection changes; the bypass is needed",
                 GetLastError());
        return FALSE;
    }
    DWORD tmp = 0;
    VirtualProtect(target, 16, old, &tmp);
    log_line("probe: VirtualProtect OK (target %p, old prot 0x%lx) -- "
             "detours should be installable",
             target, old);
    return TRUE;
}

/* --- Probe: is the image unpacked yet? ------------------------------------
 * Enigma decrypts sections in place. Before unpacking, .text reads as high-
 * entropy garbage; after, it reads as x64 code. We sample a small window and
 * count distinct byte values as a cheap entropy proxy: real code repeats
 * bytes heavily (0x48 REX prefixes, 0xCC padding), ciphertext does not. */
static void probe_image_state(void)
{
    HMODULE base = GetModuleHandleW(NULL);
    if (!base)
        return;

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);

    log_line("image: base=%p sections=%u entry_rva=0x%lx", (void *)base,
             nt->FileHeader.NumberOfSections,
             nt->OptionalHeader.AddressOfEntryPoint);

    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections && i < 12; i++) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;

        BYTE *p = (BYTE *)base + sec[i].VirtualAddress;
        /* Only read if the page is actually committed and readable. */
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
            continue;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            continue;

        int seen[256] = {0};
        int distinct = 0;
        const int window = 4096;
        for (int j = 0; j < window; j++) {
            BYTE b = p[j];
            if (!seen[b]) {
                seen[b] = 1;
                distinct++;
            }
        }
        /* ~256 distinct in 4 KB means ciphertext; real code is far lower. */
        log_line("image: exec section %u rva=0x%lx distinct_bytes=%d/256 (%s)",
                 i, sec[i].VirtualAddress, distinct,
                 distinct > 240 ? "still PACKED" : "looks UNPACKED");
    }
}

static DWORD WINAPI worker(LPVOID param)
{
    (void)param;

    log_init();
    log_line("=== CFC2 Access -- Stage 0 gate ===");
    log_line("worker thread started, pid=%lu", GetCurrentProcessId());

    /* Speech first: if the whole point is to prove we can talk, prove it. */
    BOOL spoke = speech_init();

    probe_image_state();
    BOOL can_protect = probe_can_protect();

    if (spoke) {
        const wchar_t *backend = speech_backend();
        wchar_t msg[256];
        _snwprintf(msg, 255,
                   L"Capcom Fighting Collection 2 access mod loaded. "
                   L"Speech backend %ls. Memory protection %ls.",
                   backend ? backend : L"none detected",
                   can_protect ? L"available" : L"blocked");
        msg[255] = L'\0';
        speech_say(msg, TRUE);
    }

    log_line("stage 0 result: injected=YES speech=%s protect=%s",
             spoke ? "YES" : "NO", can_protect ? "YES" : "NO");

    /* Heartbeat. If Enigma tears the thread down or unloads us later in
     * startup, the log stops here and that itself is the finding. Re-probe
     * the image periodically: the first probe may run before unpacking. */
    int beat = 0;
    while (!InterlockedCompareExchange(&g_quit, 0, 0)) {
        Sleep(2000);
        beat++;
        if (beat <= 5 || beat % 15 == 0) {
            log_line("heartbeat %d (alive %d s)", beat, beat * 2);
            if (beat == 5) {
                log_line("--- re-probing image after startup settles ---");
                probe_image_state();
                probe_can_protect();
            }
        }
    }

    log_line("worker exiting");
    return 0;
}

void mod_start(HINSTANCE self)
{
    (void)self;
    /* DllMain runs under the loader lock: spawn and return immediately.
     * No LoadLibrary, no file I/O, nothing that can take another lock. */
    g_thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
}

void mod_stop(void)
{
    InterlockedExchange(&g_quit, 1);
    /* Do not wait on the thread here: DLL_PROCESS_DETACH also holds the
     * loader lock, and the worker may be inside a call that needs it. */
    if (g_thread) {
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    speech_shutdown();
}
