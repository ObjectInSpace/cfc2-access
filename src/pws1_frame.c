/* See pws1_frame.h. Structure follows stg2_frame.c: one pointer read and plain
 * arithmetic on the game's frame thread, no syscalls, no locks. */

#include "pws1_frame.h"
#include "audio.h"
#include "log.h"
#include "render_project.h"
#include "pickup_cues.h"

#include "../libs/minhook/include/MinHook.h"

#include <math.h>
#include <string.h>

#define FRAME_FN   0x140974100ull   /* update; its only caller is the per-frame
                                       callback 0x140973090 (call at 0x140973099) */
#define ROOT_PTR   0x1423188C0ull
/* sub rsp,0x28 ; call 0x140036B70 -- the shape the CVS1 and CFJ hooks take */
static const unsigned char FRAME_SIG[] = { 0x48,0x83,0xEC,0x28, 0xE8,0x67,0x2A,0x6C,0xFF };

#define FIGHTER(p)  (0x18428u + (p) * 0x7890u)
#define OFF_SLOT    0x3u
#define OFF_X       0x30u
#define OFF_Y       0x34u
#define OFF_Z       0x38u
#define OFF_HP      0x7790u
/* Full health is the constant 240: the item recovery caps at it (0x14098635D)
 * and the both-fighter refill writes it (0x14099377E). Live: 240 at every
 * round start, -1 on the KO frame. */
#define MAX_HP      240
#define OFF_STONES  0x77ECu
/* The collector (0x140986150) refuses a fourth stone (0x1409861AF); every
 * other writer zeroes it, sets 1 or decrements above 0. */
#define MAX_STONES  3
#define OFF_RUN     0x27E58u
#define OFF_PHASE   0x27E5Du
#define OFF_FREEZE  0x27E7Du     /* clock not counted (0x140993873)          */
#define OFF_TIMER   0x27E92u     /* 4: no timer (0x14099387D)                */
#define OFF_FRAMES  0x27F94u
#define OFF_SUB     0x27FACu
#define OFF_STEP    0x27FADu
#define OFF_CLOCK   0x27FCDu
#define OFF_SPLIT   0x280C4u     /* nonzero: two-view layout, not decoded    */
/* The view the ordinary draw loads into renderer slot 2 (0x1409A8865) after
 * building the projection in slot 3, ctx+0x1F80AC+3*64 (0x140F326E0). Live,
 * 2026-10-22: every fighting sample's projection had x scale 2.0059 and
 * w = -z; the fighters' positions through the stored view landed where the
 * offline pairing put them (pws-camera-route.md). */
#define OFF_VIEW    0x27974u
#define CONTEXT_PTR 0x142C79B40ull
#define PROJ_OFF    0x1F816Cu

/* The arena's objects: a pool of 100 records of 0x3E0 at R+0 (built at
 * 0x140995CC0, entry 99 the last). Header bytes, read off the code that uses
 * them: +0 in use, +1 shown, +2 class (the collector 0x14098A9F0 takes 2 or
 * 3), +4 type, +0xE a stone's index (its bit in A+0x77ED, 0x140986231),
 * +0x30 position as for the fighters, +0x3C8 an item's code. Types:
 *   5 a power stone (collector 0x1409861BC)
 *   6 an item; codes 0..17 index the 18-entry table at 0x14098AB35, and 15,
 *     16, 17 are food, healing 96, 48, 24 (0x140986317)
 *   1 a liftable prop; the pick-up 0x140986EF1 lifts type 1 or 6
 * A+0x7780 points at the object a fighter carries (0x14098AB00). */
#define POOL_COUNT  100u
#define POOL_STRIDE 0x3E0u
#define OBJ_USED    0x0u
#define OBJ_SHOWN   0x1u
#define OBJ_CLASS   0x2u
#define OBJ_TYPE    0x4u
#define OBJ_POS     0x30u
#define OBJ_CODE    0x3C8u
#define TYPE_PROP   1
#define TYPE_STONE  5
#define TYPE_ITEM   6
#define ITEM_CODES  18
#define FOOD_FIRST  15
#define OFF_CARRIED 0x7780u
/* R+0x27FF2: bit per side played by a person; 0x140976ECB derives the CPU
 * sides as its complement (xor 3). Live: 1 throughout a one-player match. */
#define OFF_HUMANS  0x27FF2u

typedef void (*pfn_frame)(void);
static pfn_frame g_real_frame;
static volatile LONG g_frames, g_changes, g_installed, g_pings;
static int g_last_hp[2] = { -1, -1 };
static match_audio_producer g_prod = {
    .feeds = MATCH_AUDIO_FEED_HEALTH | MATCH_AUDIO_FEED_RESOURCE | MATCH_AUDIO_FEED_CLOCK
};
static pickup_cues g_cues;

static int i16(const unsigned char *s, unsigned off) { short v; memcpy(&v, s + off, 2); return v; }
static float f32(const unsigned char *s, unsigned off) { float v; memcpy(&v, s + off, 4); return v; }
static uint32_t u32(const unsigned char *s, unsigned off) { uint32_t v; memcpy(&v, s + off, 4); return v; }
static uint64_t u64(const unsigned char *s, unsigned off) { uint64_t v; memcpy(&v, s + off, 8); return v; }

bool pws1_fighting(const unsigned char *s)
{
    /* The round being fought: its step leaves 0 the frame a fighter drops
     * below 0 or the clock runs out (0x140993933). Single view only. */
    return s[OFF_RUN] == 1 && s[OFF_PHASE] == 4 && s[OFF_SUB] == 3 &&
           s[OFF_STEP] == 0 && s[OFF_SPLIT] == 0;
}

uint32_t pws1_frame_counter(const unsigned char *s) { return u32(s, OFF_FRAMES); }

static bool project(const unsigned char *s, const float proj[16], const float pt[3], float *pan,
                    bool offscreen_ok)
{
    float cam[32];
    int r;
    if (!proj) return false;
    memcpy(cam, s + OFF_VIEW, 64);
    memcpy(cam + 16, proj, 64);
    /* The stored view is a rigid camera: affine, or it is not one. */
    if (cam[3] != 0.0f || cam[7] != 0.0f || cam[11] != 0.0f || cam[15] != 1.0f) return false;
    /* On screen only: nothing off screen is sounded (user, 2026-10-27),
     * unless off-screen sounds are switched on (Shift+F9): then at the edge
     * it lies beyond. */
    r = render_project_screen(cam, pt, pan);
    if (r == 0 && offscreen_ok) return render_project_side(cam, pt, pan);
    return r == 1;
}

bool pws1_project(const unsigned char *s, const float proj[16], const float pt[3], float *pan)
{
    return project(s, proj, pt, pan, false);
}

void pws1_pickups_decode(const unsigned char *s, uint64_t root, const float proj[16],
                         pickup_sample out[PICKUP_SLOTS])
{
    float me[2][3];
    unsigned humans = s[OFF_HUMANS] & 3u, n = 0;
    uint64_t carried[2];
    memset(out, 0, sizeof(pickup_sample) * PICKUP_SLOTS);
    if (!pws1_fighting(s)) return;
    for (unsigned p = 0; p < 2; p++) {
        unsigned a = FIGHTER(p);
        carried[p] = u64(s, a + OFF_CARRIED);
        if (!(humans >> p & 1u) || s[a + OFF_SLOT] != p) continue;
        me[n][0] = f32(s, a + OFF_X); me[n][1] = f32(s, a + OFF_Y); me[n][2] = f32(s, a + OFF_Z);
        n++;
    }
    for (unsigned i = 0; i < POOL_COUNT; i++) {
        const unsigned char *o = s + i * POOL_STRIDE;
        pickup_sample *q = &out[i];
        uint64_t addr = root + i * POOL_STRIDE;
        float pt[3];
        if (!o[OBJ_USED] || (o[OBJ_CLASS] != 2 && o[OBJ_CLASS] != 3)) continue;
        if (addr == carried[0] || addr == carried[1]) continue;
        switch (o[OBJ_TYPE]) {
        case TYPE_STONE:
            /* Shown exactly while it lies in the arena: held plus shown came
             * to 3 in every steady sample of the capture. */
            if (!o[OBJ_SHOWN]) continue;
            q->family = AUDIO_PING_STONE;
            break;
        case TYPE_ITEM:
            /* Not the shown flag: an item blinks before it vanishes. */
            if (o[OBJ_CODE] >= ITEM_CODES) continue;
            if (o[OBJ_CODE] >= FOOD_FIRST) {
                q->family = AUDIO_PING_FOOD;
                q->variant = o[OBJ_CODE] - FOOD_FIRST;
            } else {
                q->family = AUDIO_PING_ITEM;
                q->variant = o[OBJ_CODE];
            }
            break;
        case TYPE_PROP:
            if (!o[OBJ_SHOWN]) continue;
            q->family = AUDIO_PING_PROP;
            break;
        default:
            continue;
        }
        q->present = true;
        pt[0] = f32(o, OBJ_POS); pt[1] = f32(o, OBJ_POS + 4); pt[2] = f32(o, OBJ_POS + 8);
        /* Off screen: present, silent; it pings once it comes into view. */
        if (!n || !project(s, proj, pt, &q->pan, audio_option_get(AUDIO_OPT_OFFSCREEN) != 0)) continue;
        q->distance = 1e30f;
        for (unsigned k = 0; k < n; k++) {
            float dx = pt[0] - me[k][0], dy = pt[1] - me[k][1], dz = pt[2] - me[k][2];
            float d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d < q->distance) q->distance = d;
        }
        q->placed = isfinite(q->distance);
    }
}

void pws1_frame_decode(const unsigned char *s, match_audio_sample *m)
{
    unsigned p;
    int sub;
    bool live;

    memset(m, 0, sizeof *m);
    sub = s[OFF_SUB];
    if (s[OFF_RUN] != 1 || s[OFF_PHASE] != 4 || sub < 2 || sub > 5) return;
    m->in_match = true;
    /* Health refills to 240 in the intro (2) after a KO round, so the gauges
     * are fed only while fighting (3) and over the round's end (4, 5). */
    live = sub >= 3;

    for (p = 0; p < 2; p++) {
        unsigned a = FIGHTER(p);
        int hp = i16(s, a + OFF_HP), stones = s[a + OFF_STONES];
        match_audio_owner *o = &m->owner[p];
        /* The selection driver indexes the side by this byte; a fighter
         * record that does not name its own side is not one we can place. */
        if (s[a + OFF_SLOT] != p) continue;
        o->present = true;
        /* Damage leaves 0 or -1 (0x1409BE300); -1 ends the round. */
        o->health.valid = live && hp >= -1 && hp <= MAX_HP;
        o->health.value = hp < 0 ? 0 : hp;
        o->health.maximum = MAX_HP;
        o->resource.valid = live && stones <= MAX_STONES;
        o->resource.value = stones;
        o->resource.maximum = MAX_STONES;
    }
    if (!s[OFF_FREEZE] && s[OFF_TIMER] != 4) {
        int clock = s[OFF_CLOCK];
        m->clock_valid = clock <= 99;
        m->clock = clock;
    }
}

/* ---- pickup announcements (pickup_speech.h) --------------------------------
 * Who picked up what kind of thing, for both fighters (user, 2026-10-27,
 * consistent with PWS2). Three sources, on the game thread:
 *   - a stone: the count A+0x77EC rising (exact; the collector 0x140986150);
 *   - a lifted thing: the carried pointer A+0x7780 naming a new pool object
 *     (0x14098AB00), type 1 a thing to throw, type 6 an item, or food for
 *     codes 15..17 (0x140986317);
 *   - food eaten on touch: health rising 20 or more in one frame. Food heals
 *     24, 48 or 96 at once (0x140986317); nothing else heals that fast in a
 *     fight (the game's own recovery steps by one, 0x140991360). Not after a
 *     lifted food for the same fighter within 3 s. No food appeared in the
 *     capture, so this path is untested live. */
static pickup_ring g_pickups;

bool pws1_pickup_pop(pickup_event *e) { return pickup_ring_pop(&g_pickups, e); }

unsigned pws1_watch_events(const unsigned char *s, uint64_t root, pws1_pickup_watch *w,
                           unsigned who[6], unsigned kind[6])
{
    unsigned n = 0;
    bool on = pws1_fighting(s);
    for (unsigned p = 0; p < 2; p++) {
        unsigned a = FIGHTER(p);
        uint64_t c = u64(s, a + OFF_CARRIED);
        int hp = i16(s, a + OFF_HP);
        unsigned stones = s[a + OFF_STONES];
        if (w->food_frames[p]) w->food_frames[p]--;
        if (on && w->valid && s[a + OFF_SLOT] == p) {
            if (c && c != w->carried[p] && c >= root && (c - root) % POOL_STRIDE == 0 &&
                (c - root) / POOL_STRIDE < POOL_COUNT) {
                const unsigned char *o = s + (c - root);
                unsigned k = o[OBJ_TYPE] == TYPE_PROP ? PICKUP_THROWABLE :
                             o[OBJ_TYPE] != TYPE_ITEM ? PICKUP_KINDS :
                             o[OBJ_CODE] >= FOOD_FIRST && o[OBJ_CODE] < ITEM_CODES ? PICKUP_FOOD : PICKUP_ITEM;
                if (k < PICKUP_KINDS) {
                    who[n] = p; kind[n++] = k;
                    if (k == PICKUP_FOOD) w->food_frames[p] = 180;
                }
            }
            if (stones > w->stones[p] && stones <= MAX_STONES) { who[n] = p; kind[n++] = PICKUP_STONE; }
            if (hp - w->hp[p] >= 20 && hp <= MAX_HP && !w->food_frames[p]) { who[n] = p; kind[n++] = PICKUP_FOOD; }
        }
        w->carried[p] = on ? c : 0;
        w->stones[p] = (unsigned char)stones;
        w->hp[p] = (short)hp;
    }
    w->valid = on;
    return n;
}

static void frame_detour(void)
{
    InterlockedIncrement(&g_frames);
    uint64_t epoch = match_audio_current_for(4);   /* PWS1 is game 4 */
    ULONGLONG root = *(volatile ULONGLONG *)(ULONG_PTR)ROOT_PTR;
    if (epoch && root && root < 0x00007FFF00000000ull) {
        static uint32_t last_counter;
        const unsigned char *s = (const unsigned char *)(ULONG_PTR)root;
        match_audio_sample m;
        pws1_frame_decode(s, &m);
        for (int p = 0; p < 2; p++) {
            int v = m.in_match && m.owner[p].health.valid ? m.owner[p].health.value : -1;
            if (v >= 0 && g_last_hp[p] != v) InterlockedIncrement(&g_changes);
            g_last_hp[p] = v;
        }
        match_audio_feed(&g_prod, epoch, &m);
        {
            static pws1_pickup_watch watch;
            unsigned who[6], kind[6], n = pws1_watch_events(s, root, &watch, who, kind);
            for (unsigned k = 0; k < n; k++) {
                unsigned a = FIGHTER(who[k]);
                pickup_ring_push(&g_pickups, &(pickup_event){
                    .epoch = epoch, .fighter = who[k], .id = s[a + 4], .code = kind[k],
                    .human = (s[OFF_HUMANS] >> who[k] & 1u) != 0 });
            }
        }
        if (!pws1_fighting(s)) {
            pickup_cues_reset(&g_cues);
        } else if (pws1_frame_counter(s) != last_counter) {
            /* Only on simulated frames: the counter stood still for the
             * whole pause in the capture, and so do the pings. */
            static pickup_sample pick[PICKUP_SLOTS];
            float proj[16];
            ULONGLONG ctx = *(volatile ULONGLONG *)(ULONG_PTR)CONTEXT_PTR;
            BOOL have_cam = ctx && ctx < 0x00007FFF00000000ull;
            if (have_cam) memcpy(proj, (const void *)(ULONG_PTR)(ctx + PROJ_OFF), 64);
            pws1_pickups_decode(s, root, have_cam ? proj : NULL, pick);
            pickup_cues_frame(&g_cues, epoch, pick);
            InterlockedExchange(&g_pings, (LONG)g_cues.pings);
        }
        last_counter = pws1_frame_counter(s);
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

/* Code the decode rests on, checked before hooking. */
static const struct { ULONGLONG address; unsigned char bytes[33]; unsigned n; } g_sig[] = {
    { 0x140973094ULL, { 0xe8,0x27,0xce,0x5b,0x00,0xe8,0x62,0x10,0x00,0x00 }, 10 },          /* the only call */
    { 0x140976EFCULL, { 0x0f,0xb6,0x82,0x5d,0x7e,0x02,0x00,0x33,0xdb,0x83,0xf8,0x0a,0x0f,0x87,0x46,0x03,
                        0x00,0x00,0x4c,0x8d,0x05,0xeb,0x90,0x68,0xff,0x41,0x8b,0x8c,0x80,0xd8,0x72,0x97,0x00 }, 33 }, /* phase table */
    { 0x140976BBFULL, { 0x0f,0xb6,0x91,0x58,0x7e,0x02,0x00 }, 7 },                          /* running R+0x27E58 */
    { 0x140990B2BULL, { 0x48,0x0f,0xbe,0x88,0xac,0x7f,0x02,0x00,0x48,0x8d,0x05,0x36,0x2e,0xb9,0x00,0xff,0x14,0xc8 }, 18 }, /* sub-state */
    { 0x14099370BULL, { 0x0f,0xbe,0x91,0xad,0x7f,0x02,0x00 }, 7 },                          /* fighting step */
    { 0x140993804ULL, { 0x66,0x83,0xb9,0xb8,0xfb,0x01,0x00,0x00,0x0f,0x8c,0x21,0x01,0x00,0x00,
                        0x66,0x83,0xb9,0x48,0x74,0x02,0x00,0x00,0x0f,0x8c,0x13,0x01,0x00,0x00 }, 28 }, /* KO below 0 */
    { 0x1409BE543ULL, { 0x66,0x89,0x8b,0x90,0x77,0x00,0x00 }, 7 },                          /* damage +0x7790 */
    { 0x14099377EULL, { 0xba,0xf0,0x00,0x00,0x00 }, 5 },                                    /* refill 240 */
    { 0x14098635DULL, { 0xb8,0xf0,0x00,0x00,0x00 }, 5 },                                    /* recovery cap 240 */
    { 0x1409861AFULL, { 0x80,0xbb,0xec,0x77,0x00,0x00,0x03 }, 7 },                          /* no fourth stone */
    { 0x140986221ULL, { 0xfe,0x83,0xec,0x77,0x00,0x00 }, 6 },                               /* stone collected */
    { 0x1409937D7ULL, { 0x80,0xb9,0xcd,0x7f,0x02,0x00,0x00 }, 7 },                          /* clock R+0x27FCD */
    { 0x140993873ULL, { 0x41,0x80,0xb9,0x7d,0x7e,0x02,0x00,0x00,0x75,0x72,
                        0x41,0x80,0xb9,0x92,0x7e,0x02,0x00,0x04,0x74,0x68 }, 20 },          /* clock frozen */
    { 0x140976AFDULL, { 0xff,0x80,0x94,0x7f,0x02,0x00 }, 6 },                               /* frame counter */
    { 0x1409A8809ULL, { 0x48,0x8b,0x05,0xb0,0x00,0x97,0x01,0x80,0xb8,0xc4,0x80,0x02,0x00,0x00,0x75,0x69 }, 16 }, /* ordinary draw */
    { 0x1409A8865ULL, { 0x48,0x8b,0x0d,0x54,0x00,0x97,0x01,0x48,0x81,0xc1,0x74,0x79,0x02,0x00,0xe8,0xc8,0x9b,0x58,0x00 }, 19 }, /* loads R+0x27974 */
    { 0x1409B42CCULL, { 0x48,0x8d,0x4b,0x30,0xe8,0x4b,0xf2,0x57,0x00 }, 9 },               /* model at +0x30 */
    { 0x140F3270FULL, { 0x48,0x05,0xac,0x80,0x1f,0x00,0x48,0xc1,0xe1,0x06 }, 10 },          /* slot = ctx+0x1F80AC+n*64 */
    { 0x140995CC8ULL, { 0x41,0xb8,0xe0,0x03,0x00,0x00,0x48,0x69,0xc8,0xe0,0x03,0x00,0x00,
                        0x48,0x03,0x0d,0xe4,0x2b,0x98,0x01 }, 20 },                          /* pool R + i*0x3E0 */
    { 0x14098AA21ULL, { 0x41,0x0f,0xb6,0x51,0x02,0x80,0xea,0x02,0x80,0xfa,0x01,0x77,0x06,
                        0x45,0x38,0x59,0x04 }, 17 },                                         /* class 2/3, type +4 */
    { 0x1409861BCULL, { 0xb2,0x05 }, 2 },                                                   /* stone = type 5 */
    { 0x140986317ULL, { 0x0f,0xb6,0x8f,0xc8,0x03,0x00,0x00,0x8d,0x41,0xf1,0x3c,0x02 }, 12 }, /* food 15..17 */
    { 0x14098AB1EULL, { 0x48,0x0f,0xbe,0x88,0xc8,0x03,0x00,0x00,0x83,0xf9,0x11 }, 11 },     /* codes 0..17 */
    { 0x140986EF1ULL, { 0x80,0xf9,0x01,0x74,0x05,0x80,0xf9,0x06,0x75,0x2d }, 10 },          /* lifts type 1, 6 */
    { 0x14098AB00ULL, { 0x48,0x8b,0x81,0x80,0x77,0x00,0x00 }, 7 },                          /* carried +0x7780 */
    { 0x140976EC4ULL, { 0x0f,0xb6,0x81,0xf2,0x7f,0x02,0x00,0x34,0x03,0x88,0x81,0xf6,0x7f,0x02,0x00 }, 15 }, /* humans */
};

static BOOL code_matches(void)
{
    for (unsigned i = 0; i < sizeof g_sig / sizeof g_sig[0]; i++) {
        if (memcmp((const void *)(ULONG_PTR)g_sig[i].address, g_sig[i].bytes, g_sig[i].n)) {
            log_line("pws1-frame: ⛔ code at 0x%llx differs from the audited build; refusing",
                     g_sig[i].address);
            return FALSE;
        }
    }
    return TRUE;
}

BOOL pws1_frame_install(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (beside_exe("cfc2_hook.off")) {
        log_line("pws1-frame: disabled by cfc2_hook.off");
        return FALSE;
    }
    if (!VirtualQuery((LPCVOID)(ULONG_PTR)FRAME_FN, &mbi, sizeof mbi) ||
        mbi.State != MEM_COMMIT ||
        !VirtualQuery((LPCVOID)(ULONG_PTR)0x140F3270FULL, &mbi, sizeof mbi) ||
        mbi.State != MEM_COMMIT) {
        log_line("pws1-frame: ⚠ COULD NOT read 0x%llx -- concluding nothing", FRAME_FN);
        return FALSE;
    }
    if (memcmp((const void *)(ULONG_PTR)FRAME_FN, FRAME_SIG, sizeof FRAME_SIG)) {
        log_line("pws1-frame: ⛔ entry bytes differ from the audited build; refusing");
        return FALSE;
    }
    if (!code_matches()) return FALSE;
    if (MH_CreateHook((LPVOID)(ULONG_PTR)FRAME_FN, (LPVOID)frame_detour,
                      (LPVOID *)&g_real_frame) != MH_OK ||
        MH_EnableHook((LPVOID)(ULONG_PTR)FRAME_FN) != MH_OK) {
        log_line("pws1-frame: ⛔ MinHook refused 0x%llx", FRAME_FN);
        return FALSE;
    }
    InterlockedExchange(&g_installed, 1);
    log_line("pws1-frame: hooked Power Stone's per-frame update at 0x%llx", FRAME_FN);
    return TRUE;
}

void pws1_frame_remove(void)
{
    if (InterlockedCompareExchange(&g_installed, 0, 1) == 1)
        MH_DisableHook((LPVOID)(ULONG_PTR)FRAME_FN);
}

LONG pws1_frame_count(void) { return InterlockedCompareExchange(&g_frames, 0, 0); }

/* Rate-limited by TIME (a counter that always changes cannot gate itself). */
void pws1_frame_log_stats(void)
{
    static ULONGLONG last_tick;
    static LONG last_frames, last_changes;
    ULONGLONG now = GetTickCount64();
    LONG f, c;
    if (last_tick == 0) { last_tick = now; return; }
    if (now - last_tick < 10000) return;
    f = g_frames; c = g_changes;
    if (f != last_frames)
        log_line("pws1-frame: %ld frames (%.0f/s), %ld health-change(s), %ld pickup ping(s) in all",
                 f, (double)(f - last_frames) * 1000.0 / (double)(now - last_tick),
                 c - last_changes, (long)g_pings);
    last_tick = now; last_frames = f; last_changes = c;
}
