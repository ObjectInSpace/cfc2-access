/* See cvs2_frame.h for the derivation and the thread-safety rules. */

#include "cvs2_frame.h"
#include "cvs2_select.h"
#include "audio.h"
#include "match_audio.h"
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
static volatile LONG g_pauses;      /* pause episodes detected: liveness    */

/* Frame-thread only. Liveness for the health edge, independent of the
 * controller's own edge state (which resets on every owner change). */
static int g_last_hp[2] = { -1, -1 };

/* ⭐ The cue POLICY (change-gated gauges, pause, exit, clock warnings) lives in
 * match_audio.c now. This hook only DECODES CVS2's state into an owned sample
 * and hands it over under the current owner epoch, which the worker sets in
 * cvs2_select_tick(). Frame-thread only, so it needs no synchronisation. */
static match_audio_producer g_prod = {
    .feeds = MATCH_AUDIO_FEED_HEALTH | MATCH_AUDIO_FEED_BEACON |
             MATCH_AUDIO_FEED_CLOCK
};

/* CVS2's composite of everything that should be moving while the game
 * simulates. ⚠ Position alone is not enough: two fighters can stand still.
 * Animation frame (B+0x0D2, the field the old refuted X candidate turned out
 * to be) keeps ticking through idle animation, so it is the sensitive term.
 *
 * ⚠ The PHASE BYTES DO NOT DISTINGUISH PAUSE: a paused in-round capture reads
 * `02 01 02 02 04 ...`, identical to an unpaused one. And the frame counter at
 * phase+0x18 is incremented BY the hooked function, so it can never sit still
 * here -- the first pause attempt watched it and could never fire. */
static uint32_t motion_signature(const unsigned char *s)
{
    int xr[2];
    unsigned short tr;
    uint32_t sig;
    memcpy(&xr[0], s + B_OF(0) + OFF_POSX, 4);
    memcpy(&xr[1], s + B_OF(1) + OFF_POSX, 4);
    memcpy(&tr, s + OFF_TIMER, 2);
    sig = (unsigned)xr[0] ^ ((unsigned)xr[1] << 1) ^ ((unsigned)tr << 3);
    sig ^= (unsigned)s[B_OF(0) + 0xD2] << 11;
    sig ^= (unsigned)s[B_OF(1) + 0xD2] << 19;
    sig ^= (unsigned)s[B_OF(0) + 0xD3] << 5;
    sig ^= (unsigned)s[B_OF(1) + 0xD3] << 13;
    return sig;
}

/* ---- stance -> beacon pitch ---------------------------------------------
 * Knocked down wins: it is the lowest rung, and a downed fighter's 0x59A still
 * reads "grounded". Airborne scales CONTINUOUSLY with height, so a jump reads
 * as a rising-and-falling pitch. ~100 units is taken as a full-height jump
 * (81 was observed just past a peak); it only sets the rate, and is clamped. */
static float stance_of(const unsigned char *s, unsigned p)
{
    unsigned st = s[B_OF(p) + OFF_STANCE];
    unsigned act = s[B_OF(p) + OFF_ACTION];
    if (act == ACTION_DOWN) return AUDIO_STANCE_DOWN;
    if (st == STANCE_CROUCH) return AUDIO_STANCE_CROUCH;
    if (st == STANCE_AIR) {
        int yr;
        float h;
        memcpy(&yr, s + B_OF(p) + OFF_POSY, 4);
        h = (float)yr / 65536.0f / 100.0f;
        if (h < 0.0f) h = 0.0f;
        if (h > 1.0f) h = 1.0f;
        return AUDIO_STANCE_AIR + (AUDIO_STANCE_AIR_TOP - AUDIO_STANCE_AIR) * h;
    }
    return AUDIO_STANCE_STAND;
}

/* Decode one frame into an owned sample. Pure arithmetic on `s`; see the
 * header. Shared by the detour and the offline replay suite. */
void cvs2_frame_decode(const unsigned char *s, match_audio_sample *m)
{
    int xr[2], p;
    float x[2], mid, cam, lim = STAGE_HALF - SCREEN_HALF;
    unsigned short t;

    memset(m, 0, sizeof *m);
    /* ⛔⛔ ONLY INSIDE AN ACTUAL MATCH. Phase bytes 02 01 02 02; byte 4
     * carries the sub-state (03 intro / 04 in-round / 05 round end).
     *
     * Win/lose quotes are shown between matches (`winquote-01.bin` pins
     * 02 01 02 **03**). There R+0x184 reads a live-looking 74, so a value
     * guard would let round-clock warnings fire over a win quote. Guard on
     * the STATE, not on values; an observed exit silences once. */
    if (memcmp(s + 0x78, "\x02\x01\x02\x02", 4) != 0) return;
    m->in_match = true;
    m->motion_valid = true;
    m->motion_signature = motion_signature(s);

    for (p = 0; p < 2; p++) {
        unsigned short v, mx;
        match_audio_owner *o = &m->owner[p];
        o->present = true;
        memcpy(&v, s + B_OF(p) + OFF_HP, 2);
        memcpy(&mx, s + B_OF(p) + OFF_MAXHP, 2);
        /* ⚠ 0xFFFF is the KO/round-end sentinel, not a value. */
        o->health.valid = mx > 0 && mx != 0xFFFF && v != 0xFFFF && v <= mx;
        o->health.value = v;
        o->health.maximum = mx;
        memcpy(&xr[p], s + B_OF(p) + OFF_POSX, 4);
        x[p] = (float)xr[p] / 65536.0f;      /* 16.16 -> units */
    }

    /* screen_x = world_x - clamp(midpoint, +/-(STAGE_HALF-SCREEN_HALF)),
     * pan = screen_x / SCREEN_HALF. The camera tracks the midpoint; see the
     * note above OFF_POSX for why no camera field is needed. */
    mid = (x[0] + x[1]) * 0.5f;
    cam = mid < -lim ? -lim : (mid > lim ? lim : mid);
    for (p = 0; p < 2; p++) {
        float pan = (x[p] - cam) / SCREEN_HALF;
        if (pan < -1.0f) pan = -1.0f;
        if (pan >  1.0f) pan =  1.0f;
        m->owner[p].beacon_valid = true;
        m->owner[p].screen_pan = pan;
        m->owner[p].stance = stance_of(s, (unsigned)p);
    }

    /* R+0x184, confirmed live: counts down in Arcade, frozen at 99 in
     * Training. Read HERE so a warning tick lands on the frame the second
     * changes. */
    memcpy(&t, s + OFF_TIMER, 2);
    m->clock_valid = true;
    m->clock = t;
}

static void frame_detour(void)
{
    InterlockedIncrement(&g_frames);

    /* One pointer read, one interlocked read, then plain arithmetic. No
     * syscalls on this thread. */
    uint64_t epoch = match_audio_current_for(1);   /* CVS2 is game 1 */
    ULONGLONG root = *(volatile ULONGLONG *)(ULONG_PTR)ROOT_PTR;
    if (epoch && root && root < 0x00007FFF00000000ull) {
        match_audio_sample m;
        int p;
        cvs2_frame_decode((const unsigned char *)(ULONG_PTR)root, &m);
        for (p = 0; p < 2; p++) {
            int v = m.in_match ? m.owner[p].health.value : -1;
            if (v >= 0 && g_last_hp[p] != v) InterlockedIncrement(&g_changes);
            g_last_hp[p] = v;
        }
        match_audio_feed(&g_prod, epoch, &m);
        InterlockedExchange(&g_pauses, (LONG)g_prod.pauses);
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
