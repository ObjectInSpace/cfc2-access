/* See mjus_frame.h. Structure follows cfj_frame.c: one pointer read and plain
 * arithmetic on the game's frame thread, no syscalls, no locks. */

#include "mjus_frame.h"
#include "audio.h"
#include "mjus_text.h"
#include "mjus_story.h"
#include "render_project.h"
#include "log.h"

#include "../libs/minhook/include/MinHook.h"

#include <math.h>
#include <string.h>

#define ROOT_PTR   0x142996830ull   /* published by setup 0x140B930D0 */
/* mov [rsp+8],rbx ; push rdi ; sub rsp,0x20 ; call 0x140036B70. Its only
 * caller is the per-frame callback 0x140B92930 (call at 0x140B92939). */
static const unsigned char FRAME_SIG[] = { 0x48,0x89,0x5C,0x24,0x08, 0x57, 0x48,0x83,0xEC,0x20, 0xE8,0x61 };

#define FIGHTER(p)  (0x3B48u + (p) * 0x608u)
#define OFF_HP      0x2DEu
#define MAX_HP      200
#define OFF_GUTS    0x3CCu      /* progress byte; +1 is the level */
#define GUTS_LEVELS 5
#define OFF_X       0xECu
#define OFF_Y       0xF0u
#define OFF_Z       0xF4u
#define OFF_WORLD   0xD0u       /* world position, 3 floats: what the camera sees */
#define PHASE_OFF   0x21C0u
/* The game's camera, in the shared renderer's context (*0x142C79B40, the
 * slot MJUS's own code reads at 0x140B95953): the view matrix at +0x1F812C
 * and the perspective projection at +0x1F816C, both row-vector 4x4, which
 * 0x140F33830 composes with the viewport. A point's horizontal screen
 * position is clip.x / clip.w, -1 left edge .. +1 right edge. Live: across
 * five fights both fighters projected on screen in nearly every sample, in
 * the order the facing flag gives except while it catches up after a
 * cross-over (mjus-match-fields.md). */
#define CONTEXT_PTR 0x142C79B40ull
#define VIEW_OFF    0x1F812Cu
#define PROJ_OFF    0x1F816Cu
/* Jump height: up to about 200 in the captures (0xF0 rose to 174..198). */
#define JUMP_TOP    200.0f

typedef void (*pfn_frame)(void);
static pfn_frame g_real_frame;
static volatile LONG g_frames, g_changes, g_installed, g_pauses;
static int g_last_hp[2] = { -1, -1 };
static match_audio_producer g_prod = {
    .feeds = MATCH_AUDIO_FEED_HEALTH | MATCH_AUDIO_FEED_RESOURCE | MATCH_AUDIO_FEED_BEACON
};

static int i16(const unsigned char *s, unsigned off) { short v; memcpy(&v, s + off, 2); return v; }
static float f32(const unsigned char *s, unsigned off) { float v; memcpy(&v, s + off, 4); return v; }
static uint32_t u32(const unsigned char *s, unsigned off) { uint32_t v; memcpy(&v, s + off, 4); return v; }

/* Horizontal screen position of a world point, or FALSE when the camera does
 * not look like a perspective camera or the point is behind it. */
BOOL mjus_project_x(const float cam[32], const float pt[3], float *ndc_x)
{
    return render_project_x(cam, pt, ndc_x) ? TRUE : FALSE;
}

void mjus_frame_decode(const unsigned char *s, const float cam[32], match_audio_sample *m)
{
    unsigned p;
    memset(m, 0, sizeof *m);
    if (s[PHASE_OFF] != 2) return;
    m->in_match = true;

    /* Pause: positions and the facing byte change in most samples of play. */
    m->motion_valid = true;
    for (p = 0; p < 2; p++) {
        unsigned a = FIGHTER(p);
        m->motion_signature ^= u32(s, a + OFF_X) << p;
        m->motion_signature ^= u32(s, a + OFF_Y) << (p + 3);
        m->motion_signature ^= u32(s, a + OFF_Z) << (p + 7);
    }

    for (p = 0; p < 2; p++) {
        unsigned a = FIGHTER(p);
        int hp = i16(s, a + OFF_HP);
        int level = s[a + OFF_GUTS + 1], part = s[a + OFF_GUTS];
        float y = f32(s, a + OFF_Y), world[3], pan, h;
        match_audio_owner *o = &m->owner[p];
        o->present = true;
        /* Below 0 is the KO frame (-6 live): a sentinel, never a value. */
        o->health.valid = hp >= 0 && hp <= MAX_HP;
        o->health.value = hp;
        o->health.maximum = MAX_HP;
        o->resource.valid = level <= GUTS_LEVELS && (level < GUTS_LEVELS || part == 0);
        o->resource.value = level * 256 + part;
        o->resource.maximum = GUTS_LEVELS * 256;

        /* No camera, no beacon: the pan is never estimated. */
        world[0] = f32(s, a + OFF_WORLD);
        world[1] = f32(s, a + OFF_WORLD + 4);
        world[2] = f32(s, a + OFF_WORLD + 8);
        if (!cam || !isfinite(y) || !mjus_project_x(cam, world, &pan)) continue;
        if (pan < -1.0f) pan = -1.0f;
        if (pan > 1.0f) pan = 1.0f;
        o->beacon_valid = true;
        o->screen_pan = pan;
        if (y > 0.0f) {
            h = y / JUMP_TOP;
            if (h > 1.0f) h = 1.0f;
            o->stance = AUDIO_STANCE_AIR + (AUDIO_STANCE_AIR_TOP - AUDIO_STANCE_AIR) * h;
        } else {
            o->stance = AUDIO_STANCE_STAND;
        }
    }
}

static void frame_detour(void)
{
    InterlockedIncrement(&g_frames);
    uint64_t epoch = match_audio_current_for(6);   /* MJUS is game 6 */
    ULONGLONG root = *(volatile ULONGLONG *)(ULONG_PTR)ROOT_PTR;
    if (epoch && root && root < 0x00007FFF00000000ull) {
        match_audio_sample m;
        float cam[32];
        ULONGLONG ctx = *(volatile ULONGLONG *)(ULONG_PTR)CONTEXT_PTR;
        BOOL have_cam = ctx && ctx < 0x00007FFF00000000ull;
        if (have_cam) {
            memcpy(cam, (const void *)(ULONG_PTR)(ctx + VIEW_OFF), 64);
            memcpy(cam + 16, (const void *)(ULONG_PTR)(ctx + PROJ_OFF), 64);
        }
        mjus_frame_decode((const unsigned char *)(ULONG_PTR)root, have_cam ? cam : NULL, &m);
        for (int p = 0; p < 2; p++) {
            int v = m.in_match && m.owner[p].health.valid ? m.owner[p].health.value : -1;
            if (v >= 0 && g_last_hp[p] != v) InterlockedIncrement(&g_changes);
            g_last_hp[p] = v;
        }
        match_audio_feed(&g_prod, epoch, &m);
        InterlockedExchange(&g_pauses, (LONG)g_prod.pauses);
    }
    g_real_frame();
    /* The update has drawn this frame's text: publish it now. */
    mjus_text_capture();
    /* And run its scene script: queue a story line that just appeared. Not
     * gated on the audio epoch -- story scenes are not matches -- only on the
     * root, which this hook (MJUS's own update) runs inside. */
    mjus_story_frame(*(volatile ULONGLONG *)(ULONG_PTR)ROOT_PTR);
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

BOOL mjus_frame_install(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (beside_exe("cfc2_hook.off")) {
        log_line("mjus-frame: disabled by cfc2_hook.off");
        return FALSE;
    }
    if (!VirtualQuery((LPCVOID)(ULONG_PTR)MJUS_FRAME_FN, &mbi, sizeof mbi) ||
        mbi.State != MEM_COMMIT) {
        log_line("mjus-frame: ⚠ COULD NOT read 0x%llx -- concluding nothing", MJUS_FRAME_FN);
        return FALSE;
    }
    if (memcmp((const void *)(ULONG_PTR)MJUS_FRAME_FN, FRAME_SIG, sizeof FRAME_SIG)) {
        log_line("mjus-frame: ⛔ entry bytes differ from the audited build; refusing");
        return FALSE;
    }
    /* The text draw first: the frame detour publishes what it collects. */
    mjus_text_install();
    if (MH_CreateHook((LPVOID)(ULONG_PTR)MJUS_FRAME_FN, (LPVOID)frame_detour,
                      (LPVOID *)&g_real_frame) != MH_OK ||
        MH_EnableHook((LPVOID)(ULONG_PTR)MJUS_FRAME_FN) != MH_OK) {
        log_line("mjus-frame: ⛔ MinHook refused 0x%llx", MJUS_FRAME_FN);
        return FALSE;
    }
    InterlockedExchange(&g_installed, 1);
    log_line("mjus-frame: hooked Project Justice's per-frame update at 0x%llx", MJUS_FRAME_FN);
    return TRUE;
}

void mjus_frame_remove(void)
{
    if (InterlockedCompareExchange(&g_installed, 0, 1) == 1)
        MH_DisableHook((LPVOID)(ULONG_PTR)MJUS_FRAME_FN);
    mjus_text_remove();
}

LONG mjus_frame_count(void) { return InterlockedCompareExchange(&g_frames, 0, 0); }

/* Rate-limited by TIME (a counter that always changes cannot gate itself). */
void mjus_frame_log_stats(void)
{
    static ULONGLONG last_tick;
    static LONG last_frames, last_changes;
    ULONGLONG now = GetTickCount64();
    LONG f, c;
    if (last_tick == 0) { last_tick = now; return; }
    if (now - last_tick < 10000) return;
    f = g_frames; c = g_changes;
    if (f != last_frames)
        log_line("mjus-frame: %ld frames (%.0f/s), %ld health-change(s), %ld pause(s)",
                 f, (double)(f - last_frames) * 1000.0 / (double)(now - last_tick),
                 c - last_changes, (long)g_pauses);
    last_tick = now; last_frames = f; last_changes = c;
}
