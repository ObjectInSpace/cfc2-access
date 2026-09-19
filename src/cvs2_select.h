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
 *   max health  B+0x170   per-character: 11520 Ryu, 11200 for P2's fighter
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
