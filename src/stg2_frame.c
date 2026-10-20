/* See stg2_frame.h. Structure follows cfj_frame.c: one pointer read and plain
 * arithmetic on the game's frame thread, no syscalls, no locks. */

#include "stg2_frame.h"
#include "audio.h"
#include "log.h"
#include "render_project.h"

#include "../libs/minhook/include/MinHook.h"

#include <math.h>
#include <string.h>

#define FRAME_FN   0x140CCCB60ull   /* update; its only caller is the per-frame
                                       callback 0x140CCB440 (call at 0x140CCB449) */
#define ROOT_PTR   0x142BE6370ull   /* published by setup 0x140CCC940 */
/* push rbx ; sub rsp,0x20 ; mov ecx,0x45 */
static const unsigned char FRAME_SIG[] = { 0x40,0x53, 0x48,0x83,0xEC,0x20, 0xB9,0x45,0x00,0x00,0x00 };

#define FIGHTER(p)  ((p) * 0xA40u)
#define OFF_HP      0x57Cu
/* No per-fighter maximum: the round intro fills health to the constant 200
 * (0x140CD5E6E) and the training refill caps at it (0x140D16C89). */
#define MAX_HP      200
#define OFF_X       0x220u
#define OFF_Y       0x224u
#define OFF_Z       0x228u
#define OFF_ID      0x428u
#define PLASMA_FILL 0x4DFF0u
#define PLASMA_LVL  0x4DFF8u
#define PLASMA_STEP 0x90         /* fill per level, 0x140CD97E5 */
/* Max level by native ID 0..12, table 0x141954058: the gain 0x140CD97BF
 * indexes it with fighter+0x370, where its fighter is R + side*0xA40 + 0xB8,
 * i.e. R+0x428 (side index at R+0x4DC reads 0 and 1 live). Install checks
 * these bytes against the running image. */
static const unsigned char PLASMA_CAP[13] = { 3,3,3,3,3,3,3,3,3,3,3,3,3 };
#define OFF_CLOCK   0x4DF33u
#define OFF_NOCLOCK 0x4E598u
#define OFF_MODE    0x4E5ECu
#define OFF_RNG     0x94u        /* the game's random word: new every frame */
/* The game's camera, as handed to the shared renderer: 0x140CEAA70 builds a
 * row-vector 4x4 at R+0x4D4B4 (rotation from the camera angles, 0x140CCB510;
 * translation row +0x30 from the camera's eye, 0x140CEABC7) and pushes it
 * onto the renderer's matrix stack (0x140CEABEF -> 0x140F331C0). The renderer
 * then applies its own view and projection from the context (render_project.h;
 * 0x140F39830 stores them at ctx+0x1F812C/+0x1F816C). Live, 2026-10-20: the
 * context view was diag(1,-1,-1) and the projection cot(39.5/2)/1.3,
 * cot(39.5/2) in every sample, and the fighters' stored positions through
 * all three landed on screen in all 2639 fighting frames, every jump drawn
 * above its ground point (stg2-match-fields.md). */
#define GAME_VIEW   0x4D4B4u
#define CONTEXT_PTR 0x142C79B40ull
#define VIEW_OFF    0x1F812Cu
#define PROJ_OFF    0x1F816Cu
/* Jumps rose to 680 in the labelled jump and 2344 at most in the capture. */
#define JUMP_TOP    1000.0f

typedef void (*pfn_frame)(void);
static pfn_frame g_real_frame;
static volatile LONG g_frames, g_changes, g_installed, g_pauses;
static int g_last_hp[2] = { -1, -1 };
static match_audio_producer g_prod = {
    .feeds = MATCH_AUDIO_FEED_HEALTH | MATCH_AUDIO_FEED_RESOURCE |
             MATCH_AUDIO_FEED_BEACON | MATCH_AUDIO_FEED_CLOCK
};

static int i16(const unsigned char *s, unsigned off) { short v; memcpy(&v, s + off, 2); return v; }
static int i32(const unsigned char *s, unsigned off) { int v; memcpy(&v, s + off, 4); return v; }
static float f32(const unsigned char *s, unsigned off) { float v; memcpy(&v, s + off, 4); return v; }
static uint32_t u32(const unsigned char *s, unsigned off) { uint32_t v; memcpy(&v, s + off, 4); return v; }

/* Horizontal screen position of fighter `a`: its stored position through the
 * game's camera matrix, then the renderer's `cam`. FALSE when the game matrix
 * is not affine or the point is not in front of the camera. */
static BOOL project(const unsigned char *s, const float cam[32], unsigned a, float *pan)
{
    float g[16], p[3], e[3];
    memcpy(g, s + GAME_VIEW, sizeof g);
    if (g[3] != 0.0f || g[7] != 0.0f || g[11] != 0.0f || g[15] != 1.0f) return FALSE;
    p[0] = f32(s, a + OFF_X); p[1] = f32(s, a + OFF_Y); p[2] = f32(s, a + OFF_Z);
    for (int c = 0; c < 3; c++)
        e[c] = p[0] * g[c] + p[1] * g[4 + c] + p[2] * g[8 + c] + g[12 + c];
    if (!isfinite(e[0]) || !isfinite(e[1]) || !isfinite(e[2])) return FALSE;
    return render_project_x(cam, e, pan) ? TRUE : FALSE;
}

void stg2_frame_decode(const unsigned char *s, const float cam[32], match_audio_sample *m)
{
    unsigned p;
    int mode;
    bool live, fighting;

    memset(m, 0, sizeof *m);
    mode = i16(s, OFF_MODE);
    if (s[0] != 3 || (mode != 3 && mode != 4 && mode != 11)) return;
    m->in_match = true;
    /* Health refills from 20..80 to 200 during the intro (mode 3), so the
     * gauges are fed only while fighting and over the KO (modes 4, 11). */
    live = mode == 4 || mode == 11;
    /* The KO replay (mode 11) cuts the camera around; beacons only fight. */
    fighting = mode == 4;

    /* Pause: the random word changes every frame of play; the whole block
     * froze while paused. Positions as a second witness. */
    m->motion_valid = true;
    m->motion_signature = (uint32_t)i16(s, OFF_RNG);
    for (p = 0; p < 2; p++) {
        unsigned a = FIGHTER(p);
        m->motion_signature ^= u32(s, a + OFF_X) << (p + 1);
        m->motion_signature ^= u32(s, a + OFF_Y) << (p + 3);
        m->motion_signature ^= u32(s, a + OFF_Z) << (p + 7);
    }

    for (p = 0; p < 2; p++) {
        unsigned a = FIGHTER(p);
        int hp = i16(s, a + OFF_HP), id = i32(s, a + OFF_ID);
        int level = s[PLASMA_LVL + p], fill = i16(s, PLASMA_FILL + 2 * p);
        float y = f32(s, a + OFF_Y), pan, h;
        match_audio_owner *o = &m->owner[p];
        o->present = true;
        /* Damage is floored only when context+0x828 is set (0x140D19DDE; 0
         * live), so the KO frame read below 0 by the overkill (-39, -7): 0. */
        o->health.valid = live && hp >= -MAX_HP && hp <= MAX_HP;
        o->health.value = hp < 0 ? 0 : hp;
        o->health.maximum = MAX_HP;
        /* Level * 0x90 + fill; at the cap the gain stores fill 0x90. No
         * guessed cap: an unknown fighter ID leaves the gauge silent. */
        if (id >= 0 && id < (int)sizeof PLASMA_CAP) {
            int cap = PLASMA_CAP[id];
            o->resource.valid = live && level <= cap && fill >= 0 && fill <= PLASMA_STEP;
            o->resource.value = level >= cap ? cap * PLASMA_STEP
                                             : level * PLASMA_STEP + (fill < PLASMA_STEP ? fill : PLASMA_STEP - 1);
            o->resource.maximum = cap * PLASMA_STEP;
        }

        /* No camera, no beacon: the pan is never estimated. */
        if (!fighting || !cam || !isfinite(y) || !project(s, cam, a, &pan)) continue;
        if (pan < -1.0f) pan = -1.0f;
        if (pan > 1.0f) pan = 1.0f;
        o->beacon_valid = true;
        o->screen_pan = pan;
        if (y < 0.0f) {
            h = -y / JUMP_TOP;
            if (h > 1.0f) h = 1.0f;
            o->stance = AUDIO_STANCE_AIR + (AUDIO_STANCE_AIR_TOP - AUDIO_STANCE_AIR) * h;
        } else {
            o->stance = AUDIO_STANCE_STAND;
        }
    }
    if (!s[OFF_NOCLOCK]) {
        int clock = s[OFF_CLOCK];
        m->clock_valid = clock <= 99;
        m->clock = clock;
    }
}

static void frame_detour(void)
{
    InterlockedIncrement(&g_frames);
    uint64_t epoch = match_audio_current_for(7);   /* STG2 is game 7 */
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
        stg2_frame_decode((const unsigned char *)(ULONG_PTR)root, have_cam ? cam : NULL, &m);
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

/* Code and data the decode rests on, checked before hooking. */
static const struct { ULONGLONG address; unsigned char bytes[16]; unsigned n; } g_sig[] = {
    { 0x140D19DA9ULL, { 0x66,0x29,0x9c,0x3e,0x7c,0x05,0x00,0x00 }, 8 },       /* damage +0x57C   */
    { 0x140CD5E6EULL, { 0xb9,0xc8,0x00,0x00,0x00 }, 5 },                       /* fill to 200     */
    { 0x140D16C89ULL, { 0x41,0xb8,0xc8,0x00,0x00,0x00 }, 6 },                  /* refill cap 200  */
    { 0x140CD97BFULL, { 0x48,0x8d,0x1d,0x92,0xa8,0xc7,0x00,0x0f,0xb6,0x04,0x18,0x42,0x38,0x84,0x0a,0xf8 }, 16 }, /* cap table vs level */
    { 0x140CD97E5ULL, { 0x41,0xbb,0x90,0x00,0x00,0x00 }, 6 },                  /* 0x90 a level    */
    { 0x140CD72EAULL, { 0x80,0xb8,0x33,0xdf,0x04,0x00,0x00 }, 7 },             /* clock tick      */
    { 0x140CD5CABULL, { 0x88,0x88,0x33,0xdf,0x04,0x00,0xeb,0x15,0x40,0x88,0xb8,0x98,0xe5,0x04,0x00 }, 15 }, /* clock, no-timer flag */
    { 0x140CDE243ULL, { 0x0f,0xb7,0x48,0x4c }, 4 },                            /* mode W+0x4C     */
    { 0x140CEAB31ULL, { 0x48,0x8d,0xbb,0xb4,0xd4,0x04,0x00 }, 7 },             /* game view R+0x4D4B4 */
    { 0x140CEABC7ULL, { 0xf3,0x0f,0x11,0x93,0xe4,0xd4,0x04,0x00 }, 8 },        /* its translation row */
    { 0x140CEABECULL, { 0x48,0x8b,0xcf,0xe8,0xcc,0x85,0x24,0x00 }, 8 },        /* pushed to renderer  */
    { 0x140F398A7ULL, { 0x48,0x81,0xc1,0x2c,0x81,0x1f,0x00 }, 7 },             /* ctx view +0x1F812C  */
    { 0x140F398BAULL, { 0x48,0x81,0xc1,0x6c,0x81,0x1f,0x00 }, 7 },             /* ctx proj +0x1F816C  */
    { 0x140CCC783ULL, { 0x48,0x8b,0x05,0xe6,0x9b,0xf1,0x01,0x44,0x0f,0xb6,0x80,0x28,0x04,0x00,0x00 }, 15 }, /* ID +0x428 */
};

static BOOL code_matches(void)
{
    for (unsigned i = 0; i < sizeof g_sig / sizeof g_sig[0]; i++) {
        if (memcmp((const void *)(ULONG_PTR)g_sig[i].address, g_sig[i].bytes, g_sig[i].n)) {
            log_line("stg2-frame: ⛔ code at 0x%llx differs from the audited build; refusing",
                     g_sig[i].address);
            return FALSE;
        }
    }
    if (memcmp((const void *)(ULONG_PTR)0x141954058ull, PLASMA_CAP, sizeof PLASMA_CAP)) {
        log_line("stg2-frame: ⛔ the plasma cap table differs from the audited build; refusing");
        return FALSE;
    }
    return TRUE;
}

BOOL stg2_frame_install(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (beside_exe("cfc2_hook.off")) {
        log_line("stg2-frame: disabled by cfc2_hook.off");
        return FALSE;
    }
    if (!VirtualQuery((LPCVOID)(ULONG_PTR)FRAME_FN, &mbi, sizeof mbi) ||
        mbi.State != MEM_COMMIT ||
        !VirtualQuery((LPCVOID)(ULONG_PTR)0x141954058ull, &mbi, sizeof mbi) ||
        mbi.State != MEM_COMMIT) {
        log_line("stg2-frame: ⚠ COULD NOT read 0x%llx -- concluding nothing", FRAME_FN);
        return FALSE;
    }
    if (memcmp((const void *)(ULONG_PTR)FRAME_FN, FRAME_SIG, sizeof FRAME_SIG)) {
        log_line("stg2-frame: ⛔ entry bytes differ from the audited build; refusing");
        return FALSE;
    }
    if (!code_matches()) return FALSE;
    if (MH_CreateHook((LPVOID)(ULONG_PTR)FRAME_FN, (LPVOID)frame_detour,
                      (LPVOID *)&g_real_frame) != MH_OK ||
        MH_EnableHook((LPVOID)(ULONG_PTR)FRAME_FN) != MH_OK) {
        log_line("stg2-frame: ⛔ MinHook refused 0x%llx", FRAME_FN);
        return FALSE;
    }
    InterlockedExchange(&g_installed, 1);
    log_line("stg2-frame: hooked Plasma Sword's per-frame update at 0x%llx", FRAME_FN);
    return TRUE;
}

void stg2_frame_remove(void)
{
    if (InterlockedCompareExchange(&g_installed, 0, 1) == 1)
        MH_DisableHook((LPVOID)(ULONG_PTR)FRAME_FN);
}

LONG stg2_frame_count(void) { return InterlockedCompareExchange(&g_frames, 0, 0); }

/* Rate-limited by TIME (a counter that always changes cannot gate itself). */
void stg2_frame_log_stats(void)
{
    static ULONGLONG last_tick;
    static LONG last_frames, last_changes;
    ULONGLONG now = GetTickCount64();
    LONG f, c;
    if (last_tick == 0) { last_tick = now; return; }
    if (now - last_tick < 10000) return;
    f = g_frames; c = g_changes;
    if (f != last_frames)
        log_line("stg2-frame: %ld frames (%.0f/s), %ld health-change(s), %ld pause(s)",
                 f, (double)(f - last_frames) * 1000.0 / (double)(now - last_tick),
                 c - last_changes, (long)g_pauses);
    last_tick = now; last_frames = f; last_changes = c;
}
