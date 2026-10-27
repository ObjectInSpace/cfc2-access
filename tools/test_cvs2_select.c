/* Replays real captures through the shipping decoder and speech decisions.
 * No game writes or audible output. Run from the repository root. */
#include "../src/cvs2_select.h"
#include "../src/cvs2_frame.h"
#include "../src/match_audio.h"
#include "../src/audio.h"
#include "../libs/minhook/include/MinHook.h"
#include "../src/text_channel.h"
/* The generated quote table, so cases 7f/7g can assert that the string spoken
 * is exactly the entry the capture's own index selects. */
#include "../src/quote_table.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static int calls, speech_accepts = 1;
static wchar_t last[600];
BOOL speech_say(const wchar_t *s, BOOL interrupt)
{ (void)interrupt; calls++; wcscpy(last,s); return speech_accepts; }
void log_line(const char *fmt, ...) { (void)fmt; }
void cvs2_story_observe(const BYTE *s, SIZE_T n, ULONGLONG root)
{ (void)s; (void)n; (void)root; } /* Exercised by test_cvs2_story.c. */
void cvs2_text_observe(ULONGLONG root) { (void)root; }
/* Audio mock. Records the last gauge feed so the suite can assert that the
 * health value reaches the mixer with the right value, owner and max -- and that the
 * KO sentinel is rejected rather than swept to full. */
static int ga_sets, ga_resets, ga_last_player, ga_last_value, ga_last_max;
/* Per-gauge, per-player record so the suite can assert on the METER
 * independently of health. 0 = SUPER, 1 = HEALTH (audio.h order).
 * ⚠ CVS2 has no stun gauge -- see the note in audio.h. */
static int ga_val[2][2], ga_max[2][2], ga_seen[2][2];
void audio_gauge_set(audio_gauge g, unsigned player, int value, int maxv)
{ ga_sets++; ga_last_player=(int)player; ga_last_value=value; ga_last_max=maxv;
  if (g>=0 && g<2 && player<2) { ga_val[g][player]=value; ga_max[g][player]=maxv;
                                 ga_seen[g][player]=1; } }
void audio_gauge_reset(audio_gauge g, unsigned player) { (void)g; (void)player; ga_resets++; }
void audio_gauge_reset_all(void) { ga_resets++; }
/* The shipping path goes through the REAL match_audio.c into these owned
 * setters; they forward to the records above. Owner stop counts as a reset:
 * it is how a departing owner silences every gauge. */
static int owner_begins, owner_stops;
BOOL audio_owner_begin(uint64_t e) { (void)e; owner_begins++; return TRUE; }
void audio_owner_stop(uint64_t e) { (void)e; owner_stops++; ga_resets++; }
void audio_gauge_set_owned(uint64_t e, audio_gauge g, unsigned player, int value, int maxv)
{ (void)e; audio_gauge_set(g,player,value,maxv); }
/* CVS2 never sets fighter_gauges; a call here would be a routing bug. */
void audio_gauge_set_fighter_owned(uint64_t e, audio_gauge g, unsigned player, int value, int maxv, float pan)
{ (void)e; (void)g; (void)player; (void)value; (void)maxv; (void)pan; abort(); }
void audio_gauge_reset_owned(uint64_t e, audio_gauge g, unsigned player)
{ (void)e; audio_gauge_reset(g,player); }
void audio_beacon_set_owned(uint64_t e, unsigned player, BOOL live, float pan, float stance)
{ (void)e; (void)player; (void)live; (void)pan; (void)stance; }
void audio_tick_owned(uint64_t e) { (void)e; }
/* cvs2_frame.c is linked for cvs2_frame_decode(); its installer is never run. */
MH_STATUS WINAPI MH_CreateHook(LPVOID t, LPVOID d, LPVOID *o)
{ (void)t; (void)d; (void)o; return MH_UNKNOWN; }
MH_STATUS WINAPI MH_EnableHook(LPVOID t) { (void)t; return MH_UNKNOWN; }
MH_STATUS WINAPI MH_DisableHook(LPVOID t) { (void)t; return MH_UNKNOWN; }
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
static void observe(const BYTE *s) { cvs2_select_observe(s,CVS2_STATE_SIZE,1,3); }
static bool shared_emit(void *context, const wchar_t *text)
{ (void)context; return tc_say(text, TRUE, "shared-selection-replay"); }
static unsigned shared_sample(game_session *session, game_selection *presenter,
                              const BYTE *state)
{
    game_selection_observation o = {0};
    assert(game_session_capture(session, &o.token));
    o.captured_ms = 1000;
    o.complete = true;
    cvs2_selection_copy(state, CVS2_STATE_SIZE, o.owner);
    assert(game_session_publish(session, &o.token));
    assert(game_selection_offer(presenter, session, &o, 1000));
    return game_selection_drain(presenter, session, 1000, shared_emit, NULL);
}
static void shared_replay(void)
{
    /* Real copied state fixtures, synthetic session labels and timestamps.
     * This uses the existing tc_say -> fake speech backend, not a second queue. */
    game_session session = {0};
    game_selection presenter = {0};
    game_selection_choice choices[GAME_SELECTION_OWNERS];
    assert(game_session_begin(&session, 1, 3));
    assert(game_session_resume(&session, session.issued.generation, session.issued.revision));
    cvs2_selection_copy(ryu, sizeof ryu, choices);
    assert(choices[0].status == GAME_SELECTION_VALID && choices[0].value == 0);
    assert(!wcscmp(choices[0].label, L"Ryu"));
    assert(choices[2].status == GAME_SELECTION_INACTIVE);
    assert(choices[3].status == GAME_SELECTION_INACTIVE);
    int before = calls;
    speech_accepts = 1;
    assert(!shared_sample(&session, &presenter, ryu));
    assert(shared_sample(&session, &presenter, ryu) == 1);
    assert(calls == before + 1 && !wcscmp(last, L"Ryu"));
    assert(!shared_sample(&session, &presenter, ryu));
    assert(!shared_sample(&session, &presenter, sagat));
    speech_accepts = 0;
    assert(!shared_sample(&session, &presenter, sagat));
    speech_accepts = 1;
    assert(shared_sample(&session, &presenter, sagat) == 1);
    assert(!wcscmp(last, L"Sagat"));
    game_session_suspend(&session);
    assert(game_session_resume(&session, session.issued.generation, session.issued.revision));
    before = calls;
    assert(!shared_sample(&session, &presenter, sagat));
    assert(!shared_sample(&session, &presenter, sagat));
    assert(calls == before); /* Restore alone does not rearm the accepted name. */
    assert(!shared_sample(&session, &presenter, inactive));
    assert(!shared_sample(&session, &presenter, sagat));
    assert(shared_sample(&session, &presenter, sagat) == 1);
    assert(!shared_sample(&session, &presenter, yun));
    assert(shared_sample(&session, &presenter, yun) == 1);
    assert(!wcscmp(last, L"Yun"));
    cvs2_selection_copy(NULL, 0, choices);
    assert(choices[0].status == GAME_SELECTION_INACTIVE);
    cvs2_selection_copy(ryu, sizeof ryu - 1, choices);
    assert(choices[0].status == GAME_SELECTION_INACTIVE);
    puts("PASS: copied CVS2 selections through shared session/presenter and tc_say");
}
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
    cvs2_select_observe(yun,sizeof yun,2,3); assert(calls==5);
    cvs2_select_observe(yun,sizeof yun,2,3); assert(calls==6);
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
    cvs2_select_observe(NULL,0,0,3);
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
    cvs2_select_observe(NULL,0,0,3);
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
    cvs2_select_observe(NULL,0,0,3);
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
    cvs2_select_observe(NULL,0,0,3);
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
        cvs2_select_observe(NULL,0,0,3); observe(altered); observe(altered);
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
        cvs2_select_observe(NULL,0,0,3);
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
        cvs2_select_observe(NULL,0,0,3); observe(altered); observe(altered);
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
    cvs2_select_observe(NULL,0,0,3); observe(altered); observe(altered);
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
        static BYTE g[CVS2_STATE_SIZE];
        unsigned b0 = 0x1eba8, b1 = 0x1eba8 + 0x678;
        int before;
        /* The gauges belong to the MATCH, so the base is a real in-match
         * capture. (This block used to borrow the selection-screen capture,
         * which the meter path now refuses by phase -- see 7e.) */
        load("hp-hurt", g);
        assert(match_audio_begin(1) != 0);

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

        /* An unchanged sample touches nothing: the change rule is what keeps
         * the mixer lock off the hot path. */
        ga_sets=0;
        cvs2_gauges_observe(g,sizeof g,0x15ba1000ULL);
        assert(ga_sets==0);

        /* 0xFFFF is the KO/round-end sentinel: the shipping FRAME decode must
         * mark it invalid (the controller then resets, never sweeps to it). */
        {
            match_audio_sample m;
            cvs2_frame_decode(g,&m);
            assert(m.in_match && m.owner[0].health.valid);
            assert(m.owner[0].health.value==4260 && m.owner[0].health.maximum==11520);
            assert(m.owner[1].health.valid && m.owner[1].health.value==8216);
            g[b0+0x16c]=0xff; g[b0+0x16d]=0xff;
            cvs2_frame_decode(g,&m);
            assert(!m.owner[0].health.valid);
            assert(m.owner[1].health.valid);           /* per player */
        }

        /* A relocated block / new owner is a new epoch: the old owner is
         * stopped (every gauge silenced) and the meter reseeds, rather than
         * reading as a huge change. */
        before=ga_resets;
        memset(ga_seen,0,sizeof ga_seen);
        assert(match_audio_begin(1) != 0);
        assert(ga_resets>before);
        cvs2_gauges_observe(g,sizeof g,0x16000000ULL);
        assert(ga_seen[0][0] && ga_seen[0][1]);

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
            /* The meter was live just before: the observed exit silences it
             * once, rather than holding a tone over the quote. */
            assert(ga_resets>0);
            /* ⭐ And the FRAME decode, which feeds the clock, reports no match
             * -- so R+0x184's live-looking 74 here can never tick. */
            {
                match_audio_sample m;
                cvs2_frame_decode(wq,&m);
                assert(!m.in_match && !m.clock_valid);
            }

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

        /* ---- 7f. Partial and pre-display captures must remain silent. ---- */
        {
            static BYTE wq[CVS2_STATE_SIZE];
            int b;

            load("winquote-01", wq);
            cvs2_quote_observe(NULL,0,0);
            b = calls;
            cvs2_quote_observe(wq,sizeof wq,0x145f1000ULL);
            assert(calls == b);

            load("winquote-ken-02", wq);
            cvs2_quote_observe(NULL,0,0);
            b = calls;
            cvs2_quote_observe(wq,sizeof wq,0x16571000ULL);
            assert(calls == b);

            /* ⭐⭐ THE CAPTURE THAT KILLED IT, kept as the permanent bar any
             * future candidate must clear. User: P1 Sagat, HAOHMARU (P2) won.
             *
             *   R+0x05931 reads 0      -- not Haohmaru (43), not anything
             *   B+0x5D3   reads 1 on P1 -- i.e. on the LOSER
             *
             * Both candidates were coincidences of a two-sample set. A reader
             * re-enabled without clearing this case is repeating the mistake. */
            load("winquote-raiden-03", wq);
            assert(memcmp(wq + 0x78, "\x02\x01\x02\x03", 4) == 0);  /* a quote screen */
            assert(wq[0x7c] == 1);          /* ...but a THIRD phase sub-shape */
            assert(wq[0x05931] == 0);       /* the shipped field: empty */
            assert(wq[0x1eba8 + 1] == 9);   /* P1 Sagat (lost) */
            assert(wq[0x1eba8 + 0x678 + 1] == 43);          /* P2 Haohmaru (won) */
            assert(wq[0x1eba8 + 0x5d3] == 1);               /* 5D3 marks the LOSER */
            assert(wq[0x1eba8 + 0x678 + 0x5d3] == 0);
            cvs2_quote_observe(NULL,0,0);
            b = calls;
            cvs2_quote_observe(wq,sizeof wq,0x164a1000ULL);
            assert(calls == b);             /* silence, not a wrong winner */
        }

        /* Completed draw buffer captured live on 2026-09-26. */
        {
            static BYTE wq[CVS2_STATE_SIZE];
            wchar_t text[161];
            int b;
            FILE *f=fopen("tools/fixtures/cvs2-quote-complete.bin","rb");
            assert(f);
            load("winquote-01",wq);
            assert(fread(wq+0x20f30,1,0xd0,f)==0xd0); fclose(f);
            assert(cvs2_quote_decode(wq,sizeof wq,text,161));
            assert(!wcscmp(text,L"You should not have challenged me!"));
            cvs2_quote_observe(NULL,0,0); b=calls;
            cvs2_quote_observe(wq,sizeof wq,1); assert(calls==b);
            speech_accepts=0;
            cvs2_quote_observe(wq,sizeof wq,1); assert(calls==b+1);
            speech_accepts=1;
            cvs2_quote_observe(wq,sizeof wq,1); assert(calls==b+2);
            assert(!wcscmp(last,text));
            cvs2_quote_observe(wq,sizeof wq,1); assert(calls==b+2);
            cvs2_quote_observe(wq,sizeof wq,2);
            cvs2_quote_observe(wq,sizeof wq,2); assert(calls==b+3);
            wq[0x7c]=1; cvs2_quote_observe(wq,sizeof wq,2);
            wq[0x7c]=2;
            cvs2_quote_observe(wq,sizeof wq,2);
            cvs2_quote_observe(wq,sizeof wq,2); assert(calls==b+4);
            /* Completed text must not leak into match/selection phases. */
            wq[0x7b]=2;
            assert(!cvs2_quote_decode(wq,sizeof wq,text,161));
            wq[0x7b]=3;
            wq[0x20f58]--; assert(!cvs2_quote_decode(wq,sizeof wq,text,161));
            wq[0x20f58]++;
            wq[0x20f5c+34]='X'; assert(!cvs2_quote_decode(wq,sizeof wq,text,161));
            wq[0x20f5c+34]=0;
            /* A dangling Shift-JIS lead byte refuses the quote. */
            wq[0x20f5c+33]=0x81; assert(!cvs2_quote_decode(wq,sizeof wq,text,161));
            wq[0x20f5c+33]='!';
            /* The Japanese ROM's quotes are CP932 (its table holds
             * "負けを認めるのか？" as these bytes): spoken as Japanese. */
            {
                static const BYTE jp[] = { 0x95,0x89,0x82,0xaf,0x82,0xf0,0x94,0x46,0x82,0xdf,
                                           0x82,0xe9,0x82,0xcc,0x82,0xa9,0x81,0x48 };
                BYTE keep[0xd0];
                memcpy(keep, wq+0x20f30, sizeof keep);
                memset(wq+0x20f5c, 0, 0x50);
                memcpy(wq+0x20f5c, jp, sizeof jp);
                wq[0x20f54]=wq[0x20f58]=(BYTE)sizeof jp;
                wq[0x20f55]=wq[0x20f59]=0;
                assert(cvs2_quote_decode(wq,sizeof wq,text,161));
                assert(wcsncmp(text, L"\x8ca0\x3051\x3092\x8a8d\x3081\x308b\x306e\x304b\xff1f", 9) == 0);
                memcpy(wq+0x20f30, keep, sizeof keep);
                assert(cvs2_quote_decode(wq,sizeof wq,text,161));
            }
            assert(!cvs2_quote_decode(wq,sizeof wq-1,text,161));
            assert(!cvs2_quote_decode(wq,sizeof wq,text,160));
            /* Synthetic completion of a REAL partial Sagat capture. The
             * first line and lengths are captured, only the animation's
             * unfinished second line and completion state are filled in. */
            load("winquote-01",wq);
            assert(!cvs2_quote_decode(wq,sizeof wq,text,161));
            strcpy((char *)wq+0x20fac,"be good enough to defeat me!");
            wq[0x20f5a]=wq[0x20f56]; wq[0x20f50]=3;
            assert(cvs2_quote_decode(wq,sizeof wq,text,161));
            assert(!wcscmp(text,L"Practice all you want! You will never be good enough to defeat me!"));
            for (unsigned mode=0; mode<3; mode++) {
                wq[0x20f3e]=(BYTE)mode;
                assert(cvs2_quote_decode(wq,sizeof wq,text,161));
            }
            wq[0x20f50]=4; /* skip-animation completion */
            assert(cvs2_quote_decode(wq,sizeof wq,text,161));
            cvs2_quote_observe(NULL,0,0); b=calls;
            cvs2_quote_observe(wq,sizeof wq,3);
            cvs2_quote_observe(wq,sizeof wq,3); assert(calls==b+1);
            assert(!wcscmp(last,text));
            wq[0x20f54]=80; assert(!cvs2_quote_decode(wq,sizeof wq,text,161));
        }

        /* ---- 7g. No quote outside the post-match display. ---------------- */
        {
            static BYTE other[CVS2_STATE_SIZE];
            static const char *const nonquote[] = {
                "crouch-01", "jump-01", "knockdown-01", "meter-empty",
                "meter-spent", "pos-right-01",        /* +0x05931 == 16 */
                "return-ryu-06", "up-from-ryu-04",
                "up-from-ryu-idle-05",                /* +0x05931 == 1 (Ken) */
                "hp-full", "hp-low", "timer-99", "state-b", "ryu-01",
            };
            unsigned i;
            for (i = 0; i < sizeof nonquote / sizeof *nonquote; i++) {
                int b;
                load(nonquote[i], other);
                /* Each really is outside the quote state... */
                assert(memcmp(other + 0x78, "\x02\x01\x02\x03", 4) != 0);
                cvs2_quote_observe(NULL,0,0);
                b = calls;
                cvs2_quote_observe(other,sizeof other,0x15ba1000ULL);
                assert(calls == b);      /* ...and nothing is spoken. */
            }
            /* The premise of the case: these captures really do carry a
             * roster-valid ID in the winner byte, so the silence above is the
             * GATE working and not the field happening to be zero. */
            load("up-from-ryu-04", other);
            assert(other[0x05931] == 1);
            load("crouch-01", other);
            assert(other[0x05931] == 16);
        }
    }

    /* ---- frame decode: stance ladder, pan and clock from real captures ----
     * The labels are the user's own (crouch, jump, knockdown, right side). */
    {
        static BYTE f[CVS2_STATE_SIZE];
        match_audio_sample m;
        load("crouch-01", f); cvs2_frame_decode(f,&m);
        assert(m.in_match && m.owner[0].stance==AUDIO_STANCE_CROUCH);
        assert(m.owner[1].stance==AUDIO_STANCE_STAND);
        load("jump-01", f); cvs2_frame_decode(f,&m);
        assert(m.owner[0].stance>=AUDIO_STANCE_AIR &&
               m.owner[0].stance<=AUDIO_STANCE_AIR_TOP);
        load("knockdown-01", f); cvs2_frame_decode(f,&m);
        assert(m.owner[1].stance==AUDIO_STANCE_DOWN);
        load("pos-right-01", f); cvs2_frame_decode(f,&m);
        for (int p=0; p<2; p++)
            assert(m.owner[p].beacon_valid &&
                   m.owner[p].screen_pan>=-1.0f && m.owner[p].screen_pan<=1.0f);
        load("hp-hurt", f); cvs2_frame_decode(f,&m);
        assert(m.clock_valid && m.clock>=0 && m.clock<=99 && m.motion_valid);
        /* The selection screen is not a match. */
        cvs2_frame_decode(ryu,&m);
        assert(!m.in_match);
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

        /* ⭐ CORRECTION: a REFUSED fighter change is retried, not lost. The
         * old path latched the fighter before speech was accepted. */
        cvs2_announce_observe(NULL,0,0);
        cvs2_announce_observe(m1,sizeof m1,0x30000000ULL);   /* intro */
        speech_accepts=0; before=calls;
        cvs2_announce_observe(m2,sizeof m2,0x30000000ULL);   /* refused */
        assert(calls==before+1);
        speech_accepts=1;
        cvs2_announce_observe(m2,sizeof m2,0x30000000ULL);   /* retried */
        assert(calls==before+2 && wcsstr(last,L"Ken")!=NULL);
        assert(wcsstr(last,L"Groove")==NULL);
        cvs2_announce_observe(m2,sizeof m2,0x30000000ULL);
        assert(calls==before+2);                             /* then done */

        /* A refused INTRODUCTION is retried too, and stays the intro. */
        cvs2_announce_observe(NULL,0,0);
        speech_accepts=0; before=calls;
        cvs2_announce_observe(m1,sizeof m1,0x31000000ULL);
        speech_accepts=1;
        cvs2_announce_observe(m1,sizeof m1,0x31000000ULL);
        assert(calls==before+2 && wcsstr(last,L"C Groove")!=NULL);

        /* ⭐ OWNERSHIP: Arcade's joined mask names P1 as the player... */
        {
            transition_observation o;
            static BYTE tr[CVS2_STATE_SIZE];
            cvs2_announce_observation(m1,&o);
            assert(o.side[0].ownership==TRANSITION_OWNER_LOCAL);
            assert(o.side[1].ownership==TRANSITION_OWNER_OPPONENT);
            assert(o.team_ready && o.side[0].count==3 && o.side[1].count==2);
            assert(o.side[0].member[2].ratio==2);          /* 1/1/2 budget */
            /* ...but Training's mask is 0: nobody is named "You". */
            load("crouch-01",tr);
            cvs2_announce_observation(tr,&o);
            assert(o.side[0].ownership==TRANSITION_OWNER_UNKNOWN);
            assert(o.side[1].ownership==TRANSITION_OWNER_UNKNOWN);
            cvs2_announce_observe(NULL,0,0);
            cvs2_announce_observe(tr,sizeof tr,0x32000000ULL);
            /* 1v1, so the stored 1/1/1 is not a 4-point budget: no ratios. */
            assert(!wcscmp(last,L"Player 1: C Groove, Ryu. Player 2: C Groove, Kyo"));
            /* Two joined humans also identify nobody. */
            tr[0x1ffec]=3;
            cvs2_announce_observation(tr,&o);
            assert(o.side[0].ownership==TRANSITION_OWNER_UNKNOWN);
        }
    }

    /* ROM changes at a reused root must rearm the common presentation owner. */
    cvs2_select_observe(NULL,0,0,0);
    int before_rom_switch = calls;
    cvs2_select_observe(ryu,sizeof ryu,1,3);
    cvs2_select_observe(ryu,sizeof ryu,1,3);
    assert(calls == before_rom_switch + 1 && !wcscmp(last,L"Ryu"));
    cvs2_select_observe(ryu,sizeof ryu,1,2);
    assert(calls == before_rom_switch + 1);
    cvs2_select_observe(ryu,sizeof ryu,1,2);
    assert(calls == before_rom_switch + 2 && !wcscmp(last,L"Ryu"));
    shared_replay();
    puts("PASS: Arcade and alternate character/groove, team/P2, ratio/stage, transitions, speech without prefixes, gauge feed + KO sentinel, super meter per-groove caps, match/round speech");
    return 0;
}
