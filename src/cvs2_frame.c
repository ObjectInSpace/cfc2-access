/* See cvs2_frame.h for the derivation and the thread-safety rules. */

#include "cvs2_frame.h"
#include "cvs2_select.h"
#include "audio.h"
#include "log.h"

#include "../libs/minhook/include/MinHook.h"

#include <string.h>

#define FRAME_FN   0x1406AA7A0ull
#define ROOT_PTR   0x1422DEBD0ull
/* mov [rsp+8],rbx ; mov [rsp+0x10],rsi ; push rdi ; sub rsp,0x20 ; call */
static const unsigned char FRAME_SIG[] = {
    0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x74,0x24,0x10, 0x57,
    0x48,0x83,0xEC,0x20, 0xE8
};

/* Offsets, all traced and live-verified. See docs/cvs2-state-access-trace.md. */
#define B_OF(p)    (0x1EBA8u + (p) * 0x678u)
#define OFF_HP     0x16Cu
#define OFF_MAXHP  0x170u
/* Round clock, an absolute offset in R (not per player). Confirmed live:
 * the user reported 99 on screen and this field read 99 in the capture taken
 * at that instant. Training freezes it at 99; Arcade counts down. */
#define OFF_TIMER  0x184u

typedef void (*pfn_frame)(void);
static pfn_frame g_real_frame;

static volatile LONG g_frames;      /* detour entries: liveness             */
static volatile LONG g_changes;     /* frames where health actually moved   */
static volatile LONG g_installed;

/* Published state, read by the audio layer. A seqlock: the writer bumps the
 * sequence to odd, writes, bumps to even. A reader retries while the sequence
 * is odd or changed across the read. No locks, so the frame thread never
 * blocks on the worker. */
static volatile LONG g_seq;
static struct { int hp[2], maxhp[2]; } g_pub;

static int g_last_hp[2] = { -1, -1 };
static int g_last_timer = -1;
/* Frame-thread only: tracks whether the previous frame was inside a match, so
 * the gauges are silenced exactly once on the way out rather than every frame. */
static int g_in_match;

static void frame_detour(void)
{
    InterlockedIncrement(&g_frames);

    /* One pointer read, then plain arithmetic. No syscalls on this thread. */
    ULONGLONG root = *(volatile ULONGLONG *)(ULONG_PTR)ROOT_PTR;
    if (root && root < 0x00007FFF00000000ull) {
        const unsigned char *s = (const unsigned char *)(ULONG_PTR)root;
        int hp[2], mx[2], changed[2], moved = 0, p;

        /* ⛔⛔ ONLY INSIDE AN ACTUAL MATCH. Phase bytes 02 01 02 02; byte 4
         * carries the sub-state (03 intro / 04 in-round / 05 round end).
         *
         * The user's point: win and lose quotes are shown BETWEEN matches, and
         * that window is an explicit SAFE TEARDOWN POINT -- state may be torn
         * down and rebuilt there. ⚠ They also said plainly that nothing was
         * misbehaving there; do not cite this guard as fixing a heard defect.
         * `winquote-01.bin`
         * pins that state as 02 01 02 **03** -- byte 3 = 03, a sibling of the
         * in-match state, and a value no in-match capture shows.
         *
         * ⚠ The health feed was saved only by accident: B+0x16C/B+0x170 read
         * 0/0 in that state, so the `mx > 0` gate happened to reject it. The
         * TIMER had no such luck -- R+0x184 reads a live-looking **74** there,
         * and this function fires audio_tick() on any decrease landing on
         * 30/15/1..5. Round-clock warnings during a win quote is exactly the
         * "cue at the wrong moment" failure the timer work warned about.
         *
         * ⭐ Guard on the STATE, not on the values. A value guard (`mx > 0`)
         * tests a side effect of the state and holds only while that side
         * effect happens to be true; this project has been bitten by proxy
         * tests before. Reset the edge trackers on the way out so re-entering
         * a match cannot fire a stale decrease. */
        if (memcmp(s + 0x78, "\x02\x01\x02\x02", 4) != 0) {
            /* Silence the gauges on the way OUT, once per departure. Without
             * this they hold their last in-match value and keep sounding over
             * the quote screen; with it the tones stop when the match does. */
            if (g_in_match) {
                g_in_match = 0;
                audio_gauge_reset_all();
            }
            g_last_hp[0] = g_last_hp[1] = -1;
            g_last_timer = -1;
            g_real_frame();
            return;
        }
        g_in_match = 1;

        for (p = 0; p < 2; p++) {
            unsigned b = B_OF(p);
            unsigned short v, m;
            memcpy(&v, s + b + OFF_HP,    2);
            memcpy(&m, s + b + OFF_MAXHP, 2);
            hp[p] = v;
            mx[p] = m;
            changed[p] = (g_last_hp[p] != (int)v);
            if (changed[p]) { g_last_hp[p] = (int)v; moved = 1; }
        }
        if (moved) InterlockedIncrement(&g_changes);

        /* Publish. Odd sequence = write in progress. */
        InterlockedIncrement(&g_seq);
        g_pub.hp[0] = hp[0]; g_pub.hp[1] = hp[1];
        g_pub.maxhp[0] = mx[0]; g_pub.maxhp[1] = mx[1];
        InterlockedIncrement(&g_seq);

        /* ⛔⛔ ONLY FEED THE MIXER WHEN THE VALUE ACTUALLY CHANGED.
         *
         * The first version called audio_gauge_set() unconditionally for both
         * players every frame: 120 calls/second, each taking the mixer's
         * CRITICAL SECTION, from the game's frame thread -- contending with
         * the render thread that needs the same lock every 5.8 ms. The user
         * heard that as crackling on the health tone ONLY, because health is
         * the only gauge fed from this hook; the meter goes through the 10 ms
         * worker poll and was clean.
         *
         * ⚠ I first blamed the pitch smoother and "fixed" it. Measuring the
         * smoother directly showed the coefficient changes NOTHING (7.2 Hz per
         * feed at every value from 0.010 to 0.0003) -- the pitch was never
         * involved. A symptom that appears on exactly one gauge points at what
         * is UNIQUE to that gauge, which here was the feed path, not the tone.
         *
         * Gating on change drops this to a handful of calls per second, and
         * loses nothing: an unchanged value would not re-arm the hold anyway. */
        for (p = 0; p < 2; p++) {
            if (!changed[p]) continue;
            if (mx[p] > 0 && hp[p] != 0xFFFF && hp[p] <= mx[p])
                audio_gauge_set(AUDIO_GAUGE_HEALTH, (unsigned)p, hp[p], mx[p]);
        }

        /* ---- round clock ------------------------------------------------
         * R+0x184, confirmed live: the user read 99 off the arcade timer and
         * this field read 99 in the capture taken at that moment. It counts
         * down in Arcade and sits frozen at 99 in Training.
         *
         * Warn at 30 and 15, then every second for the last five. Reading it
         * HERE means the tick lands on the frame the second changes, which is
         * the whole reason health moved to this hook. */
        {
            unsigned short t;
            memcpy(&t, s + OFF_TIMER, 2);
            if (t != (unsigned short)g_last_timer) {
                /* Only on a DECREASE: a round reset jumps the value upward
                 * and must not fire the whole warning set on its way past. */
                if (g_last_timer >= 0 && (int)t < g_last_timer) {
                    if (t == 30 || t == 15 || (t >= 1 && t <= 5))
                        audio_tick();
                }
                g_last_timer = (int)t;
            }
        }
    }

    g_real_frame();
}

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

BOOL cvs2_frame_install(void)
{
    MEMORY_BASIC_INFORMATION mbi;

    if (!beside_exe("cfc2_audio.on")) return FALSE;
    if (beside_exe("cfc2_hook.off")) {
        log_line("frame: disabled by cfc2_hook.off");
        return FALSE;
    }

    /* One VirtualQuery at INSTALL time is fine; the detour itself does none. */
    if (!VirtualQuery((LPCVOID)(ULONG_PTR)FRAME_FN, &mbi, sizeof mbi) ||
        mbi.State != MEM_COMMIT) {
        log_line("frame: ⚠ COULD NOT read 0x%llx -- concluding nothing", FRAME_FN);
        return FALSE;
    }
    if (memcmp((const void *)(ULONG_PTR)FRAME_FN, FRAME_SIG, sizeof FRAME_SIG)) {
        log_line("frame: ⛔ entry bytes differ from the audited build; refusing");
        return FALSE;
    }
    if (MH_CreateHook((LPVOID)(ULONG_PTR)FRAME_FN, (LPVOID)frame_detour,
                      (LPVOID *)&g_real_frame) != MH_OK ||
        MH_EnableHook((LPVOID)(ULONG_PTR)FRAME_FN) != MH_OK) {
        log_line("frame: ⛔ MinHook refused 0x%llx", FRAME_FN);
        return FALSE;
    }
    InterlockedExchange(&g_installed, 1);
    log_line("frame: hooked CVS2's per-frame update at 0x%llx -- health is now "
             "read ON the frame it changes, not on the worker poll.", FRAME_FN);
    return TRUE;
}

void cvs2_frame_remove(void)
{
    if (InterlockedCompareExchange(&g_installed, 0, 1) == 1)
        MH_DisableHook((LPVOID)(ULONG_PTR)FRAME_FN);
}

/* ⛔⛔ THIS LOGGED AT ~30 Hz AND MADE THE AUDIO CRACKLE. The guard was
 * `if (f == last_frames) return;` -- but `frames` increments every frame, so
 * it never suppressed anything: 3,087 lines, a THIRD of the whole log, each a
 * file write from the worker, starving the render thread that has only 23 ms
 * of buffer.
 *
 * ⭐ The tell I should have caught when writing it: a counter that changes
 * continuously cannot also be the thing that gates "has anything changed?".
 * Rate-limit by TIME, and report the derived number (frames per second) that
 * a human can actually act on. */
void cvs2_frame_log_stats(void)
{
    static ULONGLONG last_tick;
    static LONG last_frames, last_changes;
    ULONGLONG now = GetTickCount64();
    LONG f, c;

    if (last_tick == 0) { last_tick = now; return; }
    if (now - last_tick < 10000) return;          /* at most once per 10 s */

    f = g_frames; c = g_changes;
    /* Both numbers matter: frames proves the detour runs at all, changes
     * proves it is seeing real health movement. Either at zero means
     * something different is wrong. */
    log_line("frame: %ld frames (%.0f/s), %ld health-change(s)",
             f, (double)(f - last_frames) * 1000.0 / (double)(now - last_tick),
             c - last_changes);
    last_tick = now; last_frames = f; last_changes = c;
}
