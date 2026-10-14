/* CFJ text-record decoding against the producer literals referenced from
 * CFJ code, and the renderer's escape and style rules. No game. Run from the
 * repository root. */
#include "../src/cfj_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int calls;
BOOL speech_say(const wchar_t *s, BOOL interrupt) { (void)s; (void)interrupt; calls++; return TRUE; }
void log_line(const char *fmt, ...) { (void)fmt; }

static wchar_t out[TC_LINE_CAP];
static BOOL dec(int style, const char *s) { return cfj_text_decode(style, s, out, TC_LINE_CAP); }

int main(void)
{
    /* 1. Literals CFJ code formats into records. */
    assert(dec(1, "CONTINUE 9") && !wcscmp(out, L"CONTINUE 9"));
    assert(dec(1, "NO CHALLENGER") && !wcscmp(out, L"NO CHALLENGER"));
    assert(dec(2, "PRESS P1 or P2 START") && !wcscmp(out, L"PRESS P1 or P2 START"));
    /* Centring padding collapses. */
    assert(dec(1, "   INSERT COIN    ") && !wcscmp(out, L"INSERT COIN"));
    /* The copyright's leading ^J is a special glyph: omitted. */
    assert(dec(1, "^JCAPCOM CO., LTD. 2004 ALL RIGHTS RESERVED.") &&
           !wcscmp(out, L"CAPCOM CO., LTD. 2004 ALL RIGHTS RESERVED."));

    /* 2. The renderer's named escapes. */
    assert(dec(1, "A^ZB^YC^MD^NE") && !wcscmp(out, L"A\\B^C\"D`E"));
    assert(dec(1, "AB^") == FALSE);

    /* 3. Style 0 is never drawn, so never spoken. */
    assert(dec(0, "PRESS START") == FALSE);

    /* 4. Outside printable ASCII refuses the row. */
    assert(dec(1, "GAME \x90OVER") == FALSE);
    assert(dec(1, "GAME\x7f") == FALSE);
    assert(dec(1, "    ") == FALSE);

    /* 5. Nothing published: observing speaks nothing. */
    cfj_text_observe(0x174c1000ULL);
    cfj_text_observe(0);
    assert(calls == 0);

    printf("PASS: CFJ text records -- producer literals, padding, special glyphs, named "
           "escapes, undrawn style 0, refusals\n");
    return 0;
}
