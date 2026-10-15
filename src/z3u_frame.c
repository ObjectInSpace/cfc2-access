/* See z3u_frame.h. Structure follows cfj_frame.c: one pointer read and plain
 * arithmetic on the game's frame thread, no syscalls, no locks. Alpha 3 has
 * two copies of its update (z3u_branch.h); each gets its own detour, which
 * knows its copy's layout. */

#include "z3u_frame.h"
#include "audio.h"
#include "z3u_text.h"
#include "log.h"

#include "../libs/minhook/include/MinHook.h"

#include <string.h>

/* Update entries up to the first call. Ordinary: mov [rsp+8],rbx ;
 * mov [rsp+0x10],rbp ; mov [rsp+0x18],rsi ; push rdi ; sub rsp,0x60 ;
 * call 0x14087DFC0 -- its only caller is the per-frame callback 0x14086BE70.
 * Alternate: the same plus mov [rsp+0x20],rdi ; push r14, then
 * call 0x140DDDA60 -- its only caller is the wrapper 0x140DCB970. */
static const unsigned char FRAME_SIG_ORD[] = {
    0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10, 0x48,0x89,0x74,0x24,0x18,
    0x57, 0x48,0x83,0xEC,0x60, 0xE8,0x47,0x0E,0x01,0x00
};
static const unsigned char FRAME_SIG_ALT[] = {
    0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10, 0x48,0x89,0x74,0x24,0x18,
    0x48,0x89,0x7C,0x24,0x20, 0x41,0x56, 0x48,0x83,0xEC,0x60, 0xE8,0x51,0x16,0x01,0x00
};

#define OFF_POSX    0x10u
#define OFF_POSY    0x14u
#define OFF_MOTION_A 0x20u
#define HP_MAX      0x90
#define GAUGE_MAX   0x90
/* Mode words R+0x28 (game task), +0x2A (game mode), +0x2C (screen): 1, 2, 3
 * in every match sample; select is 1, 1, 2. Round stage R+0x2E: 2 intro
 * (health refills and the gauge fills), 3 fighting, 4 KO, 5 round over,
 * then 0 and 1 while the next round is set up. Same offsets on both copies
 * (0x140DF6C68 builds the same task block). */
#define MODE_OFF    0x28u
#define STAGE_OFF   0x2Eu
/* The game's own on-screen X (0x140872550 / 0x140DD2960): object+0x12 -
 * camera, on a 384-pixel screen. Fighters stop 24 pixels inside each edge. */
#define SCREEN_HALF 192.0f
/* Landing 0x1408C51A8 / 0x140E5462B: Y below 0x28 snaps to 40.0. */
#define GROUND      40.0f

/* Attract walker: the opening screens (task modes 1 and 2) print through
 * it, and it draws and CLEARS each record inside the update, so the text is
 * collected on its entry. Both copies begin mov rax,rsp ;
 * mov [rax+0x10],rbx ; mov [rax+0x18],rbp ; mov [rax+0x20],rsi */
static const unsigned char WALKER_SIG[] = {
    0x48,0x8B,0xC4, 0x48,0x89,0x58,0x10, 0x48,0x89,0x68,0x18, 0x48,0x89,0x70,0x20
};

typedef void (*pfn_frame)(void);
typedef struct {
    const z3u_branch *b;
    const unsigned char *sig;
    size_t sig_len;
    pfn_frame real_frame, real_walker;
    volatile LONG installed, walker_installed;
} hook_set;

static hook_set g_hooks[2] = {
    { &z3u_ordinary, FRAME_SIG_ORD, sizeof FRAME_SIG_ORD, NULL, NULL, 0, 0 },
    { &z3u_alternate, FRAME_SIG_ALT, sizeof FRAME_SIG_ALT, NULL, NULL, 0, 0 },
};
static volatile LONG g_frames, g_changes, g_pauses;
static int g_last_hp[2] = { -1, -1 };
static match_audio_producer g_prod = {
    .feeds = MATCH_AUDIO_FEED_HEALTH | MATCH_AUDIO_FEED_RESOURCE |
             MATCH_AUDIO_FEED_BEACON | MATCH_AUDIO_FEED_CLOCK
};

static int i32(const unsigned char *s, unsigned off) { int v; memcpy(&v, s + off, 4); return v; }
static int i16(const unsigned char *s, unsigned off) { short v; memcpy(&v, s + off, 2); return v; }

void z3u_frame_decode_branch(const z3u_branch *b, const unsigned char *s, match_audio_sample *m)
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
     * and the motion byte (+0x52 ordinary), which change in 40-60% of
     * samples during play. */
    m->motion_valid = true;
    for (p = 0; p < 2; p++) {
        unsigned a = b->obj_off + p * b->obj_step;
        m->motion_signature ^= (uint32_t)i32(s, a + OFF_POSX) << p;
        m->motion_signature ^= (uint32_t)i32(s, a + OFF_POSY) << (p + 3);
        m->motion_signature ^= (uint32_t)s[a + OFF_MOTION_A] << (8 + 8 * p);
        m->motion_signature ^= (uint32_t)s[a + b->o_motion] << (12 + 8 * p);
    }

    for (p = 0; p < 2; p++) {
        unsigned a = b->obj_off + p * b->obj_step;
        int hp = i16(s, a + b->o_hp), gauge = i16(s, a + b->o_gauge);
        int sx = i16(s, a + OFF_POSX + 2) - i16(s, b->camera);
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
        int clock = s[b->clock];
        m->clock_valid = clock <= 99;
        m->clock = clock;
    }
}

void z3u_frame_decode(const unsigned char *s, match_audio_sample *m)
{
    z3u_frame_decode_branch(&z3u_ordinary, s, m);
}

static void on_frame(hook_set *h)
{
    InterlockedIncrement(&g_frames);
    uint64_t epoch = match_audio_current_for(3);   /* Alpha 3 is game 3 */
    ULONGLONG root = *(volatile ULONGLONG *)(ULONG_PTR)h->b->root_slot;
    if (epoch && root && root < 0x00007FFF00000000ull) {
        match_audio_sample m;
        z3u_frame_decode_branch(h->b, (const unsigned char *)(ULONG_PTR)root, &m);
        for (int p = 0; p < 2; p++) {
            int v = m.in_match && m.owner[p].health.valid ? m.owner[p].health.value : -1;
            if (v >= 0 && g_last_hp[p] != v) InterlockedIncrement(&g_changes);
            g_last_hp[p] = v;
        }
        match_audio_feed(&g_prod, epoch, &m);
        InterlockedExchange(&g_pauses, (LONG)g_prod.pauses);
    }
    h->real_frame();
    /* The update has drawn its text queue: copy it now. */
    z3u_text_capture(h->b);
}

static void frame_detour_ord(void) { on_frame(&g_hooks[0]); }
static void frame_detour_alt(void) { on_frame(&g_hooks[1]); }
static void walker_detour_ord(void) { z3u_text_collect(g_hooks[0].b); g_hooks[0].real_walker(); }
static void walker_detour_alt(void) { z3u_text_collect(g_hooks[1].b); g_hooks[1].real_walker(); }

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

static BOOL install_one(hook_set *h, pfn_frame frame, pfn_frame walker)
{
    MEMORY_BASIC_INFORMATION mbi;
    ULONGLONG fn = h->b->frame_fn, wk = h->b->walker_fn;
    if (!VirtualQuery((LPCVOID)(ULONG_PTR)fn, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) {
        log_line("z3u-frame: ⚠ COULD NOT read the %s update 0x%llx -- concluding nothing",
                 h->b->name, fn);
        return FALSE;
    }
    if (memcmp((const void *)(ULONG_PTR)fn, h->sig, h->sig_len)) {
        log_line("z3u-frame: ⛔ %s update entry bytes differ from the audited build; refusing",
                 h->b->name);
        return FALSE;
    }
    if (MH_CreateHook((LPVOID)(ULONG_PTR)fn, (LPVOID)frame, (LPVOID *)&h->real_frame) != MH_OK ||
        MH_EnableHook((LPVOID)(ULONG_PTR)fn) != MH_OK) {
        log_line("z3u-frame: ⛔ MinHook refused 0x%llx", fn);
        return FALSE;
    }
    InterlockedExchange(&h->installed, 1);
    log_line("z3u-frame: hooked Alpha 3's %s per-frame update at 0x%llx", h->b->name, fn);
    /* Text only: the frame hook stands without it. */
    if (memcmp((const void *)(ULONG_PTR)wk, WALKER_SIG, sizeof WALKER_SIG)) {
        log_line("z3u-frame: ⛔ %s attract walker bytes differ; its opening-screen text is off",
                 h->b->name);
    } else if (MH_CreateHook((LPVOID)(ULONG_PTR)wk, (LPVOID)walker,
                             (LPVOID *)&h->real_walker) != MH_OK ||
               MH_EnableHook((LPVOID)(ULONG_PTR)wk) != MH_OK) {
        log_line("z3u-frame: ⛔ MinHook refused 0x%llx; its opening-screen text is off", wk);
    } else {
        InterlockedExchange(&h->walker_installed, 1);
        log_line("z3u-frame: hooked the %s attract text walker at 0x%llx", h->b->name, wk);
    }
    return TRUE;
}

BOOL z3u_frame_install(void)
{
    BOOL ord, alt;
    if (beside_exe("cfc2_hook.off")) {
        log_line("z3u-frame: disabled by cfc2_hook.off");
        return FALSE;
    }
    ord = install_one(&g_hooks[0], frame_detour_ord, walker_detour_ord);
    alt = install_one(&g_hooks[1], frame_detour_alt, walker_detour_alt);
    return ord || alt;
}

void z3u_frame_remove(void)
{
    for (int i = 0; i < 2; i++) {
        if (InterlockedCompareExchange(&g_hooks[i].installed, 0, 1) == 1)
            MH_DisableHook((LPVOID)(ULONG_PTR)g_hooks[i].b->frame_fn);
        if (InterlockedCompareExchange(&g_hooks[i].walker_installed, 0, 1) == 1)
            MH_DisableHook((LPVOID)(ULONG_PTR)g_hooks[i].b->walker_fn);
    }
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
