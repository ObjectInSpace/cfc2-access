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
#include "probe_hash.h"
#include "hash_watch.h"
#include "gmd_find.h"
#include "gui_find.h"
#include "gui_tree.h"
#include "heapdiff.h"

#include <stdio.h>

static HANDLE g_thread;
static volatile LONG g_quit;
static BOOL g_gmd_found;
/* The GUI inventory runs once: its answer is a constant (24/25 layouts are
 * always resident), so repeating it only costs scan time. */
static BOOL g_gui_done;

/* --- Route 3: the instantiated GUI tree ----------------------------------
 * Two passes taken on DIFFERENT screens, then diffed. A word identical across
 * both is file-derived or structural; a word that CHANGES is the live state
 * the two closed routes never found.
 *
 * The passes are triggered by a KEY, not a timer. The whole experiment depends
 * on the two captures happening on genuinely different screens, and only the
 * player knows when that is true -- a timer would fire mid-transition and the
 * diff would be uninterpretable. */
static gtree_result g_pass_a, g_pass_b;
static int          g_pass_count;

/* The capture hotkey: F5. An ordinary key, deliberately.
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
 * the arrow VKs, so they steal navigation. F5 has no such aliasing, is not a
 * toggle, is not an NVDA modifier (NVDA uses Insert/CapsLock), and is not used
 * by these menus. If it ever does collide, the fix is one line here. */
#define GTREE_HOTKEY      VK_F5
#define GTREE_HOTKEY_NAME "F5"

/* F6 drives the differential heap scan (route 4). Same reasoning as F5: an
 * ordinary function key, not a toggle, no VK aliasing, not an NVDA modifier. */
#define HDIFF_HOTKEY      VK_F6
#define HDIFF_HOTKEY_NAME "F6"

/* F7 = sample the latched index candidates. Press it after each cursor move;
 * a real selection index walks by one and reverses when you go back. This is
 * the only confirmation available, since heap addresses differ between runs
 * and a candidate cannot be re-found in a later session. */
#define HTRACK_HOTKEY      VK_F7
#define HTRACK_HOTKEY_NAME "F7"

/* F8 = find a STABLE PATH to the best candidate. The hunt's address changes
 * every launch, so a chain rooted in the exe image (ASLR off, no relocations)
 * is what makes the field re-findable -- and therefore shippable. */
#define HPATH_HOTKEY      VK_F8
#define HPATH_HOTKEY_NAME "F8"

/* F9 = follow the RECORDED static paths and print what they land on. This is
 * the cross-launch test: the offsets are baked into the mod, so if they still
 * resolve to a cursor-tracking field after a restart, the field is findable on
 * any launch and the mod is buildable. */
#define HVERIFY_HOTKEY      VK_F9
#define HVERIFY_HOTKEY_NAME "F9"

/* F10 = SPEAK the watched field's value. The decisive test is whether the
 * number equals the position of the item the player is sitting on, and only
 * the player can see that -- so speak it and let them compare. One press
 * settles what no amount of log analysis can. */
#define HSAY_HOTKEY      VK_F10
#define HSAY_HOTKEY_NAME "F10"

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


/* Capture one pass, and diff as soon as there are two. */
static void gui_tree_capture(void)
{
    /* ⚠ ANNOUNCE ON ENTRY, BEFORE ANY SLOW WORK.
     *
     * The first press pays a cold gui_find -- measured at ~15 s in-game -- and
     * gui_tree_scan does not log until after it. That left a 15 s window in
     * which the log said "triggered by F5" and nothing else, making "the user
     * quit early" and "it stalled again" indistinguishable. Exactly the
     * ambiguity that has already cost two diagnostic rounds here. Speak and
     * log first, so the silence itself becomes informative. */
    log_line("gtree: capture starting (pass %d). The first pass runs a cold "
             "layout search, which took ~15 s in-game; later passes reuse the "
             "cached pool. If the log stops here, the scan did not finish.",
             g_pass_count + 1);
    speech_say(g_pass_count == 0 ? L"Capturing pass A, please wait."
                                 : L"Capturing, please wait.", FALSE);

    if (g_pass_count == 0) {
        if (gui_tree_scan(&g_pass_a, "A")) {
            gui_tree_report(&g_pass_a, "A");
            g_pass_count = 1;
            speech_say(L"Pass A captured. Move to a different screen, "
                       L"then press F5 again.", FALSE);
        } else {
            speech_say(L"Pass A found nothing. Check the log.", FALSE);
        }
        return;
    }

    if (!gui_tree_scan(&g_pass_b, "B")) {
        speech_say(L"Pass B found nothing. Check the log.", FALSE);
        return;
    }
    gui_tree_report(&g_pass_b, "B");
    gui_tree_diff(&g_pass_a, &g_pass_b);

    /* B becomes the new A, so each further press diffs against the previous
     * screen instead of forever against the first one. */
    g_pass_a = g_pass_b;
    g_pass_count++;
    speech_say(L"Pass captured and compared. See the log.", FALSE);
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

/* Is <leaf> present next to the game exe? Used to gate experiments that can
 * destabilize the game behind an explicit opt-in. */
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

    int hash_matches = 0;
    int dumped = 0;
    if (unpacked) {
        dumped = image_dump();
        /* Only meaningful once .text is decrypted -- calling into ciphertext
         * would fault every time and prove nothing. */
        hash_matches = probe_hash_candidates();
        /* Static tracing cannot see through MTFramework's hasher vtable, so
         * observe the callers at runtime instead of inferring them.
         *
         * OFF BY DEFAULT. The first version of this crashed the game within a
         * second of installing: the hooks read up to 128 bytes from whatever
         * pointer the caller passed, and crc32_B is called during archive
         * decompression with buffers that are not NUL-terminated strings.
         * Reading past the end faults on a hot path with nothing to catch it.
         * An experiment must never be the reason the game will not start, so
         * it now takes an explicit opt-in: create a file named
         * `cfc2_hashwatch.on` next to the exe. */
        if (opt_in_file_present("cfc2_hashwatch.on")) {
            log_line("hashwatch: opt-in file present -- installing hooks");
            hash_watch_install();
        } else {
            log_line("hashwatch: disabled (create cfc2_hashwatch.on next to "
                     "the exe to enable)");
        }
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
             "sections_dumped=%d hash_matches=%d",
             spoke ? "YES" : "NO", can_protect ? "YES" : "NO",
             unpacked ? "YES" : "NO", dumped, hash_matches);

    /* Heartbeat: if Enigma tears the thread down later in startup, the log
     * stopping is itself the finding. */
    int beat = 0;
    if (unpacked) {
        log_line("gtree: press F5 to capture a GUI-tree pass. Capture one "
                 "screen, navigate to a DIFFERENT screen, then capture again; "
                 "the second press logs the diff. ⚠ ROUTE CLOSED: the "
                 "pool-anchored subtree proved entirely static (1789 nodes "
                 "plus one hop out, 0 changes across complete passes). Kept "
                 "for reference only.");
        log_line("heapdiff: F6 = the hunt, FIVE presses, wait for speech each "
                 "time: (1) baseline, (2) touch NOTHING, (3) cursor DOWN one "
                 "item, (4) cursor BACK UP one item. It then names the fields "
                 "that reacted to the cursor and returned -- state -- and "
                 "rejects the ones that kept climbing -- counters.");
        log_line("heapdiff: F7 = confirm. After the hunt, press F7, move the "
                 "cursor one item, press F7 again, and repeat. A real "
                 "selection index walks by one each time and reverses when "
                 "you go back.");
        log_line("heapdiff: F8 = find a static path to the best candidate. "
                 "F9 = follow the RECORDED static paths (the cross-launch "
                 "test).");
        /* ⚠ Do NOT verify here. This runs before the shell's menu exists, so
         * the pointers would be null and the log would record a misleading
         * "0 of 4 resolved". The check happens at a later beat instead, once
         * a menu is actually up. */
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
            if (unpacked) {
                hd_poll_path();
                /* ⚠ The span scan watches the HUNT's pages, so it can only be
                 * armed after a hunt has produced some. Arming at a fixed beat
                 * (as an earlier version did) armed it with nothing to watch.
                 * hd_hunt() arms it at its verdict instead. */
                hd_span_tick();
            }

            if (unpacked && key_pressed(GTREE_HOTKEY)) {
                log_line("gtree: triggered by %s", GTREE_HOTKEY_NAME);
                gui_tree_capture();
            }
            /* ⭐ TWO KEYS, NOT FIVE.
             *
             * The old workflow was F6 x3 per trial x6+ trials, then F7 x4,
             * then F9 x4 -- around 30 presses with a different cursor
             * instruction at each stage. That is a tool shaped by its own
             * implementation history rather than by the person using it.
             *
             * F6 now runs the whole hunt in four presses; F7 confirms a
             * candidate over repeated moves. The old staged entry points still
             * exist in heapdiff.c and are exercised by the test suite, but
             * they are no longer bound to keys. */
            if (unpacked && key_pressed(HDIFF_HOTKEY)) {
                log_line("hunt: triggered by %s", HDIFF_HOTKEY_NAME);
                hd_hunt();
            }
            if (unpacked && key_pressed(HTRACK_HOTKEY)) {
                log_line("track: triggered by %s", HTRACK_HOTKEY_NAME);
                hd_track_sample();
            }
            if (unpacked && key_pressed(HPATH_HOTKEY)) {
                log_line("path: triggered by %s", HPATH_HOTKEY_NAME);
                hd_find_path();
            }
            if (unpacked && key_pressed(HVERIFY_HOTKEY)) {
                log_line("verify: triggered by %s", HVERIFY_HOTKEY_NAME);
                hd_verify_path();
            }
            if (unpacked && key_pressed(HSAY_HOTKEY)) {
                hd_say_value();
            }
        }
        beat++;
        if (beat <= 3 || beat % 30 == 0)
            log_line("heartbeat %d (alive %d s)", beat, beat * 2);
        /* Report what the hooks have seen, at a few points spread across
         * startup and early menu use. The user has to actually navigate a
         * menu for the interesting callers to appear, so sample repeatedly
         * rather than once. */
        if (unpacked && (beat == 5 || beat == 15 || beat == 30 ||
                         beat % 60 == 0))
            hash_watch_report();

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

        /* Look for the loaded GUI layout -- WHICH screen is up.
         *
         * Unlike the GMD table this does NOT stop on success: the shell loads
         * and frees layouts as the player moves between screens, so the answer
         * changes and re-scanning is the point. Only a CHANGE is logged, so
         * navigating produces a readable trail instead of one line per beat.
         *
         * Read-only (VirtualQuery then guarded reads, no hooks), which is
         * exactly why this route was chosen over hooking. */
        /* ⛔ Run ONCE, not per beat.
         *
         * Measured in-game: 24 of the 25 layouts are resident simultaneously,
         * all at 6/6 corroboration -- the whole of menu.arc loads up front. So
         * this scan's answer is a CONSTANT and cannot indicate the displayed
         * screen. Re-running it every few seconds would burn 3.6 s of scanning
         * to re-print the same list, and the "top candidate" ranking measured
         * a property of the FILE (each layout's static state-name count), not
         * live state.
         *
         * Kept as a one-shot inventory: it proves which layouts are in memory
         * and locates their pools as an entry point into the instantiated
         * tree. Screen detection needs live state that CHANGES -- per-node
         * animation-state fields, or the tree's current-screen pointer. */
        /* The cross-launch check, run automatically once a menu is up. This
         * is the single most important open question and it needs no
         * keypress, so do not make the user remember one. */
        if (unpacked && beat == 10) {
            log_line("verify: automatic cross-launch check (beat 10) --");
            hd_verify_path();
        }

        if (unpacked && !g_gui_done && beat == 4) {
            gui_result g;
            g_gui_done = TRUE;
            if (gui_find(&g)) {
                log_line("guifind: inventory only -- %d layouts resident. "
                         "This does NOT identify the displayed screen; see "
                         "the note in gui_find.h.", g.nresident);
                /* Hand the located pool to the tree walk so its first capture
                 * does not repeat this ~15 s scan. */
                gui_tree_seed(&g);
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
    hash_watch_shutdown();
    /* Do not wait on the thread here: DLL_PROCESS_DETACH also holds the
     * loader lock, and the worker may be inside a call that needs it. */
    if (g_thread) {
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    speech_shutdown();
}
