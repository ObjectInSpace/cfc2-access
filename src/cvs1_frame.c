/* See cvs1_frame.h. Structure follows cvs2_frame.c: one pointer read and plain
 * arithmetic on the game's frame thread, no syscalls, no locks. */

#include "cvs1_frame.h"
#include "audio.h"
#include "log.h"

#include "../libs/minhook/include/MinHook.h"

#include <string.h>

#define FRAME_FN   0x1405A65F0ull   /* update, called once per frame by 0x1405A5C85 */
#define ROOT_PTR   0x1422D2B90ull   /* published by setup 0x1405A6440 */
/* sub rsp,0x28 ; call 0x140036B70 */
static const unsigned char FRAME_SIG[] = { 0x48,0x83,0xEC,0x28, 0xE8,0x77,0x05,0xA9,0xFF };

#define PHASE_OFF  0x3E58u
#define ACTOR(p)   (0x3FB8u + (p) * 0x700u)
#define OFF_HP     0x1FCu
#define OFF_MAXHP  0x200u
#define OFF_POSX   0xB4u
#define OFF_POSY   0xB8u
/* Power gauge, actor+0x2D1, u8. Gain paths clamp it at 0x90 when groove flag
 * actor+0x2C1 is 0 and at 0x64 otherwise (0x1405B7F3B..0x1405B7F78); level
 * checks compare 0x30/0x60/0x90. Arcade capture: +1 per hit, +4..6 per
 * special, -48 for a super. */
#define OFF_METER  0x2D1u
#define OFF_GROOVE 0x2C1u
/* Round clock, P+0x6C, signed 16-bit: set at round start from the time table
 * 0x141217D78 (30/60/99) indexed by P+0x43 (0x1405B1546, 0x1405B2B68); the
 * Arcade trace counts it down one per ~0.8 s and resets it to 99 per round. */
#define OFF_CLOCK  0x6Cu
/* Same world geometry as CVS2, measured, not assumed: fighters start at
 * exactly -100/+100 and the walls are at -352/+352 (snapshots 1, 8, 9 of
 * tools/fixtures/cvs1-match-20261013). The screen half-width is CVS2's; the
 * shared NAOMI presentation makes it the best available projection, and a
 * listening check confirms or corrects it. */
#define STAGE_HALF  352.0f
#define SCREEN_HALF 192.0f

typedef void (*pfn_frame)(void);
static pfn_frame g_real_frame;
static volatile LONG g_frames, g_changes, g_installed, g_pauses;
static int g_last_hp[2] = { -1, -1 };
static match_audio_producer g_prod = {
    .feeds = MATCH_AUDIO_FEED_HEALTH | MATCH_AUDIO_FEED_RESOURCE |
             MATCH_AUDIO_FEED_BEACON | MATCH_AUDIO_FEED_CLOCK
};

static int i32(const unsigned char *s, unsigned off) { int v; memcpy(&v, s + off, 4); return v; }
static int i16(const unsigned char *s, unsigned off) { short v; memcpy(&v, s + off, 2); return v; }

void cvs1_frame_decode(const unsigned char *s, match_audio_sample *m)
{
    float x[2], mid, cam, lim = STAGE_HALF - SCREEN_HALF;
    unsigned p;
    bool live;

    memset(m, 0, sizeof *m);
    /* In a match: phase 02 01 02 02, as CVS2. Every in-match snapshot has it;
     * the select screen reads 02 01 01 06. Guard on the state, not values. */
    if (memcmp(s + PHASE_OFF, "\x02\x01\x02\x02", 4) != 0) return;
    m->in_match = true;
    /* Phase byte 4: 4 intro, 5 fighting, 6 round over, 7 transition (Arcade
     * trace). Health refills and the gauge blips to 0 outside 5/6, so the
     * gauges are only valid while the round is live: outside it they reset
     * silently and reseed, instead of sweeping a tone at every round start. */
    live = s[PHASE_OFF + 4] == 5 || s[PHASE_OFF + 4] == 6;

    /* Pause: positions plus the animation counters +0x162/+0x164/+0x168,
     * which change in about half of all 20 Hz samples during play. */
    m->motion_valid = true;
    for (p = 0; p < 2; p++) {
        unsigned a = ACTOR(p);
        m->motion_signature ^= (uint32_t)i32(s, a + OFF_POSX) << p;
        m->motion_signature ^= (uint32_t)i32(s, a + OFF_POSY) << (p + 3);
        m->motion_signature ^= (uint32_t)s[a + 0x162] << (8 + 8 * p);
        m->motion_signature ^= (uint32_t)s[a + 0x164] << (4 + 8 * p);
        m->motion_signature ^= (uint32_t)s[a + 0x168] << (12 + 8 * p);
    }

    for (p = 0; p < 2; p++) {
        unsigned a = ACTOR(p);
        int hp = i16(s, a + OFF_HP), mx = i16(s, a + OFF_MAXHP);
        match_audio_owner *o = &m->owner[p];
        o->present = true;
        /* 0x1405B4E3B writes -1 here: a sentinel, never a value. */
        o->health.valid = live && mx > 0 && hp >= 0 && hp <= mx;
        o->health.value = hp;
        o->health.maximum = mx;
        x[p] = (float)i32(s, a + OFF_POSX) / 65536.0f;
        {
            int cap = s[a + OFF_GROOVE] ? 0x64 : 0x90, meter = s[a + OFF_METER];
            o->resource.valid = live && meter <= cap;
            o->resource.value = meter;
            o->resource.maximum = cap;
        }
    }
    {
        int clock = i16(s, PHASE_OFF + OFF_CLOCK);
        m->clock_valid = clock >= 0 && clock <= 99;
        m->clock = clock;
    }

    mid = (x[0] + x[1]) * 0.5f;
    cam = mid < -lim ? -lim : (mid > lim ? lim : mid);
    for (p = 0; p < 2; p++) {
        float pan = (x[p] - cam) / SCREEN_HALF, h;
        if (pan < -1.0f) pan = -1.0f;
        if (pan >  1.0f) pan =  1.0f;
        m->owner[p].beacon_valid = true;
        m->owner[p].screen_pan = pan;
        /* Airborne from height alone (the motion routine's own field), scaled
         * so a jump reads as a rising and falling pitch. Crouch and knockdown
         * are not traced: standing pitch, as the audio design requires. */
        h = (float)i32(s, ACTOR(p) + OFF_POSY) / 65536.0f;
        if (h > 0.0f) {
            h /= 100.0f;
            if (h > 1.0f) h = 1.0f;
            m->owner[p].stance = AUDIO_STANCE_AIR + (AUDIO_STANCE_AIR_TOP - AUDIO_STANCE_AIR) * h;
        } else {
            m->owner[p].stance = AUDIO_STANCE_STAND;
        }
    }
}

static void frame_detour(void)
{
    InterlockedIncrement(&g_frames);
    uint64_t epoch = match_audio_current_for(0);   /* CVS1 is game 0 */
    ULONGLONG root = *(volatile ULONGLONG *)(ULONG_PTR)ROOT_PTR;
    if (epoch && root && root < 0x00007FFF00000000ull) {
        match_audio_sample m;
        cvs1_frame_decode((const unsigned char *)(ULONG_PTR)root, &m);
        for (int p = 0; p < 2; p++) {
            int v = m.in_match && m.owner[p].health.valid ? m.owner[p].health.value : -1;
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
    char *slash;
    if (n == 0 || n >= MAX_PATH) return FALSE;
    slash = strrchr(path, 0x5C);
    if (!slash) return FALSE;
    slash[1] = 0;
    strncat(path, leaf, MAX_PATH - strlen(path) - 1);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

BOOL cvs1_frame_install(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (beside_exe("cfc2_hook.off")) {
        log_line("cvs1-frame: disabled by cfc2_hook.off");
        return FALSE;
    }
    if (!VirtualQuery((LPCVOID)(ULONG_PTR)FRAME_FN, &mbi, sizeof mbi) ||
        mbi.State != MEM_COMMIT) {
        log_line("cvs1-frame: ⚠ COULD NOT read 0x%llx -- concluding nothing", FRAME_FN);
        return FALSE;
    }
    if (memcmp((const void *)(ULONG_PTR)FRAME_FN, FRAME_SIG, sizeof FRAME_SIG)) {
        log_line("cvs1-frame: ⛔ entry bytes differ from the audited build; refusing");
        return FALSE;
    }
    if (MH_CreateHook((LPVOID)(ULONG_PTR)FRAME_FN, (LPVOID)frame_detour,
                      (LPVOID *)&g_real_frame) != MH_OK ||
        MH_EnableHook((LPVOID)(ULONG_PTR)FRAME_FN) != MH_OK) {
        log_line("cvs1-frame: ⛔ MinHook refused 0x%llx", FRAME_FN);
        return FALSE;
    }
    InterlockedExchange(&g_installed, 1);
    log_line("cvs1-frame: hooked CVS1's per-frame update at 0x%llx", FRAME_FN);
    return TRUE;
}

void cvs1_frame_remove(void)
{
    if (InterlockedCompareExchange(&g_installed, 0, 1) == 1)
        MH_DisableHook((LPVOID)(ULONG_PTR)FRAME_FN);
}

LONG cvs1_frame_count(void) { return InterlockedCompareExchange(&g_frames, 0, 0); }

/* Rate-limited by TIME (a counter that always changes cannot gate itself). */
void cvs1_frame_log_stats(void)
{
    static ULONGLONG last_tick;
    static LONG last_frames, last_changes;
    ULONGLONG now = GetTickCount64();
    LONG f, c;
    if (last_tick == 0) { last_tick = now; return; }
    if (now - last_tick < 10000) return;
    f = g_frames; c = g_changes;
    if (f != last_frames)
        log_line("cvs1-frame: %ld frames (%.0f/s), %ld health-change(s), %ld pause(s)",
                 f, (double)(f - last_frames) * 1000.0 / (double)(now - last_tick),
                 c - last_changes, (long)g_pauses);
    last_tick = now; last_frames = f; last_changes = c;
}
