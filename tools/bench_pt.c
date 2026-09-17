/* Measure the page-table sweep against a realistic amount of committed memory,
 * because a predicted duration was wrong by 20x once already on this branch. */
#include "../src/emu_cursor.h"
#include "../src/memsafe.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
int main(void){
    SIZE_T ASZ = 16u*1024u*1024u;
    SIZE_T BALLAST = 512u*1024u*1024u;
    BYTE *arena, *ball; ec_ptrun r[8]; SIZE_T sc=0; DWORD t0,t1; unsigned n;
    if(!mem_guard_install()){printf("guard failed\n");return 1;}
    arena=VirtualAlloc(NULL,ASZ,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    ball =VirtualAlloc(NULL,BALLAST,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!arena||!ball){printf("alloc failed\n");return 1;}
    for(SIZE_T o=0;o<BALLAST;o+=4096) ball[o]=(BYTE)o;
    emu_cursor_test_window((ULONGLONG)(ULONG_PTR)arena,ASZ);
    t0=GetTickCount();
    n=emu_cursor_find_pagetable(r,8,&sc,0,NULL);
    t1=GetTickCount();
    printf("sweep over %llu MB committed ballast + %llu MB arena\n",
       (unsigned long long)(BALLAST/1048576),(unsigned long long)(ASZ/1048576));
    printf("  wall    : %lu ms\n",(unsigned long)(t1-t0));
    printf("  scanned : %llu MB\n",(unsigned long long)(sc/1048576));
    printf("  runs    : %u\n",n);
    if(t1>t0) printf("  rate    : %llu MB/s\n",
       (unsigned long long)(sc/1048576*1000/(t1-t0)));
    emu_cursor_test_window(0,0);
    return 0;
}
