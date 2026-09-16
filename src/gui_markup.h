/*
 * Turn the shell's live GUI markup into speech-ready text.
 *
 * WHY THIS EXISTS
 * ---------------
 * The setText hook (src/gui_text.c) captures the shell's on-screen text as
 * UTF-8 with inline engine markup:
 *
 *     "<FONT 2>PRESS <SIZE 80><ICON DECIDE></SIZE> TO START</FONT>"
 *     "<SIZE 58><ICON DECIDE></SIZE>Confirm <SIZE 58><ICON CANCEL></SIZE>Back"
 *
 * Spoken raw, that is tag soup. This module is the C half of the translation
 * that tools/markup.py performs offline -- same rules, same expected output,
 * cross-checked against it by tools/test_gui_markup.c.
 *
 * ⭐ THE PYTHON IS THE ORACLE, NOT A PARALLEL IMPLEMENTATION. markup.py is
 * verified against the whole corpus (58310 strings, 14 languages, 0 residue)
 * and against the real live captures. The expected strings in the C test are
 * taken FROM it, so the two cannot drift silently.
 *
 * ⭐⭐ WHAT RUNNING IT AGAINST REAL CAPTURES CHANGED
 * The offline-audited path was NOT sufficient for the live channel, and the
 * two defects could not have been found by auditing the GMD:
 *
 *  1. <FONT> occurs ZERO times in the corpus -- all 58310 strings, all 14
 *     languages -- and DOES occur in the live captures. The live vocabulary is
 *     a SUPERSET of the file's.
 *  2. A button hint strip is a run of `<ICON button>Action` pairs, and the
 *     button is NAMED AFTER WHAT IT DOES, so the icon's spoken name and the
 *     printed action are the same word:
 *         "<ICON DECIDE>Confirm"  ->  "Confirm Confirm"
 *     Measured: 25 such pairs in the corpus, concentrated in exactly the
 *     HELP_BUTTON_* / HB_* labels this channel captures.
 *
 * ⚠ TWO SHAPES ARE NOT PAIR BOUNDARIES, both measured in the corpus and both
 * mangled by a naive per-icon split:
 *     "<ICON BTN_LB>/<ICON BTN_RB>Change Game"  -> "LB or RB Change Game"
 *     "<ICON BTN_LT>+<ICON D_KEY>Move 100"      -> "LT plus d-pad Move 100"
 * An icon whose gap to the next is only "/" or "+" belongs to the SAME hint.
 *
 * ⚠ The stutter is dropped by PREFIX and only the duplicated word:
 * "<ICON CANCEL>Cancel Ready" stays "Cancel Ready", not "Ready".
 *
 * SAFETY
 * ------
 * Pure string handling over a caller-owned buffer. No allocation, no game
 * memory, no locks, bounded output. It never runs in a detour -- gui_text.c
 * copies bytes in the hook and this runs later on the worker thread.
 */
#ifndef CFC2_GUI_MARKUP_H
#define CFC2_GUI_MARKUP_H

#include <windows.h>

/* Longest spoken line we will produce. The capture buffer is 256 bytes and
 * translation can only shrink a string (tags out, short words in), but the
 * output is bounded explicitly rather than by that argument. */
#define GM_OUT_MAX 512

/* Spoken form of one ICON token, or NULL if the token is unknown.
 *
 * ⚠ Returns a pointer to static storage for table hits, and writes into `tmp`
 * for the mechanical KB_ rule (KB_A -> "A", KB_F10 -> "F10"), so `tmp` must
 * stay alive as long as the result is used. */
const char *gm_icon(const char *token, char *tmp, SIZE_T tmpsz);

/* Translate a captured string into speech-ready text.
 *
 * Handles both shapes the live channel produces -- ordinary prose with inline
 * icons ("PRESS Confirm TO START") and button hint strips ("Confirm; Cancel
 * Back") -- because the strip form is detected from the text itself.
 *
 * Returns the length written. `out` is always NUL-terminated.
 *
 * ⭐ Unknown ICON tokens are rendered as [TOKEN] rather than dropped: losing
 * an icon silently destroys meaning (an input sequence becomes nothing at
 * all), and a visible token is a bug report from the field. */
SIZE_T gm_speak(const char *in, char *out, SIZE_T outsz);

/* ⭐⭐ PER-GAME ICON NAMES, LEARNED FROM THE SCREEN'S OWN LEGEND.
 *
 * ⛔ Three tokens mean different attacks in different games: C_A is "Any
 * Attack" in Cybots and "Vertical Slash" in Star Gladiator, and C_EX has NINE
 * readings. The static table carries the generic one, so the move list spoke
 * Cybots' names on other games -- a confident wrong reading.
 *
 * ⭐ The game prints its own legend on that screen ("<ICON C_A>Vertical
 * Slash<ICON C_B>Horizontal Slash…") and it is co-resident with the moves, so
 * the right names are READ rather than guessed -- and they follow the game's
 * language for free.
 *
 * Usage: clear, then feed each candidate string; gm_icon() prefers what was
 * learned. ⚠ Clear on every screen change: a stale override from the previous
 * game is the same bug, one game late. */
void gm_icon_overrides_clear(void);
BOOL gm_icon_override_add(const char *token, const char *say);
int  gm_icon_override_count(void);

/* Parse one legend string; returns how many names it defined, 0 if the string
 * is not a legend. ⚠ Requires TWO OR MORE definitions: "<ICON C_S2> and" is an
 * ordinary move row, and letting one of those redefine a token would be the
 * same class of error this exists to fix. */
int  gm_icon_learn_legend(const char *s);

/* TRUE if `s` still contains a markup tag. The shipping speech path asserts on
 * this: no tag may ever reach the user.
 *
 * ⭐ This is a RETURN VALUE, not a logged warning -- if the code notices
 * residue, something must fail on it. */
BOOL gm_has_residue(const char *s);

#endif /* CFC2_GUI_MARKUP_H */
