/*
 * Pickup announcements for the Power Stone games: who picked up what kind of
 * thing ("Falcon, food"). Shared mechanism; each adapter decides what was
 * picked up and by whom (pws1_frame.c, pws2_frame.c).
 *
 * The queue: a single-producer ring, filled on the game's frame thread and
 * drained by the speech worker. pickup_say: the worker's formatter, with the
 * F9 setting (audio.h AUDIO_OPT_PICKUP_SPEECH: every fighter, players only,
 * off) applied. The game names no items as text, so the kind is what is
 * said. User, 2026-10-27: every fighter, since any player may be blind.
 */
#ifndef CFC2_PICKUP_SPEECH_H
#define CFC2_PICKUP_SPEECH_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

/* The kinds, in words (mod_text.h MT_PICK_*). */
typedef enum {
    PICKUP_STONE = 0, PICKUP_FOOD, PICKUP_ITEM, PICKUP_TREASURE, PICKUP_THROWABLE, PICKUP_KINDS
} pickup_kind;

typedef struct {
    unsigned kind;                /* adapter-defined event kind; 0 = a pickup  */
    uint64_t epoch;               /* match-audio owner when it happened        */
    unsigned fighter, id, code;   /* fighter index, native/shell ID, code      */
    bool human;                   /* played by a person                        */
} pickup_event;

#define PICKUP_RING 16u
typedef struct {
    pickup_event slot[PICKUP_RING];
    volatile LONG head, tail;     /* head: producer; tail: consumer             */
    volatile LONG pushed, dropped;
} pickup_ring;

/* Producer thread only. A full ring drops (counted), never blocks. */
static inline void pickup_ring_push(pickup_ring *r, const pickup_event *e)
{
    LONG h = r->head, t = InterlockedCompareExchange(&r->tail, 0, 0);
    if ((unsigned)(h - t) >= PICKUP_RING) { InterlockedIncrement(&r->dropped); return; }
    r->slot[(unsigned)h % PICKUP_RING] = *e;
    InterlockedExchange(&r->head, h + 1);   /* publishes the slot */
    InterlockedIncrement(&r->pushed);
}

/* Consumer thread only. */
static inline bool pickup_ring_pop(pickup_ring *r, pickup_event *e)
{
    LONG t = r->tail;
    if (t == InterlockedCompareExchange(&r->head, 0, 0)) return false;
    *e = r->slot[(unsigned)t % PICKUP_RING];
    InterlockedExchange(&r->tail, t + 1);
    return true;
}

/* Worker: speak "name, kind" for fighter `name` (UTF-8, the shell roster's),
 * unless the F9 setting excludes it (off, or players only and `human` is
 * false). Returns TRUE when it was spoken. */
BOOL pickup_say(const char *name, pickup_kind kind, bool human);

#endif
