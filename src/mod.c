/*
 * CFC2 Access -- mod core: the worker thread.
 *
 * Waits for Enigma to decrypt the exe, installs the hooks, then runs one
 * 10 ms poll loop on the speech worker: hotkeys, every game's selection and
 * text readers, and the shell's menu speech. Every address used anywhere is
 * fixed for this build (ASLR off, no relocations), and each module checks the
 * code it depends on before touching it.
 *
 * The research probes that once lived here (emulator-RAM search, in-game
 * cursor probe, sPad and keyboard watchers, caller trace, IAT probe, image
 * dumper, collection boundary probe) were deleted on 2026-11-25 once every
 * question they existed for was answered; they are in git history.
 */

#include "mod.h"
#include "log.h"
#include "speech.h"
#include "audio.h"
#include "image.h"
#include "gmd_find.h"     /* the message table, gmd_text_language()          */
#include "gui_text.h"
#include "help_line.h"
#include "menu_cursor.h"
#include "menu_speak.h"
#include "mod_text.h"     /* the mod's own words, in the game's language    */
#include "text_channel.h"
#include "cvs1_select.h"
#include "cvs1_frame.h"
#include "cvs1_text.h"
#include "cvs2_select.h"
#include "cvs2_frame.h"
#include "cvs2_story.h"
#include "cvs2_text.h"
#include "cfj_select.h"
#include "cfj_frame.h"
#include "cfj_text.h"
#include "z3u_select.h"
#include "z3u_frame.h"
#include "z3u_text.h"
#include "mjus_select.h"
#include "mjus_frame.h"
#include "mjus_text.h"
#include "mjus_story.h"
#include "stg2_select.h"
#include "stg2_frame.h"
#include "pws1_select.h"
#include "pws1_frame.h"
#include "pws2_select.h"
#include "pws2_frame.h"

#include <stdio.h>

static HANDLE g_thread;
static volatile LONG g_quit;

/* Was `vk` pressed since the last check? Edge-triggered, so holding the key
 * acts once rather than flooding.
 *
 * ⚠ Shift is read SEPARATELY by the caller, not folded in here: the edge must
 * belong to the cue key itself, so that shifting mid-hold cannot manufacture a
 * second press. */
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

/* --- Per-cue volume hotkeys ----------------------------------------------
 * F5 health · F6 super meter · F7 beacons · F8 clock tick.
 * Plain key steps UP, Shift+key steps DOWN, both wrapping on
 * 0/25/50/75/100. From the 100 default one press mutes -- the user's design.
 *
 * ⚠ THE KEY CHOICE WAS CHECKED, not assumed (user asked directly):
 *  - the mod's only other hotkey was the pad dump on F7, now removed;
 *  - CFC2's keyboard defaults use F1-F3 (Start F1, Menu F2, Coin F3 -- read
 *    from the Keyboard Settings layout dump, 2026-09-11 15:17) and letters
 *    for the rest, so F5-F8 are free. ⚠ An earlier note here said the game
 *    binds NO function keys; the screen itself shows otherwise;
 *  - the `KB_F10` seen in gui_markup.c and markup.py is an EXAMPLE in a
 *    comment about the mechanical KB_ naming rule, not a binding;
 *  - F12 is deliberately avoided: it is Steam's default screenshot key.
 *
 * ⚠ These are NOT gated on the audio device opening. A key that silently
 * does nothing is exactly the failure a blind user cannot diagnose. The
 * handler announces the level either way.
 *
 * ⭐ SPEECH IS THE ONLY FEEDBACK. There is no on-screen indicator, so an
 * unspoken change is an invisible one; the announcement interrupts, because
 * holding a key through three steps should say where it LANDED, not queue
 * four sentences. */
static void volume_hotkeys_tick(void)
{
    for (int c = 0; c < AUDIO_CUE_COUNT; c++) {
        if (!key_pressed(VK_F5 + c))
            continue;
        /* Read shift at the moment of the edge. */
        BOOL down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        int pct = audio_cue_step_volume((audio_cue)c, !down);

        wchar_t say[96];
        /* "beacons 50 percent" / "health muted" -- the word MUTED rather than
         * "0 percent", because 0 is the state the user most needs to
         * recognise instantly and a number is easy to mishear. */
        /* Spoken in the game's language; audio_cue_name() stays English for
         * the log line below. Order is audio_cue's. */
        static const mt_id cue_word[AUDIO_CUE_COUNT] = {
            MT_CUE_HEALTH, MT_CUE_SUPER, MT_CUE_BEACONS, MT_CUE_TICK };
        if (pct == 0)
            mt_fmt(say, 96, MT_MUTED, mt(cue_word[c]), NULL, NULL);
        else {
            wchar_t n[16];
            _snwprintf(n, 16, L"%d", pct);
            n[15] = 0;
            mt_fmt(say, 96, MT_PERCENT, mt(cue_word[c]), n, NULL);
        }
        speech_say(say, TRUE);

        /* Persist so the balance survives a restart. ⚠ A profile write is a
         * file I/O on the worker thread, but it happens only on a KEYPRESS --
         * a handful of times a session, never in a loop. That is the test this
         * project applies before any I/O near the poll: count how often it
         * runs, not how fast it is. */
        audio_save_volumes();
        log_line("audio: %ls volume -> %d%%",
                 audio_cue_name((audio_cue)c), pct);
    }
}


/* --- Power Stone settings key --------------------------------------------
 * F9 cycles pickup announcements (every fighter, players only, off);
 * Shift+F9 toggles off-screen sounds. User, 2026-10-27, "in case it is too
 * busy". Checked: the mod uses F5-F8 (+Shift) and `; CFC2 binds no function
 * keys; Steam takes F12; the capture tools moved their snapshot key from F9
 * to F11; F10 is Windows' menu key. Spoken and saved like the volume keys. */
static void power_stone_hotkeys_tick(void)
{
    BOOL shift;
    wchar_t say[128];
    if (!key_pressed(VK_F9)) return;
    shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (shift) {
        int on = audio_option_cycle(AUDIO_OPT_OFFSCREEN);
        wcsncpy(say, mt(on ? MT_OFFSCREEN_ON : MT_OFFSCREEN_OFF), 127);
        log_line("options: off-screen sounds %s", on ? "on" : "off");
    } else {
        static const mt_id word[3] = { MT_PICKUPS_OFF, MT_PICKUPS_PEOPLE, MT_PICKUPS_ALL };
        int mode = audio_option_cycle(AUDIO_OPT_PICKUP_SPEECH);
        wcsncpy(say, mt(word[mode]), 127);
        log_line("options: pickup announcements %d", mode);
    }
    say[127] = 0;
    speech_say(say, TRUE);
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

static DWORD WINAPI worker(LPVOID param)
{
    (void)param;

    log_init();
    log_line("=== CFC2 Access ===");
    /* The build stamp: a log that cannot say which build wrote it cannot be
     * trusted to report on any change. Compare it before concluding anything. */
    log_line("build: compiled " __DATE__ " " __TIME__
             "  -- ⚠ if this predates the change you are testing, the game is "
             "running a STALE DLL (build.ps1 -Deploy).");
    log_line("worker thread started, pid=%lu", GetCurrentProcessId());

    BOOL spoke = speech_init();

    /* .text is ciphertext at DllMain and decrypts ~0.5 s later. Nothing may
     * scan, hook or read code before this succeeds. */
    BOOL unpacked = image_wait_for_unpack(60000);

    /* `cfc2_hook.off` next to the exe makes the whole mod read-only. */
    BOOL hook_enabled = !opt_in_file_present("cfc2_hook.off");
    if (unpacked && !hook_enabled)
        log_line("mod: every hook DISABLED by cfc2_hook.off next to the exe.");

    /* The menu cursor: both generic cursor helpers hand us the screen object
     * and a pointer to its cursor, so the selected row and count come free. */
    BOOL mc_ok = FALSE;
    if (unpacked && hook_enabled) {
        mc_ok = mc_install();
        if (!mc_ok)
            log_line("mc: install FAILED (reason %d) -- nothing was written to "
                     "game memory, and no menu movement is available.",
                     (int)mc_install_why());
    }

    if (unpacked && hook_enabled) cvs2_story_install();
    if (unpacked && hook_enabled) cvs2_text_install();
    /* The ` key sources, in priority order: registration order IS the order
     * they are asked. CVS2's text is captured by its renderer hooks above;
     * CVS1's, CFJ's and Alpha 3's by their frame hooks. */
    if (unpacked && hook_enabled) cvs2_text_register();
    if (unpacked && hook_enabled) cvs1_text_register();
    if (unpacked && hook_enabled) cfj_text_register();
    if (unpacked && hook_enabled) z3u_text_register();
    /* The story source first: during dialogue it is the line on screen. */
    if (unpacked && hook_enabled) mjus_story_register();
    if (unpacked && hook_enabled) mjus_text_register();
    /* The shell LAST, so a running game answers first. Not gated on a hook:
     * it repeats what was spoken, and a key that silently does nothing is
     * what a blind user cannot diagnose. */
    menu_speak_register_reader();

    /* Gauge cues, always on. The mixer no-ops if the device will not open. */
    if (unpacked) {
        wchar_t dir[MAX_PATH];
        DWORD dn = GetModuleFileNameW(NULL, dir, MAX_PATH);
        if (dn > 0 && dn < MAX_PATH) {
            wchar_t *slash = wcsrchr(dir, (wchar_t)0x5C);
            if (slash) *slash = 0;
            audio_init(dir);
        }
        log_line("audio: %s", audio_ready() ? "cues ON" : "device unavailable");
        log_line("audio: volume keys -- F5 health %d%%, F6 super %d%%, "
                 "F7 beacons %d%%, F8 clock tick %d%%. The key steps UP and "
                 "shift+key steps DOWN, wrapping 0/25/50/75/100, so one press "
                 "from 100%% mutes. Saved to cfc2_audio.ini.",
                 audio_cue_volume(AUDIO_CUE_HEALTH),
                 audio_cue_volume(AUDIO_CUE_SUPER),
                 audio_cue_volume(AUDIO_CUE_BEACON),
                 audio_cue_volume(AUDIO_CUE_TICK));
    }
    /* Every game's per-frame hook. Not gated on the audio device: each select
     * tick speaks only while its game's frame counter moves (frame_liveness.h)
     * and CVS1, CFJ and Alpha 3 capture their text there. */
    if (unpacked && hook_enabled) {
        cvs2_frame_install();
        cvs1_frame_install();
        cfj_frame_install();
        z3u_frame_install();
        mjus_frame_install();
        stg2_frame_install();
        pws1_frame_install();
        pws2_frame_install();
    }

    /* The GUI text hook: per-row captions and the hints view ride on it. */
    BOOL gt_ok = FALSE;
    if (unpacked && hook_enabled) {
        gt_ok = gt_install();
        if (!gt_ok)
            log_line("gt: install FAILED (reason %d) -- nothing was written to "
                     "game memory, and no GUI text element is available.",
                     (int)gt_install_why());
    }

    /* Row descriptions (the help-line setter) and every shell dialog (its
     * one opener), from the game's own fields. */
    if (unpacked && hook_enabled) {
        help_line_install();
        dialog_install();
    }

    if (spoke) {
        const wchar_t *backend = speech_backend();
        wchar_t msg[256];
        /* The language is not known yet this early; unknown speaks English. */
        mt_fmt(msg, 256, unpacked ? MT_READY : MT_NOT_UNPACKED,
               backend ? backend : mt(MT_NONE), NULL, NULL);
        speech_say(msg, TRUE);
    }
    log_line("startup: speech=%s unpacked=%s hooks=%s",
             spoke ? "YES" : "NO", unpacked ? "YES" : "NO",
             hook_enabled ? "on" : "OFF");

    int beat = 0;
    while (!InterlockedCompareExchange(&g_quit, 0, 0)) {
        /* 200 x 10 ms = the 2 s beat. The 10 ms tick is what makes speech
         * follow a held arrow key with no stutter; speech never moves into a
         * detour, because a stall on the render thread is a frozen game. */
        for (int t = 0; t < 200; t++) {
            if (InterlockedCompareExchange(&g_quit, 0, 0))
                break;
            Sleep(10);

            volume_hotkeys_tick();
            power_stone_hotkeys_tick();
            /* ` = read the current text, everywhere. VK_OEM_3 is the key left
             * of 1 on a US layout; only while the game has the foreground. */
            if (key_pressed(VK_OEM_3)) {
                DWORD foreground_pid = 0;
                GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
                if (foreground_pid == GetCurrentProcessId()) tc_request_read();
            }

            /* Follow the game's language for the mod's own words, once a second. */
            {
                static ULONGLONG last_lang;
                ULONGLONG now_lang = GetTickCount64();
                if (!last_lang || now_lang - last_lang >= 1000) {
                    last_lang = now_lang;
                    mt_update_language(gmd_text_language());
                }
            }
            /* Every game's selection, on the worker, through the same shared
             * channel; each tick admits only its own game. */
            cvs2_select_tick();
            cvs1_select_tick();
            cfj_select_tick();
            z3u_select_tick();
            mjus_select_tick();
            stg2_select_tick();
            pws1_select_tick();
            pws2_select_tick();
            mjus_story_tick();

            /* AFTER the select ticks: they tell each reader whether its game is
             * live, so asking first would answer "not active" for a game that
             * is on screen. */
            if (tc_read_requested()) tc_read_current();
            cvs2_frame_log_stats();
            cvs1_frame_log_stats();
            cfj_frame_log_stats();
            z3u_frame_log_stats();
            mjus_frame_log_stats();
            stg2_frame_log_stats();
            pws1_frame_log_stats();
            pws2_frame_log_stats();

            /* Screens with no cursor movement (dialogs, the hints view) are
             * polled here; a selection moved several times a second cannot be
             * watched on the 2 s beat. */
            if (gt_ok) {
                menu_speak_check_modal();
                menu_speak_check_hint();
            }

            /* The message table resolves incrementally (it is built during
             * load and moves between runs); until then rows speak position. */
            if (mc_ok) {
                gmd_text_ready();
                mc_poll_and_log();
            }
        }
        beat++;

        /* Liveness: every 5 beats, so a stopped worker is visible in the log. */
        if (beat <= 3 || beat % 5 == 0)
            log_line("heartbeat %d (alive %d s)", beat, beat * 2);

        /* A language changed in Options > System: re-resolve the table. Slow
         * beat only; it reads config.ini. */
        if (mc_ok && (beat == 10 || beat % 5 == 0))
            gmd_text_recheck_language();
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
