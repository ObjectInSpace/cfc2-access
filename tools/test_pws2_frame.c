/* Replays every distinct frame-decoder input of the 2026-10-27 live Power
 * Stone 2 capture (Arcade, the user on fighter 0 against three CPU fighters)
 * through the shipping frame and pickup decoders: select, the screens before
 * the fight, a three-minute fight with a pause and the user's KO, the continue
 * screen. Fixture: tools/make_pws2_fixtures.py. Run from the repository root.
 *
 * The renderer was not sampled during this capture, so the projection used
 * for pans is SYNTHETIC: the one the traced constructor 0x140F338F0 builds
 * from the game's own constants (angle 0x1D28, aspect 4/3, depth 0.3..120000;
 * docs/pws2-actor-trace.md). At runtime the live slot 3 is read. */
#include "../src/pws2_frame.h"
#include "../src/audio.h"
#include "../libs/minhook/include/MinHook.h"
#include "pws2_fixture.h"
#include <math.h>

/* The hook is never installed here. */
MH_STATUS WINAPI MH_CreateHook(LPVOID t, LPVOID d, LPVOID *o) { (void)t; (void)d; (void)o; return MH_UNKNOWN; }
MH_STATUS WINAPI MH_EnableHook(LPVOID t) { (void)t; return MH_UNKNOWN; }
MH_STATUS WINAPI MH_DisableHook(LPVOID t) { (void)t; return MH_UNKNOWN; }

static unsigned char *S;
static float proj[16];

static void make_projection(void)
{
    double a = 0x1D28 * 6.2831854820251465 / 65536.0 * 0.5, cs = cos(a) / sin(a);
    double q = 4.0 / 3.0, n = 0.3, f = 120000.0;
    memset(proj, 0, sizeof proj);
    proj[0] = (float)(cs / q);
    proj[5] = (float)cs;
    proj[10] = (float)(-(n + f) / (f - n));
    proj[11] = -1.0f;
    proj[14] = (float)(-(2.0 * f * n) / (f - n));
}

static float hp_of(unsigned p) { float v; memcpy(&v, S + PWS2_FX_A(p) + 0x1DC, 4); return v; }

int main(void)
{
    unsigned in_match = 0, fighting = 0, clock_samples = 0, ko = 0;
    int cpu_stones_max[4] = { 0 }, last_hp[4] = { 0 };
    unsigned fighter_on = 0, fighter_off = 0;
    int first_clock = -1, last_clock = -1, prev_clock = 1000, stones[16], ns = 0;
    double paused_from = -1, paused_for = 0, last_t = 0;
    uint32_t last_counter = 0;
    S = calloc(1, PWS2_FX_SIZE);
    assert(S);
    pws2_fixture_load();
    make_projection();

    /* 1. Match gates, health, stones and clock, every distinct sample. */
    for (unsigned i = 0; i < pws2_fx_count; i++) {
        double t = pws2_fixture_time(i);
        match_audio_sample m;
        pws2_fixture_apply(i, S);
        pws2_frame_decode(S, proj, &m);
        /* Before the fight's "ready" (63.9 s) and after the KO decided it
         * (mode 0 from 232.16 s, the continue screen): not a match. */
        if (t < 63.5 || t > 232.15) assert(!m.in_match);
        if (t > 64.0 && t < 232.0) assert(m.in_match);
        if (!m.in_match) { assert(!pws2_fighting(S)); continue; }
        in_match++;
        /* All four fighters, CPU too, as owner = fighter index, each placed
         * on screen. */
        assert(m.fighter_gauges);
        for (unsigned p = 0; p < 4; p++) {
            const match_audio_owner *o = &m.owner[p];
            float pt[3], pan;
            memcpy(pt, S + PWS2_FX_A(p) + 0x3C, 12);
            assert(o->present);
            /* Off screen: no tones (user, 2026-10-27). */
            if (!pws2_project(S, proj, pt, &pan)) {
                assert(!o->health.valid && !o->resource.valid && !o->gauge_pan_valid);
                fighter_off++;
                continue;
            }
            assert(o->gauge_pan_valid && o->gauge_pan == pan);
            assert(o->health.valid && o->health.maximum == 1000);
            assert(abs(o->health.value - (int)lroundf(hp_of(p))) <= 1);
            assert(o->resource.valid && o->resource.maximum == 3);
            assert(o->resource.value == S[PWS2_FX_A(p) + 0x4055]);
            if (o->resource.value > cpu_stones_max[p]) cpu_stones_max[p] = o->resource.value;
            last_hp[p] = o->health.value;
            fighter_on++;
        }
        if (m.owner[0].resource.valid && (!ns || stones[ns - 1] != m.owner[0].resource.value)) {
            assert(ns < 16);
            stones[ns++] = m.owner[0].resource.value;
        }
        if (m.owner[0].health.valid && m.owner[0].health.value == 0) ko++;
        /* The clock counts only while fighting, down by one, never up. */
        assert(m.clock_valid == pws2_fighting(S));
        if (m.clock_valid) {
            clock_samples++;
            if (first_clock < 0) first_clock = m.clock;
            assert(m.clock <= prev_clock && prev_clock - m.clock <= 1 + (prev_clock == 1000) * 1000);
            prev_clock = last_clock = m.clock;
        }
        if (pws2_fighting(S)) {
            uint32_t c = pws2_frame_counter(S);
            /* The pause: the fixture keeps one of a run of identical samples,
             * so it is a gap in time across which the frame counter (about 60
             * a second otherwise) barely moved. */
            if (fighting && c - last_counter < 10 && t - last_t > paused_for) {
                paused_for = t - last_t;
                paused_from = last_t;
            }
            fighting++;
            last_counter = c;
            last_t = t;
        }
    }
    printf("in match %u samples, fighting %u, clock %d..%d over %u samples, longest pause %.1f s from %.1f s\n",
           in_match, fighting, first_clock, last_clock, clock_samples, paused_for, paused_from);
    fflush(stdout);
    /* Live: the clock ran from 180 to 19; the user paused for about 3.5 s; the
     * KO frame read 0 while the round was still being fought. */
    assert(first_clock == 180 && last_clock == 19);
    assert(paused_for > 3.0 && paused_for < 4.5);
    assert(ko >= 1);
    /* The CPUs' health at the KO (576, 54, 184 live) and the CPU that
     * collected three stones and transformed (fighter 1, 104.1 s). */
    printf("health at the end: %d %d %d %d; fighters on screen %u, off %u\n",
           last_hp[0], last_hp[1], last_hp[2], last_hp[3], fighter_on, fighter_off);
    /* The camera follows the fighters: off screen is rare, but happens. */
    assert(fighter_off > 0 && fighter_off * 20 < fighter_on);
    assert(last_hp[0] == 0 && last_hp[1] == 576 && last_hp[2] == 54 && last_hp[3] == 184);
    assert(cpu_stones_max[1] == 3);
    /* The user's stones while in the match: one at the start, lost to a hit
     * (100.2 s), then picked up again, lost, and two before the KO. */
    {
        static const int want[] = { 1, 0, 1, 0, 1, 2 };
        printf("stones:");
        for (int k = 0; k < ns; k++) printf(" %d", stones[k]);
        printf("\n");
        assert(ns == (int)(sizeof want / sizeof *want));
        for (int k = 0; k < ns; k++) assert(stones[k] == want[k]);
    }

    /* 2. Pickups on every fought sample: what lies in the arena, by family;
     *    pans on screen; nothing outside a fought round. */
    {
        static pickup_sample pick[PICKUP_SLOTS], none[PICKUP_SLOTS];
        unsigned seen[AUDIO_PING_FAMILIES] = { 0 }, maxn[AUDIO_PING_FAMILIES] = { 0 };
        unsigned present = 0, placed = 0, outside = 0;
        for (unsigned i = 0; i < pws2_fx_count; i++) {
            unsigned n[AUDIO_PING_FAMILIES] = { 0 };
            unsigned long long root = pws2_fixture_root(i);
            pws2_fixture_apply(i, S);
            pws2_pickups_decode(S, root, proj, pick);
            if (!pws2_fighting(S)) {
                for (unsigned k = 0; k < PICKUP_SLOTS; k++) assert(!pick[k].present);
                outside++;
                continue;
            }
            pws2_pickups_decode(S, root, NULL, none);
            for (unsigned k = 0; k < PICKUP_SLOTS; k++) {
                const unsigned char *o = S + PWS2_FX_POOL + k * PWS2_FX_STRIDE;
                /* No camera: the same things present, none placed. */
                assert(none[k].present == pick[k].present && !none[k].placed);
                if (!pick[k].present) continue;
                /* Only lying (state 2) arena objects, never one carried. */
                assert(o[0] == 9 && o[0x4B1] == 2);
                for (unsigned p = 0; p < 4; p++) {
                    unsigned long long c;
                    memcpy(&c, S + PWS2_FX_A(p) + 0x4080, 8);
                    assert(c != root + PWS2_FX_POOL + k * PWS2_FX_STRIDE);
                }
                present++;
                n[pick[k].family]++;
                {
                    float pt[3], pan;
                    bool on;
                    memcpy(pt, o + 0x3C, 12);
                    /* Placed exactly while on screen; silent off it. */
                    on = pws2_project(S, proj, pt, &pan);
                    assert(pick[k].placed == on);
                    if (on) assert(pick[k].pan == pan);
                }
                if (pick[k].placed) {
                    float pt[3], me[3], dx, dy, dz;
                    placed++;
                    assert(pick[k].pan >= -1.0f && pick[k].pan <= 1.0f);
                    /* Distance is to the only person's fighter, fighter 0. */
                    memcpy(pt, o + 0x3C, 12);
                    memcpy(me, S + PWS2_FX_A(0) + 0x3C, 12);
                    dx = pt[0] - me[0]; dy = pt[1] - me[1]; dz = pt[2] - me[2];
                    assert(fabsf(pick[k].distance - sqrtf(dx * dx + dy * dy + dz * dz)) < 0.5f);
                }
            }
            for (int f = 0; f < AUDIO_PING_FAMILIES; f++) {
                if (n[f]) seen[f]++;
                if (n[f] > maxn[f]) maxn[f] = n[f];
            }
            /* Seven stones exist; never more lying than that. */
            assert(n[AUDIO_PING_STONE] <= 7);
        }
        printf("pickups: %u present (%u placed) over the fight; samples with stone %u, food %u, item %u, prop %u;"
               " most at once %u/%u/%u/%u; %u samples outside a fought round\n",
               present, placed, seen[0], seen[1], seen[2], seen[3], maxn[0], maxn[1], maxn[2], maxn[3], outside);
        /* Every family turned up in the capture; about half of what lay in
         * the arena at a time was off screen, and silent. */
        for (int f = 0; f < AUDIO_PING_TONE; f++) assert(seen[f] > 0);   /* the pickup families */
        assert(maxn[AUDIO_PING_PROP] >= 8);
        assert(placed * 4 > present && placed * 4 < present * 3);
        assert(outside > 0);
    }

    /* 3. A carried object never pings, even if its state read "lying". */
    {
        static pickup_sample pick[PICKUP_SLOTS];
        unsigned done = 0;
        for (unsigned i = 0; i < pws2_fx_count && !done; i++) {
            unsigned long long root = pws2_fixture_root(i), c;
            pws2_fixture_apply(i, S);
            if (!pws2_fighting(S)) continue;
            for (unsigned p = 0; p < 4 && !done; p++) {
                unsigned k;
                memcpy(&c, S + PWS2_FX_A(p) + 0x4080, 8);
                if (!c || c < root + PWS2_FX_POOL) continue;
                k = (unsigned)((c - root - PWS2_FX_POOL) / PWS2_FX_STRIDE);
                if (k >= 200 || S[PWS2_FX_POOL + k * PWS2_FX_STRIDE] != 9) continue;
                S[PWS2_FX_POOL + k * PWS2_FX_STRIDE + 0x4B1] = 2;
                pws2_pickups_decode(S, root, proj, pick);
                assert(!pick[k].present);
                /* .. and once dropped (no longer carried), it does. */
                memset(S + PWS2_FX_A(p) + 0x4080, 0, 8);
                pws2_pickups_decode(S, root, proj, pick);
                {
                    audio_ping_family fam;
                    int var;
                    unsigned code = S[PWS2_FX_POOL + k * PWS2_FX_STRIDE + 0x4B0];
                    assert(pick[k].present == pws2_pickup_kind(code, &fam, &var));
                }
                done = 1;
            }
        }
        assert(done);
    }

    /* 4. Families and types, from the item table's categories. */
    {
        audio_ping_family f;
        int v;
        assert(pws2_pickup_kind(0xC1, &f, &v) && f == AUDIO_PING_STONE && v == 0);
        assert(pws2_pickup_kind(0x10, &f, &v) && f == AUDIO_PING_FOOD && v == 0);
        assert(pws2_pickup_kind(0x68, &f, &v) && f == AUDIO_PING_FOOD && v == 9);
        assert(pws2_pickup_kind(0x53, &f, &v) && f == AUDIO_PING_FOOD && v == 3);
        assert(pws2_pickup_kind(0xC3, &f, &v) && f == AUDIO_PING_PROP && v == 0);
        assert(pws2_pickup_kind(0xC8, &f, &v) && f == AUDIO_PING_PROP && v == 1);
        assert(pws2_pickup_kind(0x3B, &f, &v) && f == AUDIO_PING_ITEM && v == 0x3B % 25);
        assert(pws2_pickup_kind(0x4A, &f, &v) && f == AUDIO_PING_ITEM);   /* category 3, no heal */
        assert(pws2_pickup_kind(0xC6, &f, &v) && f == AUDIO_PING_ITEM);   /* treasure */
        assert(pws2_pickup_kind(0x81, &f, &v) && f == AUDIO_PING_ITEM);   /* collectible */
        assert(!pws2_pickup_kind(0xC2, &f, &v) && !pws2_pickup_kind(0xCA, &f, &v));   /* fixtures */
        assert(!pws2_pickup_kind(0x46, &f, &v));   /* category 1 */
        assert(!pws2_pickup_kind(0x59, &f, &v));   /* category 2 */
        assert(!pws2_pickup_kind(0xCC, &f, &v) && !pws2_pickup_kind(0xFF, &f, &v));
        for (unsigned c = 0; c < 0xCC; c++)
            if (pws2_pickup_kind(c, &f, &v)) assert(v >= 0 && v <= 24 && f < AUDIO_PING_FAMILIES);
    }

    /* 5. Pings through the shared scheduler over the fight, advanced only on
     *    simulated frames: none during the pause. The fixture holds about 20
     *    samples a second, so each stands for three game frames. */
    {
        static pickup_cues cues;
        static pickup_sample pick[PICKUP_SLOTS];
        unsigned pings_in_pause = 0, pings = 0;
        uint32_t counter = 0;
        for (unsigned i = 0; i < pws2_fx_count; i++) {
            unsigned before;
            pws2_fixture_apply(i, S);
            if (!pws2_fighting(S)) { pickup_cues_reset(&cues); continue; }
            if (pws2_frame_counter(S) == counter) continue;
            counter = pws2_frame_counter(S);
            pws2_pickups_decode(S, pws2_fixture_root(i), proj, pick);
            before = cues.pings;
            for (int k = 0; k < 3; k++) pickup_cues_frame(&cues, 1, pick);
            pings += cues.pings - before;
            {
                double t = pws2_fixture_time(i);
                if (t > 94.2 && t < 97.3) pings_in_pause += cues.pings - before;
            }
        }
        printf("pings over the fight: %u\n", pings);
        assert(pings > 100 && pings_in_pause == 0);
    }

    /* 6. Fail closed: a split draw, an unknown view index, the gates. */
    {
        match_audio_sample m;
        unsigned i;
        for (i = 0; i < pws2_fx_count; i++) {
            pws2_fixture_apply(i, S);
            if (pws2_fighting(S)) break;
        }
        assert(i < pws2_fx_count);
        S[0x52FDC] = 1; assert(!pws2_fighting(S)); S[0x52FDC] = 0;
        S[0x52E88] = 4; assert(!pws2_fighting(S)); S[0x52E88] = 0;
        S[0x52D2B] = 1; pws2_frame_decode(S, proj, &m); assert(!m.in_match); S[0x52D2B] = 0;
        S[0x52BDD] = 4; pws2_frame_decode(S, proj, &m); assert(!m.in_match); S[0x52BDD] = 5;
        S[0x52BD9] = 1; pws2_frame_decode(S, proj, &m); assert(!m.in_match); S[0x52BD9] = 2;
        /* No camera: still every gauge, centred. */
        pws2_frame_decode(S, NULL, &m);
        assert(m.in_match && m.owner[0].health.valid && !m.owner[0].gauge_pan_valid);
        {
            float nan = NAN, big = 1500.0f;
            memcpy(S + PWS2_FX_A(0) + 0x1DC, &nan, 4);
            pws2_frame_decode(S, proj, &m);
            assert(m.in_match && !m.owner[0].health.valid);
            memcpy(S + PWS2_FX_A(0) + 0x1DC, &big, 4);
            pws2_frame_decode(S, proj, &m);
            assert(!m.owner[0].health.valid);
            big = -5.0f;
            memcpy(S + PWS2_FX_A(0) + 0x1DC, &big, 4);
            pws2_frame_decode(S, proj, &m);
            assert(!m.owner[0].health.valid);
            big = 0.3f;
            memcpy(S + PWS2_FX_A(0) + 0x1DC, &big, 4);
            pws2_frame_decode(S, proj, &m);
            assert(m.owner[0].health.valid && m.owner[0].health.value == 1);
        }
        S[PWS2_FX_A(0) + 0x4055] = 4; pws2_frame_decode(S, proj, &m); assert(!m.owner[0].resource.valid);
        S[PWS2_FX_A(0) + 0x4055] = 1;
        /* Constructed from the live view: a stone behind the camera on its
         * right or left, past the right edge, above the top edge -- present
         * and silent; dead ahead, placed at the centre; and a view that is
         * not a rigid camera places nothing. */
        {
            static const float ev[5][3] = {   /* view space; the camera looks down -z */
                { 800, 0, 500 }, { -800, 0, 500 }, { 3000, 0, -1000 }, { 0, 3000, -1000 }, { 0, 0, -1000 },
            };
            static pickup_sample pick[PICKUP_SLOTS];
            unsigned slot = 199, off = PWS2_FX_POOL + slot * PWS2_FX_STRIDE;
            float v[16], pt[3], pan = 0.0f;
            memcpy(v, S + 0x51D90, 64);
            for (int k = 0; k < 5; k++) {
                bool ahead = k == 4;
                for (int c = 0; c < 3; c++)   /* world = (e - T) R^T, R rigid */
                    pt[c] = (ev[k][0] - v[12]) * v[c * 4 + 0] + (ev[k][1] - v[13]) * v[c * 4 + 1] +
                            (ev[k][2] - v[14]) * v[c * 4 + 2];
                assert(pws2_project(S, proj, pt, &pan) == ahead);
                S[off] = 9; S[off + 0x4B1] = 2; S[off + 0x4B0] = 0xC1;
                memset(S + off + 0xB8, 0, 8);
                memcpy(S + off + 0x3C, pt, 12);
                pws2_pickups_decode(S, pws2_fixture_root(i), proj, pick);
                assert(pick[slot].present && pick[slot].placed == ahead);
                if (ahead) {
                    float bad = 2.0f, keep;
                    assert(fabsf(pick[slot].pan) < 0.01f);
                    memcpy(&keep, S + 0x51D90 + 60, 4);
                    memcpy(S + 0x51D90 + 60, &bad, 4);
                    pws2_pickups_decode(S, pws2_fixture_root(i), proj, pick);
                    assert(pick[slot].present && !pick[slot].placed);
                    memcpy(S + 0x51D90 + 60, &keep, 4);
                }
            }
            S[off] = 0;
        }
        /* The person's record naming another slot: no one to measure pickup
         * distance from, so nothing is placed (they are still present). */
        {
            static pickup_sample pick[PICKUP_SLOTS];
            unsigned any = 0;
            S[PWS2_FX_A(0) + 1] = 2;
            pws2_pickups_decode(S, pws2_fixture_root(i), proj, pick);
            for (unsigned k = 0; k < PICKUP_SLOTS; k++) { any += pick[k].present; assert(!pick[k].placed); }
            assert(any > 0);
            S[PWS2_FX_A(0) + 1] = 0;
        }
        /* A record that does not name its own slot is not placed. */
        S[PWS2_FX_A(2) + 1] = 3;
        pws2_frame_decode(S, proj, &m);
        assert(!m.owner[2].present && m.owner[3].present);
        S[PWS2_FX_A(2) + 1] = 2;
    }

    /* 7. Pickup announcements, against the capture. Stones: one event per
     *    stone gained, for the fighter whose count rose (20 in the fight,
     *    5 of them caught as they fell, never a lying object). Lifted: one
     *    event per new carried object, always a prop or an item. Touched:
     *    an object lying before and gone after is what pws2_taken names. */
    {
        static unsigned char prev_stones[4];
        pws2_pickup_watch w;
        unsigned rises = 0, stone_events = 0, lifts = 0, lift_events = 0, kinds[AUDIO_PING_FAMILIES] = { 0 };
        unsigned long long prev_carried[4] = { 0 };
        int have_prev = 0;
        memset(&w, 0, sizeof w);
        for (unsigned i = 0; i < pws2_fx_count; i++) {
            unsigned long long root = pws2_fixture_root(i);
            unsigned who[8], code[8], n;
            pws2_fixture_apply(i, S);
            n = pws2_watch_events(S, root, &w, who, code);
            if (!pws2_fighting(S)) { assert(n == 0); have_prev = 0; memset(prev_carried, 0, sizeof prev_carried); continue; }
            for (unsigned p = 0; p < 4; p++) {
                unsigned char st = S[PWS2_FX_A(p) + 0x4055];
                unsigned long long c;
                unsigned want_stone = have_prev && st > prev_stones[p], want_lift;
                memcpy(&c, S + PWS2_FX_A(p) + 0x4080, 8);
                want_lift = have_prev && c && c != prev_carried[p];
                rises += want_stone;
                lifts += want_lift;
                for (unsigned k = 0; k < n; k++) {
                    if (who[k] != p) continue;
                    if (code[k] == 0xC1) { assert(want_stone); stone_events++; want_stone = 0; }
                    else {
                        audio_ping_family f;
                        int v;
                        assert(want_lift && code[k] == S[(size_t)(c - root) + 0x4B0]);
                        assert(pws2_pickup_kind(code[k], &f, &v) && (f == AUDIO_PING_PROP || f == AUDIO_PING_ITEM));
                        kinds[f]++;
                        lift_events++;
                        want_lift = 0;
                    }
                }
                assert(!want_stone && !want_lift);
                prev_stones[p] = st;
                prev_carried[p] = c;
            }
            have_prev = 1;
        }
        printf("pickup events: %u stones gained, %u lifted (%u props, %u items)\n",
               stone_events, lift_events, kinds[AUDIO_PING_PROP], kinds[AUDIO_PING_ITEM]);
        fflush(stdout);
        assert(rises == 20 && stone_events == rises && lift_events == lifts && lifts > 10);
        assert(kinds[AUDIO_PING_PROP] > 0 && kinds[AUDIO_PING_ITEM] > 0);
        /* Touched: a lying object gone is named; nothing gone, -1. A pointer
         * outside the pool or between records names nothing. */
        {
            static unsigned short before[200];
            unsigned long long root;
            unsigned i, k = 200;
            for (i = 0; i < pws2_fx_count; i++) {
                pws2_fixture_apply(i, S);
                if (!pws2_fighting(S)) continue;
                for (k = 0; k < 200; k++) {
                    const unsigned char *o = S + PWS2_FX_POOL + k * PWS2_FX_STRIDE;
                    if (o[0] == 9 && o[0x4B1] == 2 && o[0x4B0] == 0x10) break;   /* food lying */
                }
                if (k < 200) break;
            }
            assert(i < pws2_fx_count);
            root = pws2_fixture_root(i);
            pws2_lying_snapshot(S, before);
            assert(pws2_taken(S, before) == -1);
            S[PWS2_FX_POOL + k * PWS2_FX_STRIDE + 0x4B1] = 5;   /* taken */
            assert(pws2_taken(S, before) == 0x10);
            assert(pws2_carried_code(S, root, root) == -1);
            assert(pws2_carried_code(S, root, root + PWS2_FX_POOL + 4) == -1);
            assert(pws2_carried_code(S, root, root + PWS2_FX_POOL + 200ull * PWS2_FX_STRIDE) == -1);
            assert(pws2_carried_code(S, root, root + PWS2_FX_POOL + (unsigned long long)k * PWS2_FX_STRIDE) == 0x10);
        }
    }

    /* 8. Off-screen sounds switched on (Shift+F9): a stone behind the camera
     *    on its right is placed, hard right; switched off again, silent. */
    {
        unsigned i;
        static pickup_sample pick[PICKUP_SLOTS];
        unsigned slot = 199, off = PWS2_FX_POOL + slot * PWS2_FX_STRIDE;
        float v[16], pt[3];
        for (i = 0; i < pws2_fx_count; i++) { pws2_fixture_apply(i, S); if (pws2_fighting(S)) break; }
        memcpy(v, S + 0x51D90, 64);
        for (int c = 0; c < 3; c++)
            pt[c] = (800 - v[12]) * v[c * 4 + 0] + (0 - v[13]) * v[c * 4 + 1] + (500 - v[14]) * v[c * 4 + 2];
        S[off] = 9; S[off + 0x4B1] = 2; S[off + 0x4B0] = 0xC1;
        memset(S + off + 0xB8, 0, 8);
        memcpy(S + off + 0x3C, pt, 12);
        assert(audio_option_get(AUDIO_OPT_OFFSCREEN) == 0);
        assert(audio_option_cycle(AUDIO_OPT_OFFSCREEN) == 1);
        pws2_pickups_decode(S, pws2_fixture_root(i), proj, pick);
        assert(pick[slot].present && pick[slot].placed && pick[slot].pan == 1.0f);
        assert(audio_option_cycle(AUDIO_OPT_OFFSCREEN) == 0);
        pws2_pickups_decode(S, pws2_fixture_root(i), proj, pick);
        assert(pick[slot].present && !pick[slot].placed);
        /* Pickup announcements cycle: every fighter, players only, off, back. */
        assert(audio_option_get(AUDIO_OPT_PICKUP_SPEECH) == 2);
        assert(audio_option_cycle(AUDIO_OPT_PICKUP_SPEECH) == 1);
        assert(audio_option_cycle(AUDIO_OPT_PICKUP_SPEECH) == 0);
        assert(audio_option_cycle(AUDIO_OPT_PICKUP_SPEECH) == 2);
    }

    /* 9. The stage select, from the Versus capture (Stage Selection: Manual):
     *    walking only from the landing to the choice; on landing the five
     *    stages lie left, ahead left, ahead, ahead right, right; five spots
     *    ping (same-numbered objects of other kinds do not), each at its own
     *    pitch; the last step before the choice is within reach of stage 3
     *    (index 2), and index 2 is what the game chose. */
    {
        static pickup_sample spots[PICKUP_SLOTS];
        pws2_fx st;
        unsigned walking = 0, decoy_samples = 0, landed = 0, chosen_seen = 0;
        int was = 0, last_walk = -1;
        pws2_fx_open(&st, "tools/fixtures/pws2-stage-20261027.bin");
        for (unsigned i = 0; i < st.count; i++) {
            unsigned present = 0, smallidx = 0, pitches = 0;
            bool now;
            pws2_fx_apply(&st, i, S);
            now = pws2_stage_walk(S);
            pws2_spots_decode(S, proj, spots);
            for (unsigned k = 0; k < PICKUP_SLOTS; k++) {
                const unsigned char *o = S + PWS2_FX_POOL + k * PWS2_FX_STRIDE;
                if (o[0] == 5 && o[2] < 5) smallidx++;
                if (!spots[k].present) continue;
                present++;
                {
                    static const int pitch[5] = { 0, 4, 7, 11, 14 };
                    assert(spots[k].family == AUDIO_PING_TONE && spots[k].variant == pitch[o[2]]);
                }
                assert(spots[k].placed && spots[k].pan >= -1.0f && spots[k].pan <= 1.0f && spots[k].distance >= 0.0f);
                pitches |= 1u << o[2];
            }
            if (!now) {
                assert(present == 0);
                if (was) {
                    printf("stage chosen: index %d at %.1f s\n", pws2_stage_chosen(S), pws2_fx_time(&st, i));
                    assert(pws2_stage_chosen(S) == 2 && S[0x53010] == 2);
                    chosen_seen++;
                }
                was = 0;
                continue;
            }
            walking++;
            assert(present == 5 && pitches == 0x1F);
            if (smallidx > present) decoy_samples++;
            if (!was) {
                static const int want[5] = { 6, 7, 0, 1, 2 };   /* left .. right */
                for (unsigned k = 0; k < 5; k++) assert(pws2_spot_direction(S, k) == want[k]);
                landed++;
            }
            last_walk = (int)i;
            was = 1;
        }
        assert(walking > 100 && landed == 1 && chosen_seen == 1 && decoy_samples == walking);
        /* The last step before the choice: stage 3 (index 2) within reach. */
        pws2_fx_apply(&st, (unsigned)last_walk, S);
        pws2_spots_decode(S, proj, spots);
        for (unsigned k = 0; k < PICKUP_SLOTS; k++)
            if (spots[k].present && S[PWS2_FX_POOL + k * PWS2_FX_STRIDE + 2] == 2) {
                printf("last step before the choice: %.0f from stage 3's reach\n", spots[k].distance);
                assert(spots[k].distance < 30.0f);
            }
        /* Gates: another step or sub-phase, or not yet landed (mode 0). */
        S[0x52BDD] = 5; assert(!pws2_stage_walk(S)); S[0x52BDD] = 3;
        S[0x52EE5] = 0; assert(!pws2_stage_walk(S)); S[0x52EE5] = 1;
        S[0x52BDE] = 2; assert(!pws2_stage_walk(S)); S[0x52BDE] = 1;
        assert(pws2_stage_walk(S));
        printf("stage select: %u walking samples, directions on landing, five spots each sample\n", walking);
    }

    printf("PASS: %u live PWS2 samples through the frame and pickup decoders\n", pws2_fx_count);
    return 0;
}
