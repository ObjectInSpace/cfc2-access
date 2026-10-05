#include "pws2_draw_reader.h"
#include "memsafe.h"
#include <string.h>

static BOOL range(uint64_t p, SIZE_T n) { return p && n && n <= UINT64_MAX-p; }
static uint64_t u64(const void *p) { uint64_t v; memcpy(&v,p,8); return v; }
static int32_t i32(const void *p) { int32_t v; memcpy(&v,p,4); return v; }
static BOOL finite_words(const unsigned char *p, unsigned n)
{
    for (unsigned i=0;i<n;i++) {
        uint32_t v; memcpy(&v,p+4*i,4);
        if ((v & 0x7f800000u)==0x7f800000u) return FALSE;
    }
    return TRUE;
}
static BOOL take(pws2_draw_record *r, pws2_read_fn read, pws2_clock_fn clock,
                 void *ctx, uint64_t address, void *out, SIZE_T size)
{
    enum pws2_read_error error=PWS2_READ_OK;
    unsigned queries=0;
    if (!range(address,size)) { r->error=PWS2_READ_RANGE; return FALSE; }
    if (r->attempts>=10 || size>904-r->bytes) {
        r->error=PWS2_READ_BUDGET; return FALSE;
    }
    if (clock(ctx)>=r->request.deadline_qpc) {
        r->error=PWS2_READ_DEADLINE; return FALSE;
    }
    r->attempts++; r->bytes+=(unsigned)size;
    SIZE_T copied=read(ctx,address,out,size,&queries,&error);
    r->queries+=queries;
    if (copied!=size || error!=PWS2_READ_OK || queries>2) {
        memset(out,0,size);
        r->error=queries>2 ? PWS2_READ_BUDGET :
            error ? error : PWS2_READ_MEMORY;
        return FALSE;
    }
    r->valid_mask |= 1u << (r->attempts-1);
    return TRUE;
}

void pws2_draw_read(pws2_draw_record *r, const pws2_read_request *q,
                   pws2_read_fn read, pws2_clock_fn clock, void *ctx)
{
    memset(r,0,sizeof(*r)); r->request=*q; r->ordinal=-1;
    if (!read || !clock || q->caller!=0x140a5df81ULL || !q->invocation) {
        r->error=PWS2_READ_STAGE; return;
    }
    if (!range(q->root,0x8cd40) || !range(q->object,0x120) ||
        !range(q->resource,0xa8) || q->matrix!=q->resource+0x34) {
        r->error=PWS2_READ_RANGE; return;
    }
    uint64_t base=q->root+0x55010;
    if (q->resource<base || q->resource-base>=640*0xa8 ||
        (q->resource-base)%0xa8) { r->error=PWS2_READ_POOL; return; }
#define TAKE(field,address,size) \
    do { if (!take(r,read,clock,ctx,address,r->field,size)) return; } while(0)
    TAKE(root_before,0x14262f0b0ULL,8);
    if (u64(r->root_before)!=q->root) { r->error=PWS2_READ_ROOT; return; }
    TAKE(context_before,0x142c79b40ULL,8);
    uint64_t context=u64(r->context_before);
    if (!range(context,0x1f81ac)) { r->error=PWS2_READ_RANGE; return; }
    TAKE(object,q->object,0xe0);
    int32_t count=i32(r->object+0xd8);
    if (count<1 || count>8) { r->error=PWS2_READ_COUNT; return; }
    TAKE(association,q->object+0xe0,(SIZE_T)count*8);
    unsigned matches=0;
    for (int i=0;i<count;i++) if (u64(r->association+i*8)==q->resource) {
        matches++; r->ordinal=i;
    }
    if (matches!=1) { r->ordinal=-1; r->error=PWS2_READ_ASSOCIATION; return; }
    uint64_t callback=u64(r->object+8);
    if (r->object[0]!=8 || (callback!=0x140afab70ULL && callback!=0x140af9a90ULL))
        r->issues|=PWS2_OBJECT;
    if (r->object[2]>3) r->issues|=PWS2_OWNER;
    if (!r->object[0xc0]) r->issues|=PWS2_OBJECT_DISABLED;
    TAKE(resource,q->resource,0xa8);
    if (!r->resource[0]) r->issues|=PWS2_RESOURCE_DISABLED;
    if (r->resource[1]) r->issues|=PWS2_TYPE;
    TAKE(packet,u64(r->resource+8),0x18);
    if (i32(r->packet)<0) r->issues|=PWS2_PACKET;
    TAKE(slots,context+0x1f80a4,0x108);
    if (i32(r->slots)!=1) r->issues|=PWS2_SLOT;
    TAKE(globals,0x141c00210ULL,0x80);
    if (!finite_words(r->resource+0x34,16) || !finite_words(r->slots+8,64) ||
        !finite_words(r->globals,32)) r->issues|=PWS2_NONFINITE;
    TAKE(root_after,0x14262f0b0ULL,8);
    TAKE(context_after,0x142c79b40ULL,8);
    if (memcmp(r->root_before,r->root_after,8) ||
        memcmp(r->context_before,r->context_after,8)) r->issues|=PWS2_IDENTITY;
#undef TAKE
}

BOOL pws2_draw_reader_page_size_ok(void)
{
    SYSTEM_INFO info; GetSystemInfo(&info); return info.dwPageSize>=264;
}
int64_t pws2_draw_qpc(void *ctx)
{
    LARGE_INTEGER q; (void)ctx; QueryPerformanceCounter(&q); return q.QuadPart;
}
SIZE_T pws2_draw_native_copy(void *ctx, uint64_t address, void *out, SIZE_T n,
                            unsigned *queries, enum pws2_read_error *error)
{
    (void)ctx; *queries=0; *error=PWS2_READ_OK;
    if (!range(address,n) || n>264) { *error=PWS2_READ_RANGE; return 0; }
    if (g_mem_guard_armed) { *error=PWS2_READ_CONTENTION; return 0; }
    SIZE_T done=0;
    while (done<n && *queries<2) {
        MEMORY_BASIC_INFORMATION mbi;
        (*queries)++;
        if (!VirtualQuery((void *)(uintptr_t)(address+done),&mbi,sizeof(mbi)) ||
            mbi.State!=MEM_COMMIT || (mbi.Protect & (PAGE_GUARD|PAGE_NOACCESS)) ||
            !(mbi.Protect & (PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|
              PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) break;
        uint64_t start=(uint64_t)(uintptr_t)mbi.BaseAddress;
        if (!range(start,mbi.RegionSize) || start>address+done ||
            start+mbi.RegionSize<=address+done) break;
        uint64_t available=start+mbi.RegionSize-address-done;
        done+=available<n-done ? (SIZE_T)available : n-done;
    }
    if (done!=n) { *error=PWS2_READ_MEMORY; return 0; }
    volatile BOOL faulted;
    MEM_GUARDED(faulted, memcpy(out,(void *)(uintptr_t)address,n));
    if (faulted) { *error=PWS2_READ_MEMORY; return 0; }
    return n;
}
