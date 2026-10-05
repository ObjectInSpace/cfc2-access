#include "pws2_draw_batches.h"
#include <limits.h>

pws2_draw_batch *pws2_batch_reserve(pws2_draw_batches *pool,uint64_t arm,
                                   uint64_t invocation,uint64_t revision)
{
    for(unsigned i=0;i<8;i++) {
        pws2_draw_batch *b=&pool->batches[i];
        if (InterlockedCompareExchange(&b->state,PWS2_WRITING,PWS2_FREE)!=PWS2_FREE)
            continue;
        b->arm=arm; b->invocation=invocation; b->revision=revision;
        b->thread=GetCurrentThreadId(); b->encounters=b->stored=b->losses=0;
        b->complete=FALSE;
        return b;
    }
    InterlockedIncrement(&pool->dropped); return NULL;
}
pws2_draw_record *pws2_batch_event(pws2_draw_batch *b)
{
    if (b->encounters!=UINT_MAX) b->encounters++;
    if (b->stored==32) { b->losses|=PWS2_LIMIT; return NULL; }
    return &b->events[b->stored++];
}
void pws2_batch_publish(pws2_draw_batch *b,uint64_t closing_revision,unsigned losses)
{
    b->losses|=losses;
    if (b->revision!=closing_revision) b->losses|=PWS2_STALE;
    /* Reader rejection is an accounted event, but a failed native observation
     * cannot mark the enclosing interval complete. */
    for(unsigned i=0;i<b->stored;i++)
        if (b->events[i].error) b->losses|=PWS2_FAULT;
    b->complete=!b->losses && b->encounters==b->stored;
    InterlockedExchange(&b->state,PWS2_READY);
}
pws2_draw_batch *pws2_batch_acquire(pws2_draw_batches *pool,uint64_t revision)
{
    for(unsigned i=0;i<8;i++) {
        pws2_draw_batch *b=&pool->batches[i];
        if (InterlockedCompareExchange(&b->state,PWS2_READING,PWS2_READY)!=PWS2_READY)
            continue;
        if (b->revision!=revision) {
            InterlockedIncrement(&pool->stale);
            InterlockedExchange(&b->state,PWS2_FREE);
            continue;
        }
        return b;
    }
    return NULL;
}
void pws2_batch_release(pws2_draw_batch *b) { InterlockedExchange(&b->state,PWS2_FREE); }
