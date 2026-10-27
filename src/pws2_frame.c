/* See pws2_frame.h. Structure follows pws1_frame.c: one pointer read and plain
 * arithmetic on the game's frame thread, no syscalls, no locks. */

#include "pws2_frame.h"
#include "audio.h"
#include "log.h"
#include "render_project.h"
#include "pickup_cues.h"

#include "../libs/minhook/include/MinHook.h"

#include <math.h>
#include <string.h>

#define FRAME_FN   0x140A47290ull   /* update; its only caller is the per-frame
                                       callback 0x140A46420 (call at 0x140A46429) */
#define ROOT_PTR   0x14262F0B0ull
/* sub rsp,0x28 ; call 0x140036B70 -- the shape the PWS1 hook takes */
static const unsigned char FRAME_SIG[] = { 0x48,0x83,0xEC,0x28, 0xE8,0xD7,0xF8,0x5E,0xFF };

#define FIGHTERS    4u
#define FIGHTER(p)  (0x40818u + (p) * 0x42B8u)
#define OFF_SLOT    0x1u
#define OFF_HP      0x1DCu
#define OFF_POS     0x3Cu
#define OFF_IN      0x401Cu
#define OFF_CPU     0x4028u
#define OFF_STONES  0x4055u
#define OFF_CARRIED 0x4080u
/* Every fighter starts the round at 1000.0 (0x140A66A99) and food caps there
 * (0x140A5588E). Live: 1000 at the start, 0.0 on the KO frame. */
#define MAX_HP      1000
/* The collector (0x140ADC7A3) and the pickup (0x140A55799) refuse a fourth. */
#define MAX_STONES  3
#define OFF_TOP     0x52BD8u
#define OFF_NESTED  0x52BD9u
#define OFF_GAME    0x52BDCu
#define OFF_STEP    0x52BDDu
#define OFF_GATE1   0x52D2Bu
#define OFF_GATE2   0x52D2Eu
#define OFF_FRAMES  0x52D34u
#define OFF_VIEWIX  0x52E88u
#define OFF_CLOCK   0x52EE0u
#define OFF_MODE    0x52EE5u
#define OFF_SPLIT   0x52FDCu     /* nonzero: two-pass draw, not decoded      */
#define STEP_FIGHT  5
#define MODE_READY  1
#define MODE_FIGHT  2
/* Live: 180 at the start, one step per 60 frames. */
#define MAX_CLOCK   999
/* The view the ordinary draw copies into renderer slot 2 (0x140A5D6A0):
 * R+0x51D90 + 0x308 * signed R+0x52E88. Projection: slot 3. */
#define OFF_VIEW    0x51D90u
#define VIEW_STRIDE 0x308u
#define CONTEXT_PTR 0x142C79B40ull
#define PROJ_OFF    0x1F816Cu

/* The arena's objects: a pool of 200 records of 0x4C8 at R+0x4B10 (built at
 * 0x140A4BF69). +0 is the list the object is on; the pickup handler
 * 0x140A55430 walks list 9 (head R+0x40798) and takes an object whose state
 * +0x4B1 is 2. Its code +0x4B0 indexes the item table 0x1417319E0 (0x34
 * bytes an entry), whose first byte is the category the handler switches on
 * (0x140A5565E). */
#define POOL_OFF    0x4B10u
#define POOL_COUNT  200u
#define POOL_STRIDE 0x4C8u
#define OBJ_LIST    0x0u
#define OBJ_POS     0x3Cu
#define OBJ_PARENT  0xB8u
#define OBJ_CODE    0x4B0u
#define OBJ_STATE   0x4B1u
#define LIST_ARENA  9
#define STATE_LYING 2

/* The item table's category bytes for codes 0..0xCB, from the reviewed dump;
 * install refuses if the game's table differs. Read off the handler:
 *   4 a power stone (the branch that increments A+0x4055)
 *   3 food: adds the entry's word +0x1A to health, capped at 1000; codes
 *     0x4A, 0x75 and 0x78 branch elsewhere before that
 *   6 treasure: adds to the side's score (0x140A59B00)
 *   9 a collectible the game records in a list at R+0x5436A
 *   0 the rest; codes below 0xC0 are the things fighters carry and use
 * Code 0xC3 is the liftable prop (the code fighters carried most in the
 * capture, 8 to 18 lying at once); 0xC8 was also carried. 0xC2 and 0xCA sit
 * at fixed places and are never carried: stage fixtures, not pinged. */
#define ITEM_CODES 0xCCu
static const unsigned char pws2_item_category[ITEM_CODES] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,1,1,1,0,3,0,0,0,0,0,0,0,0,3,3,3,3,3,3,2,2,2,2,2,2,2,
    2,2,2,2,0,0,0,2,3,0,0,0,2,2,2,2,2,2,0,0,0,3,2,2,3,0,0,0,0,0,0,0,
    0,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    5,4,0,0,1,0,6,1,7,0,1,8,
};
#define ITEM_TABLE  0x1417319E0ull
#define ITEM_STRIDE 0x34u
#define CODE_STONE  0xC1u
#define CODE_PROP   0xC3u
#define CODE_PROP2  0xC8u
static const unsigned char g_food[] = { 0x10,0x11,0x12,0x53,0x54,0x55,0x56,0x57,0x58,0x68 };

typedef void (*pfn_frame)(void);
static pfn_frame g_real_frame;
static volatile LONG g_frames, g_changes, g_installed, g_pings;
static int g_last_hp[MATCH_AUDIO_OWNERS] = { -1, -1, -1, -1 };
static match_audio_producer g_prod = {
    .feeds = MATCH_AUDIO_FEED_HEALTH | MATCH_AUDIO_FEED_RESOURCE | MATCH_AUDIO_FEED_CLOCK
};
static pickup_cues g_cues;

static float f32(const unsigned char *s, unsigned off) { float v; memcpy(&v, s + off, 4); return v; }
static short i16(const unsigned char *s, unsigned off) { short v; memcpy(&v, s + off, 2); return v; }
static uint32_t u32(const unsigned char *s, unsigned off) { uint32_t v; memcpy(&v, s + off, 4); return v; }
static uint64_t u64(const unsigned char *s, unsigned off) { uint64_t v; memcpy(&v, s + off, 8); return v; }

static bool in_fight_step(const unsigned char *s)
{
    return s[OFF_TOP] == 1 && s[OFF_NESTED] == 2 && s[OFF_GAME] == 1 && s[OFF_STEP] == STEP_FIGHT &&
           !s[OFF_GATE1] && !s[OFF_GATE2];
}

bool pws2_fighting(const unsigned char *s)
{
    /* Mode leaves 2 the frame the round is decided (live: one sample after
     * the human's health reached 0). Single view only. */
    return in_fight_step(s) && s[OFF_MODE] == MODE_FIGHT && !s[OFF_SPLIT] && s[OFF_VIEWIX] < 4;
}

uint32_t pws2_frame_counter(const unsigned char *s) { return u32(s, OFF_FRAMES); }

bool pws2_pickup_kind(unsigned code, audio_ping_family *family, int *variant)
{
    unsigned cat;
    if (code >= ITEM_CODES) return false;
    cat = pws2_item_category[code];
    *variant = 0;
    if (cat == 4) { *family = AUDIO_PING_STONE; return true; }
    if (code == CODE_PROP || code == CODE_PROP2) {
        *family = AUDIO_PING_PROP;
        *variant = code == CODE_PROP2;
        return true;
    }
    if (cat == 3) {
        for (unsigned k = 0; k < sizeof g_food; k++) if (g_food[k] == code) {
            *family = AUDIO_PING_FOOD;
            *variant = (int)k;
            return true;
        }
    } else if (!(cat == 6 || cat == 9 || (cat == 0 && code < 0xC0))) {
        return false;
    }
    /* Dozens of kinds and two octaves of pitch: a type's semitone repeats
     * every 25 codes. */
    *family = AUDIO_PING_ITEM;
    *variant = (int)(code % 25u);
    return true;
}

static bool view_of(const unsigned char *s, float v[16])
{
    unsigned i = s[OFF_VIEWIX];
    if (i >= 4) return false;
    memcpy(v, s + OFF_VIEW + i * VIEW_STRIDE, 64);
    /* A rigid camera: affine, or it is not one. */
    return v[3] == 0.0f && v[7] == 0.0f && v[11] == 0.0f && v[15] == 1.0f;
}

/* Where a world point is drawn. 1: on screen, `pan` its horizontal position
 * (-1 left edge .. +1 right edge); 0: off screen -- behind the camera, or
 * past an edge either way; -1: cannot tell (no projection, not a perspective
 * camera, not a rigid view, non-finite input). Nothing off screen is sounded
 * (user, 2026-10-27: nothing there can be picked up, hit or hit you). */
#define OFF_SCREEN 0     /* render_project_screen's results */
#define ON_SCREEN  1
static int screen(const unsigned char *s, const float proj[16], const float pt[3], float *pan,
                  bool offscreen_ok)
{
    float cam[32];
    int r;
    if (!proj || !view_of(s, cam)) return -1;
    memcpy(cam + 16, proj, 64);
    r = render_project_screen(cam, pt, pan);
    /* Off-screen sounds switched on (Shift+F9): at the edge it lies beyond. */
    if (r == OFF_SCREEN && offscreen_ok && render_project_side(cam, pt, pan)) return ON_SCREEN;
    return r;
}

static int place(const unsigned char *s, const float proj[16], const float pt[3], float *pan)
{
    return screen(s, proj, pt, pan, audio_option_get(AUDIO_OPT_OFFSCREEN) != 0);
}

bool pws2_project(const unsigned char *s, const float proj[16], const float pt[3], float *pan)
{
    return screen(s, proj, pt, pan, false) == ON_SCREEN;
}

static bool human(const unsigned char *s, unsigned p)
{
    unsigned a = FIGHTER(p);
    return s[a + OFF_SLOT] == p && !s[a + OFF_CPU];
}

void pws2_pickups_decode(const unsigned char *s, uint64_t root, const float proj[16],
                         pickup_sample out[PICKUP_SLOTS])
{
    float me[FIGHTERS][3];
    uint64_t carried[FIGHTERS];
    unsigned n = 0;
    memset(out, 0, sizeof(pickup_sample) * PICKUP_SLOTS);
    if (!pws2_fighting(s)) return;
    for (unsigned p = 0; p < FIGHTERS; p++) {
        unsigned a = FIGHTER(p);
        carried[p] = u64(s, a + OFF_CARRIED);
        if (!human(s, p) || !s[a + OFF_IN]) continue;
        for (unsigned k = 0; k < 3; k++) me[n][k] = f32(s, a + OFF_POS + 4 * k);
        n++;
    }
    for (unsigned i = 0; i < POOL_COUNT; i++) {
        unsigned off = POOL_OFF + i * POOL_STRIDE;
        const unsigned char *o = s + off;
        pickup_sample *q = &out[i];
        uint64_t addr = root + off;
        float pt[3];
        bool held = false;
        if (o[OBJ_LIST] != LIST_ARENA || o[OBJ_STATE] != STATE_LYING) continue;
        for (unsigned p = 0; p < FIGHTERS; p++) held |= carried[p] == addr;
        if (held || !pws2_pickup_kind(o[OBJ_CODE], &q->family, &q->variant)) continue;
        q->present = true;
        /* On a parent (a moving platform) the position is local: no pan. */
        if (u64(o, OBJ_PARENT)) continue;
        for (unsigned k = 0; k < 3; k++) pt[k] = f32(o, OBJ_POS + 4 * k);
        /* Off screen: present, silent; it pings once it comes into view. */
        if (!n || place(s, proj, pt, &q->pan) != ON_SCREEN) continue;
        q->distance = 1e30f;
        for (unsigned k = 0; k < n; k++) {
            float dx = pt[0] - me[k][0], dy = pt[1] - me[k][1], dz = pt[2] - me[k][2];
            float d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d < q->distance) q->distance = d;
        }
        q->placed = isfinite(q->distance);
    }
}

void pws2_frame_decode(const unsigned char *s, const float proj[16], match_audio_sample *m)
{
    int mode = s[OFF_MODE];

    memset(m, 0, sizeof *m);
    if (!in_fight_step(s) || (mode != MODE_READY && mode != MODE_FIGHT)) return;
    m->in_match = true;

    /* Every fighter, CPU too (user, 2026-10-27), as owner = fighter index:
     * each its own articulation, its tones panned to where it is on screen. */
    m->fighter_gauges = true;
    for (unsigned p = 0; p < FIGHTERS; p++) {
        unsigned a = FIGHTER(p);
        float hp = f32(s, a + OFF_HP), pt[3];
        int stones = s[a + OFF_STONES], v;
        match_audio_owner *o = &m->owner[p];
        /* The selection driver indexes the side by this byte; a record that
         * does not name its own slot is not one we can place. */
        if (s[a + OFF_SLOT] != p) continue;
        o->present = true;
        for (unsigned k = 0; k < 3; k++) pt[k] = f32(s, a + OFF_POS + 4 * k);
        switch (place(s, proj, pt, &o->gauge_pan)) {
        case ON_SCREEN:  o->gauge_pan_valid = true; break;
        /* Off screen: its tones are not played. A change there is not heard;
         * the first value back on screen seeds silently (match_audio). */
        case OFF_SCREEN: continue;
        default:         break;            /* no camera: centred */
        }
        o->health.valid = hp >= 0.0f && hp <= (float)MAX_HP;   /* false for NaN too */
        v = o->health.valid ? (int)(hp + 0.5f) : 0;
        if (v == 0 && hp > 0.0f) v = 1;          /* alive is never "empty" */
        o->health.value = v;
        o->health.maximum = MAX_HP;
        o->resource.valid = stones <= MAX_STONES;
        o->resource.value = stones;
        o->resource.maximum = MAX_STONES;
    }
    if (mode == MODE_FIGHT) {
        int clock = i16(s, OFF_CLOCK);
        m->clock_valid = clock >= 0 && clock <= MAX_CLOCK;
        m->clock = clock;
    }
}

/* ---- pickup announcements ------------------------------------------------
 * Who picked up what (user, 2026-10-27: any player may be the blind one, so
 * every fighter's pickups, CPU too; F9 cycles every fighter / players only /
 * off). Two exact sources, both on the game thread:
 *   - touched: the pickup handler 0x140A55430 (one call per fighter, RCX the
 *     fighter; returns 1 exactly when something was taken, 0x140A55B4D). The
 *     object it took is the one lying before the call and gone after it.
 *   - lifted: the fighter's carried pointer A+0x4080 becoming a new object.
 * Events go to the worker through a single-producer ring; it speaks them. */
#define PICKUP_FN 0x140A55430ull
static const unsigned char PICKUP_SIG[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20 };
typedef unsigned long long (*pfn_pickup)(void *, void *, void *, void *);
static pfn_pickup g_real_pickup;
static volatile LONG g_pickup_installed;

static pickup_ring g_ring;
static void push(const pws2_pickup_event *e) { pickup_ring_push(&g_ring, e); }

static void push_event(uint64_t epoch, const unsigned char *s, unsigned p, unsigned code)
{
    unsigned a = FIGHTER(p);
    push(&(pws2_pickup_event){ .kind = PWS2_EVENT_PICKUP, .epoch = epoch, .fighter = p,
                               .id = s[a + 2], .code = code, .human = !s[a + OFF_CPU] });
}

/* ---- stage select ----------------------------------------------------------
 * Step 3 with Stage Selection: Manual (live, 2026-10-27, Versus): the
 * fighter drops in, then walks to one of five stage spots. The walk
 * 0x140A6A090 measures the flat distance from each fighter in play
 * (A+0x3C, A+0x44) to each spot; the first within 300 is chosen; a 20 s
 * countdown (R+0x52BE8, from 1199) picks one at random if none is. The
 * choice R+0x52BEA becomes the fight's stage R+0x53010 (0x140A69971). The
 * spots are list-5 objects with callback 0x140A68A00, +2 the stage 0..4:
 * live at (-740,700) (-530,177) (0,-40) (530,177) (740,700), an arc ahead of
 * where the fighter lands (-10,680). */
#define SPOT_CB        0x140A68A00ull
#define SPOT_RADIUS_AT 0x140FE478Cull
#define SPOT_RADIUS    300.0f
#define SPOTS          5u
#define OFF_SUBPHASE   0x52BDEu
#define OFF_SUBSTEP    0x52BDFu
#define OFF_STAGE_PICK 0x52BEAu

bool pws2_stage_walk(const unsigned char *s)
{
    /* Mode 1 from the moment the fighter lands (live, both captures). */
    return s[OFF_TOP] == 1 && s[OFF_NESTED] == 2 && s[OFF_GAME] == 1 && s[OFF_STEP] == 3 &&
           s[OFF_SUBPHASE] == 1 && s[OFF_SUBSTEP] == 0 && s[OFF_MODE] == MODE_READY &&
           !s[OFF_GATE1] && !s[OFF_GATE2] && !s[OFF_SPLIT] && s[OFF_VIEWIX] < 4;
}

int pws2_stage_chosen(const unsigned char *s)
{
    if (s[OFF_TOP] != 1 || s[OFF_NESTED] != 2 || s[OFF_STEP] != 3 || s[OFF_SUBPHASE] != 2) return -1;
    return s[OFF_STAGE_PICK] < SPOTS ? s[OFF_STAGE_PICK] : -1;
}

static int spot_index(const unsigned char *s, unsigned i)
{
    const unsigned char *o = s + POOL_OFF + i * POOL_STRIDE;
    if (o[OBJ_LIST] != 5 || u64(o, 8) != SPOT_CB || o[2] >= SPOTS) return -1;
    return o[2];
}

/* The person whose position the stage select measures from: the first by
 * fighter index that a person plays and is in play. */
static int walker(const unsigned char *s)
{
    for (unsigned p = 0; p < FIGHTERS; p++)
        if (human(s, p) && s[FIGHTER(p) + OFF_IN]) return (int)p;
    return -1;
}

void pws2_spots_decode(const unsigned char *s, const float proj[16], pickup_sample out[PICKUP_SLOTS])
{
    int w = walker(s);
    memset(out, 0, sizeof(pickup_sample) * PICKUP_SLOTS);
    if (!pws2_stage_walk(s)) return;
    for (unsigned i = 0; i < POOL_COUNT; i++) {
        int k = spot_index(s, i);
        float pt[3], dx, dz;
        pickup_sample *q = &out[i];
        if (k < 0) continue;
        q->present = true;
        /* A plain tone, its own pitch per stage (user, 2026-10-27, after the
         * bell's whole-tone steps sounded too alike): C, E, G, B, D -- apart,
         * and no two an octave apart. */
        q->family = AUDIO_PING_TONE;
        q->variant = (int)(k == 0 ? 0 : k == 1 ? 4 : k == 2 ? 7 : k == 3 ? 11 : 14);
        for (unsigned c = 0; c < 3; c++) pt[c] = f32(s + POOL_OFF + i * POOL_STRIDE, OBJ_POS + 4 * c);
        /* A spot off screen still pings, at its edge: unlike a pickup, it can
         * be walked to unseen. */
        if (w < 0 || screen(s, proj, pt, &q->pan, true) != ON_SCREEN) continue;
        /* Flat distance, as the game measures it, less the radius that
         * selects: 0 means one more step chooses it. */
        dx = pt[0] - f32(s, FIGHTER(w) + OFF_POS);
        dz = pt[2] - f32(s, FIGHTER(w) + OFF_POS + 8);
        q->distance = sqrtf(dx * dx + dz * dz) - SPOT_RADIUS;
        if (q->distance < 0.0f) q->distance = 0.0f;
        q->placed = isfinite(q->distance);
    }
}

int pws2_spot_direction(const unsigned char *s, unsigned stage)
{
    float v[16], ex, ez, ang;
    int w = walker(s);
    if (w < 0 || !view_of(s, v)) return -1;
    for (unsigned i = 0; i < POOL_COUNT; i++) {
        float d[3];
        if (spot_index(s, i) != (int)stage) continue;
        for (unsigned c = 0; c < 3; c++)
            d[c] = f32(s + POOL_OFF + i * POOL_STRIDE, OBJ_POS + 4 * c) - f32(s, FIGHTER(w) + OFF_POS + 4 * c);
        /* Into the camera's frame: x right, z toward the viewer; ahead is -z.
         * Height is ignored, as the selection ignores it. */
        ex = d[0] * v[0] + d[2] * v[8];
        ez = d[0] * v[2] + d[2] * v[10];
        if (!isfinite(ex) || !isfinite(ez) || (ex == 0.0f && ez == 0.0f)) return -1;
        ang = atan2f(ex, -ez) * (180.0f / 3.14159265f);    /* 0 ahead, 90 right */
        return ((int)floorf((ang + 22.5f + 360.0f) / 45.0f)) % 8;
    }
    return -1;
}

bool pws2_pickup_pop(pws2_pickup_event *e) { return pickup_ring_pop(&g_ring, e); }

void pws2_lying_snapshot(const unsigned char *s, unsigned short codes[POOL_COUNT])
{
    for (unsigned i = 0; i < POOL_COUNT; i++) {
        const unsigned char *o = s + POOL_OFF + i * POOL_STRIDE;
        codes[i] = o[OBJ_LIST] == LIST_ARENA && o[OBJ_STATE] == STATE_LYING ? 0x100u | o[OBJ_CODE] : 0;
    }
}

int pws2_taken(const unsigned char *s, const unsigned short before[POOL_COUNT])
{
    unsigned short now[POOL_COUNT];
    pws2_lying_snapshot(s, now);
    for (unsigned i = 0; i < POOL_COUNT; i++)
        if (before[i] && now[i] != before[i]) return (int)(before[i] & 0xFF);
    return -1;
}

int pws2_carried_code(const unsigned char *s, uint64_t root, uint64_t carried)
{
    uint64_t off;
    if (carried < root + POOL_OFF) return -1;
    off = carried - root - POOL_OFF;
    if (off % POOL_STRIDE || off / POOL_STRIDE >= POOL_COUNT) return -1;
    return s[POOL_OFF + off + OBJ_CODE];
}

unsigned pws2_watch_events(const unsigned char *s, uint64_t root, pws2_pickup_watch *w,
                           unsigned who[8], unsigned code[8])
{
    unsigned n = 0;
    bool on = pws2_fighting(s);
    for (unsigned p = 0; p < FIGHTERS; p++) {
        unsigned a = FIGHTER(p);
        uint64_t c = u64(s, a + OFF_CARRIED);
        unsigned stones = s[a + OFF_STONES];
        int k;
        if (on && w->valid && s[a + OFF_SLOT] == p) {
            /* Lifted: the carried pointer naming a new object. */
            if (c && c != w->carried[p] && (k = pws2_carried_code(s, root, c)) >= 0) {
                who[n] = p; code[n++] = (unsigned)k;
            }
            /* A stone, however it came: the count rising. A stone caught as it
             * falls into the arena is never a lying object (live: 5 of the 20
             * stones gained in the capture). */
            if (stones > w->stones[p] && stones <= MAX_STONES) {
                who[n] = p; code[n++] = CODE_STONE;
            }
        }
        w->carried[p] = on ? c : 0;
        w->stones[p] = (unsigned char)stones;
    }
    w->valid = on;
    return n;
}

static unsigned long long pickup_detour(void *fighter, void *b, void *c, void *d)
{
    static unsigned short before[POOL_COUNT];
    uint64_t epoch = match_audio_current_for(5);
    ULONGLONG root = *(volatile ULONGLONG *)(ULONG_PTR)ROOT_PTR;
    const unsigned char *s = (const unsigned char *)(ULONG_PTR)root;
    ULONGLONG a = (ULONGLONG)(ULONG_PTR)fighter, rel;
    unsigned long long r;
    int code;
    if (!epoch || !root || root >= 0x00007FFF00000000ull || a < root + FIGHTER(0) ||
        (rel = a - root - FIGHTER(0)) % 0x42B8u || rel / 0x42B8u >= FIGHTERS || !pws2_fighting(s))
        return g_real_pickup(fighter, b, c, d);
    pws2_lying_snapshot(s, before);
    r = g_real_pickup(fighter, b, c, d);
    /* Stones are announced from the stone count (pws2_watch_events). */
    if ((unsigned)r == 1 && (code = pws2_taken(s, before)) >= 0 &&
        (code >= (int)ITEM_CODES || pws2_item_category[code] != 4))
        push_event(epoch, s, (unsigned)(rel / 0x42B8u), (unsigned)code);
    return r;
}

static void frame_detour(void)
{
    InterlockedIncrement(&g_frames);
    uint64_t epoch = match_audio_current_for(5);   /* PWS2 is game 5 */
    ULONGLONG root = *(volatile ULONGLONG *)(ULONG_PTR)ROOT_PTR;
    if (epoch && root && root < 0x00007FFF00000000ull) {
        static uint32_t last_counter;
        const unsigned char *s = (const unsigned char *)(ULONG_PTR)root;
        match_audio_sample m;
        float proj[16];
        ULONGLONG ctx = *(volatile ULONGLONG *)(ULONG_PTR)CONTEXT_PTR;
        BOOL have_cam = ctx && ctx < 0x00007FFF00000000ull;
        if (have_cam) memcpy(proj, (const void *)(ULONG_PTR)(ctx + PROJ_OFF), 64);
        pws2_frame_decode(s, have_cam ? proj : NULL, &m);
        for (int p = 0; p < MATCH_AUDIO_OWNERS; p++) {
            int v = m.in_match && m.owner[p].health.valid ? m.owner[p].health.value : -1;
            if (v >= 0 && g_last_hp[p] != v) InterlockedIncrement(&g_changes);
            g_last_hp[p] = v;
        }
        match_audio_feed(&g_prod, epoch, &m);
        {
            static pws2_pickup_watch watch;
            unsigned who[2 * FIGHTERS], code[2 * FIGHTERS];
            unsigned n = pws2_watch_events(s, root, &watch, who, code);
            for (unsigned k = 0; k < n; k++) push_event(epoch, s, who[k], code[k]);
        }
        {
            /* The stage select: the directions once the fighter has landed,
             * the spots pinging while it walks, the stage once chosen. */
            static bool walking;
            static pickup_cues spot_cues;
            static uint32_t spot_counter;
            bool now = pws2_stage_walk(s);
            if (now && !walking) {
                pws2_pickup_event e = { .kind = PWS2_EVENT_STAGES, .epoch = epoch };
                bool all = true;
                for (unsigned k = 0; k < SPOTS; k++) {
                    int d = pws2_spot_direction(s, k);
                    all &= d >= 0;
                    e.code |= (unsigned)(d & 7) << (3 * k);
                }
                if (all) push(&e);
            }
            if (!now && walking) {
                int k = pws2_stage_chosen(s);
                if (k >= 0) push(&(pws2_pickup_event){ .kind = PWS2_EVENT_STAGE_CHOSEN, .epoch = epoch, .code = (unsigned)k });
            }
            walking = now;
            if (!now) {
                pickup_cues_reset(&spot_cues);
            } else if (pws2_frame_counter(s) != spot_counter) {
                static pickup_sample spots[PICKUP_SLOTS];
                pws2_spots_decode(s, have_cam ? proj : NULL, spots);
                pickup_cues_frame(&spot_cues, epoch, spots);
            }
            spot_counter = pws2_frame_counter(s);
        }
        if (!pws2_fighting(s)) {
            pickup_cues_reset(&g_cues);
        } else if (pws2_frame_counter(s) != last_counter) {
            /* Only on simulated frames: the counter stood still for the
             * whole pause in the capture, and so do the pings. */
            static pickup_sample pick[PICKUP_SLOTS];
            pws2_pickups_decode(s, root, have_cam ? proj : NULL, pick);
            pickup_cues_frame(&g_cues, epoch, pick);
            InterlockedExchange(&g_pings, (LONG)g_cues.pings);
        }
        last_counter = pws2_frame_counter(s);
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

/* Code the decode rests on, checked before hooking. Bytes from the reviewed
 * dump. */
static const struct { ULONGLONG address; unsigned char bytes[33]; unsigned n; } g_sig[] = {
    { 0x140A46424ULL, { 0xe8,0x97,0x9a,0x4e,0x00,0xe8,0x62,0x0e,0x00,0x00 }, 10 },   /* the only call */
    { 0x140A48CCBULL, { 0x0f,0xb6,0x8a,0xd8,0x2b,0x05,0x00 }, 7 },   /* top phase R+0x52BD8 */
    { 0x140A48D46ULL, { 0x80,0xb8,0x2b,0x2d,0x05,0x00,0x00,0x0f,0xb6,0x98,0xd9,0x2b,0x05,0x00,0x75,0x51,0x80,0xb8,0x2e,0x2d,0x05,0x00,0x00 }, 23 },   /* gates, nested phase R+0x52BD9 */
    { 0x140A48D7BULL, { 0xe8,0xc0,0x20,0x01,0x00,0xe8,0xab,0x3c,0x00,0x00 }, 10 },   /* nested 2 -> 0x140a4ca30 */
    { 0x140A4CA3FULL, { 0x0f,0xb6,0x91,0xdc,0x2b,0x05,0x00 }, 7 },   /* game phase R+0x52BDC */
    { 0x140A4CB48ULL, { 0x0f,0xb6,0x81,0xdd,0x2b,0x05,0x00 }, 7 },   /* step R+0x52BDD */
    { 0x140A4CD83ULL, { 0xe8,0x48,0xf9,0xff,0xff }, 5 },   /* step 5 -> 0x140a4c6d0 */
    { 0x140A6FF4AULL, { 0x80,0xb8,0xe5,0x2e,0x05,0x00,0x02,0x75,0x46,0x66,0xff,0x88,0xe2,0x2e,0x05,0x00,0x66,0x39,0x98,0xe2,0x2e,0x05,0x00,0x7f,0x2a,0x48,0x8b,0x05,0x46,0xf1,0xbb }, 31 },   /* clock: mode 2, sub-counter */
    { 0x140A6FF82ULL, { 0x66,0x83,0x81,0xe0,0x2e,0x05,0x00,0xff }, 8 },   /* .. word R+0x52EE0 */
    { 0x140A4EA0BULL, { 0x80,0xb8,0x2b,0x2d,0x05,0x00,0x00,0x0f,0x85,0xdf,0x00,0x00,0x00,0x80,0xb8,0x2e,0x2d,0x05,0x00,0x00,0x0f,0x85,0xd2,0x00,0x00,0x00,0xff,0x80,0x34,0x2d,0x05,0x00 }, 32 },   /* frame counter R+0x52D34 */
    { 0x140A66A99ULL, { 0xc7,0x83,0xdc,0x01,0x00,0x00,0x00,0x00,0x7a,0x44,0xe8,0x88,0x95,0x08,0x00,0x89,0xb3,0x55,0x40,0x00,0x00 }, 21 },   /* health 1000.0, one stone */
    { 0x140A5586DULL, { 0xf3,0x0f,0x58,0x83,0xdc,0x01,0x00,0x00,0xf3,0x0f,0x11,0x83,0xdc,0x01,0x00,0x00,0xf3,0x0f,0x10,0x83,0xdc,0x01,0x00,0x00,0x0f,0x2f,0x05,0x84,0x19,0x5c,0x00,0x76,0x0a }, 33 },   /* food heals, caps */
    { 0x140A5588EULL, { 0xc7,0x83,0xdc,0x01,0x00,0x00,0x00,0x00,0x7a,0x44 }, 10 },   /* .. at 1000.0 */
    { 0x140ADC7A3ULL, { 0x80,0xb9,0x55,0x40,0x00,0x00,0x03,0x7d,0x29 }, 9 },   /* no fourth stone */
    { 0x140ADC7C9ULL, { 0xfe,0x83,0x55,0x40,0x00,0x00 }, 6 },   /* stone given */
    { 0x140A55799ULL, { 0x0f,0xb6,0x83,0x55,0x40,0x00,0x00,0x3c,0x03,0x0f,0x8d }, 11 },   /* pickup: no fourth stone */
    { 0x140A557C4ULL, { 0xfe,0x83,0x55,0x40,0x00,0x00 }, 6 },   /* pickup: stone collected */
    { 0x140A5565EULL, { 0x0f,0xb6,0xbe,0xb0,0x04,0x00,0x00,0x48,0x6b,0xd7,0x34,0x42,0x0f,0xbe,0x8c,0x32,0xe0,0x19,0x73,0x01,0x85,0xc9,0x0f }, 23 },   /* item code, table */
    { 0x140A55481ULL, { 0x48,0x8b,0xb8,0x98,0x07,0x04,0x00 }, 7 },   /* list 9 head R+0x40798 */
    { 0x140A55522ULL, { 0x80,0xbf,0xb1,0x04,0x00,0x00,0x02 }, 7 },   /* lying: state +0x4B1 = 2 */
    { 0x140A5552FULL, { 0xf3,0x0f,0x10,0x4f,0x3c }, 5 },   /* position +0x3C */
    { 0x140A55543ULL, { 0x48,0x8b,0x8f,0xb8,0x00,0x00,0x00 }, 7 },   /* parent +0xB8 */
    { 0x140A5559BULL, { 0xf3,0x0f,0x5c,0x43,0x44,0xf3,0x0f,0x5c,0x4b,0x3c,0xf3,0x0f,0x11,0x45,0xcf }, 15 },   /* fighter position */
    { 0x140A5619DULL, { 0x48,0x89,0xbb,0x80,0x40,0x00,0x00 }, 7 },   /* carried +0x4080 */
    { 0x140A59B11ULL, { 0x80,0xb9,0x28,0x40,0x00,0x00,0x00 }, 7 },   /* CPU flag +0x4028 */
    { 0x140A4BF69ULL, { 0x48,0x81,0xc1,0x10,0x4b,0x00,0x00,0x41,0xb8,0xc8,0x04,0x00,0x00 }, 13 },   /* pool R+0x4B10, 0x4C8 */
    { 0x140A4BFAFULL, { 0x81,0xff,0xc8,0x00,0x00,0x00 }, 6 },   /* .. 200 records */
    { 0x140A5D652ULL, { 0x38,0x90,0xdc,0x2f,0x05,0x00 }, 6 },   /* ordinary draw R+0x52FDC = 0 */
    { 0x140A5D682ULL, { 0x0f,0xbe,0x88,0x88,0x2e,0x05,0x00 }, 7 },   /* view index R+0x52E88 */
    { 0x140A5D6A0ULL, { 0x48,0x69,0xc8,0x08,0x03,0x00,0x00,0x48,0x8b,0x05,0x02,0x1a,0xbd,0x01,0x48,0x05,0x90,0x1d,0x05,0x00 }, 20 },   /* view -> slot 2 */
    { 0x140F3270FULL, { 0x48,0x05,0xac,0x80,0x1f,0x00,0x48,0xc1,0xe1,0x06 }, 10 },   /* slot = ctx+0x1F80AC+n*64 */
    { 0x140A68A00ULL, { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b }, 12 },   /* stage spot callback (list 5, +2 = stage 0..4) */
    { 0x140A6A2D1ULL, { 0x48,0x8d,0x35,0x68,0x26,0xf2,0x01 }, 7 },   /* the five spots, a table of object pointers */
    { 0x140A6A2E3ULL, { 0xf3,0x43,0x0f,0x10,0x8c,0x37,0x5c,0x08,0x04,0x00,0xf3,0x43,0x0f,0x10,0x84,0x37,0x54,0x08,0x04,0x00,0xf3,0x0f,0x5c,0x40,0x3c,0xf3,0x0f,0x5c,0x48,0x44 }, 30 },   /* flat distance fighter (+0x3C,+0x44) to spot */
    { 0x140A6A36DULL, { 0x83,0xfb,0x05 }, 3 },   /* .. five of them */
    { 0x140A6A3E9ULL, { 0x66,0x41,0x83,0xae,0xe8,0x2b,0x05,0x00,0x01 }, 9 },   /* 20 s countdown R+0x52BE8 */
    { 0x140A69971ULL, { 0x0f,0xb6,0x81,0xea,0x2b,0x05,0x00,0x88,0x81,0x10,0x30,0x05,0x00 }, 13 },   /* choice R+0x52BEA -> fight stage R+0x53010 */
    { 0x140A6990BULL, { 0x0f,0xb6,0x91,0xde,0x2b,0x05,0x00 }, 7 },   /* step 3 sub-phase R+0x52BDE */
    { 0x140B14A68ULL, { 0x48,0x8b,0xcb,0xe8,0xc0,0x09,0xf4,0xff,0x85,0xc0 }, 10 },   /* pickup handler: RCX fighter, tests EAX */
    { 0x140A55B4DULL, { 0xb8,0x01,0x00,0x00,0x00,0xeb,0x02,0x33,0xc0 }, 9 },   /* .. 1 taken, else 0 */
    { 0x140A5619DULL, { 0x48,0x89,0xbb,0x80,0x40,0x00,0x00,0x33,0xed,0x0f,0xb6,0x8f,0xb0,0x04,0x00,0x00 }, 16 },   /* lift: carried, code */
};

static BOOL code_matches(void)
{
    for (unsigned i = 0; i < sizeof g_sig / sizeof g_sig[0]; i++) {
        if (memcmp((const void *)(ULONG_PTR)g_sig[i].address, g_sig[i].bytes, g_sig[i].n)) {
            log_line("pws2-frame: ⛔ code at 0x%llx differs from the audited build; refusing",
                     g_sig[i].address);
            return FALSE;
        }
    }
    {
        float radius;
        memcpy(&radius, (const void *)(ULONG_PTR)SPOT_RADIUS_AT, 4);
        if (radius != SPOT_RADIUS) {
            log_line("pws2-frame: ⛔ the stage-select radius differs from the audited build; refusing");
            return FALSE;
        }
    }
    for (unsigned c = 0; c < ITEM_CODES; c++) {
        if (*(const unsigned char *)(ULONG_PTR)(ITEM_TABLE + c * ITEM_STRIDE) != pws2_item_category[c]) {
            log_line("pws2-frame: ⛔ item table entry 0x%x differs from the audited build; refusing", c);
            return FALSE;
        }
    }
    return TRUE;
}

BOOL pws2_frame_install(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (beside_exe("cfc2_hook.off")) {
        log_line("pws2-frame: disabled by cfc2_hook.off");
        return FALSE;
    }
    if (!VirtualQuery((LPCVOID)(ULONG_PTR)FRAME_FN, &mbi, sizeof mbi) ||
        mbi.State != MEM_COMMIT ||
        !VirtualQuery((LPCVOID)(ULONG_PTR)0x140F3270FULL, &mbi, sizeof mbi) ||
        mbi.State != MEM_COMMIT ||
        !VirtualQuery((LPCVOID)(ULONG_PTR)ITEM_TABLE, &mbi, sizeof mbi) ||
        mbi.State != MEM_COMMIT) {
        log_line("pws2-frame: ⚠ COULD NOT read 0x%llx -- concluding nothing", FRAME_FN);
        return FALSE;
    }
    if (memcmp((const void *)(ULONG_PTR)FRAME_FN, FRAME_SIG, sizeof FRAME_SIG)) {
        log_line("pws2-frame: ⛔ entry bytes differ from the audited build; refusing");
        return FALSE;
    }
    if (!code_matches()) return FALSE;
    if (MH_CreateHook((LPVOID)(ULONG_PTR)FRAME_FN, (LPVOID)frame_detour,
                      (LPVOID *)&g_real_frame) != MH_OK ||
        MH_EnableHook((LPVOID)(ULONG_PTR)FRAME_FN) != MH_OK) {
        log_line("pws2-frame: ⛔ MinHook refused 0x%llx", FRAME_FN);
        return FALSE;
    }
    InterlockedExchange(&g_installed, 1);
    log_line("pws2-frame: hooked Power Stone 2's per-frame update at 0x%llx", FRAME_FN);
    /* The pickup handler, for touched pickups. Without it the rest still
     * works; lifted pickups are still announced. */
    if (memcmp((const void *)(ULONG_PTR)PICKUP_FN, PICKUP_SIG, sizeof PICKUP_SIG)) {
        log_line("pws2-frame: ⛔ pickup handler entry differs; touched pickups will not be announced");
    } else if (MH_CreateHook((LPVOID)(ULONG_PTR)PICKUP_FN, (LPVOID)pickup_detour,
                             (LPVOID *)&g_real_pickup) != MH_OK ||
               MH_EnableHook((LPVOID)(ULONG_PTR)PICKUP_FN) != MH_OK) {
        log_line("pws2-frame: ⛔ MinHook refused the pickup handler 0x%llx", PICKUP_FN);
    } else {
        InterlockedExchange(&g_pickup_installed, 1);
        log_line("pws2-frame: hooked the pickup handler at 0x%llx", PICKUP_FN);
    }
    return TRUE;
}

void pws2_frame_remove(void)
{
    if (InterlockedCompareExchange(&g_installed, 0, 1) == 1)
        MH_DisableHook((LPVOID)(ULONG_PTR)FRAME_FN);
    if (InterlockedCompareExchange(&g_pickup_installed, 0, 1) == 1)
        MH_DisableHook((LPVOID)(ULONG_PTR)PICKUP_FN);
}

LONG pws2_frame_count(void) { return InterlockedCompareExchange(&g_frames, 0, 0); }

/* Rate-limited by TIME (a counter that always changes cannot gate itself). */
void pws2_frame_log_stats(void)
{
    static ULONGLONG last_tick;
    static LONG last_frames, last_changes;
    ULONGLONG now = GetTickCount64();
    LONG f, c;
    if (last_tick == 0) { last_tick = now; return; }
    if (now - last_tick < 10000) return;
    f = g_frames; c = g_changes;
    if (f != last_frames)
        log_line("pws2-frame: %ld frames (%.0f/s), %ld health-change(s), %ld pickup ping(s) in all",
                 f, (double)(f - last_frames) * 1000.0 / (double)(now - last_tick),
                 c - last_changes, (long)g_pings);
    last_tick = now; last_frames = f; last_changes = c;
}
