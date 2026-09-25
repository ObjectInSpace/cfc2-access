#ifndef CFC2_CVS2_SELECT_H
#define CFC2_CVS2_SELECT_H
#include <windows.h>
#define CVS2_STATE_SIZE 0x216B0u
/* -1 inactive, -2 inconsistent/special; otherwise an ordinary character ID. */
int cvs2_select_decode(const BYTE *state, SIZE_T size, unsigned player);
/* Groove index 0..5, or -1 inactive / -2 unsupported custom groove. */
int cvs2_groove_decode(const BYTE *state, SIZE_T size, unsigned player);
/* Team layouts 0..5: (team size - 2)*3 + layout index.
 * Individual ratios 1..4: returns 6..9. Negative values are status. */
int cvs2_ratio_decode(const BYTE *state, SIZE_T size, unsigned player);
/* Shared stage ID 0..10, 11 random; only the traced custom-menu path. */
int cvs2_stage_decode(const BYTE *state, SIZE_T size);
void cvs2_select_observe(const BYTE *state, SIZE_T size, ULONGLONG root);
void cvs2_select_tick(void);
#endif

/* ---- In-match gauges ----------------------------------------------------
 * Feeds audio.c from the SAME validated state block the selection reader uses
 * (see docs/cvs2-state-access-trace.md).
 *
 *   health      B+0x16C   (B = state + 0x1EBA8 + player*0x678)
 *   shadow bar  B+0x16E   the delayed/drain bar -- lags behind +0x16C
 *   max health  B+0x170   per-fighter AND per-RATIO -- read live, never hardcode
 *
 * ⚠ +0x16E sits BETWEEN them and usually EQUALS current health, so reading
 * +0x16C/+0x170 as an adjacent u16 pair gives (health, shadow) and a fill
 * fraction stuck at 1.00. They are 4 bytes apart. `state-b` separates them:
 * hp 2406, shadow 3108, max 11520.
 *
 * ⭐⭐ IDENTIFIED BY THE USER IN TRAINING MODE, which is the only evidence
 * here that could tell health from the super meter:
 *   spending a super -> nothing moved; the meter refilling -> nothing moved;
 *   hitting the opponent -> THEIR value dropped.
 *
 * ⛔ This shipped first as the SUPER METER on my reading of 0x1406C26A0 (adds
 * in units of 100) and 0x1406C4241 (subtracts). Damage and recovery have the
 * same shape, so those instructions never distinguished the two. Nor did the
 * "P1 moves while P2 holds" check across 7 captures -- that is equally true of
 * one player taking damage. See the long note in cvs2_select.c.
 *
 * ⚠ THE SUPER METER IS NOT LOCATED. Find the field that moves when a super is
 * SPENT and refills over time; do not re-derive it from these instructions.
 * Stun is not located either. Do not invent addresses for either.
 *
 * Pass state=NULL when CVS2 is not active, so the gauges reset rather than
 * holding a stale value. */
void cvs2_gauges_observe(const BYTE *state, SIZE_T size, ULONGLONG root);

/* Log fed/rejected counts when they change. A silent mod with no counters
 * cannot distinguish "never ran" from "ran and rejected everything". */
void cvs2_gauges_log_stats(void);

/* Speech OUTSIDE the fight: team order + groove when a match starts, and the
 * fighter + ratio when a round starts. Gauges remain audio cues -- the design
 * forbids narrating DURING play, which these do not do. */
void cvs2_announce_observe(const BYTE *state, SIZE_T size, ULONGLONG root);

/* ---- Win quotes ---------------------------------------------------------
 * The post-match win screen: speak the winning character's quote.
 *
 * STATE GATE: phase bytes at state+0x78 == 02 01 02 03. Across all 36 captures
 * in build/cvs2-state, exactly the two taken while a quote was on screen carry
 * that value; every in-match capture is 02 01 02 02 and every selection screen
 * 02 01 01 01. The quote screen is a SIBLING of the match state, not a
 * sub-state, so a win quote cannot occur during a round.
 *
 * WINNER: R+0x05931 holds the winning character's ID inside that gate (Sagat 9,
 * Ken 1, both user-confirmed, two independent sessions/arenas).
 *
 * ⚠ That byte is NOT self-gating -- it reads 16 or 1 in nine in-match captures.
 * It is only meaningful once the phase gate has passed, which is why the gate
 * is checked first and never inferred from this field.
 *
 * ⛔ THE 1-IN-8 INDEX IS NOT LOCATED. CVS2 picks one of the character's eight
 * quotes at RANDOM at display time. Byte +2 of the record (R+0x05933) is a
 * LEAD only: it decodes to a coherent quote in both captures, but any value
 * 0..7 would, and its apparent corroboration (a second record agreeing) occurs
 * in 10 of 32 non-quote captures too, so that agreement carries little
 * information. Speaking the wrong quote is indistinguishable from the right one
 * to this user.
 *
 * Therefore the index is gated on cfc2_cvs2_quote_index.on. Absent (the
 * default) the mod speaks only the WINNER -- true, useful, and never wrong.
 * Present, it additionally speaks the quote chosen by the lead, for the one
 * experiment that would settle it: the same character winning twice. If +2
 * varies across those wins it is the random pick; if it is constant it is a
 * property of the character and the lead is dead. */
void cvs2_quote_observe(const BYTE *state, SIZE_T size, ULONGLONG root);
