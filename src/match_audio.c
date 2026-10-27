/* See match_audio.h. Policy moved here from cvs2_frame.c and
 * cvs2_gauges_observe; the reasons behind each rule are kept with it. */

#include "match_audio.h"
#include "audio.h"

#include <windows.h>
#include <string.h>

enum { EDGE_UNKNOWN = 0, EDGE_VALID, EDGE_INVALID };

static volatile LONG64 g_current;   /* written by the worker only */
static uint64_t g_next;             /* worker only                */

uint64_t match_audio_current(void)
{
    return (uint64_t)InterlockedCompareExchange64(&g_current, 0, 0);
}

void match_audio_depart(uint64_t epoch)
{
    if (!epoch || (LONG64)epoch != g_current) return;
    InterlockedExchange64(&g_current, 0);
    audio_owner_stop(epoch);
}

uint64_t match_audio_current_for(unsigned adapter)
{
    uint64_t epoch = match_audio_current();
    return epoch && (epoch & 15u) == (adapter & 15u) ? epoch : 0;
}

uint64_t match_audio_begin(unsigned adapter)
{
    uint64_t old = match_audio_current();
    if (old) match_audio_depart(old);
    uint64_t epoch = (++g_next << 4) | (adapter & 15u);
    /* audio.c first: from this store on, nothing stamped with an older epoch
     * can sound, so a producer still holding the old epoch is harmless. */
    if (!audio_owner_begin(epoch)) return 0;
    InterlockedExchange64(&g_current, (LONG64)epoch);
    return epoch;
}

static void forget_edges(match_audio_producer *p)
{
    memset(p->health, 0, sizeof p->health);
    memset(p->resource, 0, sizeof p->resource);
    p->clock_seeded = false;
    p->motion_seeded = false;
    p->static_frames = 0;
    p->paused = false;
}

/* ⛔⛔ ONLY TOUCH THE MIXER WHEN SOMETHING CHANGED. Each gauge call takes the
 * mixer lock; 120 locked calls per second from the frame thread is what made
 * the health tone crackle. An unchanged value would not re-arm the hold
 * anyway, so nothing is lost. */
static void feed_gauge(match_audio_producer *p, uint64_t epoch, audio_gauge g,
                       unsigned o, match_audio_edge *e, const match_audio_gauge *v,
                       bool present, const match_audio_owner *fighter)
{
    if (!present || !v->valid || v->maximum <= 0 || v->value < 0 ||
        v->value > v->maximum) {
        /* A sentinel (CVS2's 0xFFFF at KO) or missing field must never be
         * swept to as a value. Forget history ONCE so the next real value
         * seeds silently instead of sounding as a jump. */
        p->rejected++;
        if (e->state != EDGE_INVALID) {
            audio_gauge_reset_owned(epoch, g, o);
            p->gauge_writes++;
            e->state = EDGE_INVALID;
        }
        return;
    }
    if (e->state == EDGE_VALID && e->value == v->value &&
        e->maximum == v->maximum) return;
    /* A new maximum is a new scale (another fighter, groove or form), not
     * movement: reseed silently rather than sound the rescaled level. */
    if (e->state == EDGE_VALID && e->maximum != v->maximum) {
        audio_gauge_reset_owned(epoch, g, o);
        p->gauge_writes++;
    }
    /* A fighter's tone takes its screen position at the moment of change. */
    if (fighter)
        audio_gauge_set_fighter_owned(epoch, g, o, v->value, v->maximum,
                                      fighter->gauge_pan_valid ? fighter->gauge_pan : 0.0f);
    else
        audio_gauge_set_owned(epoch, g, o, v->value, v->maximum);
    p->gauge_writes++;
    e->state = EDGE_VALID;
    e->value = v->value;
    e->maximum = v->maximum;
}

#define BEACON_OWNERS 2
static void beacons_off(uint64_t epoch)
{
    for (unsigned o = 0; o < BEACON_OWNERS; o++)
        audio_beacon_set_owned(epoch, o, FALSE, 0.0f, 1.0f);
}

/* Silence exactly the families this producer feeds, once per departure.
 * Without it the cues hold their last in-match value over the next screen. */
static void leave_match(match_audio_producer *p, uint64_t epoch)
{
    for (unsigned o = 0; o < MATCH_AUDIO_OWNERS; o++) {
        if (p->feeds & MATCH_AUDIO_FEED_HEALTH)
            audio_gauge_reset_owned(epoch, AUDIO_GAUGE_HEALTH, o);
        if (p->feeds & MATCH_AUDIO_FEED_RESOURCE)
            audio_gauge_reset_owned(epoch, AUDIO_GAUGE_SUPER, o);
    }
    if (p->feeds & MATCH_AUDIO_FEED_BEACON) beacons_off(epoch);
}

/* ⭐ PAUSE: a signal the hooked function itself advances cannot say that the
 * function is idle (the first CVS2 attempt watched its own frame counter and
 * never fired). The adapter supplies a signature of state only the SIMULATION
 * moves; if it holds still long enough, the game is not simulating. */
static void track_motion(match_audio_producer *p, const match_audio_sample *s)
{
    if (!s->motion_valid) {
        p->motion_seeded = false;
        p->static_frames = 0;
        p->paused = false;
        return;
    }
    if (p->motion_seeded && s->motion_signature == p->motion) {
        if (p->static_frames < 1000) p->static_frames++;
    } else {
        p->static_frames = 0;
        p->motion = s->motion_signature;
        p->motion_seeded = true;
    }
    bool paused = p->static_frames >= MATCH_AUDIO_PAUSE_FRAMES;
    if (paused && !p->paused) p->pauses++;
    p->paused = paused;
}

/* Warn at 30 and 15, then every second for the last five -- only on a
 * DECREASE, so a round reset jumping upward never fires the set on its way
 * past, and a frozen clock (Training) never ticks. */
static void track_clock(match_audio_producer *p, uint64_t epoch,
                        const match_audio_sample *s)
{
    if (!s->clock_valid) { p->clock_seeded = false; return; }
    int t = s->clock;
    if (p->clock_seeded && t < p->clock &&
        (t == 30 || t == 15 || (t >= 1 && t <= 5))) {
        audio_tick_owned(epoch);
        p->ticks++;
    }
    p->clock = t;
    p->clock_seeded = true;
}

void match_audio_feed(match_audio_producer *p, uint64_t epoch,
                      const match_audio_sample *s)
{
    if (!p || !s || !epoch) return;
    if (epoch != p->epoch) {
        /* A new owner: nothing the previous one saw may produce an edge. The
         * mixer side was already silenced by audio_owner_begin. */
        forget_edges(p);
        p->in_match = false;
        p->epoch = epoch;
    }

    if (!s->in_match) {
        if (p->in_match) leave_match(p, epoch);
        p->in_match = false;
        forget_edges(p);
        return;
    }
    p->in_match = true;

    for (unsigned o = 0; o < MATCH_AUDIO_OWNERS; o++) {
        const match_audio_owner *a = &s->owner[o];
        /* HUD gauges exist for P1 and P2 only; audio_gauge_set_owned refuses
         * owners 2 and 3 itself. */
        const match_audio_owner *fighter = s->fighter_gauges ? a : NULL;
        if (p->feeds & MATCH_AUDIO_FEED_HEALTH)
            feed_gauge(p, epoch, AUDIO_GAUGE_HEALTH, o, &p->health[o],
                       &a->health, a->present, fighter);
        if (p->feeds & MATCH_AUDIO_FEED_RESOURCE)
            feed_gauge(p, epoch, AUDIO_GAUGE_SUPER, o, &p->resource[o],
                       &a->resource, a->present, fighter);
    }

    if (p->feeds & MATCH_AUDIO_FEED_BEACON) {
        bool was_paused = p->paused;
        track_motion(p, s);
        if (p->paused) {
            /* A paused game's positions are frozen and convey nothing; a
             * droning pair of tones over the pause menu is noise. */
            if (!was_paused) beacons_off(epoch);
        } else {
            /* ⭐ Fed EVERY frame: a beacon is a continuous position channel,
             * not an edge. Lock-free in audio.c, so this costs no contention. */
            for (unsigned o = 0; o < BEACON_OWNERS; o++) {
                const match_audio_owner *a = &s->owner[o];
                BOOL live = a->present && a->beacon_valid;
                audio_beacon_set_owned(epoch, o, live, a->screen_pan, a->stance);
            }
        }
    }

    if (p->feeds & MATCH_AUDIO_FEED_CLOCK) track_clock(p, epoch, s);
}
