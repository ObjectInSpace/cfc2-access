/* CVS1 text-record decoding against the producer literals pinned in
 * docs/cvs1-acquisition-trace.md, plus the capture/publish/speak path with a
 * fake queue. No game. Run from the repository root. */
#include "../src/cvs1_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int calls, accepts = 1;
static wchar_t last[TC_SAY_CAP];
BOOL speech_say(const wchar_t *s, BOOL interrupt)
{ (void)interrupt; calls++; wcsncpy(last, s, TC_SAY_CAP - 1); return accepts; }
void log_line(const char *fmt, ...) { (void)fmt; }

static wchar_t out[TC_LINE_CAP];
static BOOL dec(int style, const char *s) { return cvs1_text_decode(style, s, out, TC_LINE_CAP); }

int main(void)
{
    /* 1. Literals the producers format, as drawn by the ordinary renderer. */
    assert(dec(0, "PRESS START BUTTON") && !wcscmp(out, L"PRESS START BUTTON"));
    assert(dec(1, "INSERT COIN(S)") && !wcscmp(out, L"INSERT COIN(S)"));
    assert(dec(0, "Press Start to Return") && !wcscmp(out, L"Press Start to Return"));
    /* Win counts are padded with spaces; the padding is collapsed. */
    assert(dec(0, "WINS  12") && !wcscmp(out, L"WINS 12"));
    /* The copyright lines start with byte 0x5C, a decoration glyph. */
    assert(dec(0, "\\CAPCOM CO., LTD. 2001 ALL RIGHTS RESERVED.") &&
           !wcscmp(out, L"CAPCOM CO., LTD. 2001 ALL RIGHTS RESERVED."));

    /* 2. Caret escapes draw unmapped special glyphs: omitted, never letters. */
    assert(dec(0, "A^aB") && !wcscmp(out, L"AB"));
    assert(dec(0, "^Z") == FALSE);                /* only decoration: nothing */
    assert(dec(0, "AB^") == FALSE);               /* dangling escape: refused */
    /* Style 7 has no caret escape: it is an ordinary character there. */
    assert(dec(7, "A^B") && !wcscmp(out, L"A^B"));

    /* 3. Bytes outside printable ASCII refuse the row rather than guess. */
    assert(dec(0, "GAME \x81OVER") == FALSE);
    assert(dec(0, "GAME\x01") == FALSE);
    assert(dec(0, "   ") == FALSE);

    /* 4. Observe/speak: nothing is published here, so the worker finds no
     *    fresh frame and says nothing; leaving CVS1 is silent. */
    cvs1_text_observe(0x15d11000ULL);
    assert(calls == 0);
    cvs1_text_observe(0);
    assert(calls == 0);

    printf("PASS: CVS1 text records -- producer literals, padding, decoration, caret "
           "escapes per style, refusals\n");
    return 0;
}
