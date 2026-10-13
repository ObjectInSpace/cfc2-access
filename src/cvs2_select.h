#ifndef CFC2_CVS2_SELECT_H
#define CFC2_CVS2_SELECT_H
#include <windows.h>
#include "game_selection.h"
#include "transition_channel.h"
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
/* Copy adapter-owned labels/visits for shared presentation. No native reads,
 * speech or lifecycle claims. value preserves the legacy combined selection
 * key; kind is character/groove/ratio/stage/level (0..4), local to this adapter.
 * Unused owner slots are explicitly inactive. */
void cvs2_selection_copy(const BYTE *state, SIZE_T size,
                         game_selection_choice out[GAME_SELECTION_OWNERS]);
/* Owned state only; ROM supplied by the existing admitted acquisition path. */
void cvs2_select_observe(const BYTE *state, SIZE_T size, ULONGLONG root, unsigned rom);
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
/* The owned transition observation for an in-match state (no phase check).
 * Exposed so the replay suite can assert names, ratios and ownership. */
void cvs2_announce_observation(const BYTE *state, transition_observation *o);

/* Completed native draw buffers; see docs/cvs2-win-quote-reader.md.
 * ASCII/English only. Output capacity must be at least 161 wchar_t units. */
BOOL cvs2_quote_decode(const BYTE *state, SIZE_T size, wchar_t *out, SIZE_T capacity);
void cvs2_quote_observe(const BYTE *state, SIZE_T size, ULONGLONG root);
