/* Replays real captures through the shipping decoder and speech decisions.
 * No game writes or audible output. Run from the repository root. */
#include "../src/cvs2_select.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static int calls, speech_accepts = 1;
static wchar_t last[600];
BOOL speech_say(const wchar_t *s, BOOL interrupt)
{ (void)interrupt; calls++; wcscpy(last,s); return speech_accepts; }
void log_line(const char *fmt, ...) { (void)fmt; }
/* Audio mock. Records the last gauge feed so the suite can assert that the
 * health value reaches the mixer with the right value, owner and max -- and that the
 * KO sentinel is rejected rather than swept to full. */
static int ga_sets, ga_resets, ga_last_player, ga_last_value, ga_last_max;
void audio_gauge_set(int g, unsigned player, int value, int maxv)
{ (void)g; ga_sets++; ga_last_player=(int)player; ga_last_value=value; ga_last_max=maxv; }
void audio_gauge_reset(int g, unsigned player) { (void)g; (void)player; ga_resets++; }
void audio_gauge_reset_all(void) { ga_resets++; }
BOOL mem_guard_install(void) { return FALSE; }
BOOL mem_copy(void *d,const void *s,SIZE_T n)
{ (void)d; (void)s; (void)n; return FALSE; }
static BYTE ryu[CVS2_STATE_SIZE], sagat[CVS2_STATE_SIZE], yun[CVS2_STATE_SIZE];
static BYTE inactive[CVS2_STATE_SIZE], altered[CVS2_STATE_SIZE];
static BYTE groove[CVS2_STATE_SIZE];
static void load(const char *name, BYTE *out)
{
    char path[256]; FILE *f;
    snprintf(path,sizeof path,"build/cvs2-state/%s.bin",name);
    f=fopen(path,"rb"); assert(f);
    assert(fread(out,1,CVS2_STATE_SIZE,f)==CVS2_STATE_SIZE);
    assert(fgetc(f)==EOF); fclose(f);
}
static void observe(const BYTE *s) { cvs2_select_observe(s,CVS2_STATE_SIZE,1); }
int main(void)
{
    load("ryu-return-08",ryu); load("sagat-07",sagat);
    load("moved-03",yun); load("return-ryu-06",inactive);
    assert(cvs2_select_decode(ryu,sizeof ryu,0)==0);
    assert(cvs2_select_decode(sagat,sizeof sagat,0)==9);
    assert(cvs2_select_decode(yun,sizeof yun,0)==44);
    assert(cvs2_select_decode(ryu,sizeof ryu,1)==-1);
    assert(cvs2_select_decode(inactive,sizeof inactive,0)==-1);
    assert(cvs2_select_decode(ryu,sizeof ryu-1,0)==-1);
    /* Instruction-derived fixtures: the dispatch table uses the same
     * highlight function for substates 1, 4 and 7, for either player. */
    for (unsigned p=0; p<2; p++) {
        for (unsigned step=0; step<13; step++) {
            memcpy(altered,ryu,sizeof altered);
            altered[0x17c]=altered[0x1ffec]=3;
            altered[0x1ffd4+p]=(BYTE)step;
            altered[0x1ffe4+p]=0;
            altered[0x1eba8+p*0x678+0x5ec]=(BYTE)p;
            altered[0x1eba9+p*0x678]=9;
            altered[0x1ffde + p*2]=2; altered[0x1ffdf+p*2]=4;
            assert(cvs2_select_decode(altered,sizeof altered,p)==
                   ((step==1 || step==4 || step==7) ? 9 : -1));
        }
    }
    observe(ryu); assert(calls==0); observe(ryu);
    assert(calls==1 && !wcscmp(last,L"Ryu"));
    observe(ryu); assert(calls==1);
    observe(sagat); observe(ryu); observe(ryu); assert(calls==1);
    observe(sagat); observe(sagat);
    assert(calls==2 && !wcscmp(last,L"Sagat"));
    observe(inactive); observe(sagat); observe(sagat); assert(calls==3);
    memcpy(altered,sagat,sizeof altered); altered[0x1eba9]=0;
    assert(cvs2_select_decode(altered,sizeof altered,0)==-2);
    observe(altered); observe(sagat); observe(sagat); assert(calls==3);
    altered[0x1fff0+4*16+2]=0x30;
    assert(cvs2_select_decode(altered,sizeof altered,0)==-2);
    observe(altered); assert(calls==3);
    speech_accepts=0; observe(yun); observe(yun); assert(calls==4);
    speech_accepts=1; observe(yun); assert(calls==5 && !wcscmp(last,L"Yun"));
    observe(yun); assert(calls==5);
    cvs2_select_observe(yun,sizeof yun,2); assert(calls==5);
    cvs2_select_observe(yun,sizeof yun,2); assert(calls==6);
    cvs2_select_tick(); /* failed guarded read clears selection */
    observe(ryu); observe(ryu); assert(calls==7);
    /* P2 alone, all three slots, including unchanged ID at the next slot. */
    memcpy(altered,ryu,sizeof altered);
    altered[0x17c]=altered[0x1ffec]=2;
    altered[0x1ffe5]=0; altered[0x1f221]=9;
    altered[0x1ffe0]=2; altered[0x1ffe1]=4;
    for (unsigned step=1; step<=7; step+=3) {
        int before=calls;
        altered[0x1ffd5]=(BYTE)step;
        observe(altered); assert(calls==before);
        observe(altered); assert(calls==before+1);
        assert(!wcscmp(last,L"Sagat"));
        observe(altered); assert(calls==before+1);
    }
    /* Both players change: deliver one utterance containing both names. */
    cvs2_select_observe(NULL,0,0);
    altered[0x17c]=altered[0x1ffec]=3;
    observe(altered); observe(altered);
    assert(!wcscmp(last,L"Ryu. Sagat"));
    {
        int before=calls;
        altered[0x1ffd4]=2; altered[0x1ffd5]=8;
        observe(altered); observe(altered); assert(calls==before);
        altered[0x1ffd4]=4; altered[0x1ffd5]=10;
        observe(altered); observe(altered); assert(calls==before+1);
        assert(!wcscmp(last,L"Ryu"));
        altered[0x1ffe4]=1;
        assert(cvs2_select_decode(altered,sizeof altered,0)==-1);
    }
    load("c-groove-09",groove);
    assert(cvs2_groove_decode(groove,sizeof groove,0)==0);
    assert(cvs2_groove_decode(groove,sizeof groove,1)==-1);
    assert(cvs2_groove_decode(ryu,sizeof ryu,0)==-1);
    cvs2_select_observe(NULL,0,0);
    observe(groove); observe(groove); assert(!wcscmp(last,L"C Groove"));
    {
        int before=calls;
        observe(groove); assert(calls==before);
        groove[0x1f178]=1;
        observe(groove); observe(groove);
        assert(calls==before+1 && !wcscmp(last,L"A Groove"));
        groove[0x1ffd4]=2;
        observe(groove); observe(groove); assert(calls==before+1);
    }
    for (unsigned p=0; p<2; p++) {
        for (unsigned g=0; g<8; g++) {
            memcpy(altered,groove,sizeof altered);
            altered[0x17c]=altered[0x1ffec]=3;
            altered[0x1ffd4+p]=1;
            altered[0x1eba8+p*0x678+0x5ec]=(BYTE)p;
            altered[0x1f178+p*0x678]=(BYTE)g;
            assert(cvs2_groove_decode(altered,sizeof altered,p)==(g<6 ? (int)g : -2));
        }
    }
    load("ratio-12",altered);
    assert(cvs2_ratio_decode(altered,sizeof altered,0)==3);
    assert(cvs2_ratio_decode(altered,sizeof altered,1)==-1);
    cvs2_select_observe(NULL,0,0);
    observe(altered); observe(altered);
    assert(!wcscmp(last,L"Ratios: Ryu 2, Ken 1, Yun 1"));
    altered[0x1ffe6]=2;
    observe(altered); observe(altered);
    assert(!wcscmp(last,L"Ratios: Ryu 1, Ken 1, Yun 2"));
    for (unsigned p=0; p<2; p++) {
        unsigned b=0x1eba8+p*0x678;
        altered[0x17c]=altered[0x1ffec]=3;
        altered[0x1ffd4+p]=2; altered[b+0x5ec]=(BYTE)p;
        altered[b+0x5c0]=0; altered[b+0x5c1]=1; altered[b+0x5c2]=44;
        for (unsigned n=2; n<=3; n++) for (unsigned c=0; c<3; c++) {
            altered[b+0x5d1]=(BYTE)n; altered[0x1ffe6+p]=(BYTE)c;
            assert(cvs2_ratio_decode(altered,sizeof altered,p)==(int)((n-2)*3+c));
        }
        altered[0x1ffd4+p]=3;
        assert(cvs2_ratio_decode(altered,sizeof altered,p)==-1);
    }
    /* Stage has code-derived fixtures only; no live stage capture yet. */
    memcpy(altered,ryu,sizeof altered);
    altered[0x1a0]=1; altered[0x1ffd0]=2; altered[0x1ffef]=1;
    altered[0x1ffda]=1; altered[0x1ffec]=3;
    altered[0x1ffd4]=altered[0x1ffd5]=0;
    altered[0x1ebac]=altered[0x1f224]=3;
    altered[0x1ffe4]=altered[0x1ffe5]=0;
    for (unsigned stage=0; stage<12; stage++) {
        altered[0x64c]=(BYTE)stage;
        assert(cvs2_stage_decode(altered,sizeof altered)==(int)stage);
    }
    cvs2_select_observe(NULL,0,0);
    {
        int before=calls;
        observe(altered); observe(altered);
        assert(calls==before+1 && !wcscmp(last,L"Stage: Random"));
        observe(altered); assert(calls==before+1);
        altered[0x1ffec]=2; altered[0x64c]=0;
        observe(altered); observe(altered);
        assert(calls==before+2 && !wcscmp(last,L"Stage: Aomori"));
    }
    altered[0x1f224]=4; assert(cvs2_stage_decode(altered,sizeof altered)==-1);
    altered[0x1f224]=3; altered[0x1ffef]=0;
    assert(cvs2_stage_decode(altered,sizeof altered)==-1);
    altered[0x1ffef]=3; assert(cvs2_stage_decode(altered,sizeof altered)==0);
    altered[0x64c]=12; assert(cvs2_stage_decode(altered,sizeof altered)==-2);
    /* Alternate dispatch: the previous build rejected these solely because
     * P+0x128 was nonzero. Exercise both player structures and transitions. */
    for (unsigned p=0; p<2; p++) {
        unsigned b=0x1eba8+p*0x678;
        memcpy(altered,ryu,sizeof altered);
        altered[0x1a0]=1; altered[0x1ffd0]=2; altered[0x1ffef]=3;
        altered[0x1ffec]=(BYTE)(1u<<p); altered[0x1ffda]=10;
        altered[b+4]=1; altered[b+0x5ec]=(BYTE)p; altered[0x1ffe4+p]=0;
        altered[0x1ffd4+p]=1; altered[b+0x5d0]=4;
        assert(cvs2_groove_decode(altered,sizeof altered,p)==4);
        assert(cvs2_select_decode(altered,sizeof altered,p)==-1);
        cvs2_select_observe(NULL,0,0); observe(altered); observe(altered);
        assert(!wcscmp(last,L"N Groove"));
        altered[b+4]=2;
        assert(cvs2_groove_decode(altered,sizeof altered,p)==-1);
        observe(altered);
        altered[b+4]=1; altered[0x1ffd4+p]=2;
        altered[0x1ffd6+p]=44; altered[b+1]=44;
        assert(cvs2_select_decode(altered,sizeof altered,p)==44);
        assert(cvs2_groove_decode(altered,sizeof altered,p)==-1);
        observe(altered); observe(altered); assert(!wcscmp(last,L"Yun"));
        {
            int before=calls;
            altered[b+0x5d1]++;
            observe(altered); observe(altered); assert(calls==before+1);
        }
        altered[0x1ffd6+p]=49;
        assert(cvs2_select_decode(altered,sizeof altered,p)==-2);
        altered[0x1ffd6+p]=9;
        assert(cvs2_select_decode(altered,sizeof altered,p)==-2);
        altered[0x1ffe4+p]=1;
        assert(cvs2_select_decode(altered,sizeof altered,p)==-1);
    }
    /* Code-derived alternate ratio fixtures. These prove decoder/observer
     * behavior, not that the user's handicap screen is this handler. */
    for (unsigned p=0; p<2; p++) {
        unsigned b=0x1eba8+p*0x678;
        memcpy(altered,ryu,sizeof altered);
        altered[0x1a0]=1; altered[0x1ffd0]=2; altered[0x1ffef]=3;
        altered[0x1ffec]=(BYTE)(1u<<p); altered[0x1ffd4+p]=3;
        altered[b+4]=1; altered[b+0x5ec]=(BYTE)p; altered[0x1ffe4+p]=0;
        altered[0x1ffda]=altered[0x1ffdb]=0;
        cvs2_select_observe(NULL,0,0);
        for (unsigned c=0; c<4; c++) {
            wchar_t expected[32];
            int before=calls;
            altered[0x1ffe6+p]=(BYTE)c;
            assert(cvs2_ratio_decode(altered,sizeof altered,p)==6+(int)c);
            swprintf(expected,32,L"Level %u",c+1);
            observe(altered); assert(calls==before);
            observe(altered); assert(calls==before+1 && !wcscmp(last,expected));
            observe(altered); assert(calls==before+1);
        }
        altered[0x1ffe6+p]=4;
        assert(cvs2_ratio_decode(altered,sizeof altered,p)==-2);
        altered[0x1ffe6+p]=1; altered[b+4]=2;
        assert(cvs2_ratio_decode(altered,sizeof altered,p)==-1);
        altered[b+4]=1; altered[0x1ffe4+p]=1;
        assert(cvs2_ratio_decode(altered,sizeof altered,p)==-1);
        altered[0x1ffe4+p]=0; altered[0x1ffef]=2;
        assert(cvs2_ratio_decode(altered,sizeof altered,p)==-1);
        altered[0x1ffef]=1; altered[0x1a3]=0;
        assert(cvs2_ratio_decode(altered,sizeof altered,p)==-1); /* expired */
        altered[0x1ffda]=10;
        altered[b+0x5d1]=3;
        altered[b+0x5c0]=0; altered[b+0x5c1]=1; altered[b+0x5c2]=44;
        assert(cvs2_ratio_decode(altered,sizeof altered,p)==4);
        cvs2_select_observe(NULL,0,0); observe(altered); observe(altered);
        assert(!wcscmp(last,L"Ratios: Ryu 1, Ken 2, Yun 1"));
        altered[0x1a3]=1;
        assert(cvs2_ratio_decode(altered,sizeof altered,p)==-1);
        altered[0x1a3]=0; altered[b+0x5d1]=1;
        assert(cvs2_ratio_decode(altered,sizeof altered,p)==-1);
    }
    /* The user parked on the missing screen. This unmodified capture must
     * decode as entry 2, regardless of the still-unverified visible heading. */
    load("handicap-confirmed-02",altered);
    assert(cvs2_ratio_decode(altered,sizeof altered,0)==7);
    assert(cvs2_ratio_decode(altered,sizeof altered,1)==-1);
    cvs2_select_observe(NULL,0,0); observe(altered); observe(altered);
    assert(!wcscmp(last,L"Level 2"));
    load("handicap-right-03",altered);
    assert(cvs2_ratio_decode(altered,sizeof altered,0)==8);
    {
        int before=calls;
        observe(altered); assert(calls==before);
        observe(altered); assert(calls==before+1 && !wcscmp(last,L"Level 3"));
        observe(altered); assert(calls==before+1);
    }
    /* ---- gauges: health must reach the mixer, the sentinel must not ----- */
    {
        BYTE g[CVS2_STATE_SIZE];
        unsigned b0 = 0x1eba8, b1 = 0x1eba8 + 0x678;
        int before;
        memcpy(g, ryu, sizeof g);

        /* P1 health 4260 of max 11520; P2 8216 of 11200 -- real captured
         * values. Max health is B+0x170. */
        g[b0+0x16c]=(BYTE)(4260&0xff);  g[b0+0x16d]=(BYTE)(4260>>8);
        g[b0+0x170]=(BYTE)(11520&0xff); g[b0+0x171]=(BYTE)(11520>>8);
        g[b1+0x16c]=(BYTE)(8216&0xff);  g[b1+0x16d]=(BYTE)(8216>>8);
        g[b1+0x170]=(BYTE)(11200&0xff); g[b1+0x171]=(BYTE)(11200>>8);

        ga_sets=0;
        cvs2_gauges_observe(g,sizeof g,0x15ba1000ULL);
        assert(ga_sets==2);                      /* both players fed */
        assert(ga_last_player==1);
        assert(ga_last_value==8216 && ga_last_max==11200);

        /* 0xFFFF is the KO/round-end sentinel: reject, never announce. */
        g[b0+0x16c]=0xff; g[b0+0x16d]=0xff;
        ga_sets=0; ga_resets=0;
        cvs2_gauges_observe(g,sizeof g,0x15ba1000ULL);
        assert(ga_sets==1);                      /* only P2 fed */
        assert(ga_resets==1);                    /* P1 reset instead */

        /* A relocated state block must reset, not read as a huge change. */
        before=ga_resets;
        cvs2_gauges_observe(g,sizeof g,0x16000000ULL);
        assert(ga_resets>before);

        /* Leaving CVS2 resets and feeds nothing. */
        ga_sets=0;
        cvs2_gauges_observe(NULL,0,0);
        assert(ga_sets==0);

        /* ⛔⛔ THE REGRESSION THAT MATTERS: an UNMODIFIED real capture must be
         * ACCEPTED. The first version read the cap from B+0x5BC, which is 0 in
         * every capture, so every value was rejected and the mod was silent
         * in-game. Synthetic fixtures all passed because they wrote the cap to
         * whatever offset the code read. Feeding an untouched capture is the
         * only version of this test that could fail. */
        {
            static BYTE match[CVS2_STATE_SIZE];
            load("hp-hurt", match);      /* a real IN-MATCH capture */
            ga_sets=0; ga_resets=0;
            cvs2_gauges_observe(match,sizeof match,0x15ba1000ULL);
            assert(ga_sets==2);          /* both players accepted as-is */
            assert(ga_last_max == 11200);    /* P2 max health, measured live */
            assert(ga_last_value == 8216);   /* P2 health, measured live */
        }
    }

    puts("PASS: Arcade and alternate character/groove, team/P2, ratio/stage, transitions, speech without prefixes, gauge feed + KO sentinel");
    return 0;
}
