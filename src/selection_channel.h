#ifndef CFC2_SELECTION_CHANNEL_H
#define CFC2_SELECTION_CHANNEL_H
#include "game_selection.h"

/* Worker-only presentation of owned labels. No native reads, hooks or lifetime
 * guarantees. Zero-initialize once. One active channel may emit for selection;
 * the router must depart the old channel before switching. All calls, including
 * tc_say, run on the existing speech worker. Never call from native callbacks. */
typedef struct {
    game_session session;
    game_selection presenter;
    char source[64];
} selection_channel;

/* Adapter has admitted its context and can supply owned labels. This opens
 * presentation only; it does not authorize copying native memory. Starts a new
 * generation even for the same adapter/ROM. Source is copied, not retained. */
bool selection_channel_begin(selection_channel *c, unsigned adapter,
                             unsigned rom, const char *source);
void selection_channel_depart(selection_channel *c);
/* Each call represents a NEW copied sample, not a retry of an old sample.
 * NULL/incomplete means unavailable, never an observed selection exit. Owner
 * INACTIVE is the adapter's explicit observed exit. now/captured use ms. */
bool selection_channel_offer(selection_channel *c,
    const game_selection_choice choices[GAME_SELECTION_OWNERS],
    bool complete, uint64_t captured_ms, uint64_t now_ms);
/* Retry the latest settled sample without counting it as another observation.
 * Returns accepted owner mask. Uses tc_say, never a per-game speech backend. */
unsigned selection_channel_drain(selection_channel *c, uint64_t now_ms);
#endif
