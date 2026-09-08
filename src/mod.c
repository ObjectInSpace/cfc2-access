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
#include "iat_probe.h"
#include "caller_trace.h"

#include <stdio.h>

static HANDLE g_thread;
static volatile LONG g_quit;
static BOOL g_gmd_found;

/* --- Route 5: read the engine's own pad record ---------------------------
 * Routes 1-4 are closed with evidence (see CLAUDE.md), and route 5's first
 * four attempts failed too. What finally worked was reading the answer out of
 * the DISASSEMBLY rather than searching memory for something pad-shaped:
 * the sPad instance pointer, the field offsets and the array stride all come
 * from instructions, not from pattern matching.
 *
 * The mod's job is now only to capture bytes; all comparison happens offline.
 * See src/pad_watch.h for why that division matters.
 *
 * ⚠ Hotkey choice: F7 is ordinary, not a toggle, not an NVDA modifier. The
 * real hazard from [[hotkey-fallback-codes-eat-arrows]] is narrow -- NumLock
 * -off numpad VKs literally ARE the arrow VKs -- and F7 has no such aliasing.
 * An earlier pass excluded all 70 keys in the game's KB_* table and landed on
 * Scroll Lock, a toggle key that never registered in-game, costing two runs. */

/* ONE hotkey: F7 takes a capture. There is nothing else to drive, because
 * the mod makes no decisions -- see the header of pad_watch.h. */
#define PAD_DUMP_HOTKEY      VK_F7
#define PAD_DUMP_HOTKEY_NAME "F7"

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

    /* THE TIMING RULE. .text is ciphertext at DllMain and decrypts shortly
     * after -- measured 531 ms, NOT the ~10 s claimed until 2026-09-08.
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

    /* ⚠ The caller trace is the FIRST thing in this project that writes to
     * game memory (a MinHook detour on user32!GetAsyncKeyState). Its off
     * switch therefore ships WITH it, not after it bites -- a detour in this
     * game once made it refuse to start, and the recovery was a rebuild. With
     * `cfc2_hook.off` next to the exe the mod is read-only again, exactly as
     * every shipped version until now. */
    BOOL hook_enabled = !opt_in_file_present("cfc2_hook.off");

    /* --- The IAT probe -------------------------------------------------
     * The one open question is how the shell reads the keyboard: sPad is inert
     * (Kind = 0 on all five records), and four OFFLINE attempts to answer it
     * failed -- the last because the IAT holds resolved addresses that cannot
     * be matched to names from a dump taken when user32 sat at another base.
     * From inside the process it is a table walk. Run it once, here, where the
     * module set has settled. Read-only; see iat_probe.h. */
    iat_probe_result iat;
    BOOL iat_ok = FALSE;
    memset(&iat, 0, sizeof(iat));
    if (unpacked)
        iat_ok = iat_probe_run(&iat);
    else
        log_line("iat: SKIPPED -- image never unpacked, so nothing here would "
                 "be trustworthy.");

    /* --- The caller trace ----------------------------------------------
     * The IAT probe answered WHICH api; this answers WHO calls it. Install
     * only after unpack: the shell's code does not exist as code until then,
     * and hooking user32 before the game has started polling would just
     * accumulate our own calls.
     *
     * ⚠ Installed even though we are also a caller -- see caller_trace.h. Our
     * calls are the hook's liveness proof, not noise to be avoided. */
    BOOL hook_ok = FALSE;
    if (unpacked && hook_enabled) {
        hook_ok = ct_install();
        if (!hook_ok)
            log_line("ct: install FAILED -- nothing was written to game "
                     "memory, and no caller conclusion is available.");
    } else if (unpacked) {
        log_line("ct: DISABLED by cfc2_hook.off next to the exe. Delete that "
                 "file to re-enable the caller trace.");
    }

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
        if (unpacked && iat_ok) {
            /* ⭐ The IAT answer is what this run exists to learn, so it is the
             * headline -- not the dump count. Naming the families found means
             * the user can report the result without opening the log at all.
             * Say "none of them" explicitly: a silence would be ambiguous
             * between "not polled" and "the probe did not run". */
            _snwprintf(msg, 255,
                       L"Access mod ready. Backend %ls. Input probe examined "
                       L"%d slots and found: %ls%ls%ls%ls",
                       backend ? backend : L"none",
                       iat.slots_examined,
                       iat.polled_keyboard ? L"polled keyboard. " : L"",
                       iat.raw_input       ? L"raw input. "       : L"",
                       iat.window_messages ? L"window messages. " : L"",
                       (!iat.polled_keyboard && !iat.raw_input &&
                        !iat.window_messages) ? L"none of them. " : L"");
        } else if (unpacked) {
            /* ⚠ The probe returned FALSE, which means INCONCLUSIVE, not
             * "found nothing" -- it could not read the import block, so no
             * family flag means anything. Say so, rather than falling through
             * to a cheerful dump count that hides a failed measurement. */
            _snwprintf(msg, 255,
                       L"Access mod loaded. Backend %ls. Dumped %d sections. "
                       L"The input probe was INCONCLUSIVE -- it could not read "
                       L"the import table, so it found nothing AND proved "
                       L"nothing. Check the log.",
                       backend ? backend : L"none", dumped);
        }
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
        log_line("pad: dumping the sPad INSTANCE at image+0x%x on F7. "
                 "The mod does NO analysis -- it writes cfc2_pad_NN.bin plus a "
                 "line in cfc2_pad_index.txt recording which arrow keys were "
                 "down. Comparison happens offline.",
                 PAD_INSTANCE_RVA);
        /* ⭐ The protocol is stated in the log because it is what makes the
         * result interpretable: captures 1 and 2 are the SAME state, so their
         * difference is the noise floor. Without that control a diff measures
         * elapsed time, which is exactly how the previous attempt failed. */
        log_line("pad: press F7 five times -- (1) nothing held, (2) nothing "
                 "held again, (3) holding DOWN, (4) nothing held, "
                 "(5) holding LEFT. Captures 1 and 2 are the control.");
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
            /* ⭐⭐ CAPTURE ON THE TRANSITION, not while a key is held.
             *
             * ⚠ This corrects a flaw that probably produced FALSE NEGATIVES in
             * every earlier attempt, and the user spotted it by asking why
             * holding was necessary at all.
             *
             * The old scanners gated on `player_dirs != 0` in TWO places -- the
             * all-zero rejection at seed time and an early return in the tick
             * -- so the pad record had to be nonzero at the exact instant a
             * given address was sampled. The seed sweep takes ~600 ms to cross
             * 1.4 GB and reaches any one address at a single moment in that
             * window, while a keypress lasts ~150 ms. The odds of coinciding
             * were poor even with continuous pressing, and the user was told to
             * "just navigate". A null result from those runs therefore says
             * almost nothing.
             *
             * Two further reasons holding was the wrong instruction:
             *   - holding a direction in a menu triggers AUTO-REPEAT, so the
             *     cursor scrolls and the menu state churns between captures;
             *   - `Trg` is set for a single frame, so a capture taken while a
             *     key is held reads Trg = 0 and measures only On/Old.
             *
             * So the mod now watches the keys itself and captures on the EDGE:
             * the first poll where a direction goes down (Trg still live), and
             * the first poll where it goes up. The user just navigates
             * normally -- no holding, no two-key press, nothing to remember.
             * A measurement that needs the user to remember something is the
             * wrong measurement. */
            if (unpacked && pad_enabled) {
                unsigned dirs = 0;
                if (GetAsyncKeyState(VK_UP)    & 0x8000) dirs |= PAD_UP;
                if (GetAsyncKeyState(VK_DOWN)  & 0x8000) dirs |= PAD_DOWN;
                if (GetAsyncKeyState(VK_LEFT)  & 0x8000) dirs |= PAD_LEFT;
                if (GetAsyncKeyState(VK_RIGHT) & 0x8000) dirs |= PAD_RIGHT;
                pad_auto_capture(dirs);
            }

            /* F7 still forces a capture, for the two control captures and as
             * a manual override if the automatic edges misbehave. */
            if (unpacked && pad_enabled && key_pressed(PAD_DUMP_HOTKEY)) {
                unsigned dirs = 0;
                if (GetAsyncKeyState(VK_UP)    & 0x8000) dirs |= PAD_UP;
                if (GetAsyncKeyState(VK_DOWN)  & 0x8000) dirs |= PAD_DOWN;
                if (GetAsyncKeyState(VK_LEFT)  & 0x8000) dirs |= PAD_LEFT;
                if (GetAsyncKeyState(VK_RIGHT) & 0x8000) dirs |= PAD_RIGHT;
                pad_dump_now(dirs);
            }
        }
        beat++;
        if (beat <= 3 || beat % 30 == 0)
            log_line("heartbeat %d (alive %d s)", beat, beat * 2);

        /* Report the caller set periodically. Early beats catch the title
         * screen, later ones catch the menus -- a routine that only polls in
         * one state would otherwise be missed, and the log is truncated per
         * run so successive reports show the set GROWING rather than
         * overwriting each other. */
        if (hook_ok && (beat == 3 || beat == 10 || beat == 30 ||
                        beat % 60 == 0))
            ct_report_log();

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
