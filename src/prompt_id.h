/*
 * Identify a captured GUI string: which GMD label produced it?
 *
 * WHY THIS EXISTS
 * ---------------
 * ~14 shell screens speak position only ("3 of 6") because the mod cannot tell
 * WHICH screen the user is on. The live setText channel (src/gui_text.c) gives
 * the screen's own text, and the corpus lets that text be traced back to the
 * label that produced it.
 *
 * ⭐⭐ WHAT IT ACTUALLY ANSWERS -- and this corrects the plan it was built for.
 *
 * docs/rethink-after-gui-text.md reasoned that "Choose the game you want to
 * play." NAMES the Select Game screen. It does -- but not as a screen title.
 * CFC2's help strings are PER-ROW. That string is the help text for the row
 * `GAME_TITLE`, and its two siblings on the same screen are:
 *
 *     HELP_GAME_TITLE     "Choose the game you want to play."
 *     HELP_GAME_VERSION   "Choose a game version."
 *     HELP_PLAYER         "Choose a player number."
 *
 * The main menu likewise has a different caption per row ("Play offline.",
 * "Play online using a network connection.", ...).
 *
 * ⭐ That is BETTER than a screen title, not worse: it is a description of the
 * item the user is standing on, already authored by Capcom for essentially
 * every menu item -- the "verbose description" a screen reader would offer.
 *
 * ⚠ But it means a hit identifies a ROW, and implies the SCREEN only by
 * membership. Speaking it as a screen NAME is a claim the evidence does not
 * support, and this project has already shipped a false spoken claim that way
 * (memory: cfc2-settings-screen-shape).
 *
 * ⛔ WHY THE OBVIOUS NAME-DERIVED JOIN IS NOT USED
 * "The caption for item label X is HELP_X" holds for only 8 of the 14 item
 * labels on the three screens whose labels are known: Options' rows are
 * `MENU_OP_SYSTEM` but their captions are `HELP_OP_SYSTEM`, and `MENU_OP_NET`'s
 * caption is `HELP_OP_NETWORK`, which no string transformation derives. Right
 * often enough to look correct in testing, wrong often enough to lie in use.
 * The lookup is by exact TEXT instead, which needs no such rule.
 *
 * ⭐ AMBIGUITY IS A RETURN VALUE. Measured over the corpus: 254 distinct prompt
 * texts, of which 6 map to more than one screen even after platform variants
 * (_XB/_PS/_NX_*) are collapsed. Those report PROMPT_ID_AMBIGUOUS so the mod
 * can stay silent rather than pick. See inconclusive-must-be-a-return-value.
 *
 * SAFETY: pure string comparison over a caller-owned buffer. No allocation, no
 * game memory, no locks.
 */
#ifndef CFC2_PROMPT_ID_H
#define CFC2_PROMPT_ID_H

#include <windows.h>

typedef enum {
    PROMPT_ID_NONE = 0,   /* no entry matched this text                     */
    PROMPT_ID_FOUND,      /* exactly one label produces it                  */
    PROMPT_ID_AMBIGUOUS   /* several screens share it -- do NOT name one    */
} prompt_id_result;

typedef struct {
    prompt_id_result result;
    const char *label;    /* the GMD label, or NULL                         */
    int         index;    /* its GMD index, or -1                           */
} prompt_id;

/* Identify a captured string.
 *
 * ⚠ Matches the RAW captured text, markup and all -- that is what the table is
 * built from, and it avoids depending on the translator's output being stable.
 * Leading and trailing whitespace is ignored, because the shell pads some
 * strips.
 *
 * ⭐ Never guesses: an unmatched string returns PROMPT_ID_NONE, and a string
 * several screens share returns PROMPT_ID_AMBIGUOUS with label == NULL. */
prompt_id prompt_identify(const char *captured);

/* How many entries the table holds. Lets a caller log a POSITIVE count of what
 * was searched, so "no match" is distinguishable from "nothing to match
 * against" (memory: probe-zero-checked-looks-like-zero-failed). */
int prompt_table_size(void);

#endif /* CFC2_PROMPT_ID_H */
