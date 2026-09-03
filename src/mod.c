/*
 * CFC2 Access — mod core.
 *
 * Stage 0 (does an injected DLL survive Enigma? can we speak? can we change
 * page protection?) is PASSED and its findings are recorded. This file now
 * serves PHASE 1.3: capture the unpacked image so the shell's GMD text-lookup
 * function can be located with ordinary offline tools.
 *
 * Why a dump rather than a live pattern scan: the exe on disk is entropy-8.00
 * ciphertext, so static analysis of it is worthless, while a live scan can only
 * ever show what we thought to log in advance. Dumping the decrypted image once
 * converts an intractable problem into a normal one -- and because ASLR is off
 * and the exe carries no relocations, every address found in that dump is valid
 * in every future run. The same dump serves the later emulator-RAM work.
 */

#include "mod.h"
#include "log.h"
#include "speech.h"
#include "image.h"
#include "gmd_find.h"
#include "pad_watch.h"

#include <stdio.h>

static HANDLE g_thread;
static volatile LONG g_quit;
static BOOL g_gmd_found;

/* --- Route 5: read the engine's own pad state -----------------------------
 * Routes 1-4 are closed with evidence (see CLAUDE.md). This one does not try
 * to infer which item is selected from memory that happens to change; it reads
 * MTFramework's own input record, identified by the algebraic relationship the
 * engine maintains between Old/Trg/Rel/Chg. That relationship is a positive
 * identification test rather than a correlation, which is what every earlier
 * route lacked.
 *
 * Up/Down drive menu navigation and Left/Right drive settings, and all four are
 * bits in the same word -- so one reader covers both.
 *
 * ⚠ The scan is KEY-DRIVEN and requires a direction to be HELD while it runs:
 * a candidate is only armed when something is actually pressed, since an
 * all-zero record satisfies the algebra trivially. */

/* The hotkeys: F5/F6/F7. Ordinary keys, deliberately.
 *
 * ⚠ THE EARLIER REASONING WAS OVERCAUTIOUS AND COST TWO RUNS. The game's
 * shipped GMD lists 70 bindable keys (`KB_*` in data/text_english.json),
 * F1-F12 among them, and I excluded every one of them on that basis. But that
 * table is what the game CAN bind, not what it DOES bind: the shell menus are
 * driven by arrows, Enter and Escape. Avoiding the whole table pushed the
 * choice onto exotic keys, and the first pick -- Scroll Lock, a TOGGLE key --
 * never registered in-game at all.
 *
 * The real lesson from [[hotkey-fallback-codes-eat-arrows]] is narrower than
 * "avoid anything bindable": it is that NumLock-off numpad VKs literally ARE
 * the arrow VKs, so they steal navigation. F5-F7 have no such aliasing, are not
 * toggles, and are not NVDA modifiers (NVDA uses Insert/CapsLock).
 *
 * F5 = arm the scan. Hold a direction while pressing it. */
#define PAD_ARM_HOTKEY       VK_F5
#define PAD_ARM_HOTKEY_NAME  "F5"   /* restart the search from scratch */

/* F6 = report what survived. Separate from arming so the player can keep
 * navigating and ask for the state whenever they like. */
#define PAD_REPORT_HOTKEY      VK_F6
#define PAD_REPORT_HOTKEY_NAME "F6"

/* F7 = speak the direction currently held, read out of the engine's own pad
 * record. This is the end-to-end check: press Down, hear "down". */
#define PAD_SAY_HOTKEY      VK_F7
#define PAD_SAY_HOTKEY_NAME "F7"

/* Was `vk` pressed since the last check? Edge-triggered, so holding the key
 * captures one pass rather than a flood. */
static BOOL key_pressed(int vk)
{
    /* GetAsyncKeyState's low bit reports "pressed since last call", but it is
     * shared process-wide; test the high bit against remembered state instead
     * so this cannot be disturbed by the game polling the same key. */
    static BOOL was_down[256];
    BOOL down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    BOOL edge = down && !was_down[vk & 0xFF];
    was_down[vk & 0xFF] = down;
    return edge;
}


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

/* Is <leaf> present next to the game exe? Used to gate experiments behind an
 * explicit switch, so a half-finished one never becomes the reason the game
 * will not start -- a hook here once did exactly that. */
static BOOL opt_in_file_present(const char *leaf)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return FALSE;
    char *slash = strrchr(path, '\\');
    if (!slash)
        return FALSE;
    slash[1] = '\0';
    strncat(path, leaf, MAX_PATH - strlen(path) - 1);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

/* Log the section map as it stands right now. Cheap, and it is the context
 * every later finding gets interpreted against. */
static void report_sections(const char *when)
{
    image_section secs[CFC2_MAX_SECTIONS];
    int n = image_scan(secs, CFC2_MAX_SECTIONS);
    log_line("image [%s]: base=%p, %d readable sections", when,
             (void *)image_base(), n);
    for (int i = 0; i < n; i++)
        log_line("image [%s]:   rva=0x%-8lx size=%-9lu %s distinct=%d/256 "
                 "prot=0x%-4lx %s",
                 when, secs[i].rva, secs[i].size,
                 secs[i].executable ? "code" : "data",
                 secs[i].distinct, secs[i].protect,
                 secs[i].executable
                     ? (secs[i].unpacked ? "UNPACKED" : "packed")
                     : "");
}

static DWORD WINAPI worker(LPVOID param)
{
    (void)param;

    log_init();
    log_line("=== CFC2 Access -- Phase 1.3: unpacked image dump ===");
    log_line("worker thread started, pid=%lu", GetCurrentProcessId());

    BOOL spoke = speech_init();

    /* Before: the image as Enigma hands it to us -- mostly ciphertext. This
     * pair of reports is the evidence that the wait below did something. */
    report_sections("t0");

    /* THE TIMING RULE. .text is ciphertext at DllMain and decrypts ~10 s in.
     * Everything after this point depends on the wait having succeeded, so a
     * timeout is a hard stop, not a warning we scroll past. */
    BOOL unpacked = image_wait_for_unpack(60000);
    report_sections("unpacked");

    BOOL can_protect = probe_can_protect();

    /* ⭐ THE OFF SWITCH SHIPS WITH THE EXPERIMENT, not after it bites.
     *
     * The pad scan is read-only (VirtualQuery + guarded reads, no hooks, no
     * calls into game code), so it is far lower risk than the detour that
     * broke this game before. But "lower risk" is not "no risk", and the cost
     * of the switch is one file check. Create `cfc2_pad.off` next to the exe
     * to disable it without rebuilding. */
    BOOL pad_enabled = !opt_in_file_present("cfc2_pad.off");

    int dumped = 0;
    if (unpacked) {
        dumped = image_dump();
    } else {
        log_line("dump: SKIPPED -- image never unpacked. Any dump taken now "
                 "would be ciphertext that silently looks like a valid file.");
    }

    if (spoke) {
        const wchar_t *backend = speech_backend();
        wchar_t msg[256];
        /* Speak the outcome: the user is blind and running this themselves,
         * and the whole point of the run is to learn whether the dump worked.
         * Making them alt-tab to a log for the headline would be silly. */
        if (unpacked && dumped > 0)
            _snwprintf(msg, 255,
                       L"Access mod ready. Backend %ls. "
                       L"Dumped %d sections to cfc2 dump folder.",
                       backend ? backend : L"none", dumped);
        else if (unpacked)
            _snwprintf(msg, 255,
                       L"Access mod loaded, backend %ls, but the dump wrote "
                       L"no sections. Check the log.",
                       backend ? backend : L"none");
        else
            _snwprintf(msg, 255,
                       L"Access mod loaded, backend %ls, but the image never "
                       L"unpacked. Dump skipped.",
                       backend ? backend : L"none");
        msg[255] = L'\0';
        speech_say(msg, TRUE);
    }

    log_line("phase 1.3 result: speech=%s protect=%s unpacked=%s "
             "sections_dumped=%d",
             spoke ? "YES" : "NO", can_protect ? "YES" : "NO",
             unpacked ? "YES" : "NO", dumped);

    /* Heartbeat: if Enigma tears the thread down later in startup, the log
     * stopping is itself the finding. */
    int beat = 0;
    if (unpacked && !pad_enabled)
        log_line("pad: DISABLED by cfc2_pad.off next to the exe. Delete that "
                 "file to re-enable.");
    if (unpacked && pad_enabled) {
        /* Starts itself and re-seeds until a press makes the record visible.
         * Nothing is asked of the player except to navigate. */
        log_line("pad: watching the sPad singleton at image+0x%x. "
                 "HOLD a direction for a second, then RELEASE it and wait a "
                 "second. It captures one snapshot of each state and reports "
                 "the bytes that differ. F6 repeats the report.",
                 PAD_SINGLETON_RVA);
    }

    while (!InterlockedCompareExchange(&g_quit, 0, 0)) {
        /* Poll the hotkey at 100 ms so a press is never missed, but only do
         * the (expensive) work on an edge. The 2 s beat below is unchanged. */
        for (int t = 0; t < 20; t++) {
            if (InterlockedCompareExchange(&g_quit, 0, 0))
                break;
            Sleep(100);

            /* Watch the verified static path continuously. The last open
             * question -- is that field the selection? -- is then answered by
             * simply navigating the menus, with no protocol for the user to
             * follow and get wrong. Two reads and a compare. */
            /* Sample the armed candidates every 100 ms. Cheap: a bounded
             * number of sites, each a 20-byte guarded read. This is what
             * accumulates the evidence, so it must run while the player is
             * simply navigating -- no protocol for them to follow. */
            if (unpacked && pad_enabled) {
                /* Follow the ONE established pointer and snapshot the object
                 * in both states. No searching: the address came from the
                 * reflection metadata offline and is launch-stable. */
                unsigned dirs = 0;
                if (GetAsyncKeyState(VK_UP)    & 0x8000) dirs |= PAD_UP;
                if (GetAsyncKeyState(VK_DOWN)  & 0x8000) dirs |= PAD_DOWN;
                if (GetAsyncKeyState(VK_LEFT)  & 0x8000) dirs |= PAD_LEFT;
                if (GetAsyncKeyState(VK_RIGHT) & 0x8000) dirs |= PAD_RIGHT;
                pad_watch_tick(dirs);
            }
            if (unpacked && pad_enabled && key_pressed(PAD_REPORT_HOTKEY)) {
                log_line("pad: report triggered by %s",
                         PAD_REPORT_HOTKEY_NAME);
                pad_watch_report();
            }
        }
        beat++;
        if (beat <= 3 || beat % 30 == 0)
            log_line("heartbeat %d (alive %d s)", beat, beat * 2);

        /* Look for the live GMD table. Retried rather than done once: the
         * message file is loaded with the shell's first menu, so an early
         * scan can legitimately find nothing. Read-only -- VirtualQuery then
         * read, no writes and no hooks -- so retrying is free of risk to the
         * game. Stops as soon as it succeeds. */
        if (unpacked && !g_gmd_found &&
            (beat == 3 || beat == 8 || beat == 20 || beat % 25 == 0)) {
            gmd_result r;
            if (gmd_find(&r)) {
                /* Only stops retrying on VERIFIED success -- a structural
                 * match alone is not enough to build on. */
                g_gmd_found = TRUE;
                log_line("gmdfind: array_base=%p form=%s label_verified=%s "
                         "text_verified=%s",
                         (void *)r.array_base,
                         r.form == GMD_FORM_FILE_IMAGE ? "FILE_IMAGE"
                       : r.form == GMD_FORM_LIVE_TABLE ? "LIVE_TABLE"
                                                       : "UNKNOWN",
                         r.label_verified ? "YES" : "no",
                         r.text_verified ? "YES" : "no");
                if (r.text_verified)
                    speech_say(L"Message table located and verified.", FALSE);
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
