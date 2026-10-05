#include "../src/pws2_draw_reader.h"
#include "../src/memsafe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
void log_line(const char *fmt, ...) { (void)fmt; }
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
typedef struct {
    unsigned char root[8], context[8], object[288], resource[168], packet[24];
    unsigned char slots[264], globals[128];
    unsigned calls, fail, short_read, expire, mutate;
} fixture;
static void put64(void *p,uint64_t x) { memcpy(p,&x,8); }
static void put32(void *p,uint32_t x) { memcpy(p,&x,4); }
static void init(fixture *f)
{
    memset(f,0,sizeof(*f)); put64(f->root,0x100000); put64(f->context,0x300000);
    f->object[0]=8; f->object[1]=0; f->object[2]=3; f->object[0xc0]=1;
    put64(f->object+8,0x140afab70ULL); put32(f->object+0x28,2);
    put32(f->object+0xd8,8); put64(f->object+0xe0+24,0x155010);
    f->resource[0]=1; put64(f->resource+8,0x600000); put32(f->slots,1);
    put32(f->resource+0x34,0x3f800000); /* Distinct from pre-copy slot 1. */
}
static int64_t now(void *ctx) { fixture *f=ctx; return f->expire && f->calls>=f->expire-1 ? 100 : 0; }
static SIZE_T read_fixture(void *ctx,uint64_t p,void *out,SIZE_T n,unsigned *queries,enum pws2_read_error *error)
{
    fixture *f=ctx; const void *src=NULL; f->calls++; *queries=1; *error=PWS2_READ_OK;
    if (f->calls==f->fail) { memset(out,0xab,n); return f->short_read ? n-1 : 0; }
    switch(p) {
    case 0x14262f0b0ULL: src=f->root; break;
    case 0x142c79b40ULL: src=f->context; break;
    case 0x200000: src=f->object; break;
    case 0x2000e0: src=f->object+0xe0; break;
    case 0x155010: src=f->resource; break;
    case 0x600000: src=f->packet; break;
    case 0x4f80a4: src=f->slots; break;
    case 0x141c00210ULL: src=f->globals; break;
    }
    CHECK(src!=NULL); memcpy(out,src,n);
    if (f->mutate && f->calls==9) memset(out,0,n);
    return n;
}
int main(void)
{
    fixture f; pws2_draw_record r;
    pws2_read_request q={0x100000,0x200000,0x155010,0x155044,0x140a5df81ULL,1,0,100};
    init(&f); pws2_draw_read(&r,&q,read_fixture,now,&f);
    CHECK(r.error==0 && r.issues==0 && r.ordinal==3);
    CHECK(r.attempts==10 && r.bytes==904 && r.queries==10 && r.valid_mask==1023);
    CHECK(memcmp(r.resource+0x34,r.slots+72,64)!=0);
    CHECK(r.object[2]==3 && r.object[1]==0 && r.object[0x28]==2);
    for(unsigned i=1;i<=10;i++) for(unsigned short_read=0;short_read<2;short_read++) {
        init(&f); f.fail=i; f.short_read=short_read;
        pws2_draw_read(&r,&q,read_fixture,now,&f);
        CHECK(r.error==PWS2_READ_MEMORY && r.attempts==i && r.valid_mask==((1u<<(i-1))-1));
    }
    for(unsigned i=1;i<=10;i++) {
        init(&f); f.expire=i; pws2_draw_read(&r,&q,read_fixture,now,&f);
        CHECK(r.error==PWS2_READ_DEADLINE && r.attempts==i-1);
    }
    for(unsigned count=0;count<=9;count+=9) {
        init(&f); put32(f.object+0xd8,count); pws2_draw_read(&r,&q,read_fixture,now,&f);
        CHECK(r.error==PWS2_READ_COUNT && r.attempts==3);
    }
    init(&f); put64(f.object+0xe0,0x155010); pws2_draw_read(&r,&q,read_fixture,now,&f);
    CHECK(r.error==PWS2_READ_ASSOCIATION && r.attempts==4);
    init(&f); f.object[1]=255; put64(f.object+8,0x140af9a90ULL);
    pws2_draw_read(&r,&q,read_fixture,now,&f); CHECK(!r.error && !r.issues);
    f.calls=0; f.object[2]=255; put32(f.resource+0x34,0x7fc00001); f.mutate=1;
    pws2_draw_read(&r,&q,read_fixture,now,&f);
    CHECK(r.issues==(PWS2_OWNER|PWS2_NONFINITE|PWS2_IDENTITY));
    init(&f); q.object=UINT64_MAX; pws2_draw_read(&r,&q,read_fixture,now,&f);
    CHECK(r.error==PWS2_READ_RANGE && !r.attempts);

    CHECK(pws2_draw_reader_page_size_ok()); CHECK(mem_guard_install());
    SYSTEM_INFO si; GetSystemInfo(&si); SIZE_T page=si.dwPageSize;
    unsigned char *mem=VirtualAlloc(NULL,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(mem!=NULL); memset(mem,0x5a,page*2);
    unsigned char out[264]; unsigned queries; enum pws2_read_error error; DWORD old;
    CHECK(VirtualProtect(mem+page,page,PAGE_READONLY,&old));
    CHECK(pws2_draw_native_copy(NULL,(uintptr_t)(mem+page-132),out,264,&queries,&error)==264);
    CHECK(queries==2 && !error && out[263]==0x5a);
    CHECK(VirtualProtect(mem+page,page,PAGE_NOACCESS,&old));
    CHECK(!pws2_draw_native_copy(NULL,(uintptr_t)(mem+page-132),out,264,&queries,&error));
    CHECK(queries==2 && error==PWS2_READ_MEMORY);
    CHECK(VirtualProtect(mem+page,page,PAGE_READWRITE|PAGE_GUARD,&old));
    CHECK(!pws2_draw_native_copy(NULL,(uintptr_t)(mem+page),out,264,&queries,&error));
    MEMORY_BASIC_INFORMATION mbi; VirtualQuery(mem+page,&mbi,sizeof(mbi));
    CHECK(queries==1 && (mbi.Protect&PAGE_GUARD));
    g_mem_guard_armed=1;
    CHECK(!pws2_draw_native_copy(NULL,(uintptr_t)mem,out,8,&queries,&error));
    CHECK(error==PWS2_READ_CONTENTION && queries==0 && g_mem_guard_armed==1);
    g_mem_guard_armed=0;
    CHECK(!pws2_draw_native_copy(NULL,UINT64_MAX,out,8,&queries,&error));
    CHECK(error==PWS2_READ_RANGE && queries==0);
    CHECK(!pws2_draw_native_copy(NULL,(uintptr_t)mem,out,265,&queries,&error));
    CHECK(error==PWS2_READ_RANGE && queries==0);
    CHECK(VirtualFree(mem,0,MEM_RELEASE)); mem_guard_remove();
    printf("PWS2 native reader: %u checks passed\n",checks); return 0;
}
