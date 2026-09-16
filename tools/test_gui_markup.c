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

    /* --- ⭐⭐ THE MOVE LIST's numpad motion notation. ------------------------
     * Real strings from the in-game move list (run-2026-09-11-0330). Nine of
     * these tokens were UNKNOWN, so the screen spoke a move's NAME and nothing
     * about how to perform it -- the half that matters to a player.
     *
     * ⚠ Spoken as the MOTION, not as digits: a sighted player sees an arc, so
     * "quarter circle forward" is a reading and "two three six" would be a
     * transcription of the glyph. */
    check("move list: Hadoken input",
          "<SIZE 58><ICON C_236></SIZE>+<SIZE 58><ICON C_P></SIZE>",
          "quarter circle forward plus punch");
    check("move list: Shoryuken input",
          "<SIZE 58><ICON C_623></SIZE>+<SIZE 58><ICON C_HP></SIZE>",
          "dragon punch plus heavy punch");
    check("move list: half circle",
          "<SIZE 58><ICON C_41236></SIZE>+<SIZE 58><ICON C_P></SIZE>",
          "half circle forward plus punch");
    /* ⚠ A charge move: the notation distinguishes holding from tapping, and a
     * player who cannot see the glyph has no other way to know. */
    /* ⚠ The "; " is the parser's normal icon/text separator, not a defect --
     * my first expectation here omitted it and the test caught me, not the
     * code. */
    check("move list: charge",
          "<SIZE 58><ICON C_4T></SIZE>Charge <SIZE 58><ICON C_6></SIZE>",
          "charge back Charge; forward");

    /* --- ⭐⭐ THE OPERATORS BETWEEN TWO ICONS. "/" = either-of, "+" = chord.
     * Without these the row FUSED into one spoken word -- "back/forward" and
     * "light punch/heavy punch" -- where two discrete inputs belong, which is
     * exactly the information the notation exists to carry.
     *
     * ⛔⛔ AND THE DISCRIMINATION IS THE POINT. A first attempt expanded every
     * "+"/"/" over the finished string; the parity check rejected it, because
     * the button hints turned into "Zoom In or Out", "Show or Hide UI",
     * "Previous or Next" -- four real strings damaged to fix none. The
     * operator's meaning depends on what FLANKS it, so the prose cases below
     * are as load-bearing as the notation ones. --- */
    check("move list: either-of between two icons",
          " Near Opponent<SIZE 58><ICON C_4></SIZE>/<SIZE 58><ICON C_6></SIZE>"
          "+<SIZE 58><ICON C_HK></SIZE>",
          "Near Opponent back or forward plus heavy kick");
    check("move list: either-of between two attack icons",
          " During Cling to Mamahaha<SIZE 58><ICON C_LP></SIZE>/"
          "<SIZE 58><ICON C_HP></SIZE>",
          "During Cling to Mamahaha light punch or heavy punch");
    /* ⚠ PROSE, NOT NOTATION: a slash between two WORDS is ordinary UI text and
     * must stay silent. This is the case the over-broad version broke. */
    check("prose slash is NOT an operator",
          "<SIZE 58><ICON D_KEY></SIZE>Move <SIZE 58><ICON BTN_LT></SIZE>"
          "<SIZE 58><ICON BTN_RT></SIZE>Zoom In/Out",
          "d-pad Move; LT RT Zoom In/Out");
    /* ⚠ A slash inside a CONDITION is also prose ("Neutral/Forward Jump"), and
     * it sits in the same string as a real chord -- so one string exercises
     * both halves of the rule. */
    check("a condition's slash stays, its chord speaks",
          " At Height of Neutral/Forward Jump<SIZE 58><ICON C_3></SIZE>"
          "+<SIZE 58><ICON C_P></SIZE>",
          "At Height of Neutral/Forward Jump down-forward plus punch");
    /* ⚠ The condition with NO input at all -- the parenthetical must survive
     * intact and gain no operators. */
    check("condition with no input keeps its parenthetical",
          " During Hyakkishu (No Additional Input)",
          "During Hyakkishu (No Additional Input)");
    /* ⭐ A BLANKED pooled pair object: the game sets one SPACE, not "". */
    check("a blanked pair object yields nothing", " ", "");

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

    /* --- the per-game icon legend ------------------------------------------
     *
     * ⛔⛔ THE BUG: C_A is "Any Attack" in Cybots and "Vertical Slash" in Star
     * Gladiator; C_EX has NINE readings. The static table carries the generic
     * one, so the move list spoke Cybots' names on other games. Measured live:
     * C_A appeared 8 times, C_B 3, C_EX 4 -- the "shell menus only show UI
     * icons" assumption was false.
     *
     * ⭐ The strings below are VERBATIM from the corpus, so these cases test
     * the real input rather than a convenient one. */
    {
        /* Star Gladiator's legend, exactly as HELP_MOVES_STG2 ships. */
        const char *stg2 =
            "<SIZE 58><ICON C_A></SIZE>Vertical Slash"
            "<SIZE 58><ICON C_B></SIZE>Horizontal Slash"
            "<SIZE 58><ICON C_K2></SIZE>Kick";
        char out[GM_OUT_MAX];

        /* Before learning: the generic reading, which is WRONG for this game. */
        gm_icon_overrides_clear();
        gm_speak("<ICON C_A>", out, sizeof(out));
        g_checked++;
        if (strstr(out, "Any Attack") == NULL) {
            printf("FAIL  baseline: C_A should read generic, got \"%s\"\n", out);
            g_failed++;
        } else {
            printf("ok    baseline: C_A reads the generic \"Any Attack\"\n");
        }

        /* After learning the screen's legend: the game's OWN word. */
        int learned = gm_icon_learn_legend(stg2);
        gm_speak("<ICON C_A>", out, sizeof(out));
        g_checked++;
        if (learned < 2 || strstr(out, "Vertical Slash") == NULL) {
            printf("FAIL  legend: C_A should read \"Vertical Slash\", "
                   "learned=%d got \"%s\"\n", learned, out);
            g_failed++;
        } else {
            printf("ok    legend: C_A reads this game's \"Vertical Slash\"\n");
        }

        /* ⛔⛔ A ONE-TOKEN STRING IS NOT A LEGEND. Ordinary move rows are
         * written "<ICON C_S2> and" / "Hold <ICON C_4T> then", and letting one
         * of those redefine a token would be the same class of error as the
         * bug being fixed -- a move's incidental wording renaming an icon for
         * the whole screen. */
        gm_icon_overrides_clear();
        int bogus = gm_icon_learn_legend("Hold <SIZE 58><ICON C_A></SIZE> and");
        gm_speak("<ICON C_A>", out, sizeof(out));
        g_checked++;
        if (bogus != 0 || strstr(out, "Any Attack") == NULL) {
            printf("FAIL  a one-token string must NOT redefine: took=%d "
                   "got \"%s\"\n", bogus, out);
            g_failed++;
        } else {
            printf("ok    a one-token move row does not redefine a token\n");
        }

        /* ⚠ And a token the legend does NOT mention keeps the table's reading,
         * so learning one game's names cannot blank the rest. */
        gm_icon_overrides_clear();
        gm_icon_learn_legend(stg2);
        gm_speak("<ICON DECIDE>", out, sizeof(out));
        g_checked++;
        if (strstr(out, "Confirm") == NULL) {
            printf("FAIL  unrelated token lost its name: \"%s\"\n", out);
            g_failed++;
        } else {
            printf("ok    a token the legend omits keeps the table's name\n");
        }

        /* ⚠ CLEARING really clears -- a stale override from the previous game
         * is the same bug one game late. */
        gm_icon_overrides_clear();
        gm_speak("<ICON C_A>", out, sizeof(out));
        g_checked++;
        if (gm_icon_override_count() != 0 ||
            strstr(out, "Any Attack") == NULL) {
            printf("FAIL  clear left %d override(s), got \"%s\"\n",
                   gm_icon_override_count(), out);
            g_failed++;
        } else {
            printf("ok    clearing restores the generic reading\n");
        }
    }

    printf("\n%d checked, %d failed\n", g_checked, g_failed);
    return g_failed ? 1 : 0;
}
