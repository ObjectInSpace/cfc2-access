/* Renderer replay, not a live-screen capture. */
#include "../src/text_channel.c"
#include "../src/text_capture.c"
#include "../src/cvs2_text.c"
#include <assert.h>
#include <stdio.h>
static int calls, accepts = 1, original_calls;
static BOOL last_interrupt;
static wchar_t last[TC_SAY_CAP];
BOOL speech_say(const wchar_t *text, BOOL interrupt)
{ ++calls; wcscpy(last,text); last_interrupt=interrupt; return accepts; }
void log_line(const char *format, ...) { (void)format; }
MH_STATUS WINAPI MH_CreateHook(LPVOID a, LPVOID b, LPVOID *c)
{ (void)a; (void)b; (void)c; return MH_ERROR_UNSUPPORTED_FUNCTION; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID a) { (void)a; return MH_OK; }
MH_STATUS WINAPI MH_EnableHook(LPVOID a) { (void)a; return MH_OK; }
MH_STATUS WINAPI MH_DisableHook(LPVOID a) { (void)a; return MH_OK; }
static void draw_record(const void *r, float z) { (void)r; (void)z; ++original_calls; }
static void draw_string(float x,float y,float z,const char *s)
{ (void)x;(void)y;(void)z;(void)s; ++original_calls; }
static void finish(void) { ++original_calls; }
static void record(short x,short y,int style,const char *s)
{
    struct { short x,y; int style; char text[56]; } r;
    r.x=x; r.y=y; r.style=style; memset(r.text,0,sizeof r.text);
    strcpy(r.text,s); record_detour(&r,1.0f);
}
static void poll(void) { cvs2_text_observe(1); }
static void frame(void) { end_detour(); poll(); }
/* The ` key now goes through the shared registry, exactly as mod.c drives it. */
static void press_f9(void) { cvs2_text_request_read();
                             if (tc_read_requested()) tc_read_current(); }
int main(void)
{
    tc_row r = {0}; wchar_t decoded[TC_LINE_CAP]; int before;
    assert(mem_guard_install());
    real_record=draw_record; real_string=draw_string; real_end=finish;
    text_installed=TRUE;
    assert(tc_register("CVS2", read_current) && tc_test_source_count()==1);
    r.kind=1; strcpy(r.raw,"   ^JCAPCOM CO., LTD. 2001 ALL RIGHTS RESERVED.");
    assert(decode_row(&r,decoded) && !wcscmp(decoded,L"CAPCOM CO., LTD. 2001 ALL RIGHTS RESERVED."));
    strcpy(r.raw,"A^YB^ZC"); assert(decode_row(&r,decoded) && !wcscmp(decoded,L"A^B\\C"));
    strcpy(r.raw,"A^"); assert(!decode_row(&r,decoded));
    strcpy(r.raw,"A\x90"); assert(!decode_row(&r,decoded));
    /* Japanese ROM rows are CP932: decoded, and a trail byte equal to '^'
     * (0x83 0x5E = katakana TA) is part of its character, not an escape. */
    strcpy(r.raw,"\x83\x5E\x83\x43\x83\x80"); assert(decode_row(&r,decoded) && !wcscmp(decoded,L"\x30bf\x30a4\x30e0"));
    strcpy(r.raw,"\x95\x89\x82\xaf"); assert(decode_row(&r,decoded) && !wcscmp(decoded,L"\x8ca0\x3051"));
    strcpy(r.raw,"\xb1\xb2"); assert(decode_row(&r,decoded) && !wcscmp(decoded,L"\xff71\xff72"));
    strcpy(r.raw,"\xfd"); assert(!decode_row(&r,decoded));
    /* Nothing is exposed before the renderer's end-of-text-frame call. */
    record(10,10,9,"PRESS START BUTTON"); poll(); assert(calls==0);
    frame(); assert(calls==1 && !last_interrupt && !wcscmp(last,L"PRESS START BUTTON"));
    for (int i=0;i<300;i++) { record(10,10,9,"PRESS START BUTTON"); frame(); }
    assert(calls==1);
    frame(); /* Blink off, then on, must not repeat. */
    record(10,10,9,"PRESS START BUTTON"); frame(); assert(calls==1);
    record(20,20,9,"CONTINUE 9"); frame(); assert(calls==2 && !wcscmp(last,L"CONTINUE 9"));
    accepts=0; record(20,20,9,"CONTINUE 8"); frame(); assert(calls==3);
    accepts=1; record(20,20,9,"CONTINUE 8"); frame(); assert(calls==4);
    record(20,20,9,"CONTINUE 8"); frame(); assert(calls==4);
    record(20,20,9,"GAME OVER"); frame(); assert(calls==5 && !wcscmp(last,L"GAME OVER"));
    /* Names, pure digits, story/quote font runs stay out of auto narration. */
    record(10,20,16,"RYU"); record(20,20,16,"KEN"); record(10,30,9,"12345");
    string_detour(10,10,1,"Stand up and face me again."); frame(); assert(calls==5);
    press_f9();
    assert(calls==6 && last_interrupt && !wcscmp(last,L"Stand up and face me again. \nRYU \nKEN \n12345"));
    /* An empty frame invalidates everything, rather than reading history. */
    frame(); press_f9();
    assert(calls==7 && wcsstr(last,L"No readable text"));
    publisher.published_at=GetTickCount64()-1000;
    press_f9(); assert(calls==8 && wcsstr(last,L"No fresh"));
    /* An unreadable input is contained without killing the process. */
    before=(int)collecting.count;
    tc_frame_add(&collecting,0,0,0,0,(const char *)(ULONG_PTR)1,80);
    assert((int)collecting.count==before);
    { char long_line[80]; memset(long_line,'A',sizeof long_line);
      tc_frame_add(&collecting,0,0,0,0,long_line,sizeof long_line);
      assert((int)collecting.count==before); }
    /* Leaving CVS2 clears automatic suppression; identical text is new next run. */
    cvs2_text_observe(0); poll(); record(10,10,9,"PRESS START BUTTON"); frame();
    assert(calls==9 && !wcscmp(last,L"PRESS START BUTTON"));
    /* The ` key with CVS2 not running must still SAY something, not fall silent. */
    cvs2_text_observe(0);
    press_f9();
    assert(calls==10 && wcsstr(last,L"No text reader is active here."));
    /* And with no source registered at all, the key still answers. */
    tc_test_reset();
    assert(tc_test_source_count()==0);
    press_f9();
    assert(calls==11 && wcsstr(last,L"No text reader"));
    assert(original_calls>300);
    puts("PASS: completed-frame text, prompts/countdown, blink dedup, speech retry, "
         "read-current ordering, empty/stale-frame rejection, guarded bounds, session reset, "
         "shared-registry fallbacks");
    mem_guard_remove();
    return 0;
}
