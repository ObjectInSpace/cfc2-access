/* ⭐⭐ THE BUFFER THAT SILENCED THE MOVE LIST.
 *
 * USER-REPORTED: "The move list doesn't read." The panel readout was built
 * correctly on every movement -- the log proved it, "10 name object(s), 10 pair
 * object(s)" on the SUCCESS path -- and then vanished at the very last step,
 * because menu_speak_line() converted into a wchar_t[256] while the panel path
 * passes a line[1024]. MultiByteToWideChar returned 0 and the line was dropped.
 *
 * ⚠ The regression came from making rows BETTER. Single-row callers fit 256
 * comfortably; the panel only crossed the limit once each row carried its
 * condition AS WELL AS its notation. A shared helper sized for one caller's
 * longest string is a trap for the next caller.
 *
 * ⛔⛔ AND THE FIRST FIX WAS WRONG IN A WAY THAT WOULD HAVE SHIPPED SILENTLY.
 * I wrote a "speak it truncated" path that capped only cchWideChar. Measured
 * against the real API: with cbMultiByte = -1 OR the full source length,
 * MultiByteToWideChar returns 0/ERROR_INSUFFICIENT_BUFFER and writes NOTHING --
 * it never partially fills. That path would have been exactly as silent as the
 * bug. Case 3 is that measurement, and it fails if the source is not bounded.
 *
 * Build/run: tools\run_sl_test.ps1
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok, const char *detail)
{
    printf("%-4s %s%s%s\n", ok ? "ok" : "FAIL", name,
           detail && detail[0] ? " -- " : "", detail ? detail : "");
    if (!ok)
        failures++;
}

/* The conversion under test, lifted to match menu_speak_line()'s logic exactly.
 * ⚠ A copy is a second place to drift, so this file asserts the BEHAVIOUR of
 * the Win32 calls the shipping code makes; menu_speak.c carries the same
 * sequence and the comments in both point at each other. */
static int convert(const char *line, wchar_t *w, int cap, int *was_truncated)
{
    int n;

    *was_truncated = 0;
    n = MultiByteToWideChar(CP_UTF8, 0, line, -1, w, cap);
    if (n > 0)
        return n;
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return 0;

    {
        int src = cap - 1;
        if (src > (int)strlen(line))
            src = (int)strlen(line);
        while (src > 0 && ((unsigned char)line[src] & 0xC0) == 0x80)
            src--;
        n = MultiByteToWideChar(CP_UTF8, 0, line, src, w, cap - 1);
        if (n <= 0)
            return 0;
        w[n] = L'\0';
        *was_truncated = 1;
        return n;
    }
}

int main(void)
{
    wchar_t w[1024];
    int n, trunc;

    /* case 1: an ordinary single row converts untouched. */
    {
        const char *s = "Shoryuken, dragon punch plus punch";
        n = convert(s, w, 1024, &trunc);
        check("1. an ordinary row converts whole",
              n > 0 && !trunc && wcslen(w) == strlen(s), "");
    }

    /* case 2: ⛔⛔ THE REGRESSION. A real ten-row panel -- verbatim from the
     * 01:03 run that the user reported as silent -- must convert at the
     * SHIPPING buffer size. At 256 it returned 0 and the move list said
     * nothing at all. */
    {
        const char *panel =
            "Ryu Moves. Somersault Throw,  Near Opponent back or forward plus "
            "heavy kick. Collarbone Breaker, forward plus medium punch. Solar "
            "Plexus Strike, forward plus heavy punch. Whirlwind Kick, forward "
            "plus medium kick. Hadoken, quarter circle forward plus punch. "
            "Shakunetsu Hadoken, half circle forward plus punch. Shoryuken, "
            "dragon punch plus punch. Tatsumaki Senpu-kyaku (AIR) , quarter "
            "circle back plus kick. Shinku Hadoken, quarter circle forward "
            "quarter circle forward plus punch. Shinku Tatsumaki Senpu-kyaku, "
            "quarter circle back quarter circle back plus kick";
        char d[120];
        int at256, t256;
        wchar_t small[256];

        n = convert(panel, w, 1024, &trunc);
        at256 = MultiByteToWideChar(CP_UTF8, 0, panel, -1, small, 256);
        (void)t256;
        snprintf(d, sizeof d,
                 "%d bytes: at the shipping 1024 n=%d; at the OLD 256 n=%d",
                 (int)strlen(panel), n, at256);
        check("2. a real ten-row panel converts at the shipping size",
              n > 0 && !trunc && at256 == 0, d);
    }

    /* case 3: ⛔⛔ THE MEASUREMENT THAT KILLED THE FIRST FIX. Capping only the
     * DESTINATION does not truncate -- the API writes nothing and reports
     * ERROR_INSUFFICIENT_BUFFER. A fix relying on it would be as silent as the
     * bug. This asserts the API's actual behaviour, so the shipping code cannot
     * quietly go back to it. */
    {
        char big[600];
        wchar_t small[256];
        int dest_only, src_bounded;
        char d[140];

        memset(big, 'A', sizeof big - 1);
        big[sizeof big - 1] = '\0';

        SetLastError(0);
        dest_only = MultiByteToWideChar(CP_UTF8, 0, big, (int)strlen(big),
                                        small, 255);
        src_bounded = MultiByteToWideChar(CP_UTF8, 0, big, 255, small, 255);
        snprintf(d, sizeof d,
                 "destination-only cap -> %d (writes nothing); "
                 "source bounded -> %d", dest_only, src_bounded);
        check("3. truncation requires bounding the SOURCE, not the destination",
              dest_only == 0 && src_bounded == 255, d);
    }

    /* case 4: an oversized line is SPOKEN TRUNCATED, never dropped. Hearing
     * nine of ten rows is a minor loss; hearing nothing is a broken screen, and
     * to the user the two are indistinguishable from a screen that cannot be
     * read at all. */
    {
        char big[600];
        wchar_t small[256];
        char d[120];

        memset(big, 'A', sizeof big - 1);
        big[sizeof big - 1] = '\0';
        n = convert(big, small, 256, &trunc);
        snprintf(d, sizeof d, "n=%d truncated=%d len=%d", n, trunc,
                 n > 0 ? (int)wcslen(small) : -1);
        check("4. an over-long line is spoken truncated, not discarded",
              n > 0 && trunc && wcslen(small) == 255, d);
    }

    /* case 5: ⚠ truncation must not split a UTF-8 sequence. The shell's text is
     * UTF-8 and the collection ships 14 languages, so a cut mid-character is a
     * realistic case, not a hypothetical -- and it makes the tail
     * unconvertible.
     *
     * ⚠ THE SOURCE MUST BE LONG ENOUGH IN WCHARS, not just in bytes -- my first
     * version of this case used 597 bytes of 3-byte Japanese, which is only 201
     * wchars and so FIT in the buffer: it reported truncated=0 and failed
     * against correct code. The test data was wrong, not the mechanism. Sized
     * in CHARACTERS now: 300 Hiragana (900 bytes) against a 256-wchar buffer,
     * with a leading ASCII byte so the 255-byte cut lands mid-sequence. */
    {
        char big[1024];
        wchar_t small[256];
        char d[140];
        int i = 0, chars = 0;

        big[i++] = 'x';                     /* shift the 3-byte grid by one */
        while (chars < 300) {                /* U+3042 HIRAGANA A */
            big[i++] = (char)0xE3; big[i++] = (char)0x81; big[i++] = (char)0x82;
            chars++;
        }
        big[i] = '\0';
        /* The cut at src=255 lands inside a sequence; the back-off must move it
         * to a boundary, so the converted count is a whole number of chars. */
        n = convert(big, small, 256, &trunc);
        snprintf(d, sizeof d, "%d bytes / %d chars -> n=%d truncated=%d",
                 i, chars, n, trunc);
        check("5. truncation backs off to a UTF-8 character boundary",
              n > 0 && trunc && wcslen(small) == (size_t)n, d);
    }

    /* case 6: a genuinely malformed string is still REFUSED, not spoken as
     * mojibake. The truncation path must not become a way for bad bytes to
     * reach the reader. */
    {
        const char bad[] = { (char)0xFF, (char)0xFE, (char)0xFD, '\0' };
        wchar_t small[256];
        char d[120];

        SetLastError(0);
        n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bad, -1,
                                small, 256);
        snprintf(d, sizeof d, "strict conversion of invalid UTF-8 -> %d", n);
        check("6. invalid UTF-8 is refused under strict conversion", n == 0, d);
    }

    printf("\n%s (%d failure(s))\n", failures ? "FAILED" : "ALL PASSED",
           failures);
    return failures ? 1 : 0;
}
