#ifndef CFC2_PWS2_DRAW_BATCHES_H
#define CFC2_PWS2_DRAW_BATCHES_H
#include "pws2_draw_reader.h"
enum pws2_batch_state { PWS2_FREE, PWS2_WRITING, PWS2_READY, PWS2_READING };
enum pws2_batch_loss { PWS2_LIMIT=1, PWS2_NESTING=2, PWS2_FAULT=4,
    PWS2_STALE=8, PWS2_TIMEOUT=16, PWS2_CONTENTION=32 };
typedef struct {
    volatile LONG state;
    uint64_t arm, invocation, revision;
    DWORD thread;
    unsigned encounters, stored, losses;
    BOOL complete;
    pws2_draw_record events[32];
} pws2_draw_batch;
typedef struct {
    pws2_draw_batch batches[8];
    volatile LONG dropped, stale;
} pws2_draw_batches;
/* Zero-initialize before use; never reset while producers/readers exist.
 * Reservation is bounded and nonblocking. Each returned batch has one owner.
 * Interlocked operations provide full barriers, stronger than acquire/release.
 * These primitives do not implement arming, TLS enclosure or shutdown. */
pws2_draw_batch *pws2_batch_reserve(pws2_draw_batches *,uint64_t,uint64_t,uint64_t);
pws2_draw_record *pws2_batch_event(pws2_draw_batch *);
void pws2_batch_publish(pws2_draw_batch *,uint64_t,unsigned);
pws2_draw_batch *pws2_batch_acquire(pws2_draw_batches *,uint64_t);
void pws2_batch_release(pws2_draw_batch *);
#endif
