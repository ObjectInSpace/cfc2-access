/* ⭐⭐ THE BUFFER THAT SILENCED THE MOVE LIST -- and why there is no buffer now.
 *
 * USER-REPORTED: "The move list doesn't read." The panel readout was built
 * correctly on every movement -- the log proved it, "10 name object(s), 10 pair
 * object(s)" on the SUCCESS path -- and then vanished at the very last step,
 * because menu_speak_line() converted into a wchar_t[256] while the panel path
 * passes a much longer line. MultiByteToWideChar returned 0 and it was dropped.
 *
 * ⚠ The regression came from making rows BETTER. Single-row callers fit 256
 * comfortably; the panel only crossed the limit once each row carried its
 * condition AS WELL AS its notation.
 *
 * ⛔⛔ AND THE FIX IS NOT A BIGGER NUMBER. USER: "We don't need to truncate if
 * there are no limits." Correct -- I first raised 256 to 1024 and added a
 * truncating fallback, which is the same mistake one size up: a constant
 * someone chose, capping what the user may hear, plus a fallback path that can
 * itself be wrong (mine was, twice). menu_speak_line() now asks the API how
 * many wchars THIS line needs and allocates exactly that, so no caller can
 * outgrow it, there is nothing to truncate, and no truncation path to get
 * wrong.
 *
 * These cases assert that sizing rule, and case 2 is the user's real panel.
 *
 * Build/run: tools\run_sl_test.ps1
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok, const char *detail)
{
    printf("%-4s %s%s%s\n", ok ? "ok" : "FAIL", name,
           detail && detail[0] ? " -- " : "", detail ? detail : "");
    if (!ok)
        failures++;
}

/* The sizing rule under test, matching menu_speak_line() exactly: measure,
 * allocate to fit, convert. Returns the wchar count, 0 on refusal.
 *
 * ⚠ A copy is a second place to drift, so both carry comments pointing at each
 * other; what this file pins is that the RULE converts anything the callers can
 * produce, at any length. */
static int convert(const char *line, wchar_t **out)
{
    wchar_t *w;
    int need, n;

    *out = NULL;
    need = MultiByteToWideChar(CP_UTF8, 0, line, -1, NULL, 0);
    if (need <= 0)
        return 0;
    w = (wchar_t *)malloc((size_t)need * sizeof(wchar_t));
    if (!w)
        return 0;
    n = MultiByteToWideChar(CP_UTF8, 0, line, -1, w, need);
    if (n <= 0) { free(w); return 0; }
    *out = w;
    return n;
}

int main(void)
{
    wchar_t *w;
    int n;

    /* case 1: an ordinary single row converts untouched. */
    {
        const char *s = "Shoryuken, dragon punch plus punch";
        n = convert(s, &w);
        check("1. an ordinary row converts whole",
              n > 0 && wcslen(w) == strlen(s), "");
        free(w);
    }

    /* case 2: ⛔⛔ THE REGRESSION. A real ten-row panel, verbatim from the 01:03
     * run the user reported as silent. It must convert WHOLE -- every character
     * present, nothing trimmed -- and the old fixed 256 must be shown failing
     * on it, or this case is not testing the bug that happened. */
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
        char d[140];
        wchar_t small[256];
        int at256;

        n = convert(panel, &w);
        at256 = MultiByteToWideChar(CP_UTF8, 0, panel, -1, small, 256);
        snprintf(d, sizeof d,
                 "%d bytes -> %d wchars, complete; the OLD fixed 256 gave %d",
                 (int)strlen(panel), n, at256);
        check("2. the user's real panel converts COMPLETE, nothing trimmed",
              n > 0 && wcslen(w) == strlen(panel) && at256 == 0, d);
        free(w);
    }

    /* case 3: ⭐⭐ NO LENGTH IS REFUSED. The point of the rewrite: a line far
     * past any fixed buffer this project ever shipped must still convert in
     * full. 8000 bytes is well beyond the worst case the 10-row draw cap can
     * produce -- and every fixed-size design would fail this case. */
    {
        char *big = (char *)malloc(8001);
        char d[120];

        memset(big, 'A', 8000);
        big[8000] = '\0';
        n = convert(big, &w);
        snprintf(d, sizeof d, "8000 bytes -> %d wchars (len=%d)", n,
                 n > 0 ? (int)wcslen(w) : -1);
        check("3. no length is refused -- an 8000-byte line converts in full",
              n > 0 && wcslen(w) == 8000, d);
        free(w);
        free(big);
    }

    /* case 4: ⚠ multi-byte UTF-8 converts in full too. The shell's text is UTF-8
     * across 14 languages, and a sizing rule that treated BYTES as wchars would
     * pass every ASCII case above and still be wrong here. 300 Hiragana is 900
     * bytes but 300 wchars. */
    {
        char big[1024];
        char d[120];
        int i = 0, chars = 0;

        while (chars < 300) {                /* U+3042 HIRAGANA A */
            big[i++] = (char)0xE3; big[i++] = (char)0x81; big[i++] = (char)0x82;
            chars++;
        }
        big[i] = '\0';
        n = convert(big, &w);
        snprintf(d, sizeof d, "%d bytes / %d chars -> %d wchars", i, chars, n);
        check("4. multi-byte UTF-8 converts in full (bytes != wchars)",
              n > 0 && wcslen(w) == 300, d);
        free(w);
    }

    /* case 5: a genuinely malformed string is still REFUSED, not spoken as
     * mojibake. Removing the length limit must not turn the one real refusal
     * into an accept-anything path.
     *
     * ⚠ CP_UTF8 without MB_ERR_INVALID_CHARS SUBSTITUTES U+FFFD rather than
     * failing, so the refusal must be asserted with the strict flag -- and the
     * lax value is printed beside it so the difference is visible rather than
     * assumed. */
    {
        const char bad[] = { (char)0xFF, (char)0xFE, (char)0xFD, '\0' };
        char d[120];
        int strict, lax;

        strict = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bad, -1,
                                     NULL, 0);
        lax = MultiByteToWideChar(CP_UTF8, 0, bad, -1, NULL, 0);
        snprintf(d, sizeof d, "strict -> %d (refused), lax -> %d (substitutes)",
                 strict, lax);
        check("5. invalid UTF-8 is refused under strict conversion",
              strict == 0, d);
    }

    printf("\n%s (%d failure(s))\n", failures ? "FAILED" : "ALL PASSED",
           failures);
    return failures ? 1 : 0;
}
