/* See cvs2_frame.h for the derivation and the thread-safety rules. */

#include "cvs2_frame.h"
#include "cvs2_select.h"
#include "audio.h"
#include "log.h"

#include "../libs/minhook/include/MinHook.h"

#include <string.h>

#define FRAME_FN   0x1406AA7A0ull
#define ROOT_PTR   0x1422DEBD0ull
/* R+0x78, published by the initializer. The hooked function increments a
 * frame counter at +0x18 from here (0x1406AA7BB); a stalled counter is how
 * pause is detected. See docs/cvs2-state-access-trace.md. */
#define PHASE_PTR  0x1422DEC30ull
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

/* ---- fighter position, for beacon pan ----------------------------------
 * B+0x54 is world X in 16.16 fixed point, traced to the motion routine at
 * 0x140708F50 (`pos += vel; vel += accel`) and corroborated by the captures:
 * fighters start at exactly -100.00 / +100.00 and the stage walls sit at
 * +/-352.00.
 *
 * ⚠ SIGNED 32-BIT. Read as u16 it wraps to ~65000 across the whole left half
 * of the stage -- the width misreading that produced the refuted B+0x0D8
 * "pointer as coordinate" result. Use int32_t and nothing else.
 *
 * ⭐⭐ WHY THERE IS NO CAMERA FIELD HERE. Pan needs SCREEN x, not world x,
 * and no camera origin was found (see the trace: 3 subtraction sites, all
 * object-relative placement; zero R fields correlate with the fighters'
 * midpoint). It is not needed. In a 2D fighter the camera IS a function of
 * the fighters -- it tracks their midpoint, clamped at the stage edges,
 * because that is what keeps both on screen. So screen position follows from
 * the two X values alone.
 *
 * Validated against all 12 in-round captures: no fighter ever lands off
 * screen, and corner captures land at exactly +/-1.00. Neither was forced by
 * the arithmetic. */
#define OFF_POSX   0x54u
#define OFF_POSY   0x58u   /* height; confirmed live at 81.38 mid-jump */
#define STAGE_HALF   352.0f   /* MEASURED: 3 readings pin to exactly +/-352 */
#define SCREEN_HALF  192.0f   /* CVS2 renders 384 wide                      */

/* ---- stance ------------------------------------------------------------
 * B+0x59A, found by diffing a user-labelled CROUCH capture and an AIRBORNE
 * one (paused mid-jump) against ten standing captures:
 *
 *     0 = grounded/standing     1 = airborne     2 = crouching
 *
 * ⭐ THE FALSIFIABLE CHECK that makes it shippable: value 1 occurs in
 * exactly the captures where height B+0x58 > 0, and never otherwise. Height
 * was independently confirmed by the same jump capture (P1 y = 81.38 with a
 * small downward velocity -- just past the peak -- while grounded P2 read 0).
 * Two independent fields agreeing across 26 player-captures.
 *
 * ⚠ THE SAME VALUE APPEARS AT 0x59A, 0x5A6 AND 0x5B2 (0xC apart, one value
 * replicated in a small array). All three agree in all 26 captures; 0x59A is
 * used. If one ever disagrees, they are not redundant and this needs redoing.
 *
 * ⛔ KNOCKDOWN IS NOT IN THIS BYTE. A user-labelled knockdown capture
 * (`knockdown-01`, P2 down after a hit) reads 0x59A = 0 -- the same as
 * standing -- so this field does not have a fourth value. That is a clean
 * negative from a real capture, not an assumption.
 *
 * ---- action state, B+0x007 ---------------------------------------------
 * The knockdown capture identified a SEPARATE field. Across 14 in-round
 * captures it reads 0 (not in a match), 1 (normal), 2 (crouching, P1 in the
 * crouch capture), 3 (P2 while knocked down) and 5 (twice, in captures where
 * nobody was reported knocked down).
 *
 * ⚠⚠ ONLY VALUE 3 IS EVIDENCED, FROM ONE CAPTURE. This is an ACTION state,
 * not a stance ladder: value 2 duplicates information 0x59A already carries,
 * and 5 appears with damage but so does 1, so "5 = hurt" is NOT supported.
 * Do not build a fuller interpretation on this without more labelled
 * captures -- a single sample is exactly the evidence that mislabelled
 * health as the super meter.
 *
 * So: value 3 maps to DOWN, everything else defers to 0x59A. A wrong stance
 * is worse than a missing one, and this is the narrowest claim the evidence
 * supports. */
#define OFF_STANCE 0x59Au
#define STANCE_GROUND 0u
#define STANCE_AIR    1u
#define STANCE_CROUCH 2u

#define OFF_ACTION 0x007u
#define ACTION_DOWN   3u

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
/* Pause detection: the game's own frame counter and how long it has sat
 * still. Frame-thread only, so no synchronisation is needed. */
static unsigned g_last_counter;
static int g_static_frames;
static int g_paused_now;
static volatile LONG g_pauses;      /* pause episodes detected: liveness */
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
                audio_beacons_off();
            }
            g_last_hp[0] = g_last_hp[1] = -1;
            g_last_timer = -1;
            /* A stale counter must not read as "paused" on the next match. */
            g_static_frames = 0;
            g_last_counter = 0;
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

        /* ---- the two position beacons -----------------------------------
         * screen_x = world_x - clamp(midpoint, +/-(STAGE_HALF-SCREEN_HALF))
         * pan      = screen_x / SCREEN_HALF, clamped to -1..+1.
         *
         * ⭐ FED EVERY FRAME, unlike the gauges. A beacon is a CONTINUOUS
         * position channel: it must sound whether or not the fighter moved,
         * so there is no change rule and no hold timer to re-arm. The mixer
         * holds the last value, so feeding an unchanged one is harmless.
         *
         * ⚠ Affordable only because audio_beacon_set() is LOCK-FREE. The
         * gauge feed above is gated on change precisely because it takes the
         * mixer's critical section, and 120 locked calls per second from this
         * thread is what made the health tone crackle. Do not "optimise" this
         * into the gauge path, and do not give audio_beacon_set() a lock. */
        /* ⭐ PAUSE SUPPRESSION (user request, 2026-09-19): the beacons kept
         * sounding over the pause menu. A paused game is not simulating, so
         * the position the beacons report is frozen and conveys nothing --
         * and a droning pair of tones over a menu is noise.
         *
         * ⚠ The PHASE BYTES DO NOT DISTINGUISH PAUSE: a paused in-round
         * capture reads `02 01 02 02 04 ...`, identical to an unpaused one.
         *
         * ⛔⛔ MY FIRST ATTEMPT WATCHED THE FRAME COUNTER AT phase+0x18 AND
         * COULD NEVER HAVE WORKED. The hooked function is the one that
         * INCREMENTS that counter (0x1406AA7BB, `inc dword [rax+0x18]`), so
         * by the time our detour runs the counter has always just advanced --
         * `ctr == g_last_counter` is essentially never true. The check was
         * self-defeating by construction, and the user duly reported the
         * beacons still playing on pause.
         *
         * ⭐ A signal generated by the very function you hook cannot tell you
         * that function is idle. Watch something the SIMULATION advances, not
         * something the hook itself advances.
         *
         * So: watch state that only moves while the game is simulating --
         * both fighters' positions and the round timer. If NONE of them has
         * changed for ~30 frames, the game is not simulating. Fighters are
         * near-constantly in motion (gravity, idle animation, walking), so a
         * genuine standstill that long is a pause, not gameplay.
         *
         * ⚠ 30 frames (~0.5 s) is deliberately generous: a false positive
         * silences the beacons mid-fight, which is far worse than a beacon
         * running half a second into a pause. */
        {
            int running = 1;
            int xr[2];
            unsigned short tr;
            unsigned sig;
            memcpy(&xr[0], s + B_OF(0) + OFF_POSX, 4);
            memcpy(&xr[1], s + B_OF(1) + OFF_POSX, 4);
            memcpy(&tr, s + OFF_TIMER, 2);
            /* A cheap composite of everything that should be moving.
             * ⚠ Position alone is not enough: two fighters can stand
             * perfectly still. Animation frame (B+0x0D2, the field the old
             * refuted X candidate turned out to be) keeps ticking through
             * idle animation, so it is the sensitive term here. */
            sig = (unsigned)xr[0] ^ ((unsigned)xr[1] << 1) ^ ((unsigned)tr << 3);
            sig ^= (unsigned)s[B_OF(0) + 0xD2] << 11;
            sig ^= (unsigned)s[B_OF(1) + 0xD2] << 19;
            sig ^= (unsigned)s[B_OF(0) + 0xD3] << 5;
            sig ^= (unsigned)s[B_OF(1) + 0xD3] << 13;
            if (sig == g_last_counter) {
                if (g_static_frames < 1000) g_static_frames++;
            } else {
                g_static_frames = 0;
                g_last_counter = sig;
            }
            running = g_static_frames < 30;
            /* ⭐ LIVENESS FOR THE SUPPRESSION ITSELF. The first attempt could
             * never fire and nothing said so -- the log could not distinguish
             * "never paused" from "pause never detected". Count both edges so
             * the next run answers it directly. */
            if (!running && g_paused_now == 0) {
                g_paused_now = 1;
                InterlockedIncrement(&g_pauses);
            } else if (running && g_paused_now) {
                g_paused_now = 0;
            }
            if (!running) {
                audio_beacons_off();
                g_real_frame();
                return;
            }
        }

        {
            int xr[2];
            float x[2], mid, cam, lim = STAGE_HALF - SCREEN_HALF;
            for (p = 0; p < 2; p++) {
                memcpy(&xr[p], s + B_OF(p) + OFF_POSX, 4);
                x[p] = (float)xr[p] / 65536.0f;      /* 16.16 -> units */
            }
            mid = (x[0] + x[1]) * 0.5f;
            cam = mid < -lim ? -lim : (mid > lim ? lim : mid);
            for (p = 0; p < 2; p++) {
                float pan = (x[p] - cam) / SCREEN_HALF;
                if (pan < -1.0f) pan = -1.0f;
                if (pan >  1.0f) pan =  1.0f;
                /* ---- stance -> beacon pitch --------------------------
                 * ⭐ Airborne scales CONTINUOUSLY with height rather than
                 * snapping to one "in the air" value: a jump then reads as a
                 * rising-and-falling pitch, which is the arc itself. The
                 * ladder's AIR constant is the floor of that range.
                 *
                 * ⚠ 81 units was the observed height just past a jump's peak,
                 * so ~100 is taken as a full-height jump for scaling. This
                 * only sets how fast pitch rises, not the direction, and it
                 * is clamped -- a super-jump simply reaches the top. */
                {
                    unsigned st = s[B_OF(p) + OFF_STANCE];
                    unsigned act = s[B_OF(p) + OFF_ACTION];
                    float stance = AUDIO_STANCE_STAND;
                    /* Knocked down wins: it is the lowest rung, and a downed
                     * fighter's 0x59A still reads "grounded". */
                    if (act == ACTION_DOWN) {
                        stance = AUDIO_STANCE_DOWN;
                    } else if (st == STANCE_CROUCH) {
                        stance = AUDIO_STANCE_CROUCH;
                    } else if (st == STANCE_AIR) {
                        int yr;
                        float h;
                        memcpy(&yr, s + B_OF(p) + OFF_POSY, 4);
                        h = (float)yr / 65536.0f / 100.0f;   /* 0..1-ish */
                        if (h < 0.0f) h = 0.0f;
                        if (h > 1.0f) h = 1.0f;
                        stance = AUDIO_STANCE_AIR +
                                 (AUDIO_STANCE_AIR_TOP - AUDIO_STANCE_AIR) * h;
                    }
                    audio_beacon_set((unsigned)p, pan, stance);
                }
            }
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
    log_line("frame: %ld frames (%.0f/s), %ld health-change(s), %ld pause(s)",
             f, (double)(f - last_frames) * 1000.0 / (double)(now - last_tick),
             c - last_changes, (long)g_pauses);
    last_tick = now; last_frames = f; last_changes = c;
}
