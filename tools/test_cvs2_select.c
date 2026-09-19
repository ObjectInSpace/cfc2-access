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
/* Per-gauge, per-player record so the suite can assert on the METER
 * independently of health. 0 = SUPER, 1 = HEALTH (audio.h order).
 * ⚠ CVS2 has no stun gauge -- see the note in audio.h. */
static int ga_val[2][2], ga_max[2][2], ga_seen[2][2];
void audio_gauge_set(int g, unsigned player, int value, int maxv)
{ ga_sets++; ga_last_player=(int)player; ga_last_value=value; ga_last_max=maxv;
  if (g>=0 && g<2 && player<2) { ga_val[g][player]=value; ga_max[g][player]=maxv;
                                 ga_seen[g][player]=1; } }
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

        /* ⛔ HEALTH IS NO LONGER FED FROM THE POLL. cvs2_frame.c feeds it on
         * the game's own frame; feeding it here too would overwrite that with
         * a stale value and re-introduce the lag the hook removes. The poll
         * still READS health for its counters and the KO-sentinel guard, so
         * assert that it does not reach the mixer. */
        memset(ga_seen,0,sizeof ga_seen);
        cvs2_gauges_observe(g,sizeof g,0x15ba1000ULL);
        assert(!ga_seen[1][0] && !ga_seen[1][1]);    /* health not fed here */
        assert(ga_seen[0][0] && ga_seen[0][1]);      /* meter still is      */

        /* 0xFFFF is the KO/round-end sentinel: reject, never announce. */
        g[b0+0x16c]=0xff; g[b0+0x16d]=0xff;
        memset(ga_seen,0,sizeof ga_seen); ga_resets=0;
        cvs2_gauges_observe(g,sizeof g,0x15ba1000ULL);
        assert(ga_resets>0);       /* the sentinel still forces a reset */

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
        /* ---- super meter: B+0x226 scaled by the PER-GROOVE cap --------
         * ⭐ These four captures are the ones that found the field: three
         * were taken at FULL meter and one genuinely empty, and the cap table
         * must reproduce that description. A wrong cap (e.g. hardcoding 0x48,
         * which is K-Groove's) fails these outright. */
        {
            static BYTE mf[CVS2_STATE_SIZE], me[CVS2_STATE_SIZE];
            load("meter-full",   mf);    /* both players FULL  */
            load("meter-empty2", me);    /* P1 empty, P2 full  */

            memset(ga_seen,0,sizeof ga_seen);
            cvs2_gauges_observe(mf,sizeof mf,0x15541000ULL);
            assert(ga_seen[0][0] && ga_seen[0][1]);
            assert(ga_val[0][0]==168 && ga_max[0][0]==168);   /* P1 full  */
            assert(ga_val[0][1]==168 && ga_max[0][1]==168);   /* P2 full  */

            memset(ga_seen,0,sizeof ga_seen);
            cvs2_gauges_observe(me,sizeof me,0x15541000ULL);
            assert(ga_val[0][0]==4   && ga_max[0][0]==168);   /* P1 spent */
            assert(ga_val[0][1]==168 && ga_max[0][1]==168);   /* P2 held  */

            /* Each groove has its OWN ceiling; a single constant is wrong.
             * Verify the table is actually consulted by forcing the index. */
            {
                static BYTE g2[CVS2_STATE_SIZE];
                static const unsigned caps[6]={168,144,192,100,216,72};
                unsigned gi;
                memcpy(g2,mf,sizeof g2);
                for (gi=0; gi<6; gi++) {
                    g2[0x1eba8+0x22d]=(BYTE)gi;
                    g2[0x1eba8+0x226]=1; g2[0x1eba8+0x227]=0;
                    memset(ga_seen,0,sizeof ga_seen);
                    cvs2_gauges_observe(g2,sizeof g2,0x15541000ULL);
                    assert(ga_max[0][0]==(int)caps[gi]);
                }
                /* An unknown groove has no known ceiling: stay silent. */
                g2[0x1eba8+0x22d]=9;
                memset(ga_seen,0,sizeof ga_seen);
                cvs2_gauges_observe(g2,sizeof g2,0x15541000ULL);
                assert(!ga_seen[0][0]);
            }
        }

        {
            static BYTE match[CVS2_STATE_SIZE];
            load("hp-hurt", match);      /* a real IN-MATCH capture */
            memset(ga_seen,0,sizeof ga_seen); ga_resets=0;
            cvs2_gauges_observe(match,sizeof match,0x15ba1000ULL);
            /* An unmodified real capture must still be ACCEPTED -- the guard
             * that caught the B+0x5BC cap bug. Health goes to the mixer via
             * the frame hook now, so assert the meter path here. */
            assert(ga_seen[0][0] && ga_seen[0][1]);
        }

        /* ---- 7e. the WIN-QUOTE state must feed nothing --------------------
         * ⭐ REAL CAPTURE, taken by the user while a win quote was on screen:
         * phase 02 01 02 **03**, a sibling of the in-match 02 01 02 02.
         *
         * ⚠ No defect was ever heard here -- the user said so explicitly. This
         * case pins a boundary they designated safe for teardown/rebuild, not
         * a reproduction of a reported bug.
         *
         * The requirement: quotes are shown BETWEEN
         * matches, so the audio engine must be idle there.
         *
         * ⚠ WHY THIS CASE EXISTS RATHER THAN TRUSTING THE VALUES. In this
         * capture B+0x16C/B+0x170 are 0/0, so the health feed was rejected by
         * the `mx > 0` gate -- by ACCIDENT, not by design. The round timer had
         * no such luck: R+0x184 reads a live-looking 74, and the frame hook
         * fires a tick on any decrease landing on 30/15/1..5. The fix guards
         * on the phase bytes; this pins it.
         *
         * Sabotage check: delete the phase guard in frame_detour() and the
         * timer can tick over a win quote -- which no value assertion catches. */
        {
            static BYTE wq[CVS2_STATE_SIZE];
            load("winquote-01", wq);

            /* The capture really is the post-match state, not an in-match one. */
            assert(memcmp(wq + 0x78, "\x02\x01\x02\x03", 4) == 0);
            /* ...and it is NOT the phase the gauges/announcements accept. */
            assert(memcmp(wq + 0x78, "\x02\x01\x02\x02", 4) != 0);

            memset(ga_seen,0,sizeof ga_seen); ga_resets=0;
            cvs2_gauges_observe(wq,sizeof wq,0x145f1000ULL);
            assert(!ga_seen[0][0] && !ga_seen[0][1]);
            assert(!ga_seen[1][0] && !ga_seen[1][1]);

            /* No match/round speech over a quote either. */
            {
                int b = calls;
                cvs2_announce_observe(wq,sizeof wq,0x145f1000ULL);
                assert(calls == b);
            }

            /* ⭐ The character ID DOES survive here (P1 Sagat=9, P2 Todoh=40),
             * which is what an ending/quote reader would key on. Pinned so a
             * later change cannot quietly break the field this depends on. */
            assert(wq[0x1eba8 + 1] == 9);
            assert(wq[0x1eba8 + 0x678 + 1] == 40);
        }
    }

    /* ---- in-match announcements: team/groove, then per-round fighter ----
     * ⭐ Replays REAL in-match captures. hp-full and hp-ken are the same match
     * with P1 having switched Ryu -> Ken, which is exactly the round-change
     * case, and their teams/ratios were read off the live captures. */
    {
        static BYTE m1[CVS2_STATE_SIZE], m2[CVS2_STATE_SIZE];
        int before;
        load("hp-full", m1);          /* P1 Ryu, team 0/1/9, C groove */
        load("hp-ken",  m2);          /* same match, P1 now Ken       */

        cvs2_announce_observe(NULL,0,0);
        before=calls;
        cvs2_announce_observe(m1,sizeof m1,0x15ba1000ULL);
        assert(calls==before+1);                       /* match announced */
        assert(wcsstr(last,L"You")!=NULL);
        assert(wcsstr(last,L"C Groove")!=NULL);
        assert(wcsstr(last,L"Ryu")!=NULL);
        assert(wcsstr(last,L"Ken")!=NULL);             /* team ORDER */
        assert(wcsstr(last,L"Sagat")!=NULL);
        assert(wcsstr(last,L"Opponent")!=NULL);
        assert(wcsstr(last,L"K Groove")!=NULL);        /* P2 groove */

        /* Repeating the same state must not repeat the announcement. */
        cvs2_announce_observe(m1,sizeof m1,0x15ba1000ULL);
        assert(calls==before+1);

        /* A new round with a different fighter speaks just that fighter. */
        before=calls;
        cvs2_announce_observe(m2,sizeof m2,0x15ba1000ULL);
        assert(calls==before+1);
        assert(wcsstr(last,L"Ken")!=NULL);
        assert(wcsstr(last,L"Groove")==NULL);          /* not the whole team */
        cvs2_announce_observe(m2,sizeof m2,0x15ba1000ULL);
        assert(calls==before+1);                       /* no repeat */

        /* A relocated block is a new session: announce the match again. */
        before=calls;
        cvs2_announce_observe(m2,sizeof m2,0x16000000ULL);
        assert(calls==before+1);
        assert(wcsstr(last,L"Groove")!=NULL);

        /* ⭐⭐ RATIOS ARE GATED ON THE 4-POINT BUDGET, NOT TEAM SIZE.
         * The user corrected this twice: plain 3-on-3 team battle has NO
         * ratios, but Ratio Match with a 3-member team DOES (hp-full stores
         * 1/1/2). Size alone cannot tell them apart; the budget can. */
        {
            static BYTE t3[CVS2_STATE_SIZE];
            unsigned b0=0x1eba8;
            int before2;

            /* Real Ratio Match 3-member team: 1+1+2 = 4 -> ratios spoken. */
            memcpy(t3,m1,sizeof t3);
            cvs2_announce_observe(NULL,0,0);
            cvs2_announce_observe(t3,sizeof t3,0x21000000ULL);
            assert(wcsstr(last,L"ratio")!=NULL);

            /* Plain 3-on-3: every fighter equal, so the field is not a real
             * allocation (1+1+1 = 3, not 4) -> say NOTHING about ratios. */
            t3[b0+0x5cc]=0; t3[b0+0x5cd]=0; t3[b0+0x5ce]=0;
            t3[0x1f220+0x5cc]=0; t3[0x1f220+0x5cd]=0; t3[0x1f220+0x5ce]=0;
            cvs2_announce_observe(NULL,0,0);
            before2=calls;
            cvs2_announce_observe(t3,sizeof t3,0x22000000ULL);
            assert(calls==before2+1);
            assert(wcsstr(last,L"ratio")==NULL);       /* no ratio claimed */
            assert(wcsstr(last,L"Ryu")!=NULL);         /* team still named */
            assert(wcsstr(last,L"C Groove")!=NULL);    /* groove still named */
        }

        /* Selection-screen phases must NOT trigger match speech. */
        before=calls;
        cvs2_announce_observe(ryu,sizeof ryu,0x17000000ULL);
        assert(calls==before);
    }

    puts("PASS: Arcade and alternate character/groove, team/P2, ratio/stage, transitions, speech without prefixes, gauge feed + KO sentinel, super meter per-groove caps, match/round speech");
    return 0;
}
