/* See cfj_frame.h. Structure follows cvs1_frame.c: one pointer read and plain
 * arithmetic on the game's frame thread, no syscalls, no locks. */

#include "cfj_frame.h"
#include "audio.h"
#include "cfj_text.h"
#include "log.h"

#include "../libs/minhook/include/MinHook.h"

#include <string.h>

#define FRAME_FN   0x1407CB8B0ull   /* update; its only caller is the per-frame
                                       callback 0x1407CAEE0 (call at 0x1407CAEEE) */
#define ROOT_PTR   0x1422F3A80ull   /* published by setup 0x1407CB660 */
/* sub rsp,0x28 ; call 0x1407D1020 */
static const unsigned char FRAME_SIG[] = { 0x48,0x83,0xEC,0x28, 0xE8,0x67,0x57,0x00,0x00 };

#define FIGHTER(p) (0x1E678u + (p) * 0x660u)
#define OFF_HP     0x16Cu
#define OFF_MAXHP  0x170u
#define OFF_POSX   0x54u
#define OFF_POSY   0x58u
/* Power gauge, fighter+0x294, signed 16-bit. The gain routine 0x1407E6033
 * dispatches on gauge type fighter+0x221 (0..5, bounded by `cmp eax,5`)
 * through the table at 0x1407E625C; each branch clamps at its own cap. */
#define OFF_METER  0x294u
#define OFF_MTYPE  0x221u
static const int METER_CAP[6] = { 0x73, 0xAA, 0xC8, 0xF0, 0xB4, 0xD2 };
/* Phase chain R+0xC8: 02 01 01 01 on character select, 02 01 02 02 in a
 * match (both captures). Round stage R+0xCC: 3 intro, 4 fighting, 5..7 KO and
 * round over, 8 transition, then 0..3 while the next round is set up. */
#define PHASE_OFF  0xC8u
#define STAGE_OFF  0xCCu
/* Round clock R+0x1D4, u8: 99 at round start, one step about every second
 * (capture only; the decrement instruction is not located, as for CVS1). */
#define OFF_CLOCK  0x1D4u
/* Same world geometry as CVS1 and CVS2, measured: fighters start at exactly
 * -100/+100 and the walls are at -352/+352 (tools/fixtures/cfj-match-20261014
 * snapshots 1, 10, 11). The screen half-width is CVS2's assumption; a
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

void cfj_frame_decode(const unsigned char *s, match_audio_sample *m)
{
    float x[2], mid, cam, lim = STAGE_HALF - SCREEN_HALF;
    unsigned p;
    bool live;

    memset(m, 0, sizeof *m);
    if (memcmp(s + PHASE_OFF, "\x02\x01\x02\x02", 4) != 0) return;
    m->in_match = true;
    /* Health refills (and the next fighter comes in) and the gauge blips while
     * the next round is set up, so the gauges are fed only from the start of
     * fighting to the end of the KO: outside it they reseed silently. */
    live = s[STAGE_OFF] >= 4 && s[STAGE_OFF] <= 7;

    /* Pause: positions plus fighter bytes +0xCE/+0xD0/+0xD8/+0x246, which
     * change in 20-80% of all 20 Hz samples during play; the whole block is
     * frozen while paused. */
    m->motion_valid = true;
    for (p = 0; p < 2; p++) {
        unsigned a = FIGHTER(p);
        m->motion_signature ^= (uint32_t)i32(s, a + OFF_POSX) << p;
        m->motion_signature ^= (uint32_t)i32(s, a + OFF_POSY) << (p + 3);
        m->motion_signature ^= (uint32_t)s[a + 0xCE] << (8 + 8 * p);
        m->motion_signature ^= (uint32_t)s[a + 0xD0] << (4 + 8 * p);
        m->motion_signature ^= (uint32_t)s[a + 0xD8] << (12 + 8 * p);
        m->motion_signature ^= (uint32_t)s[a + 0x246] << (16 + 8 * p);
    }

    for (p = 0; p < 2; p++) {
        unsigned a = FIGHTER(p);
        int hp = i16(s, a + OFF_HP), mx = i16(s, a + OFF_MAXHP);
        int type = (signed char)s[a + OFF_MTYPE], meter = i16(s, a + OFF_METER);
        match_audio_owner *o = &m->owner[p];
        o->present = true;
        /* 0x1407E640D writes -1 at a KO: a sentinel, never a value. */
        o->health.valid = live && mx > 0 && hp >= 0 && hp <= mx;
        o->health.value = hp;
        o->health.maximum = mx;
        x[p] = (float)i32(s, a + OFF_POSX) / 65536.0f;
        /* No guessed cap: an unknown type leaves the gauge silent. */
        if (type >= 0 && type < 6) {
            o->resource.valid = live && meter >= 0 && meter <= METER_CAP[type];
            o->resource.value = meter;
            o->resource.maximum = METER_CAP[type];
        }
    }
    {
        int clock = s[OFF_CLOCK];
        m->clock_valid = clock <= 99;
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
        /* Airborne from height alone (the motion routine's own field). */
        h = (float)i32(s, FIGHTER(p) + OFF_POSY) / 65536.0f;
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
    uint64_t epoch = match_audio_current_for(2);   /* CFJ is game 2 */
    ULONGLONG root = *(volatile ULONGLONG *)(ULONG_PTR)ROOT_PTR;
    if (epoch && root && root < 0x00007FFF00000000ull) {
        match_audio_sample m;
        cfj_frame_decode((const unsigned char *)(ULONG_PTR)root, &m);
        for (int p = 0; p < 2; p++) {
            int v = m.in_match && m.owner[p].health.valid ? m.owner[p].health.value : -1;
            if (v >= 0 && g_last_hp[p] != v) InterlockedIncrement(&g_changes);
            g_last_hp[p] = v;
        }
        match_audio_feed(&g_prod, epoch, &m);
        InterlockedExchange(&g_pauses, (LONG)g_prod.pauses);
    }
    g_real_frame();
    /* The update has reset, filled and drained its text queue: copy it now. */
    cfj_text_capture();
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

BOOL cfj_frame_install(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (beside_exe("cfc2_hook.off")) {
        log_line("cfj-frame: disabled by cfc2_hook.off");
        return FALSE;
    }
    if (!VirtualQuery((LPCVOID)(ULONG_PTR)FRAME_FN, &mbi, sizeof mbi) ||
        mbi.State != MEM_COMMIT) {
        log_line("cfj-frame: ⚠ COULD NOT read 0x%llx -- concluding nothing", FRAME_FN);
        return FALSE;
    }
    if (memcmp((const void *)(ULONG_PTR)FRAME_FN, FRAME_SIG, sizeof FRAME_SIG)) {
        log_line("cfj-frame: ⛔ entry bytes differ from the audited build; refusing");
        return FALSE;
    }
    if (MH_CreateHook((LPVOID)(ULONG_PTR)FRAME_FN, (LPVOID)frame_detour,
                      (LPVOID *)&g_real_frame) != MH_OK ||
        MH_EnableHook((LPVOID)(ULONG_PTR)FRAME_FN) != MH_OK) {
        log_line("cfj-frame: ⛔ MinHook refused 0x%llx", FRAME_FN);
        return FALSE;
    }
    InterlockedExchange(&g_installed, 1);
    log_line("cfj-frame: hooked CFJ's per-frame update at 0x%llx", FRAME_FN);
    return TRUE;
}

void cfj_frame_remove(void)
{
    if (InterlockedCompareExchange(&g_installed, 0, 1) == 1)
        MH_DisableHook((LPVOID)(ULONG_PTR)FRAME_FN);
}

LONG cfj_frame_count(void) { return InterlockedCompareExchange(&g_frames, 0, 0); }

/* Rate-limited by TIME (a counter that always changes cannot gate itself). */
void cfj_frame_log_stats(void)
{
    static ULONGLONG last_tick;
    static LONG last_frames, last_changes;
    ULONGLONG now = GetTickCount64();
    LONG f, c;
    if (last_tick == 0) { last_tick = now; return; }
    if (now - last_tick < 10000) return;
    f = g_frames; c = g_changes;
    if (f != last_frames)
        log_line("cfj-frame: %ld frames (%.0f/s), %ld health-change(s), %ld pause(s)",
                 f, (double)(f - last_frames) * 1000.0 / (double)(now - last_tick),
                 c - last_changes, (long)g_pauses);
    last_tick = now; last_frames = f; last_changes = c;
}
