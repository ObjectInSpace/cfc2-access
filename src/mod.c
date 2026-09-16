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
#include "kbd_state.h"
#include "gui_text.h"
#include "menu_cursor.h"
#include "menu_speak.h"   /* menu_speak_set_discovery() */

#include <stdio.h>

static HANDLE g_thread;
static volatile LONG g_quit;

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
    /* ⛔⛔ THE BUILD STAMP. Twice now a run has been analysed against source the
     * game was not running: once because `.\build.ps1` was used without
     * -Deploy, once because the switch file was missing too. Both times the log
     * looked exactly like a FAILED FEATURE -- bare positions, no diagnostics --
     * and the first cost a full round trip before the DLL timestamps were
     * checked.
     *
     * ⭐ A log that cannot say which build wrote it cannot be trusted to report
     * on any change. This is the cheapest possible fix: the compiler's own
     * timestamps, which cannot be stale by construction. Compare it against the
     * build time before concluding ANYTHING from a run. See memory
     * [[a-log-cannot-show-code-written-after-it]]. */
    log_line("build: compiled " __DATE__ " " __TIME__
             "  -- ⚠ if this predates the change you are testing, the game is "
             "running a STALE DLL (build.ps1 -Deploy).");
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
     * of the switch is one file check. Create `cfc2_pad.on` next to the exe
     * to disable it without rebuilding. */
    /* ⛔ OFF BY DEFAULT NOW (was on-unless-disabled). sPad is inert for the
     * SHELL -- Kind=0 on all five records, so the update never runs and every
     * field reads zero. That is a proven negative, not a tooling failure.
     *
     * ⚠ Kept rather than deleted because the IN-GAME work is still ahead, and
     * controller input during an emulated match may take a different path. Turn
     * it on with `cfc2_pad.on`.
     *
     * The per-poll cost this removes is real: nine GetAsyncKeyState calls every
     * 100 ms, feeding a record known to be all zeros. */
    BOOL pad_enabled = opt_in_file_present("cfc2_pad.on");

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
    /* ⛔ RETIRED. It answered its one question -- which input API the shell
     * uses -- and the answer was later CORRECTED by disassembly anyway:
     * GetAsyncKeyState is the mouse only, the keyboard arrives via DirectInput.
     * Re-running it cannot teach us anything new, and it cost ~35 log lines
     * every launch. Source kept in tree for the next "who calls X" question. */


    /* --- The caller trace ----------------------------------------------
     * The IAT probe answered WHICH api; this answers WHO calls it. Install
     * only after unpack: the shell's code does not exist as code until then,
     * and hooking user32 before the game has started polling would just
     * accumulate our own calls.
     *
     * ⚠ Installed even though we are also a caller -- see caller_trace.h. Our
     * calls are the hook's liveness proof, not noise to be avoided. */
    /* ⭐ The caller trace is RETIRED -- it did its job. It found exactly one
     * call site inside the exe (0x140253860), and disassembling around it
     * resolved the entire input model offline: sKeyboard, its vtable, the
     * update method, the state builder and the bank layout. Leaving it hooked
     * would cost a per-frame detour for information we already have.
     * It stays in the tree, tested, for the next time a caller question comes
     * up; `ct_install()` is simply no longer called.
     *
     * ⚠ It also corrected the model that produced it: GetAsyncKeyState is the
     * MOUSE path only (5 buttons). The keyboard arrives via DirectInput
     * GetDeviceState inside the builder we now hook instead. */
    BOOL hook_ok = FALSE;
    if (unpacked && hook_enabled) {
        hook_ok = kbd_install();
        if (!hook_ok)
            log_line("kbd: install FAILED -- nothing was written to game "
                     "memory, and no key state is available.");
    } else if (unpacked) {
        log_line("kbd: DISABLED by cfc2_hook.off next to the exe. Delete that "
                 "file to re-enable the keyboard state watcher.");
    }

    /* --- The menu cursor ------------------------------------------------
     * ⭐⭐ Route 7's answer. Every shell menu moves its selection through one of
     * two generic helpers (0x14007cdc0 vertical, 0x14007c9f0 horizontal), and
     * both receive the SCREEN OBJECT and a POINTER TO THE CURSOR as arguments.
     * Hooking them yields the selected index, the range, and therefore the item
     * count -- with no memory search at all. See menu_cursor.h.
     *
     * ⚠ This also corrects a claim recorded across four closed routes: that no
     * flat 0..N-1 selection index exists in this game. One does; it is a field
     * of the per-screen object, at a different address in every menu, which is
     * exactly why a scan for a single global integer could never find it.
     *
     * Shares cfc2_hook.off, so one file still makes the whole mod read-only. */
    BOOL mc_ok = FALSE;
    if (unpacked && hook_enabled) {
        mc_ok = mc_install();
        if (!mc_ok)
            log_line("mc: install FAILED (reason %d) -- nothing was written to "
                     "game memory, and no menu movement is available.",
                     (int)mc_install_why());
    } else if (unpacked) {
        log_line("mc: DISABLED by cfc2_hook.off next to the exe.");
    }

    /* --- The GUI text reader --------------------------------------------
     * ⭐⭐ The one thing that unblocks most of what is left. ~14 shell screens
     * speak position only, all for the SAME reason: their labels are assigned
     * to GUI text elements at RUNTIME, so no GMD or code search can name them.
     * The control that proved it: the Options screen's labels ARE known (GMD
     * 170..174) and 170 appears nowhere in its function.
     *
     * So let the game hand us an instance instead -- the same move that
     * cracked sKeyboard. See gui_text.h for why slot 5 of cGUIObjText's vtable
     * is the target, and for the uniqueness check that makes every captured
     * instance certainly of this class.
     *
     * ⚠⚠ OPT-IN, and deliberately so. This is the riskiest thing in the
     * project: it reads 528 bytes through a game-supplied pointer. The detour
     * itself dereferences NOTHING -- all reading is deferred to our worker
     * thread under the fault guard -- but the read is still UNPROVEN, and an
     * unproven read on a per-frame path gets a switch until it is proven.
     *
     * ⭐ That gate comes OFF once the string offset is known and the read is
     * targeted. A guard outlives its bug otherwise: the discovery sweep's gate
     * stayed on for days after the cost that justified it was fixed, keeping
     * the only screen-naming tool off in every real session.
     *
     * Shares cfc2_hook.off, so one file still makes the whole mod read-only. */
    /* ⛔ The DISCOVERY candidate sweep: a development aid, OFF by default.
     * It stalled the user's session twice (37 s, then 11.9 s) and has named
     * zero shipped screens. Unknown screens are still logged without it.
     * See menu_speak_set_discovery(). */
    {
        BOOL disc_on = opt_in_file_present("cfc2_discover.on");
        menu_speak_set_discovery(disc_on);
        if (disc_on)
            log_line("mcspeak: ⚠ cfc2_discover.on present -- the DISCOVERY "
                     "candidate sweep is ENABLED. It runs on the poll thread "
                     "and has stalled sessions before; remove the file when "
                     "you are done naming a screen.");
    }

    BOOL gt_on = opt_in_file_present("cfc2_guitext.on");
    BOOL gt_ok = FALSE;
    if (unpacked && hook_enabled && gt_on) {
        gt_ok = gt_install();
        if (!gt_ok)
            log_line("gt: install FAILED (reason %d) -- nothing was written to "
                     "game memory, and no GUI text element is available.",
                     (int)gt_install_why());
    } else if (unpacked && gt_on) {
        log_line("gt: DISABLED by cfc2_hook.off next to the exe.");
    } else if (unpacked) {
        log_line("gt: OFF -- create cfc2_guitext.on next to the exe to hook "
                 "cGUIObjText and log what a live text element contains. "
                 "This is the step that names the remaining screens.");
    }

    /* ⭐ The focus differ rides on the gt hook -- without the instance set
     * there is nothing to diff -- so it is gated on gt_ok as well as its own
     * file. See gui_text.h for what it measures and why it is not route 3. */
    if (gt_ok) {
        BOOL fd_on = opt_in_file_present("cfc2_focusdiff.on");
        gt_focus_diff_enable(fd_on);
        if (fd_on)
            gt_focus_diff_report();
    }

    /* ⭐⭐ The MOVE LIST membership probe. Also gated on gt_ok: without the
     * instance set there are no widgets to count, so it would report a
     * confident zero about a census it never took.
     *
     * ⚠ The file check lives HERE, not in gui_text.c -- mod.c owns
     * opt_in_file_present(), which resolves the EXE's directory. The two
     * probes that check a bare relative path in gui_text.c work only because
     * the CWD happens to be the game dir; that is not a pattern to copy. */
    if (gt_ok) {
        BOOL ml_on = opt_in_file_present("cfc2_movelist.on");
        gt_movelist_enable(ml_on);
        if (ml_on)
            log_line("mod: cfc2_movelist.on -- the move-list membership probe "
                     "is ENABLED. It counts widgets per cursor movement and "
                     "reports every 10 s. Nothing is spoken.");

        /* ⚠ OPT-IN, like every other diagnostic here. A settings screen is
         * walked deliberately to collect its values; there is no reason to
         * sample one during ordinary play. The lesson that turned the
         * discovery sweep off by default applies: ask whether the expensive
         * thing needs to run AT ALL before making it cheap. */
        /* ⛔⛔ WITHDRAWN: the census CRASHED THE USER'S GAME (see the call site
         * in the poll loop for the evidence and what to investigate). The
         * switch is still read so a run SAYS the feature is withheld rather
         * than appearing to work and silently recording nothing -- a silent
         * no-op is exactly the state a blind user cannot distinguish from a
         * broken one. */
        /* ⭐ The snapshot: opt-in, nothing spoken, one report per screen. */
        BOOL snap_on = opt_in_file_present("cfc2_snap.on");
        gt_snapshot_enable(snap_on);
        if (snap_on)
            log_line("mod: cfc2_snap.on -- the screen snapshot is ENABLED. It "
                     "logs each screen's row labels and values ONCE, on the "
                     "first cursor movement there. Walk in, press a key, walk "
                     "out. Nothing is spoken.");

        if (opt_in_file_present("cfc2_census.on"))
            log_line("mod: ⛔ cfc2_census.on is present but the settings "
                     "census is WITHDRAWN -- it crashed the game in ~1.5 s on "
                     "2026-09-15 (confirmed by isolation; mechanism NOT yet "
                     "established). Nothing is being recorded. Delete the file "
                     "to silence this line.");
        gt_settings_census_enable(FALSE);
    }

    /* ⚠ OPT-IN. The offline tools (disasm.py, find_callers.py,
     * build_riprefs.py, analyze_dump.py) all read this dump, and it is the
     * machinery that cracked sPad, sKeyboard and the cursor chokepoint -- so it
     * is kept, not deleted. But .text is stable across runs (ASLR off, no
     * relocations) and one dump already sits on disk, so rewriting 60 MB every
     * launch buys nothing.
     *
     * ⚠ The DATA sections are NOT stable, and build_riprefs.py refuses a stale
     * index rather than answering wrongly -- so a fresh dump IS occasionally
     * needed. Create `cfc2_dump.on` next to the exe to take one. */
    int dumped = 0;
    if (unpacked && opt_in_file_present("cfc2_dump.on")) {
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
        /* ⭐ SAY WHAT MATTERS NOW. This used to announce the IAT probe's
         * findings, which was the right headline when the run existed to answer
         * "which input API". That question is long settled, and the probe is
         * retired. What a user needs to hear today is simply whether the mod
         * can name menu items -- speech backend, and whether the message table
         * resolved. */
        if (unpacked) {
            _snwprintf(msg, 255,
                       L"Access mod ready. Backend %ls.",
                       backend ? backend : L"none");
        }
        else
            _snwprintf(msg, 255,
                       L"Access mod loaded, backend %ls, but the image never "
                       L"unpacked. Menu reading will not work.",
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
        log_line("pad: OFF -- cfc2_pad.on is absent. sPad is inert for the "
                 "shell (Kind=0, update never runs); create that file only "
                 "for in-game work.");
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
        /* ⭐⭐ 100 x 20 ms, not 20 x 100 ms -- the 2 s beat is unchanged.
         *
         * ⛔ USER: "it is impractical for me to wait... there are hundreds of
         * options in there. You will have to make it so that it reads each one
         * as soon as I find it with no delay, while I hold the button down."
         *
         * The movement poll WAS the delay. A keypress moves the cursor on the
         * render thread instantly, but nothing speaks until this loop notices --
         * 0-100 ms, averaging 50. Held on a direction through a 40-item grid
         * that is a stutter behind every single item.
         *
         * ⚠ 20 ms is a FIFTH of the old worst case and still 20x slower than
         * the render thread, so the cheap per-tick work (a sequence compare and
         * a modal check) stays cheap. Everything expensive is gated on the 2 s
         * beat below, which still fires every 100 ticks exactly as before.
         *
         * ⛔ Speech does NOT move to the detour. That runs on the render thread
         * and speech_say() calls into NVDA -- a stall there is a frozen game,
         * which this project has already paid for twice. The poll is the right
         * place; it just had to be faster. */
        for (int t = 0; t < 100; t++) {
            if (InterlockedCompareExchange(&g_quit, 0, 0))
                break;
            Sleep(20);

            /* ⭐⭐⭐ THE MODAL CHECK RUNS AT 100 ms, NOT ON THE 2 s BEAT.
             *
             * ⛔⛔ MEASURED FAILURE, user-reported as "I just didn't hear the
             * Yes or No when moving": on the 2 s beat the user pressed arrows
             * SIX times between two polls, the index went 0 -> 1 and back, and
             * the guard -- correctly -- saw no change and said nothing. The
             * announcement only arrived when a press happened to land near a
             * poll boundary.
             *
             * A selection the user moves several times a second cannot be
             * watched on a 2 s clock. Same lesson as the layout probe and the
             * focus differ: the cadence has to fit what the screen DOES.
             *
             * ⚠ Cheap enough to run at this rate: it walks the focus record
             * (bounded, 48 entries) and returns immediately on every screen
             * that has no "Yes" widget, which is all of them but this dialog. */
            if (gt_ok)
                menu_speak_check_modal();

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
            /* Read the shell's own decoded key state. Logs only on a change,
             * so the log shows navigation rather than a per-frame flood.
             * ⭐ This is a two-read compare against a pointer the game handed
             * us, not a search -- the whole point of the offline work. */
            if (hook_ok)
                kbd_poll_and_log();

            /* ⭐ The menu cursor. The hook records movements on the game's own
             * thread as they happen, so unlike a poll of a live field this
             * cannot miss a move by sampling between frames.
             *
             * ⚠ But it keeps only the MOST RECENT movement, not a queue: if the
             * user holds a direction and the cursor travels several items
             * between two polls, the intermediate positions are lost and only
             * the latest is reported. That is the right behaviour for speech
             * (announcing every skipped item would lag badly behind the user),
             * but it means the log is not a complete movement history. Said
             * plainly here so a gap in the log is not read as a missed hook. */
            /* ⭐ Resolve the message blob lazily, once. It cannot be done at
             * startup: the table is built during load, and the blob's offset
             * MOVES between runs, so it must be found in this process. Until
             * it resolves, movements still speak POSITION, never a guess. */
            if (mc_ok)
                gmd_text_ready();

            if (mc_ok)
                mc_poll_and_log();

            /* ⭐ Inspect at most one new instance per poll. The hook sees
             * every text element every frame; dumping them all would bury the
             * log and cost far more than the finding is worth. One per 100 ms
             * still walks through the elements on screen quickly, because the
             * hook keeps only the most recent and the game cycles through
             * them. */
            if (gt_ok)
                gt_poll_and_log();

            /* ⭐⭐ THE MOVE LIST PROBE SAMPLES AT 100 ms, NOT ON THE 2 s BEAT.
             *
             * The user moves through the move list every ~0.6 s (measured), so
             * a 2 s beat would lose 3-6 movements per beat -- the same failure
             * the modal check hit above, and the reason offline pairing against
             * the 6.6 s layout dump failed 172 of 175 times. At 100 ms this is
             * ~6x oversampled; what it still misses it COUNTS, so a thin
             * sample cannot masquerade as a complete one.
             *
             * ⚠ mc_peek_rest() does NOT consume, so this cannot steal a
             * movement from mc_poll_and_log() above -- which would make the
             * user's speech drop movements to feed a diagnostic.
             *
             * ⭐ The JOIN happens here, not inside gui_text: that module must
             * not depend on menu_cursor (linking the two into the gui_text
             * suite pulls in menu_speak and the GMD reader). mod.c already owns
             * both, so it reads the cursor and hands the answer over. */
            if (gt_ok && mc_ok) {
                mc_move mlm;
                if (mc_peek_rest(&mlm)) {
                    int mltot = mlm.max_index - mlm.min_index + 1;
                    /* 0x140080250 is the move list's VERTICAL axis -- the one
                     * whose total's meaning is the open question. */
                    gt_movelist_probe(mlm.screen, mltot,
                                      mlm.caller == (void *)0x140080250ULL);

                    /* ⭐⭐ THE SETTINGS CENSUS, on the same movement and for the
                     * same reason the probe above runs at 100 ms: the value
                     * cursor moves every ~0.2-0.5 s and the 2 s layout dump
                     * caught ONE value per row out of nine.
                     *
                     * ⚠ The columns are the SETTINGS-SCREEN geometry measured
                     * across 16 dumps (labels X=320, values X=835, 80px pitch),
                     * which Training Options and Game Settings share. Passed in
                     * rather than baked into gui_text: a column X has been
                     * written down as 1265, then 1268, and measured as 633 on a
                     * neighbouring screen -- three values for one number is the
                     * argument against ever hardcoding one.
                     *
                     * ⛔ It records what the game DREW next to each label. It
                     * does not decide which corpus run those values are, or
                     * which tab is showing -- that join stays offline where the
                     * corpus can check it. */
                    /* ⭐⭐ THE SCREEN SNAPSHOT: one report per SCREEN, so a
                     * capture costs a single keypress instead of a 3-second
                     * pause. 320/835 are the settings-screen geometry as a
                     * HINT only -- gt_snapshot() discovers the real columns and
                     * falls back to the leftmost populated one, which is what
                     * CVS1's Game Settings needs (it draws nothing at 320).
                     *
                     * ⚠ Unlike the withdrawn census this does NOT run per
                     * movement: it is silent until a different screen object
                     * appears, which is a human-speed event. */
                    gt_snapshot(mlm.screen, 320.0f, 835.0f, mltot);

                    /* ⛔⛔ THE CENSUS IS NOT CALLED. It CRASHED THE USER'S GAME.
                     *
                     * Confirmed by isolation on 2026-09-15: same build, same
                     * hooks, `cfc2_census.on` removed -> no crash; present ->
                     * dead in ~1.5 s. Two crashes, 0xc0000409 (stack buffer
                     * overrun) then 0xc0000005, both faulting inside the game.
                     *
                     * ⚠ THE MECHANISM IS NOT ESTABLISHED. It compiles clean at
                     * -O2 -Wall -Wextra -Wclobbered, every offline case passes,
                     * and three readings of the code produced three different
                     * unproven theories. Re-enabling it on the strength of a
                     * plausible fix would be the fourth.
                     *
                     * ⭐ It walked the instance table 15 times per sample
                     * (1 + 2 per row), taking and RELEASING g_inst_busy between
                     * every call, on the 100 ms poll, while the ctor hook adds
                     * and evicts instances from the game's own thread -- and
                     * gt_column_item() hands back `.text` POINTERS INTO that
                     * table which outlive the lock. That is the shape to
                     * investigate first, offline, before this is called again.
                     *
                     * ⛔ A diagnostic that crashes the user's game is worth
                     * less than no diagnostic. The code and its cases stay for
                     * the investigation; the CALL does not ship.
                     * See [[fix-the-users-bug-not-the-tooling]]. */
                }
            }

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
        /* ⚠ Every 5 beats (10 s), not every 30 (60 s). A session ended with
         * the log stopping mid-navigation and the user reporting the mod had
         * "stopped reading" -- and the heartbeat could not distinguish a dead
         * worker from a quiet one, because the next beat was 40 s away. A
         * liveness signal that sparse cannot answer the question it exists
         * for. */
        if (beat <= 3 || beat % 5 == 0)
            log_line("heartbeat %d (alive %d s)", beat, beat * 2);

        /* Periodic keyboard-state summary: how many times the builder ran and
         * whether the instance was captured. The per-change lines above carry
         * the actual key data; this is the liveness context they need. */
        if (hook_ok && (beat == 3 || beat == 10 || beat == 30 ||
                        beat % 60 == 0)) {
            kbd_capture cap;
            kbd_snapshot(&cap);
            log_line("kbd: builder ran %ld times; instance=%p state=%p "
                     "offset=%s",
                     cap.calls, cap.manager, cap.state,
                     cap.state ? (cap.offset_ok ? "0x138 CONFIRMED"
                                                : "⚠ UNEXPECTED")
                               : "(not captured)");
        }

        /* ⭐⭐ BOTH COUNTS, periodically. "0 instances inspected" is
         * meaningless on its own: with calls == 0 the hook is not
         * intercepting and NOTHING can be concluded, which is a different
         * finding from "inspected elements and none held text". This project's
         * rule is that "could not measure" must never look like "measured
         * nothing". */
        /* ⛔⛔ REPORT ON CHANGE, NOT ON A SCHEDULE.
         *
         * This used to fire only at beats 3, 10, 30 and every 60. A real run
         * ended at beat 25, so the last report was taken at beat 10 -- BEFORE
         * the user had navigated anywhere. It printed "0 calls", which was true
         * of the title screen and said nothing about the menus that followed.
         *
         * ⭐ A measurement whose timing is unrelated to the thing being
         * measured can report a confident zero about a period it never
         * observed. Reporting whenever the count MOVES ties the log to the
         * user's actions instead of to the clock, and the periodic beat is
         * kept only as a liveness floor for when nothing changes at all. */
        static LONG gt_last_calls = -1;
        gt_stats gs_probe;
        if (gt_ok)
            gt_get_stats(&gs_probe);
        /* ⚠ Track the CTOR count too. A run where objects are built but slot 13
         * never fires is the single most informative outcome available, and
         * watching only `calls` would leave it unreported. */
        static LONG gt_last_ctors = -1;
        static LONG gt_last_text = -1;
        BOOL gt_changed = gt_ok && (gs_probe.calls != gt_last_calls ||
                                    gs_probe.ctors != gt_last_ctors ||
                                    gs_probe.with_text != gt_last_text);
        if (gt_ok && (gt_changed || beat == 3 || beat == 10 || beat == 30 ||
                      beat % 60 == 0)) {
            gt_stats gs;
            gt_get_stats(&gs);
            gt_last_calls = gs.calls;
            gt_last_ctors = gs.ctors;
            gt_last_text  = gs.with_text;
            /* ⛔⛔ THE SETTER'S COUNT WAS COLLECTED AND NEVER PRINTED, and it
             * cost a run. The line below was built around the retired ring
             * sweep, so a run could report "0 held TEXT" while saying nothing
             * about whether the text hook had fired even once -- which is the
             * one number that separates "never called" from "called and
             * filtered". Adding a stat to the struct is not adding it to the
             * REPORT; this project's own rule is that a count nobody prints is
             * a count nobody has. */
            log_line("gt: %ld CONSTRUCTED%s; setText ran %ld time(s), %ld "
                     "string(s) captured; %ld guarded read(s) faulted.%s%s%s",
                     gs.ctors,
                     gs.ctor_hooked ? "" : " (⚠ CTOR NOT HOOKED -- this zero "
                                           "measures nothing)",
                     gs.settext_calls, gs.with_text, gs.faults,
                     /* ⭐⭐ The decisive readings, stated in the log so they do
                      * not have to be reconstructed later from raw counts. */
                     (gs.ctor_hooked && gs.ctors == 0)
                         ? "  ⛔⛔ ZERO CONSTRUCTED: the shell never builds this "
                           "class. The cGUIObjText route is CLOSED -- decisive, "
                           "not a missed hook."
                         : "",
                     (gs.ctors > 0 && gs.settext_calls == 0)
                         ? "  ⛔ Objects exist but setText NEVER RAN: the class "
                           "is right and 0x1402f0b50 is not how its text is "
                           "assigned. Find the other writer."
                         : "",
                     (gs.settext_calls > 0 && gs.with_text == 0)
                         ? "  ⛔ setText ran but captured NOTHING: it is firing "
                           "for objects the vptr filter rejects, or the string "
                           "argument is unreadable. NOT a class problem."
                         : "");

            /* ⭐ WHICH overload. Printed whenever any of them has fired, so a
             * nonzero total can never leave the reader guessing which of the
             * four the shell actually uses -- the exact gap that made the
             * previous run unreadable. */
            if (gs.settext_calls > 0)
                log_line("gt:   by overload: cGUIObjText.25=%ld .26=%ld  "
                         "cGUIObjMessage.25=%ld .26=%ld",
                         gs.settext_each[0], gs.settext_each[1],
                         gs.settext_each[2], gs.settext_each[3]);
        }

        /* ⭐⭐ THE FOCUS RECORD -- the open question this build exists to
         * answer: do per-ROW captions flow through setText as the cursor moves?
         *
         * ⚠ On the SLOW beats only, never on gt_changed. The record is a whole
         * ordered transcript, so printing it every time a counter moved would
         * reprint dozens of lines several times a second and bury the log --
         * the exact failure the retired ring sweep caused (1235 refusal lines
         * in one run). It also refuses to conclude when the cursor side never
         * notified it; see gt_focus_report(). */
        /* ⛔ BEATS 10/30/60 MISSED THE RUN. The 22:50 session ended after beat
         * 15, so exactly ONE report fired -- at beat 10, covering 2 of the 22
         * movements the user actually made. The screens of interest were never
         * dumped, and `0 dropped` was measured while only 16 strings existed,
         * so it said nothing about the later ones.
         *
         * ⭐ This is the same shape as the caller trace's old schedule, which
         * a real run outran in exactly the same way. Report on a cadence the
         * user's actual play can reach: every 5 beats (10 s) once there is
         * something to say, plus the early one. A repeat costs a few lines and
         * a missed report costs a launch. */
        /* ⚠⚠ ORDERING DEPENDENCY: this CLEARS the focus window, and
         * mc_poll_and_log() above is what consumes a caption to speak it. That
         * call must stay EARLIER in the loop than this one -- reversing them
         * would let a report discard the caption for a movement that has not
         * been spoken yet, which reads as "captions randomly stop working".
         * They are ~150 lines apart, so the dependency is stated here rather
         * than left to be noticed. */
        /* ⭐ The focus differ samples PER MOVEMENT, not per beat -- a beat
         * cadence would miss most movements. It no-ops unless the cursor moved
         * since the last poll, and unless cfc2_focusdiff.on exists. It runs
         * HERE, on the worker, because the cursor hook must stay two atomics. */
        /* ⛔⛔ AND IT ALSO SAMPLES ON TEXT CHANGE. Per-movement alone was
         * useless for exactly the screen it was most needed on: the quit
         * dialogs produce ZERO cursor movements (measured -- 3 and 8 arrow
         * presses, no movement logged), so a movement-gated differ records
         * nothing there however long the dialog is open. Sending the user to
         * capture it would have wasted the run.
         *
         * ⭐ setText's counter ticks when the screen REDRAWS, which is what a
         * modal does when its selection changes. Same clock the layout probe
         * now uses, and the same lesson: gate on the signal the target screen
         * actually produces, not the one the other screens happen to share. */
        if (gt_ok) {
            static LONG fd_last_sets;
            gt_stats fs;
            gt_get_stats(&fs);
            gt_focus_diff_note_move();
            if (fs.settext_calls != fd_last_sets) {
                fd_last_sets = fs.settext_calls;
                gt_focus_diff_note_text();
            }
        }

        if (gt_ok && (beat == 10 || beat % 5 == 0))
            gt_focus_report();

        /* ⭐⭐ FOLLOW A LANGUAGE CHANGED IN OPTIONS > SYSTEM.
         *
         * ⛔ USER: "Ensure that it will pick the correct tables for all of the
         * languages when its language is selected from settings." The blob is
         * resolved once and gmd_text_ready() returns early forever after, so
         * switching language mid-session left every item name coming from the
         * OLD language -- and nothing looked wrong, because the label hashes
         * are language-independent and every structural check still passed.
         *
         * ⚠ On the SLOW beat (10 s), never the 100 ms poll: this reads
         * config.ini from disk, and a language change is a human-speed event.
         * The common case costs one indexed read plus that file read; only a
         * genuine disagreement pays for the full re-search. */
        if (mc_ok && (beat == 10 || beat % 5 == 0))
            gmd_text_recheck_language();

        /* ⭐⭐ The INSTANCE SET -- step 1 of the GUI-instance route. Logged
         * only, never spoken; see gt_instance_report(). Same cadence as the
         * focus record so one run carries both, and on the slow beats only
         * because it prints one line per live object. */
        if (gt_ok && (beat == 10 || beat % 5 == 0))
            gt_instance_report();

        /* ⭐⭐⭐ MODAL PROMPTS -- the quit dialogs. NOT gated on a switch: this
         * is shipping speech, not a diagnostic.
         *
         * ⚠ It runs on the poll loop rather than off a movement because these
         * dialogs produce NO cursor movements at all; everything else in the
         * speech path is movement-driven and therefore blind to them.
         * menu_speak_check_modal() matches the live text against the two known
         * prompt strings and announces once per appearance, so polling it costs
         * a string compare. */
        if (gt_ok)
            menu_speak_check_modal();

        /* ⭐⭐ The MODAL SELECTION HUNT (cfc2_modal.on, absent = off). Same
         * redraw clock as the other probes -- a dialog is open for ~2 s and a
         * beat cadence misses it entirely, which already cost one run. */
        if (gt_ok)
            gt_modal_probe();

        /* ⭐⭐ The LAYOUT PROBE (cfc2_layout.on, absent = off). Looks for the
         * field that orders objects by SCREEN POSITION.
         *
         * ⛔⛔ IT FIRES ON TEXT CHANGE, NOT ON A TIMER, and that is the whole
         * point. On the 10-beat cadence it MISSED the quit dialog completely:
         * the probe sampled at 13:11:49.420, the dialog drew "No" at
         * 13:11:49.730, and the next sample was ten seconds later -- long after
         * the dialog had closed. A ~2-second dialog cannot be caught by a
         * ~10-second poll, and the user paid a real run to discover that.
         *
         * ⭐ setText's call count is the right clock: it ticks exactly when the
         * screen redraws, which is exactly when a dialog appears. Sampling on
         * CHANGE rather than on time is the same lesson as "report cadence must
         * fit REAL play" -- the beat schedule was picked for menus, and a modal
         * lives on a different timescale.
         *
         * ⚠ Still rate-limited: at most once per beat, so a busy screen (the
         * music player redraws its timer constantly) cannot flood the log. */
        if (gt_ok) {
            static LONG last_sets;
            gt_stats ls;
            gt_get_stats(&ls);
            if (ls.settext_calls != last_sets || beat == 10 || beat % 5 == 0) {
                last_sets = ls.settext_calls;
                gt_layout_report();
            }
        }

        /* ⭐ The move-list probe REPORTS on the slow beats even though it
         * SAMPLES every 100 ms -- the split gt_focus_diff already uses. Its
         * output is an aggregate histogram of a handful of lines regardless of
         * how many movements were sampled, so 10 s cannot flood a log whose
         * every line costs an _wfopen/fclose. One line per movement would be
         * hundreds of file opens.
         *
         * ⚠ Every 5 beats plus an early one -- the cadence mod.c earned after
         * beats 10/30/60 missed an entire run. */
        if (gt_ok && (beat == 10 || beat % 5 == 0))
            gt_movelist_report();

        /* ⛔ The census report is not called either: with the sampler withdrawn
         * it would print only its INCONCLUSIVE line every 10 s, which is noise
         * that reads like a finding. */

        /* ⛔ THE SEPARATE RESCAN IS GONE. It cost EIGHT SECONDS scanning
         * 1.3 GB, and it duplicated work: gmd_text_ready() already calls
         * gmd_find(), caches the resolved table, and is itself retried by this
         * poll loop until it succeeds. Two independent retry schedules over the
         * same expensive scan is one too many.
         *
         * ⚠ What is NOT lost: the retry behaviour. gmd_text_ready() returns
         * FALSE until the shell has loaded its message file, and the poll loop
         * keeps calling it -- so a table that is not ready at beat 3 is still
         * picked up later, which was the whole point of retrying. */

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
