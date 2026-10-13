/* Synthetic adapter labels: tests shared behavior, not game name semantics. */
#include "../src/selection_channel.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned calls;
static BOOL accepts = TRUE;
static selection_channel *depart_during_say;
static wchar_t last[GAME_SELECTION_TEXT];
BOOL speech_say(const wchar_t *text, BOOL interrupt)
{
    assert(interrupt);
    ++calls;
    wcscpy(last, text);
    if (depart_during_say) selection_channel_depart(depart_during_say);
    return accepts;
}
void log_line(const char *fmt, ...) { (void)fmt; }
static game_selection_choice choices[GAME_SELECTION_OWNERS];
static void offer(selection_channel *c, uint64_t now)
{ assert(selection_channel_offer(c, choices, true, now, now)); }
int main(void)
{
    selection_channel c = {0};
    for (unsigned i=0; i<GAME_SELECTION_OWNERS; ++i) {
        choices[i].status = GAME_SELECTION_VALID;
        choices[i].value = i;
        choices[i].visit = 1;
        swprintf(choices[i].label, GAME_SELECTION_LABEL, L"Player %u", i+1);
    }
    assert(!selection_channel_offer(&c, choices, true, 100, 100));
    char source[] = "adapter-test";
    assert(selection_channel_begin(&c, 7, 17, source));
    source[0] = 'X'; assert(!strcmp(c.source, "adapter-test"));
    offer(&c,100); assert(!selection_channel_drain(&c,100));
    assert(!selection_channel_drain(&c,100)); /* Drain cannot settle a sample. */
    offer(&c,101); accepts=FALSE;
    assert(!selection_channel_drain(&c,101) && calls==1);
    assert(!selection_channel_drain(&c,102) && calls==2);
    accepts=TRUE;
    assert(selection_channel_drain(&c,103)==15);
    assert(!wcscmp(last,L"Player 1. Player 2. Player 3. Player 4"));
    offer(&c,104); assert(!selection_channel_drain(&c,104));
    assert(!selection_channel_offer(&c,NULL,false,105,105));
    offer(&c,106); offer(&c,107); assert(!selection_channel_drain(&c,107));
    /* Explicit inactivity rearms just this owner. */
    choices[0].status=GAME_SELECTION_INACTIVE; offer(&c,108);
    choices[0].status=GAME_SELECTION_VALID; offer(&c,109);
    assert(!selection_channel_drain(&c,109));
    offer(&c,110); assert(selection_channel_drain(&c,110)==1);
    /* A brief new visit followed by return must rearm, even if not spoken. */
    choices[0].visit=2; offer(&c,111);
    choices[0].visit=1; offer(&c,112); offer(&c,113);
    assert(selection_channel_drain(&c,113)==1);
    /* Departure stops retries; same-ID reacquisition is a new presentation. */
    choices[0].value=99; offer(&c,114); offer(&c,115);
    selection_channel_depart(&c);
    unsigned before=calls;
    assert(!selection_channel_drain(&c,115) && calls==before);
    assert(selection_channel_begin(&c,7,17,"adapter-test"));
    offer(&c,116); offer(&c,117);
    depart_during_say=&c;
    assert(!selection_channel_drain(&c,117));
    depart_during_say=NULL;
    assert(!selection_channel_drain(&c,117));
    assert(selection_channel_begin(&c,0,1,"other-adapter"));
    offer(&c,118); offer(&c,119);
    assert(selection_channel_drain(&c,119)==15);
    /* Expired/future samples cannot speak or pair across unavailable input. */
    choices[0].value=100; offer(&c,120); offer(&c,121);
    assert(!selection_channel_drain(&c,622));
    assert(!selection_channel_offer(&c,choices,true,700,623));
    offer(&c,624); assert(!selection_channel_drain(&c,624));
    offer(&c,625); assert(selection_channel_drain(&c,625)==1);
    char too_long[65]; memset(too_long,'x',64); too_long[64]=0;
    assert(!selection_channel_begin(&c,0,1,too_long));
    assert(!selection_channel_offer(&c,choices,true,626,626));
    assert(selection_channel_begin(&c,0,1,"other-adapter"));
    c.session.issued.sequence=UINT64_MAX;
    assert(!selection_channel_offer(&c,choices,true,627,627));
    assert(!selection_channel_begin(&c,0,1,"other-adapter"));
    assert(!selection_channel_drain(NULL,627));
    puts("PASS: shared selection channel switching, four owners, visits, retries, expiry and exhaustion");
    return 0;
}
