#ifndef CFC2_CVS2_SELECT_H
#define CFC2_CVS2_SELECT_H
#include <windows.h>
#include "game_selection.h"
#include "transition_channel.h"

/* CVS2 (game 1, ROMs 2 cvs2 / 3 cvs2u): selection speech, match-transition
 * announcements and win quotes, and the worker routing for match audio,
 * story and on-screen text. Native evidence: docs/cvs2-state-access-trace.md;
 * names: docs/port-tasks/character-names.md. Read-only, worker thread only.
 *
 * The owned copy is the whole state block (CVS2_STATE_SIZE bytes): CVS2's
 * selection, transition, quote and story readers all decode from it. The
 * acquisition path that admits it is unchanged from the traced one. */
#define CVS2_STATE_SIZE 0x216B0u

/* Selection kinds spoken through the shared channel. */
enum { CVS2_KIND_CHARACTER, CVS2_KIND_GROOVE, CVS2_KIND_RATIO, CVS2_KIND_STAGE,
       CVS2_KIND_LEVEL };
#define CVS2_STAGE_OWNER 2u          /* the stage menu is shared by both sides */

/* -1 inactive, -2 inconsistent/special; otherwise an ordinary character ID. */
int cvs2_select_decode(const BYTE *state, SIZE_T size, unsigned player);
/* Groove index 0..5, or -1 inactive / -2 unsupported custom groove. */
int cvs2_groove_decode(const BYTE *state, SIZE_T size, unsigned player);
/* Team layouts 0..5: (team size - 2)*3 + layout index.
 * Individual ratios 1..4: returns 6..9. Negative values are status. */
int cvs2_ratio_decode(const BYTE *state, SIZE_T size, unsigned player);
/* Shared stage ID 0..10, 11 random; only the traced custom-menu path. */
int cvs2_stage_decode(const BYTE *state, SIZE_T size);
/* Owned labels and visits for the shared selection channel. `variant` is
 * the shell's game-version setting (0 Japanese, 1 US), which picks the
 * regional character names. value is the kind's own decoded value (the
 * level is 0..3); the stage is on CVS2_STAGE_OWNER. */
void cvs2_selection_copy(const BYTE *state, SIZE_T size, unsigned variant,
                         game_selection_choice out[GAME_SELECTION_OWNERS]);
/* NULL = CVS2 not admitted (or its update is not running): depart. */
void cvs2_select_observe(const BYTE *state, SIZE_T size, unsigned variant,
                         ULONGLONG root, unsigned rom);
void cvs2_select_tick(void);

/* Speech OUTSIDE the fight: team order + groove when a match starts, and the
 * fighter + ratio when a round starts. Gauges remain audio cues -- the design
 * forbids narrating DURING play, which these do not do. */
void cvs2_announce_observe(const BYTE *state, SIZE_T size, unsigned variant,
                           ULONGLONG root);
/* The owned transition observation for an in-match state (no phase check).
 * Exposed so the replay suite can assert names, ratios and ownership. */
void cvs2_announce_observation(const BYTE *state, unsigned variant,
                               transition_observation *o);

/* Completed native draw buffers; see docs/cvs2-win-quote-reader.md.
 * Output capacity must be at least 161 wchar_t units. */
BOOL cvs2_quote_decode(const BYTE *state, SIZE_T size, wchar_t *out, SIZE_T capacity);
void cvs2_quote_observe(const BYTE *state, SIZE_T size, ULONGLONG root);
#endif
