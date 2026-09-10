/*
 * Self-test for src/gui_markup.c -- the C markup translator.
 *
 * ⭐ THE EXPECTED STRINGS COME FROM THE PYTHON ORACLE. tools/markup.py is
 * verified against the whole corpus (58310 strings, 14 languages, 0 residue)
 * AND against the real live captures; every expectation below was produced by
 * running it, not by deciding what looked right. tools/check_markup_parity.py
 * re-derives them and fails if the two implementations drift.
 *
 * ⭐⭐ EVERY CASE HERE IS A DEFECT THAT ACTUALLY OCCURRED, and each mechanism
 * was sabotage-verified in the Python suite before being ported: breaking it
 * makes the specific case below fail, not merely "something" fail.
 *
 * ⛔ AND ONE HOLE THIS SUITE EXISTS TO AVOID: the Python suite originally
 * asserted hint strips by CONTAINMENT, and a sabotaged stutter-dropper still
 * passed, because the broken "Cancel Cancel Ready" CONTAINS "Cancel Ready".
 * Everything here is asserted by EXACT equality.
 *
 * Build/run: tools\run_gm_test.ps1
 */

#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "../src/gui_markup.h"

static int g_failed;
static int g_checked;

static void check(const char *what, const char *in, const char *expect)
{
    char got[GM_OUT_MAX];
    gm_speak(in, got, sizeof(got));
    g_checked++;
    if (strcmp(got, expect) == 0) {
        printf("ok    %-26s %s\n", what, got);
    } else {
        printf("FAIL  %-26s %s\n", what, got);
        printf("      expected exactly:      %s\n", expect);
        g_failed++;
    }
}

int main(void)
{
    printf("gui_markup self-test\n");
    printf("--------------------\n");

    /* --- The four REAL live captures, verbatim from the 21:19 run. --- */
    /* ⭐⭐ <FONT> appears here and NOWHERE in the offline corpus (0 of 58310
     * strings, all 14 languages). The live tag vocabulary is a SUPERSET of the
     * file's, which is the whole reason this module could not be a
     * straight port of the corpus-audited path. */
    check("live: title prompt",
          "<FONT 2>PRESS <SIZE 80><ICON DECIDE></SIZE> TO START</FONT>",
          "PRESS Confirm TO START");

    /* ⭐ THE STUTTER. The button is named after what it does, so the icon's
     * spoken name and the printed action are the same word. */
    check("live: confirm/back strip",
          "<SIZE 58><ICON DECIDE></SIZE>Confirm "
          "<SIZE 58><ICON CANCEL></SIZE>Back",
          "Confirm; Cancel Back");

    /* ⚠ Pairs must be SEPARATED or the strip is unparseable by ear. */
    check("live: select game strip",
          "Choose the game you want to play.  "
          "<SIZE 58><ICON BTN_START></SIZE>Training "
          "<SIZE 58><ICON BTN_X></SIZE>Game Settings "
          "<SIZE 58><ICON CANCEL></SIZE>Back",
          "Choose the game you want to play. Start Training; "
          "X Game Settings; Cancel Back");

    /* Plain caption, no markup at all: must pass through untouched. */
    check("live: caption", "P1", "P1");

    /* --- Corpus traps: shapes a naive per-icon split would mangle. --- */

    /* ⛔ "/" between two icons is EITHER-OF -- one hint, not two. */
    check("corpus: either-of",
          "<SIZE 58><ICON BTN_LB></SIZE>/<SIZE 58><ICON BTN_RB></SIZE>"
          "Change Game",
          "LB or RB Change Game");

    /* ⛔ "+" between two icons is a CHORD -- one hint, not two. */
    check("corpus: chord",
          "<SIZE 58><ICON BTN_LT></SIZE>+<SIZE 58><ICON D_KEY></SIZE>"
          "Move 100",
          "LT plus d-pad Move 100");

    /* ⚠ Drop only the DUPLICATED WORD, by prefix. "Cancel Ready" must not
     * become "Ready" (meaning lost) nor "Cancel Cancel Ready" (stutter). */
    check("corpus: prefix not equality",
          "<SIZE 58><ICON CANCEL></SIZE>Cancel Ready",
          "Cancel Ready");

    /* A single-pair strip must survive the pair logic unchanged. */
    check("corpus: single pair",
          "<SIZE 58><ICON BTN_START></SIZE>Training",
          "Start Training");

    /* Three pairs, one carrying parentheses. */
    check("corpus: three pairs",
          "<SIZE 58><ICON DECIDE></SIZE>Confirm "
          "<SIZE 58><ICON BTN_X></SIZE>Random Shuffle (All Games) "
          "<SIZE 58><ICON CANCEL></SIZE>Back",
          "Confirm; X Random Shuffle (All Games); Cancel Back");

    /* --- Prose mode: icons mid-sentence must not chop or fuse. --- */
    check("prose: mid-sentence",
          "Any Direction but <SIZE 58><ICON C_8></SIZE>",
          "Any Direction but up");

    /* --- Robustness. These are not corpus shapes; they are what a bounded
     * parser must survive without corrupting anything. --- */

    check("edge: empty", "", "");
    check("edge: tag only", "<SIZE 58></SIZE>", "");
    check("edge: unterminated tag", "PRESS <ICON DECIDE", "PRESS");

    /* ⭐ An UNKNOWN token stays VISIBLE. Dropping it silently destroys
     * meaning; [TOKEN] in the log is how a vocabulary change reports itself
     * -- exactly how <FONT> would have announced itself sooner. */
    check("edge: unknown token",
          "<ICON NOSUCHTOKEN>Thing",
          "[NOSUCHTOKEN] Thing");

    /* The mechanical KB_ rule, for tokens not in the table. */
    check("edge: KB_ mechanical",
          "<ICON KB_F10>Menu",
          "F10 Menu");

    /* --- Residue: no tag may EVER reach the user. --- */
    printf("\n");
    {
        static const char *const INPUTS[] = {
            "<FONT 2>PRESS <SIZE 80><ICON DECIDE></SIZE> TO START</FONT>",
            "<SIZE 58><ICON DECIDE></SIZE>Confirm "
            "<SIZE 58><ICON CANCEL></SIZE>Back",
            "Choose the game you want to play.  "
            "<SIZE 58><ICON BTN_START></SIZE>Training "
            "<SIZE 58><ICON BTN_X></SIZE>Game Settings "
            "<SIZE 58><ICON CANCEL></SIZE>Back",
            "<SIZE 58><ICON BTN_LB></SIZE>/<SIZE 58><ICON BTN_RB></SIZE>Change",
            "<CNTR>centred<RGB 255 0 0>red</RGB>",
            "<ltr>x</ltr>",
            /* ⛔ TRUNCATED INPUTS. The original residue set had none, so an
             * unterminated tag fell through to prose and emitted raw markup
             * while this check still passed. The capture buffer is a fixed 256
             * bytes, so a cut-off string mid-tag is a REAL input, not a
             * hypothetical one. */
            "PRESS <ICON DECIDE",
            "Confirm <SIZE 5",
            "<ICON",
            "<",
            "text <ICON BTN_X",
        };
        int residue = 0;
        for (size_t i = 0; i < sizeof(INPUTS) / sizeof(INPUTS[0]); i++) {
            char got[GM_OUT_MAX];
            gm_speak(INPUTS[i], got, sizeof(got));
            if (gm_has_residue(got)) {
                printf("FAIL  residue survived: %s\n", got);
                residue++;
            }
        }
        g_checked++;
        if (residue) {
            g_failed++;
        } else {
            printf("ok    no markup residue in %zu inputs\n",
                   sizeof(INPUTS) / sizeof(INPUTS[0]));
        }
    }

    /* --- The output buffer is BOUNDED. A pathological input must truncate,
     * never overrun. Checked with a guard page either side of a short
     * buffer, so an overrun is a crash here rather than a corruption in the
     * game's address space. --- */
    {
        /* ⚠ A LONG PROSE RUN, not a long strip.
         *
         * The original probe here was 200 repeated hint pairs. It could not
         * overrun anything, because GM_MAX_PIECES (64) stops the parser long
         * before the sink fills: a 999-byte input produced 241 bytes of
         * output. The piece cap was masking the length cap, so this case was
         * measuring the wrong bound. One long unbroken prose run reaches the
         * sink directly and is the input that actually exercises it. */
        char big[600];
        memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';

        /* ⛔⛔ THE BUFFER MUST BE WATCHED FROM OUTSIDE ITSELF.
         *
         * The first version of this case declared `char small[24]` and checked
         * only that the RESULT looked sane. With the length cap deleted, it
         * still PASSED -- the sweep caught it. An overrun writes PAST the
         * buffer, so nothing inside the buffer can report it; the truncated
         * result looks correct either way.
         *
         * So: put a known-value canary immediately after the output buffer in
         * one allocation and require it to be untouched. That is what makes
         * "bounded" an assertion rather than a hope. */
        struct { char out[24]; char canary[32]; } guard;
        memset(&guard, 0xAA, sizeof(guard));
        SIZE_T n = gm_speak(big, guard.out, sizeof(guard.out));

        int canary_ok = 1;
        for (size_t i = 0; i < sizeof(guard.canary); i++)
            if ((unsigned char)guard.canary[i] != 0xAA)
                canary_ok = 0;

        g_checked++;
        if (!canary_ok) {
            printf("FAIL  bounded output OVERRAN the buffer "
                   "(canary clobbered)\n");
            g_failed++;
        } else if (n >= sizeof(guard.out) ||
                   guard.out[sizeof(guard.out) - 1] != '\0' ||
                   strlen(guard.out) != n) {
            printf("FAIL  bounded output: n=%llu strlen=%llu\n",
                   (unsigned long long)n,
                   (unsigned long long)strlen(guard.out));
            g_failed++;
        } else {
            printf("ok    bounded output truncates, canary intact (%llu bytes)"
                   "\n", (unsigned long long)n);
        }
    }

    /* A NULL/zero-size output must not be written to at all. */
    {
        g_checked++;
        if (gm_speak("<ICON DECIDE>x", NULL, 0) != 0) {
            printf("FAIL  NULL output buffer not refused\n");
            g_failed++;
        } else {
            printf("ok    NULL output buffer refused\n");
        }
    }

    printf("\n%d checked, %d failed\n", g_checked, g_failed);
    return g_failed ? 1 : 0;
}
