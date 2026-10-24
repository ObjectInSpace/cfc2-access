/*
 * Positional pings for things lying in an arena: one repeating ping per
 * pickup, panned to where the game draws it, louder and faster the nearer it
 * is to the player's fighter. Shared mechanism; the adapter (pws1_frame.c)
 * decides what is a pickup, its family and type, its screen pan and its
 * distance. Nothing here reads game memory.
 *
 * Producer thread only (the game's frame hook), one scheduler per producer.
 * Advance it once per simulated frame; do not advance it while paused.
 */
#ifndef CFC2_PICKUP_CUES_H
#define CFC2_PICKUP_CUES_H

#include <stdbool.h>
#include <stdint.h>
#include "audio.h"

#define PICKUP_SLOTS 100          /* stable identities: the game's pool index */

typedef struct {
    bool present;                 /* lying in the arena now                    */
    bool placed;                  /* pan and distance were derived this frame  */
    audio_ping_family family;
    int variant;                  /* type inside the family, 0.. (pitch steps) */
    float pan;                    /* -1 left .. +1 right, already projected    */
    float distance;               /* world units to the nearest player fighter */
} pickup_sample;

typedef struct {
    uint64_t epoch;
    int countdown[PICKUP_SLOTS];  /* frames to this pickup's next ping          */
    int family[PICKUP_SLOTS], variant[PICKUP_SLOTS];
    bool was_present[PICKUP_SLOTS];
    unsigned pings;               /* diagnostics */
} pickup_cues;

/* Frames between pings at `distance`, and the ping's gain 0..1. Exposed for
 * the tests; the rule is: 0.2 s when touching, about 1.2 s across the arena;
 * props half again as slow and quieter (the mixer's level). */
int pickup_interval_frames(audio_ping_family family, float distance);
float pickup_gain(float distance);

/* One simulated frame under `epoch`: count every present pickup down and ping
 * those that reach zero. A pickup that newly appears pings within 0.35 s
 * (staggered by slot), so a stone dropped in a hit is heard at once without
 * a round start's dozen arrivals landing together. A new epoch forgets
 * everything. */
void pickup_cues_frame(pickup_cues *c, uint64_t epoch,
                       const pickup_sample s[PICKUP_SLOTS]);
/* Leaving the arena: forget, so the next one starts fresh. */
void pickup_cues_reset(pickup_cues *c);

#endif
