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
    assert(dec(5, "GAME OVER") == FALSE);
    /* Font 0: the opening screen's table 0x1412DA5D0, through the attract
     * walker. */
    assert(dec(0, "STREET FIGHTER ALPHA3 UPPER") && !wcscmp(out, L"STREET FIGHTER ALPHA3 UPPER"));
    assert(dec(0, "2001 01 11") && !wcscmp(out, L"2001 01 11"));
    assert(dec(0, "USA") && !wcscmp(out, L"USA"));

    /* 3. Outside printable ASCII refuses the row (the special 0x85 run). */
    assert(dec(1, "\x85\x85\x85") == FALSE);
    assert(dec(1, "GAME\x7f") == FALSE);
    assert(dec(1, "    ") == FALSE);
    assert(dec(1, "") == FALSE);

    /* 4. Story messages: Ryu's three entries as shipped in bin/z3u (file
     *    offset 0x4C3C8F6, stride 0x45, the stride 0x1408A9301 uses). Lines
     *    are NUL then an indent byte; an indent of 0 ends the entry. */
    {
        static const unsigned char e0[0x45] = {
            0x52, 0x79, 0x75, 0x20, 0x69, 0x73, 0x20, 0x61, 0x20, 0x6d, 0x61, 0x6e, 0x20, 0x66, 0x6f, 0x6c, 0x6c, 0x6f, 0x77, 0x69, 0x6e, 0x67, 0x20, 0x74, 0x68, 0x65, 0x20, 0x70, 0x61, 0x74, 0x68, 0x00, 0x08, 0x74, 0x6f, 0x20, 0x62, 0x65, 0x63, 0x6f, 0x6d, 0x65, 0x20, 0x61, 0x20, 0x74, 0x72, 0x75, 0x65, 0x20, 0x6d, 0x61, 0x72, 0x74, 0x69, 0x61, 0x6c, 0x20, 0x61, 0x72, 0x74, 0x69, 0x73, 0x74, 0x2e, 0x00, 0x00, 0x3c, 0x08 };
        static const unsigned char e1[0x45] = {
            0x57, 0x68, 0x61, 0x74, 0x20, 0x69, 0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x6d, 0x65, 0x61, 0x6e, 0x69, 0x6e, 0x67, 0x20, 0x6f, 0x66, 0x20, 0x74, 0x68, 0x65, 0x20, 0x22, 0x65, 0x76, 0x69, 0x6c, 0x00, 0x16, 0x65, 0x6e, 0x65, 0x72, 0x67, 0x79, 0x22, 0x20, 0x62, 0x75, 0x69, 0x6c, 0x64, 0x69, 0x6e, 0x67, 0x20, 0x77, 0x69, 0x74, 0x68, 0x69, 0x6e, 0x20, 0x68, 0x69, 0x6d, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x08 };
        static const unsigned char e2[0x45] = {
            0x48, 0x65, 0x20, 0x63, 0x6f, 0x6e, 0x74, 0x69, 0x6e, 0x75, 0x65, 0x73, 0x20, 0x68, 0x69, 0x73, 0x20, 0x6a, 0x6f, 0x75, 0x72, 0x6e, 0x65, 0x79, 0x20, 0x74, 0x6f, 0x20, 0x66, 0x69, 0x6e, 0x64, 0x00, 0x08, 0x74, 0x68, 0x65, 0x20, 0x61, 0x6e, 0x73, 0x77, 0x65, 0x72, 0x73, 0x2c, 0x20, 0x61, 0x6e, 0x64, 0x20, 0x6e, 0x65, 0x77, 0x20, 0x63, 0x68, 0x61, 0x6c, 0x6c, 0x65, 0x6e, 0x67, 0x65, 0x73, 0x2e, 0x00, 0x00, 0x00 };
        char joined[TC_RAW_CAP];
        assert(z3u_message_join(e0, sizeof e0, joined, sizeof joined - 1));
        assert(!strcmp(joined, "Ryu is a man following the path to become a true martial artist."));
        assert(z3u_message_join(e1, sizeof e1, joined, sizeof joined - 1));
        assert(!strcmp(joined, "What is the meaning of the \"evil energy\" building within him?"));
        assert(z3u_message_join(e2, sizeof e2, joined, sizeof joined - 1));
        assert(!strcmp(joined, "He continues his journey to find the answers, and new challenges."));
        assert(dec(Z3U_STYLE_MESSAGE, joined) &&
               !wcscmp(out, L"He continues his journey to find the answers, and new challenges."));
        /* Unterminated or over-long: refused, never half-read. */
        assert(z3u_message_join(e0, 20, joined, sizeof joined - 1) == 0);
        assert(z3u_message_join(e0, sizeof e0, joined, 30) == 0);
        /* The personal-data panel's 0x99 glyph is not ASCII: refused. */
        assert(dec(Z3U_STYLE_MESSAGE, "HEIGHT:5\x99" "7FT") == FALSE);
    }

    /* 5. Nothing published: observing speaks nothing. */
    z3u_text_observe(0x16c71000ULL);
    z3u_text_observe(0);
    assert(calls == 0);

    printf("PASS: Alpha 3 text -- print-site literals by font, opening screen (font 0), "
           "padding, decoration, picture-word font refused, non-ASCII refused, "
           "story messages joined from the shipped data\n");
    return 0;
}
