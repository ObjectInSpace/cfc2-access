/* See z3u_frame.h. Structure follows cfj_frame.c: one pointer read and plain
 * arithmetic on the game's frame thread, no syscalls, no locks. */

#include "z3u_frame.h"
#include "audio.h"
#include "z3u_text.h"
#include "log.h"

#include "../libs/minhook/include/MinHook.h"

#include <string.h>

#define FRAME_FN   0x14086D160ull   /* update; its only caller is the per-frame
                                       callback 0x14086BE70 (call at 0x14086BE79) */
#define ROOT_PTR   0x1422FB960ull   /* published by setup 0x14086CFC0 */
/* mov [rsp+8],rbx ; mov [rsp+0x10],rbp ; mov [rsp+0x18],rsi ; push rdi ;
 * sub rsp,0x60 ; call 0x14087DFC0 */
static const unsigned char FRAME_SIG[] = {
    0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10, 0x48,0x89,0x74,0x24,0x18,
    0x57, 0x48,0x83,0xEC,0x60, 0xE8,0x47,0x0E,0x01,0x00
};

#define OBJ(p)      (0x1B0u + (p) * 0x550u)
#define OFF_HP      0x88u
#define OFF_GAUGE   0x1EEu
#define OFF_POSX    0x10u
#define OFF_POSY    0x14u
#define HP_MAX      0x90
#define GAUGE_MAX   0x90
/* Mode words R+0x28 (game task), +0x2A (game mode), +0x2C (screen): 1, 2, 3
 * in every match sample; select is 1, 1, 2. Round stage R+0x2E: 2 intro
 * (health refills and the gauge fills), 3 fighting, 4 KO, 5 round over,
 * then 0 and 1 while the next round is set up. */
#define MODE_OFF    0x28u
#define STAGE_OFF   0x2Eu
#define CLOCK_OFF   0xF1u
#define CAMERA_OFF  0xC55Au
/* The game's own on-screen X (0x140872550): object+0x12 - camera, on a
 * 384-pixel screen. Fighters stop 24 pixels inside each edge. */
#define SCREEN_HALF 192.0f
#define GROUND      40.0f

/* Attract walker: the opening screens (task modes 1 and 2, 0x14089A540 and
 * 0x14089A650) print through it, and it draws and CLEARS each record inside
 * the update, so the text is collected on its entry. mov rax,rsp ;
 * mov [rax+0x10],rbx ; mov [rax+0x18],rbp ; mov [rax+0x20],rsi */
#define WALKER_FN  0x1408797D0ull
static const unsigned char WALKER_SIG[] = {
    0x48,0x8B,0xC4, 0x48,0x89,0x58,0x10, 0x48,0x89,0x68,0x18, 0x48,0x89,0x70,0x20
};

typedef void (*pfn_frame)(void);
static pfn_frame g_real_frame, g_real_walker;
static volatile LONG g_walker_installed;
static volatile LONG g_frames, g_changes, g_installed, g_pauses;
static int g_last_hp[2] = { -1, -1 };
static match_audio_producer g_prod = {
    .feeds = MATCH_AUDIO_FEED_HEALTH | MATCH_AUDIO_FEED_RESOURCE |
             MATCH_AUDIO_FEED_BEACON | MATCH_AUDIO_FEED_CLOCK
};

static int i32(const unsigned char *s, unsigned off) { int v; memcpy(&v, s + off, 4); return v; }
static int i16(const unsigned char *s, unsigned off) { short v; memcpy(&v, s + off, 2); return v; }

void z3u_frame_decode(const unsigned char *s, match_audio_sample *m)
{
    unsigned p, stage;
    bool live;

    memset(m, 0, sizeof *m);
    if (i16(s, MODE_OFF) == 0 || i16(s, MODE_OFF + 2) != 2 || i16(s, MODE_OFF + 4) != 3) return;
    m->in_match = true;
    stage = (unsigned)i16(s, STAGE_OFF);
    /* Health refills and the gauge animates up during the intro, so the
     * gauges are fed only from fighting to round over; outside that they
     * reseed silently. */
    live = stage >= 3 && stage <= 5;

    /* Pause: the whole block freezes (the update does not run), which the
     * worker's stall check covers. The signature uses positions plus +0x20
     * and +0x52, which change in 40-60% of samples during play. */
    m->motion_valid = true;
    for (p = 0; p < 2; p++) {
        unsigned a = OBJ(p);
        m->motion_signature ^= (uint32_t)i32(s, a + OFF_POSX) << p;
        m->motion_signature ^= (uint32_t)i32(s, a + OFF_POSY) << (p + 3);
        m->motion_signature ^= (uint32_t)s[a + 0x20] << (8 + 8 * p);
        m->motion_signature ^= (uint32_t)s[a + 0x52] << (12 + 8 * p);
    }

    for (p = 0; p < 2; p++) {
        unsigned a = OBJ(p);
        int hp = i16(s, a + OFF_HP), gauge = i16(s, a + OFF_GAUGE);
        int sx = i16(s, a + OFF_POSX + 2) - i16(s, CAMERA_OFF);
        float pan, h;
        match_audio_owner *o = &m->owner[p];
        o->present = true;
        /* -1 at a KO is a sentinel, never a value. */
        o->health.valid = live && hp >= 0 && hp <= HP_MAX;
        o->health.value = hp;
        o->health.maximum = HP_MAX;
        o->resource.valid = live && gauge >= 0 && gauge <= GAUGE_MAX;
        o->resource.value = gauge;
        o->resource.maximum = GAUGE_MAX;

        pan = ((float)sx - SCREEN_HALF) / SCREEN_HALF;
        if (pan < -1.0f) pan = -1.0f;
        if (pan >  1.0f) pan =  1.0f;
        o->beacon_valid = true;
        o->screen_pan = pan;
        /* Airborne from height above the fixed ground. */
        h = (float)i32(s, a + OFF_POSY) / 65536.0f - GROUND;
        if (h > 0.0f) {
            h /= 100.0f;
            if (h > 1.0f) h = 1.0f;
            o->stance = AUDIO_STANCE_AIR + (AUDIO_STANCE_AIR_TOP - AUDIO_STANCE_AIR) * h;
        } else {
            o->stance = AUDIO_STANCE_STAND;
        }
    }
    {
        int clock = s[CLOCK_OFF];
        m->clock_valid = clock <= 99;
        m->clock = clock;
    }
}

static void frame_detour(void)
{
    InterlockedIncrement(&g_frames);
    uint64_t epoch = match_audio_current_for(3);   /* Alpha 3 is game 3 */
    ULONGLONG root = *(volatile ULONGLONG *)(ULONG_PTR)ROOT_PTR;
    if (epoch && root && root < 0x00007FFF00000000ull) {
        match_audio_sample m;
        z3u_frame_decode((const unsigned char *)(ULONG_PTR)root, &m);
        for (int p = 0; p < 2; p++) {
            int v = m.in_match && m.owner[p].health.valid ? m.owner[p].health.value : -1;
            if (v >= 0 && g_last_hp[p] != v) InterlockedIncrement(&g_changes);
            g_last_hp[p] = v;
        }
        match_audio_feed(&g_prod, epoch, &m);
        InterlockedExchange(&g_pauses, (LONG)g_prod.pauses);
    }
    g_real_frame();
    /* The update has drawn its text queue: copy it now. */
    z3u_text_capture();
}

static void walker_detour(void)
{
    z3u_text_collect();
    g_real_walker();
}

static BOOL beside_exe(const char *leaf)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    char *slash;
    if (n == 0 || n >= MAX_PATH) return FALSE;
    slash = strrchr(path, 0x5C);
    if (!slash) return FALSE;
    slash[1] = 0;
    strncat(path, leaf, MAX_PATH - strlen(path) - 1);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

BOOL z3u_frame_install(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (beside_exe("cfc2_hook.off")) {
        log_line("z3u-frame: disabled by cfc2_hook.off");
        return FALSE;
    }
    if (!VirtualQuery((LPCVOID)(ULONG_PTR)FRAME_FN, &mbi, sizeof mbi) ||
        mbi.State != MEM_COMMIT) {
        log_line("z3u-frame: ⚠ COULD NOT read 0x%llx -- concluding nothing", FRAME_FN);
        return FALSE;
    }
    if (memcmp((const void *)(ULONG_PTR)FRAME_FN, FRAME_SIG, sizeof FRAME_SIG)) {
        log_line("z3u-frame: ⛔ entry bytes differ from the audited build; refusing");
        return FALSE;
    }
    if (MH_CreateHook((LPVOID)(ULONG_PTR)FRAME_FN, (LPVOID)frame_detour,
                      (LPVOID *)&g_real_frame) != MH_OK ||
        MH_EnableHook((LPVOID)(ULONG_PTR)FRAME_FN) != MH_OK) {
        log_line("z3u-frame: ⛔ MinHook refused 0x%llx", FRAME_FN);
        return FALSE;
    }
    InterlockedExchange(&g_installed, 1);
    log_line("z3u-frame: hooked Alpha 3's per-frame update at 0x%llx", FRAME_FN);
    /* Text only: the frame hook stands without it. */
    if (memcmp((const void *)(ULONG_PTR)WALKER_FN, WALKER_SIG, sizeof WALKER_SIG)) {
        log_line("z3u-frame: ⛔ attract walker bytes differ; opening-screen text is off");
    } else if (MH_CreateHook((LPVOID)(ULONG_PTR)WALKER_FN, (LPVOID)walker_detour,
                             (LPVOID *)&g_real_walker) != MH_OK ||
               MH_EnableHook((LPVOID)(ULONG_PTR)WALKER_FN) != MH_OK) {
        log_line("z3u-frame: ⛔ MinHook refused 0x%llx; opening-screen text is off", WALKER_FN);
    } else {
        InterlockedExchange(&g_walker_installed, 1);
        log_line("z3u-frame: hooked the attract text walker at 0x%llx", WALKER_FN);
    }
    return TRUE;
}

void z3u_frame_remove(void)
{
    if (InterlockedCompareExchange(&g_installed, 0, 1) == 1)
        MH_DisableHook((LPVOID)(ULONG_PTR)FRAME_FN);
    if (InterlockedCompareExchange(&g_walker_installed, 0, 1) == 1)
        MH_DisableHook((LPVOID)(ULONG_PTR)WALKER_FN);
}

LONG z3u_frame_count(void) { return InterlockedCompareExchange(&g_frames, 0, 0); }

/* Rate-limited by TIME (a counter that always changes cannot gate itself). */
void z3u_frame_log_stats(void)
{
    static ULONGLONG last_tick;
    static LONG last_frames, last_changes;
    ULONGLONG now = GetTickCount64();
    LONG f, c;
    if (last_tick == 0) { last_tick = now; return; }
    if (now - last_tick < 10000) return;
    f = g_frames; c = g_changes;
    if (f != last_frames)
        log_line("z3u-frame: %ld frames (%.0f/s), %ld health-change(s), %ld pause(s)",
                 f, (double)(f - last_frames) * 1000.0 / (double)(now - last_tick),
                 c - last_changes, (long)g_pauses);
    last_tick = now; last_frames = f; last_changes = c;
}
