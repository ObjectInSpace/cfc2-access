/*
 * Shared in-match audio controller.
 *
 * The cue POLICY that used to live inside cvs2_frame.c and cvs2_gauges_observe
 * -- health/resource edges, the clock warning ticks, pause and exit handling --
 * so a new adapter supplies decoded values instead of copying that policy.
 *
 * What stays in the ADAPTER: native offsets and reads, phase/mode decoding,
 * resource caps, camera projection into a screen pan, the mapping from native
 * stance/action IDs onto the AUDIO_STANCE_* ladder, and which fields go into
 * the motion signature. None of that is guessed here.
 *
 * Threads:
 *  - match_audio_begin / match_audio_depart: the speech worker only. They are
 *    the routing: exactly one owner epoch is current, and audio.c rejects
 *    every write from any other epoch, including writes racing a switch.
 *  - match_audio_feed: any ONE producer thread per producer object. CVS2 has a
 *    frame-thread producer (health, beacons, clock) and a worker producer
 *    (resource). A producer object is never shared between threads.
 *  - match_audio_current: any thread; one interlocked read.
 *
 * No native pointers, no memory reads, no speech.
 */
#ifndef CFC2_MATCH_AUDIO_H
#define CFC2_MATCH_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

/* The legacy mixer routes two owners (P1 HUD-left, P2 HUD-right). Four-owner
 * audio is the separate Power Stone phase; do not widen this by analogy. */
#define MATCH_AUDIO_OWNERS 2

/* Frames with an unchanged motion signature before beacons treat the game as
 * paused. ~0.5 s at 60 fps: a false pause silences beacons mid-fight, which is
 * worse than a beacon running briefly into a pause. */
#define MATCH_AUDIO_PAUSE_FRAMES 30

/* Which cue families a producer feeds. A producer touches nothing else, so
 * its exit reset cannot silence another thread's cues. */
enum {
    MATCH_AUDIO_FEED_HEALTH   = 1u << 0,
    MATCH_AUDIO_FEED_RESOURCE = 1u << 1,
    MATCH_AUDIO_FEED_BEACON   = 1u << 2,
    MATCH_AUDIO_FEED_CLOCK    = 1u << 3,
};

typedef struct {
    bool valid;            /* adapter vouches value and maximum are current */
    int value, maximum;    /* 0 <= value <= maximum, maximum > 0            */
} match_audio_gauge;

typedef struct {
    bool present;
    match_audio_gauge health, resource;
    bool beacon_valid;     /* pan/stance were derived from current state     */
    float screen_pan;      /* -1 left .. +1 right, already projected         */
    float stance;          /* AUDIO_STANCE_* multiplier                      */
} match_audio_owner;

typedef struct {
    /* An OBSERVED in-match state. false = observed outside a match, which
     * silences this producer's cues once. Unreadable state is not a sample:
     * do not call feed at all for it. */
    bool in_match;
    bool motion_valid;
    uint32_t motion_signature;   /* changes while the game simulates */
    bool clock_valid;
    int clock;                   /* the visible round clock          */
    match_audio_owner owner[MATCH_AUDIO_OWNERS];
} match_audio_sample;

typedef struct { int state, value, maximum; } match_audio_edge;

/* Zero-initialise, then set `feeds`. Edge state only; no ownership. */
typedef struct {
    unsigned feeds;
    uint64_t epoch;              /* epoch the edge state belongs to */
    bool in_match, paused;
    match_audio_edge health[MATCH_AUDIO_OWNERS], resource[MATCH_AUDIO_OWNERS];
    bool clock_seeded;
    int clock;
    bool motion_seeded;
    uint32_t motion;
    int static_frames;
    /* Liveness, read by diagnostics on the producer's own thread. */
    unsigned pauses, ticks, gauge_writes, rejected;
} match_audio_producer;

/* Worker: claim audio output for a new presentation owner. Departs any
 * previous one. Returns the new nonzero epoch, or 0 if audio refused it. */
uint64_t match_audio_begin(void);
/* Worker: stop the current owner. A stale epoch is ignored. */
void match_audio_depart(uint64_t epoch);
/* Any thread: the current epoch, 0 when nothing owns match audio. */
uint64_t match_audio_current(void);

/* Producer thread: apply one owned sample under `epoch`. A different epoch
 * than last time discards this producer's edge state first, so nothing the
 * old owner saw can produce an edge for the new one. epoch 0 does nothing. */
void match_audio_feed(match_audio_producer *p, uint64_t epoch,
                      const match_audio_sample *s);

#endif
