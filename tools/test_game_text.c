/* The shared worker half of the game text readers (src/game_text.c), driven
 * through a fake game: publish a frame, observe, check what was spoken and
 * logged. No game. Run from the repository root. */
#include "../src/game_text.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int calls, accepts = 1, refusals;
static wchar_t last[TC_SAY_CAP];
static char last_log[512];
BOOL speech_say(const wchar_t *s, BOOL interrupt)
{ (void)interrupt; calls++; wcsncpy(last, s, TC_SAY_CAP - 1); return accepts; }
void log_line(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    vsnprintf(last_log, sizeof last_log, fmt, a);
    va_end(a);
    if (strstr(last_log, "refused row")) refusals++;
}

/* The fake game: style 1 is plain ASCII, style 9 is never decodable, style 0
 * is expected-undrawable (quiet). */
static BOOL decode(int style, const char *raw, wchar_t *out, SIZE_T cap)
{
    SIZE_T n = 0;
    out[0] = 0;
    if (style != 1) return FALSE;
    for (; raw[n] && n + 1 < cap; ++n) out[n] = (wchar_t)(unsigned char)raw[n];
    out[n] = 0;
    return tc_squeeze(out) > 0;
}
static BOOL quiet(const tc_row *r) { return r->style == 0; }
static game_text_state state;
static game_text_source src = GAME_TEXT_SOURCE("fake-text", "Fake", decode, quiet, &state);
static tc_frame collecting;

static void frame(const char *a, int sa, const char *b, int sb)
{
    if (a) tc_frame_add(&collecting, 0.0f, 0.0f, sa, 0, a, TC_RAW_CAP);
    if (b) tc_frame_add(&collecting, 0.0f, 1.0f, sb, 0, b, TC_RAW_CAP);
    tc_frame_publish(&state.publisher, &collecting);
}

int main(void)
{
    int before;

    /* 1. Inactive: the ` key says so; observing nothing speaks nothing. */
    assert(game_text_read_current(&src) == TC_READ_INACTIVE);
    game_text_observe(&src, 0);
    assert(calls == 0);

    /* 2. A new line with letters is spoken once; repeats in the same frame
     *    are one reading; digits alone are not narrated. */
    game_text_observe(&src, 0x1000);
    frame("GAME OVER", 1, "GAME OVER", 1);
    game_text_observe(&src, 0x1000);
    assert(calls == 1 && !wcscmp(last, L"GAME OVER"));
    frame("GAME OVER", 1, "9", 1);
    game_text_observe(&src, 0x1000);
    assert(calls == 1);                              /* still on screen: quiet */
    frame("12345", 1, NULL, 0);
    game_text_observe(&src, 0x1000);
    assert(calls == 1);                              /* digits are not narrated */

    /* 2b. The board's credit display is never narrated; real prompts that
     *     merely contain the words still are. */
    frame("FREE PLAY", 1, "CREDITS: 03", 1);
    game_text_observe(&src, 0x1000);
    frame("CREDIT 1", 1, NULL, 0);
    game_text_observe(&src, 0x1000);
    /* Project Justice's forms: "CREDIT(S) n" and the coin fraction. */
    frame("CREDIT(S) 3", 1, "CREDIT(S) 0 1/2", 1);
    game_text_observe(&src, 0x1000);
    assert(calls == 1);
    frame("FREE PLAY MODE", 1, NULL, 0);
    game_text_observe(&src, 0x1000);
    assert(calls == 2 && !wcscmp(last, L"FREE PLAY MODE"));
    frame("CREDITS", 1, NULL, 0);
    game_text_observe(&src, 0x1000);
    assert(calls == 3 && !wcscmp(last, L"CREDITS"));     /* the roll's title */
    frame("FREE PLAY", 1, NULL, 0);
    game_text_observe(&src, 0x1000);
    assert(game_text_read_current(&src) == TC_READ_SPOKE);
    assert(calls == 4 && !wcscmp(last, L"FREE PLAY"));   /* ` still reads it */

    /* 3. The ` key reads the whole frame, digits included. */
    frame("CONTINUE?", 1, "9", 1);
    game_text_observe(&src, 0x1000);
    before = calls;
    assert(game_text_read_current(&src) == TC_READ_SPOKE);
    assert(calls == before + 1 && !wcscmp(last, L"CONTINUE? \n9"));

    /* 4. A refused utterance is not marked spoken: the next offer retries. */
    accepts = 0;
    frame("NEXT", 1, NULL, 0);
    game_text_observe(&src, 0x1000);
    accepts = 1;
    before = calls;
    frame("NEXT", 1, NULL, 0);
    game_text_observe(&src, 0x1000);
    assert(calls == before + 1 && !wcscmp(last, L"NEXT"));

    /* 5. Refused rows are logged once each; quiet rows not at all. */
    refusals = 0;
    frame("\x91\x92", 9, "PRESS", 0);
    game_text_observe(&src, 0x1000);
    frame("\x91\x92", 9, "PRESS", 0);
    game_text_observe(&src, 0x1000);
    assert(refusals == 1 && strstr(last_log, "fake-text: refused row style=9 bytes: 91 92"));

    /* 6. A new session forgets what was spoken: the same line speaks again. */
    frame("GAME OVER", 1, NULL, 0);
    game_text_observe(&src, 0x1000);
    before = calls;
    game_text_observe(&src, 0x2000);                 /* new root: old frame dropped */
    frame("GAME OVER", 1, NULL, 0);
    game_text_observe(&src, 0x2000);
    assert(calls == before + 1);

    /* 7. Leaving the game: the ` key reports inactive again. */
    game_text_observe(&src, 0);
    assert(game_text_read_current(&src) == TC_READ_INACTIVE);

    printf("PASS: shared game text -- speak once, in-frame repeats, digits, credit display, ` read, "
           "retry after refusal, refused rows logged once, quiet rows, session reset\n");
    return 0;
}
