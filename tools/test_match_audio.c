/*
 * Offline test for the shared in-match audio controller (src/match_audio.c)
 * driving the REAL mixer (src/audio.c) through its render seam. No device is
 * opened -- which is itself the "audio unavailable" case: every feed must be
 * harmless with no device, and the mixer still renders for inspection.
 *
 * Every case asserts on SAMPLES. Per test_audio.c's rule, each case silences
 * what it does not own, drains, and checks the floor before measuring.
 *
 * Sabotages, each seen to fail the named case:
 *  - legacy_stamp_ok() always TRUE        -> 9b (the race) and 11 (armed tick)
 *  - MATCH_AUDIO_PAUSE_FRAMES = 100000    -> 7 (pause)
 *  - clock edge without `t < p->clock`     -> 6 (upward jump onto 30 ticks)
 *  - feed_gauge without the change check   -> 1b (unchanged sample re-sounds)
 *  - audio_owner_stop ignoring the epoch   -> 10 (late stop)
 */
#include "../src/match_audio.h"
#include "../src/audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", (m)); fails++; } \
                         else printf("ok: %s\n", (m)); } while (0)

static int16_t blk[AUDIO_BLOCK * 2];

/* Peak per channel over n blocks. A beacon pluck repeats every ~19 blocks,
 * so beacon measurements use windows longer than that. */
static void window(int n, int *pl, int *pr)
{
    int l = 0, r = 0;
    for (int b = 0; b < n; b++) {
        audio_render_block_for_test(blk);
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            int a = abs(blk[i * 2]), c = abs(blk[i * 2 + 1]);
            if (a > l) l = a;
            if (c > r) r = c;
        }
    }
    *pl = l; *pr = r;
}
static int loud(int n) { int l, r; window(n, &l, &r); return l > r ? l : r; }
/* Let every hold, fade and tick run out, then confirm the floor. */
static int drained(void) { window(80, &(int){0}, &(int){0}); return loud(30) < 40; }

/* AUDIO_OWNER_TEST seam: when armed, a switch happens AFTER a lock-free
 * setter passed its owner check and BEFORE its write lands -- the exact race
 * the epoch stamps exist for, which a single-threaded test cannot otherwise
 * reach (the producer-side check alone rejects an already-stale epoch). */
static int race_armed;
static uint64_t race_epoch;
void audio_owner_test_race(void)
{
    if (!race_armed) return;
    race_armed = 0;
    race_epoch = match_audio_begin();
}

static uint32_t motion = 1;
static match_audio_sample sample(void)
{
    match_audio_sample m;
    memset(&m, 0, sizeof m);
    m.in_match = true;
    m.motion_valid = true;
    m.motion_signature = motion++;        /* simulating unless a case holds it */
    for (int o = 0; o < MATCH_AUDIO_OWNERS; o++) m.owner[o].present = true;
    return m;
}
static void health(match_audio_sample *m, int o, int v, int mx)
{ m->owner[o].health = (match_audio_gauge){ true, v, mx }; }
static void resource(match_audio_sample *m, int o, int v, int mx)
{ m->owner[o].resource = (match_audio_gauge){ true, v, mx }; }
static void beacon(match_audio_sample *m, int o, float pan)
{ m->owner[o].beacon_valid = true; m->owner[o].screen_pan = pan;
  m->owner[o].stance = AUDIO_STANCE_STAND; }

int main(void)
{
    match_audio_producer frame = { .feeds = MATCH_AUDIO_FEED_HEALTH |
        MATCH_AUDIO_FEED_BEACON | MATCH_AUDIO_FEED_CLOCK };
    match_audio_producer worker = { .feeds = MATCH_AUDIO_FEED_RESOURCE };
    match_audio_sample m;
    int l, r;

    /* 0. No device: nothing opened, and the controller is inert without an
     * owner (epoch 0 is not a feed). */
    CHECK(!audio_ready(), "0: no device is open in the test");
    CHECK(match_audio_current() == 0, "0: nothing owns match audio initially");
    m = sample(); health(&m, 0, 100, 100); beacon(&m, 0, 0.0f);
    match_audio_feed(&frame, 0, &m);
    CHECK(drained(), "0: epoch 0 feeds produce no sound");

    uint64_t a = match_audio_begin();
    CHECK(a != 0 && match_audio_current() == a, "begin returns the current epoch");

    /* 1. Health: first sighting seeds silently, damage sounds on P1's side. */
    m = sample(); health(&m, 0, 100, 100); health(&m, 1, 100, 100);
    match_audio_feed(&frame, a, &m);
    CHECK(loud(10) < 40, "1: first sighting of a gauge is silent");
    m = sample(); health(&m, 0, 80, 100); health(&m, 1, 100, 100);
    match_audio_feed(&frame, a, &m);
    window(6, &l, &r);
    CHECK(l > 1000 && l > r, "1: P1 damage sounds, panned to the P1 HUD side");
    CHECK(drained(), "1: the tone ends when the value settles");
    /* 1b. An unchanged sample does not re-sound. */
    {
        unsigned w = frame.gauge_writes;
        m = sample(); health(&m, 0, 80, 100); health(&m, 1, 100, 100);
        match_audio_feed(&frame, a, &m);
        CHECK(frame.gauge_writes == w && loud(10) < 40,
              "1b: unchanged health touches nothing and stays silent");
    }

    /* 2. KO sentinel: an invalid value resets; the next real value (a new
     * round at full) seeds silently instead of sounding as a jump. */
    m = sample(); m.owner[0].health = (match_audio_gauge){ false, 0xFFFF, 100 };
    health(&m, 1, 100, 100);
    match_audio_feed(&frame, a, &m);
    CHECK(loud(10) < 40, "2: KO sentinel is never swept to");
    m = sample(); health(&m, 0, 100, 100); health(&m, 1, 100, 100);
    match_audio_feed(&frame, a, &m);
    CHECK(loud(10) < 40, "2: the value after an invalid one seeds silently");

    /* 3. Resource on the worker producer: gain sounds; a NEW maximum is a new
     * scale and reseeds silently; spending sounds again. */
    m = sample(); resource(&m, 1, 50, 100);
    match_audio_feed(&worker, a, &m);
    m = sample(); resource(&m, 1, 60, 100);
    match_audio_feed(&worker, a, &m);
    window(6, &l, &r);
    CHECK(r > 1000 && r > l, "3: P2 resource gain sounds on the P2 side");
    CHECK(drained(), "3: resource tone ends");
    m = sample(); resource(&m, 1, 60, 200);
    match_audio_feed(&worker, a, &m);
    CHECK(loud(10) < 40, "3: a changed maximum reseeds silently");
    m = sample(); resource(&m, 1, 0, 200);
    match_audio_feed(&worker, a, &m);
    CHECK(loud(6) > 1000, "3: spending sounds");
    CHECK(drained(), "3: spend tone ends");

    /* 4. Beacons: continuous while in a match, at the projected position. */
    m = sample(); beacon(&m, 0, 1.0f);
    match_audio_feed(&frame, a, &m);
    window(30, &l, &r);
    CHECK(r > 300 && r > l * 2, "4: P1 beacon sounds at its screen position (right)");
    m = sample(); beacon(&m, 0, -1.0f);
    for (int i = 0; i < 40; i++) { m.motion_signature = motion++;
        match_audio_feed(&frame, a, &m); audio_render_block_for_test(blk); }
    window(30, &l, &r);
    CHECK(l > 300 && l > r * 2, "4: and follows the fighter to the left");

    /* 5. Exit: an observed non-match state silences health and beacons. */
    m = sample(); m.in_match = false;
    match_audio_feed(&frame, a, &m);
    CHECK(drained(), "5: leaving the match silences the beacons");

    /* 6. Clock: ticks only on a DECREASE landing on 30, 15 or 1..5. */
    {
        static const struct { int from, to, tick; const char *what; } c[] = {
            { 31, 30, 1, "6: 31 -> 30 ticks" },
            { 30, 30, 0, "6: an unchanged clock does not tick" },
            { 17, 16, 0, "6: 17 -> 16 does not tick" },
            { 16, 15, 1, "6: 16 -> 15 ticks" },
            { 10, 30, 0, "6: an UPWARD jump onto 30 (round reset) does not tick" },
            {  6,  5, 1, "6: 6 -> 5 ticks" },
            {  1,  0, 0, "6: 1 -> 0 does not tick" },
        };
        for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
            m = sample(); m.clock_valid = true; m.clock = c[i].from;
            match_audio_feed(&frame, a, &m);
            CHECK(drained(), "6: floor before a clock step");
            m = sample(); m.clock_valid = true; m.clock = c[i].to;
            match_audio_feed(&frame, a, &m);
            window(4, &l, &r);
            CHECK(c[i].tick ? (l > 1000 && abs(l - r) < l / 10) : (l < 40 && r < 40),
                  c[i].what);
        }
        /* An invalid clock forgets history: no tick across the gap. */
        m = sample(); m.clock_valid = true; m.clock = 31;
        match_audio_feed(&frame, a, &m);
        m = sample(); match_audio_feed(&frame, a, &m);
        m = sample(); m.clock_valid = true; m.clock = 30;
        match_audio_feed(&frame, a, &m);
        CHECK(loud(4) < 40, "6: no tick across an unavailable clock");
    }

    /* 7. Pause: a motion signature frozen for the threshold silences the
     * beacons; movement resumes them. */
    m = sample(); beacon(&m, 0, 0.0f);
    match_audio_feed(&frame, a, &m);
    CHECK(loud(30) > 300, "7: beacon sounds before the pause");
    for (int i = 0; i < MATCH_AUDIO_PAUSE_FRAMES + 2; i++) {
        match_audio_feed(&frame, a, &m);            /* same signature */
        audio_render_block_for_test(blk);
    }
    CHECK(frame.paused && frame.pauses == 1, "7: the pause is detected once");
    CHECK(drained(), "7: paused beacons are silent");
    m.motion_signature = motion++;
    match_audio_feed(&frame, a, &m);
    CHECK(!frame.paused && loud(30) > 300, "7: movement resumes the beacons");

    /* 8. Muted cue: a level of 0 silences the family, as a gain. */
    audio_cue_set_volume(AUDIO_CUE_BEACON, 0);
    CHECK(drained(), "8: beacon family muted");
    audio_cue_set_volume(AUDIO_CUE_BEACON, 100);
    m.motion_signature = motion++;
    match_audio_feed(&frame, a, &m);
    CHECK(loud(30) > 300, "8: unmuted beacons return");

    /* 9. Switch: a new owner silences the old one's beacons, and a late
     * frame-thread write still stamped with the old epoch stays silent. */
    uint64_t b = match_audio_begin();
    CHECK(b > a && match_audio_current() == b, "9: a new owner gets a newer epoch");
    CHECK(drained(), "9: the switch silences the old owner's beacons");
    audio_beacon_set_owned(a, 0, TRUE, 0.0f, 1.0f);   /* lost the race */
    CHECK(loud(30) < 40, "9: a late beacon write from the old epoch is silent");
    match_audio_feed(&frame, a, &m);                   /* stale producer */
    CHECK(loud(30) < 40, "9: a stale producer feed is silent");
    audio_gauge_set_owned(a, AUDIO_GAUGE_HEALTH, 0, 1, 100);
    audio_gauge_set_owned(a, AUDIO_GAUGE_HEALTH, 0, 50, 100);
    CHECK(loud(6) < 40, "9: a late gauge write from the old epoch is silent");
    /* Unowned legacy setters are rejected once an owner exists. */
    audio_beacon_set(0, 0.0f, 1.0f);
    audio_tick();
    CHECK(loud(30) < 40, "9: unowned legacy setters are rejected after a claim");

    /* 9b. THE RACE: the old owner's check passes, the switch lands, then its
     * write. Only the mixer's stamp check can keep that silent. */
    CHECK(drained(), "9b: floor before the race");
    race_armed = 1;
    audio_beacon_set_owned(b, 0, TRUE, 0.0f, 1.0f);
    CHECK(race_epoch > b && match_audio_current() == race_epoch,
          "9b: the switch ran inside the setter");
    CHECK(loud(30) < 40, "9b: a beacon write that lost the race is silent");
    race_armed = 1;
    audio_tick_owned(race_epoch);
    CHECK(loud(6) < 40, "9b: a tick that lost the race is silent");
    b = race_epoch = match_audio_current();

    /* The new owner works, from a fresh edge state. */
    m = sample(); beacon(&m, 1, 0.5f); health(&m, 0, 80, 100);
    match_audio_feed(&frame, b, &m);
    CHECK(loud(30) > 300, "9: the new owner's beacons sound");
    CHECK(frame.epoch == b, "9: the producer moved to the new epoch");

    /* 10. A late stop from the replaced owner is ignored. */
    match_audio_depart(a);
    audio_owner_stop(a);
    /* Measure AFTER any fade a wrongful stop would start: the first blocks of
     * a fade are still loud, and would pass a "still sounding" check. */
    for (int i = 0; i < 40; i++) {
        m.motion_signature = motion++;
        match_audio_feed(&frame, b, &m);
        audio_render_block_for_test(blk);
    }
    m.motion_signature = motion++;
    match_audio_feed(&frame, b, &m);
    CHECK(match_audio_current() == b && loud(30) > 300,
          "10: a late stop from the old owner does not silence the new one");

    /* 11. A tick armed under the old owner, then a switch before it renders,
     * is dropped. Silence case 10's beacons first, or their fade is what
     * gets measured. */
    m = sample(); m.in_match = false;
    match_audio_feed(&frame, b, &m);
    CHECK(drained(), "11: floor before the tick switch");
    audio_tick_owned(b);
    uint64_t c2 = match_audio_begin();
    CHECK(loud(6) < 40, "11: a tick armed before the switch never sounds");
    audio_tick_owned(b);
    CHECK(loud(6) < 40, "11: a late tick from the old owner is silent");
    audio_tick_owned(c2);
    CHECK(loud(6) > 1000, "11: the current owner's tick sounds");

    /* 12. Depart: everything stops; the departed epoch cannot feed. */
    m = sample(); beacon(&m, 0, 0.0f);
    match_audio_feed(&frame, c2, &m);
    CHECK(loud(30) > 300, "12: beacon live before departure");
    match_audio_depart(c2);
    CHECK(match_audio_current() == 0, "12: nothing owns audio after departure");
    CHECK(drained(), "12: departure silences everything");
    m.motion_signature = motion++;
    match_audio_feed(&frame, c2, &m);
    audio_tick_owned(c2);
    CHECK(loud(30) < 40, "12: a departed epoch cannot sound");

    if (fails) { printf("%d FAILED\n", fails); return 1; }
    puts("PASS: match audio controller -- gauges, KO, resource maxima, beacons, "
         "clock edges, pause, exit, mute, switch, late writes/stops/ticks, depart");
    return 0;
}
