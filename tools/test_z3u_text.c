/* Alpha 3 text-record decoding against the literals Alpha 3 code passes to
 * its print routine 0x140879770, and the drawer's font and byte rules. No
 * game. Run from the repository root. */
#include "../src/z3u_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int calls;
BOOL speech_say(const wchar_t *s, BOOL interrupt) { (void)s; (void)interrupt; calls++; return TRUE; }
void log_line(const char *fmt, ...) { (void)fmt; }

static wchar_t out[TC_LINE_CAP];
static BOOL dec(int font, const char *s) { return z3u_text_decode(font, s, out, TC_LINE_CAP); }

int main(void)
{
    /* 1. Literals and the fonts their call sites pass. */
    assert(dec(2, "GAME OVER") && !wcscmp(out, L"GAME OVER"));
    assert(dec(2, "CONTINUE?") && !wcscmp(out, L"CONTINUE?"));
    assert(dec(2, "GIVE UP?") && !wcscmp(out, L"GIVE UP?"));
    assert(dec(3, "QUIT GAME ") && !wcscmp(out, L"QUIT GAME"));
    assert(dec(1, "WINS") && !wcscmp(out, L"WINS"));
    assert(dec(2, "CONGRATULATIONS!!") && !wcscmp(out, L"CONGRATULATIONS!!"));
    assert(dec(3, "1P") && !wcscmp(out, L"1P"));
    /* Centring padding collapses; the score's leading blanks too. */
    assert(dec(1, "  EXIT  ") && !wcscmp(out, L"EXIT"));
    assert(dec(1, "      0") && !wcscmp(out, L"0"));
    /* The copyright line's leading '@' is decoration, not a word. */
    assert(dec(1, "@CAPCOM CO., LTD. ") && !wcscmp(out, L"CAPCOM CO., LTD."));
    /* Control bytes advance a cell, as the drawer's c <= 0x20 branch does. */
    assert(dec(1, "A\x01" "B") && !wcscmp(out, L"A B"));

    /* 2. Font 4 draws picture words from tile codes: refused, never spelled
     *    out as the letters its codes happen to be ("UVWXY" is the continue
     *    screen's picture word, 0x1412DA870). */
    assert(dec(4, "UVWXY") == FALSE);
    assert(dec(4, "\x91\x92\x93\x94\x95") == FALSE);
    assert(dec(0, "GAME OVER") == FALSE);
    assert(dec(5, "GAME OVER") == FALSE);

    /* 3. Outside printable ASCII refuses the row (the special 0x85 run). */
    assert(dec(1, "\x85\x85\x85") == FALSE);
    assert(dec(1, "GAME\x7f") == FALSE);
    assert(dec(1, "    ") == FALSE);
    assert(dec(1, "") == FALSE);

    /* 4. Nothing published: observing speaks nothing. */
    z3u_text_observe(0x16c71000ULL);
    z3u_text_observe(0);
    assert(calls == 0);

    printf("PASS: Alpha 3 text records -- print-site literals by font, padding, "
           "decoration, picture-word font refused, non-ASCII refused\n");
    return 0;
}
