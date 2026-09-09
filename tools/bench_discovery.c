/* Measure the discovery sweep against the REAL GMD string blob.
 * ⚠ Real data, not synthetic: this project already hid a per-byte reader
 * behind 6-byte synthetic strings, and a benchmark whose data is unlike the
 * real data measures nothing. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "gmd_find.h"
#include "memsafe.h"
int main(int argc,char**argv){
    if(argc<2){printf("usage: bench2 <strings.bin>\n");return 1;}
    FILE*f=fopen(argv[1],"rb"); if(!f){printf("no file\n");return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    BYTE*blob=(BYTE*)VirtualAlloc(NULL,n+4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    fread(blob,1,n,f); fclose(f);
    if(!mem_guard_install()){printf("guard failed\n");return 1;}
    gmd_test_set_blob(blob,4165);
    int total=gmd_text_count();
    printf("strings visible: %d\n",total);
    char probe[128];
    if(gmd_text_by_index(37,probe,sizeof(probe))) printf("index 37 = \"%s\"\n",probe);
    LARGE_INTEGER fq,a,b; QueryPerformanceFrequency(&fq);
    int counts[]={2,3,6,8,40,67};
    for(unsigned c=0;c<sizeof(counts)/sizeof(counts[0]);c++){
        int count=counts[c];
        int cap=total>600?600:total;
        QueryPerformanceCounter(&a);
        int shown=0;
        for(int base=0;base+count<cap&&shown<8;base++){
            char cur[128]; int ok=1;
            for(int i=0;i<count&&ok;i++){
                if(!gmd_text_by_index(base+i,cur,sizeof(cur))||!cur[0]){ok=0;break;}
                size_t L=strlen(cur);
                if(cur[0]=='<'||L>32||cur[L-1]=='.'||cur[L-1]=='?'||cur[L-1]=='!') ok=0;
            }
            if(!ok) continue;
            if(!gmd_text_by_index(base+count,cur,sizeof(cur))||!cur[0]) continue;
            size_t L=strlen(cur);
            if(cur[L-1]!='.'&&cur[L-1]!='?'&&cur[L-1]!='!') continue;
            shown++;
        }
        QueryPerformanceCounter(&b);
        printf("count=%2d -> %6.2f ms  (%d candidates)\n",count,
               (b.QuadPart-a.QuadPart)*1000.0/fq.QuadPart,shown);
    }
    return 0;
}
