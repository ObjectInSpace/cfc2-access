#include "../src/pws2_draw_batches.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pws2_draw_batches pool;
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static HANDLE start, reserved, finish;
static volatile LONG producers;
static DWORD WINAPI produce(void *arg)
{
    WaitForSingleObject(start,INFINITE);
    pws2_draw_batch *b=pws2_batch_reserve(&pool,1,(uintptr_t)arg,7);
    if (!b) return 1;
    pws2_draw_record *r=pws2_batch_event(b);
    memset(r,0,sizeof(*r)); r->request.invocation=(uintptr_t)arg;
    if (InterlockedIncrement(&producers)==2) SetEvent(reserved);
    WaitForSingleObject(finish,INFINITE);
    pws2_batch_publish(b,7,0); return 0;
}
int main(void)
{
    pws2_draw_batch *held[8];
    for(unsigned i=0;i<8;i++) { held[i]=pws2_batch_reserve(&pool,1,i+1,7); CHECK(held[i]); }
    CHECK(!pws2_batch_reserve(&pool,1,9,7) && pool.dropped==1);
    CHECK(!pws2_batch_acquire(&pool,7)); /* Never touch WRITING, even at timeout. */
    for(unsigned i=0;i<32;i++) {
        pws2_draw_record *r=pws2_batch_event(held[0]); CHECK(r);
        memset(r,0,sizeof(*r)); r->request.invocation=i+1;
    }
    CHECK(!pws2_batch_event(held[0]));
    pws2_batch_publish(held[0],7,0);
    pws2_draw_batch *b=pws2_batch_acquire(&pool,7);
    CHECK(b==held[0] && !b->complete && b->losses==PWS2_LIMIT && b->stored==32 && b->encounters==33);
    CHECK(b->events[31].request.invocation==32);
    CHECK(!pws2_batch_reserve(&pool,1,10,7)); /* READING cannot be reused. */
    pws2_batch_release(b);
    CHECK(pws2_batch_reserve(&pool,1,10,8)==b);
    pws2_batch_publish(b,9,0); CHECK(!b->complete && b->losses==PWS2_STALE);
    CHECK(!pws2_batch_acquire(&pool,9) && pool.stale==1);
    for(unsigned i=1;i<8;i++) pws2_batch_publish(held[i],7,PWS2_TIMEOUT);
    for(unsigned i=1;i<8;i++) {
        b=pws2_batch_acquire(&pool,7); CHECK(b && !b->complete && b->losses==PWS2_TIMEOUT);
        pws2_batch_release(b);
    }
    b=pws2_batch_reserve(&pool,1,11,7);
    pws2_draw_record *r=pws2_batch_event(b); memset(r,0,sizeof(*r)); r->error=PWS2_READ_MEMORY;
    pws2_batch_publish(b,7,0); b=pws2_batch_acquire(&pool,7);
    CHECK(b && !b->complete && b->losses==PWS2_FAULT); pws2_batch_release(b);

    start=CreateEvent(NULL,TRUE,FALSE,NULL); reserved=CreateEvent(NULL,TRUE,FALSE,NULL);
    finish=CreateEvent(NULL,TRUE,FALSE,NULL); CHECK(start && reserved && finish);
    HANDLE threads[2]={CreateThread(NULL,0,produce,(void *)21,0,NULL),CreateThread(NULL,0,produce,(void *)22,0,NULL)};
    CHECK(threads[0] && threads[1]); SetEvent(start);
    CHECK(WaitForSingleObject(reserved,5000)==WAIT_OBJECT_0);
    CHECK(!pws2_batch_acquire(&pool,7));
    /* Timeout observation cannot recycle the two still-owned batches. */
    unsigned writing=0; for(unsigned i=0;i<8;i++) writing+=pool.batches[i].state==PWS2_WRITING;
    CHECK(writing==2); SetEvent(finish);
    CHECK(WaitForMultipleObjects(2,threads,TRUE,5000)==WAIT_OBJECT_0);
    unsigned seen=0;
    for(unsigned i=0;i<2;i++) {
        b=pws2_batch_acquire(&pool,7); CHECK(b && b->complete && b->stored==1);
        CHECK(b->invocation==b->events[0].request.invocation);
        seen|=1u<<(unsigned)(b->invocation-21); pws2_batch_release(b);
        DWORD code; CHECK(GetExitCodeThread(threads[i],&code) && code==0); CloseHandle(threads[i]);
    }
    CHECK(seen==3 && !pws2_batch_acquire(&pool,7));
    CloseHandle(start); CloseHandle(reserved); CloseHandle(finish);
    printf("PWS2 batches: %u checks passed\n",checks); return 0;
}
