/* Prove the CLASSIFIED discovery sweep is equivalent to the original.
 *
 * ⭐⭐ WHY THIS EXISTS. The sweep was rewritten for speed -- 70 ms -> 0.03 ms --
 * after an unbounded version STALLED A LIVE RUN FOR 37 SECONDS (2026-09-09
 * 23:13, entering the Music screen; the user heard the mod stop reading).
 *
 * ⛔ A performance change that alters WHICH bases survive is a behaviour
 * change wearing a performance change's clothes: it would silently change
 * which screens can be named, and the shortlist a person acts on. "It looks
 * the same on the cases I tried" is not evidence -- so this compares the two
 * implementations exhaustively, over every item count 2..70 in both strict and
 * relaxed modes, and requires the surviving base lists to be IDENTICAL.
 *
 * ⚠ Against the REAL blob (build/strings.bin, from tools/dump_string_blob.py),
 * because this project has twice been misled by synthetic strings that sat in
 * one clean page.
 *
 * Build/run: tools/run_de_test.ps1
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "gmd_find.h"
#include "memsafe.h"
#define CLS_ITEM 1
#define CLS_BLURB 2
static unsigned char cls[8192];
static int old_sweep(int count,int total,int strict,int*out,int cap){
    int n=0;
    for(int base=0;base+count<total&&n<cap;base++){
        char cur[128];int ok=1;
        for(int i=0;i<count&&ok;i++){
            if(!gmd_text_by_index(base+i,cur,sizeof(cur))||!cur[0]){ok=0;break;}
            size_t L=strlen(cur);
            if(cur[0]=='<'||L>32||cur[L-1]=='.'||cur[L-1]=='?'||cur[L-1]=='!')ok=0;
        }
        if(!ok)continue;
        int need=strict?count:1;
        for(int i=0;i<need&&ok;i++){
            if(!gmd_text_by_index(base+count+i,cur,sizeof(cur))||!cur[0]){ok=0;break;}
            size_t H=strlen(cur);
            if(cur[H-1]!='.'&&cur[H-1]!='?'&&cur[H-1]!='!')ok=0;
        }
        if(!ok)continue;
        out[n++]=base;
    }
    return n;
}
static int new_sweep(int count,int total,int strict,int*out,int cap){
    int n=0;
    for(int base=0;base+count<total&&n<cap;base++){
        int ok=1;
        for(int i=0;i<count;i++) if(!(cls[base+i]&CLS_ITEM)){ok=0;break;}
        if(!ok)continue;
        int need=strict?count:1;
        if(base+count+need>total)continue;
        for(int i=0;i<need;i++) if(!(cls[base+count+i]&CLS_BLURB)){ok=0;break;}
        if(!ok)continue;
        out[n++]=base;
    }
    return n;
}
int main(int argc,char**argv){
    FILE*f=fopen(argv[1],"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    BYTE*blob=(BYTE*)VirtualAlloc(NULL,n+4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(fread(blob,1,n,f)!=(size_t)n)return 1; fclose(f);
    if(!mem_guard_install())return 1;
    gmd_test_set_blob(blob,4165);
    int total=gmd_text_count();
    for(int i=0;i<total&&i<8192;i++){
        char s[128];unsigned char fl=0;
        if(gmd_text_by_index(i,s,sizeof(s))&&s[0]){
            size_t L=strlen(s);char last=s[L-1];
            int term=(last=='.'||last=='?'||last=='!');
            if(s[0]!='<'&&L<=32&&!term)fl|=CLS_ITEM;
            if(term)fl|=CLS_BLURB;
        }
        cls[i]=fl;
    }
    int bad=0,checked=0;
    for(int count=2;count<=70;count++){
        for(int strict=0;strict<2;strict++){
            int a[64],b[64];
            int na=old_sweep(count,total,strict,a,64);
            int nb=new_sweep(count,total,strict,b,64);
            checked++;
            if(na!=nb||memcmp(a,b,na*sizeof(int))!=0){
                printf("MISMATCH count=%d strict=%d: old %d, new %d\n",count,strict,na,nb);
                bad++;
            }
        }
    }
    printf("%d (count,strict) combinations compared, %d mismatches\n",checked,bad);
    return bad?1:0;
}
