/* Measure the hash pass against a REAL 256 MB allocation, because a predicted
 * throughput was wrong by 20x once already. */
#include "../src/emu_cursor.h"
#include "../src/memsafe.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
int main(void){
    SIZE_T SZ = 256u*1024u*1024u;
    BYTE *a; DWORD t0,t1; ec_status s; ec_report r; int i;
    if(!mem_guard_install()){printf("guard failed\n");return 1;}
    a=(BYTE*)VirtualAlloc(NULL,SZ,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!a){printf("alloc failed\n");return 1;}
    /* Touch it so the pages are committed for real, like a live arena. */
    for(SIZE_T o=0;o<SZ;o+=4096) a[o]=(BYTE)(o>>12);
    emu_cursor_test_window((ULONGLONG)(ULONG_PTR)a,SZ);
    t0=GetTickCount();
    s=emu_cursor_arm();
    t1=GetTickCount();
    emu_cursor_status(&r);
    printf("arm/hash pass over 256 MB: %s\n", ec_status_name(s));
    printf("  wall        : %lu ms\n",(unsigned long)(t1-t0));
    printf("  reported    : %lu ms\n",(unsigned long)r.last_pass_ms);
    printf("  bytes hashed: %llu (%llu MB)\n",
        (unsigned long long)r.bytes_hashed,
        (unsigned long long)(r.bytes_hashed/1048576));
    if(r.last_pass_ms)
      printf("  throughput  : %llu MB/s\n",
        (unsigned long long)(r.bytes_hashed/1048576*1000/r.last_pass_ms));
    /* three more passes, to see a warm figure */
    for(i=0;i<3;i++){
        emu_cursor_step(EC_PHASE_CONTROL);
        emu_cursor_status(&r);
        printf("  pass %d: %lu ms\n", i+2,(unsigned long)r.last_pass_ms);
        emu_cursor_step(EC_PHASE_MOVED);
        emu_cursor_step(EC_PHASE_RETURNED);
    }
    emu_cursor_test_window(0,0); emu_cursor_reset();
    VirtualFree(a,0,MEM_RELEASE);
    return 0;
}
