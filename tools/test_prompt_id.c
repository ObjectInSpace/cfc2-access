/*
 * Self-test for src/prompt_id.c -- captured text -> GMD label.
 *
 * ⭐ THE DISCRIMINATING CASES ARE THE NEGATIVES. A matcher that returns a
 * plausible label for everything would pass a test made only of positives, and
 * that is precisely the failure this project keeps paying for: a row added on
 * plausibility made one screen speak another's labels AND suppressed the
 * discovery block that would have exposed it.
 *
 * So the cases that carry weight here are:
 *   - an AMBIGUOUS text must report ambiguity, not pick a screen;
 *   - an unknown text must report NONE, not a near match;
 *   - a blank/short text must report NONE.
 *
 * Build/run: tools\run_pi_test.ps1
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>

#include "../src/prompt_id.h"
/* The generated table, so the test can assert properties OF THE TABLE and not
 * only of the lookup that reads it. */
#include "../src/prompt_table.h"

static int g_failed;
static int g_checked;

static const char *rname(prompt_id_result r)
{
    switch (r) {
    case PROMPT_ID_NONE:      return "NONE";
    case PROMPT_ID_FOUND:     return "FOUND";
    case PROMPT_ID_AMBIGUOUS: return "AMBIGUOUS";
    }
    return "?";
}

static void expect(const char *what, const char *in,
                   prompt_id_result want_result, const char *want_label)
{
    prompt_id got = prompt_identify(in);
    g_checked++;

    BOOL ok = (got.result == want_result);
    if (ok && want_label) {
        ok = (got.label != NULL) && strcmp(got.label, want_label) == 0;
    }
    if (ok && want_result == PROMPT_ID_AMBIGUOUS) {
        /* ⛔ Ambiguity must carry NO label: returning the representative would
         * be exactly the confident wrong answer this exists to prevent. */
        ok = (got.label == NULL) && (got.index == -1);
    }

    if (ok) {
        printf("ok    %-34s %s %s\n", what, rname(got.result),
               got.label ? got.label : "");
    } else {
        printf("FAIL  %-34s got %s %s\n", what, rname(got.result),
               got.label ? got.label : "(null)");
        printf("      wanted %s %s\n", rname(want_result),
               want_label ? want_label : "(no label)");
        g_failed++;
    }
}

int main(void)
{
    printf("prompt_id self-test\n");
    printf("-------------------\n");
    printf("table holds %d entries\n\n", prompt_table_size());

    /* ⭐ LIVENESS FIRST. An empty table would make every negative below pass
     * vacuously -- "0 matched" and "nothing to match against" must not look
     * alike (memory: probe-zero-checked-looks-like-zero-failed). */
    g_checked++;
    if (prompt_table_size() < 100) {
        printf("FAIL  table is implausibly small (%d) -- regenerate with "
               "tools/gen_prompt_table.py\n", prompt_table_size());
        g_failed++;
    } else {
        printf("ok    table is populated (%d entries)\n\n",
               prompt_table_size());
    }

    /* --- Positives: the real live capture, and the Select Game row set. --- */

    /* ⭐ THE ACTUAL LIVE CAPTURE from the 21:19 run. */
    expect("live: select game caption",
           "Choose the game you want to play.",
           PROMPT_ID_FOUND, "HELP_GAME_TITLE");

    /* Its two siblings on the SAME screen -- which is the evidence that these
     * captions are per-ROW, not per-screen. */
    expect("sibling row: game version",
           "Choose a game version.",
           /* ⛔ AMBIGUOUS: HELP_GAME_VERSION and HELP_GAME_VERSION_ON share
            * this text (offline vs online Select Game). Must not pick one. */
           PROMPT_ID_AMBIGUOUS, NULL);

    expect("sibling row: player",
           "Choose a player number.",
           PROMPT_ID_FOUND, "HELP_PLAYER");

    /* Main menu rows: a different caption each, same screen. */
    expect("main menu row: offline",
           "Play offline.", PROMPT_ID_FOUND, "HELP_OFFLINE_PLAY");
    expect("main menu row: online",
           "Play online using a network connection.",
           PROMPT_ID_FOUND, "HELP_ONLINE_PLAY");
    expect("main menu row: quit",
           "Exit the application.", PROMPT_ID_FOUND, "HELP_EXIT");

    /* Whitespace on either side must not defeat a match: the shell pads some
     * strips. Interior spacing is still exact. */
    expect("leading/trailing space ignored",
           "   Play offline.  ", PROMPT_ID_FOUND, "HELP_OFFLINE_PLAY");

    printf("\n");

    /* --- ⛔ THE DISCRIMINATING NEGATIVES. --- */

    expect("ambiguous: two lobby screens",
           "<SIZE 58><ICON CANCEL></SIZE>Cancel Ready",
           PROMPT_ID_AMBIGUOUS, NULL);

    expect("ambiguous: options vs system",
           "Change various in-game settings.",
           PROMPT_ID_AMBIGUOUS, NULL);

    expect("unknown text",
           "This string is not in the corpus at all.",
           PROMPT_ID_NONE, NULL);

    /* A NEAR miss must not match. Substring or fuzzy matching would turn every
     * partial capture into a confident wrong screen -- and the capture buffer
     * is only 256 bytes, so partial captures are real. */
    expect("near miss: truncated caption",
           "Choose the game you want to pl",
           PROMPT_ID_NONE, NULL);

    expect("near miss: superstring",
           "Choose the game you want to play. And then some.",
           PROMPT_ID_NONE, NULL);

    expect("blank", "   ", PROMPT_ID_NONE, NULL);
    expect("empty", "", PROMPT_ID_NONE, NULL);
    expect("NULL", NULL, PROMPT_ID_NONE, NULL);

    /* The excluded placeholder must not be matchable. */
    expect("blank placeholder not in table",
           " ", PROMPT_ID_NONE, NULL);

    /* ⭐ THE GENERATOR'S EXCLUSION, ASSERTED DIRECTLY.
     *
     * The runtime blank guard in prompt_identify() is currently REDUNDANT: the
     * generator already drops HELP_BUTTON_SPACE, so no table entry is short
     * enough for a blank capture to match. The sabotage sweep found this --
     * deleting the guard left the suite green.
     *
     * ⛔ But "redundant today" is not "safe to leave untested". The property
     * that actually protects the user is THE TABLE CONTAINING NO DEGENERATE
     * ENTRY, and nothing asserted it: a future regeneration that stopped
     * excluding blanks would make every short capture match a real label, and
     * every case above would still pass. So assert the table directly, and
     * keep the runtime guard as defence in depth for exactly that case. */
    g_checked++;
    {
        int degenerate = 0;
        for (int i = 0; i < PROMPT_ROW_COUNT; i++) {
            const char *p = PROMPT_ROWS[i].text;
            int visible = 0;
            for (; *p; p++)
                if (!isspace((unsigned char)*p))
                    visible++;
            if (visible < 3)
                degenerate++;
        }
        if (degenerate) {
            printf("FAIL  %d table entries are degenerate (<3 visible chars) "
                   "-- they would match noise\n", degenerate);
            g_failed++;
        } else {
            printf("ok    no degenerate entries in %d table rows\n",
                   PROMPT_ROW_COUNT);
        }
    }

    printf("\n%d checked, %d failed\n", g_checked, g_failed);
    return g_failed ? 1 : 0;
}
