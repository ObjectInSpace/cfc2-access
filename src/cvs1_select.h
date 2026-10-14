#ifndef CFC2_CVS1_SELECT_H
#define CFC2_CVS1_SELECT_H
#include <windows.h>
#include "game_selection.h"

/* CVS1 (game 0, ROMs 0 cvs1 / 1 cvs1u) character-highlight speech.
 * Native evidence: docs/cvs1-selection-visits.md, docs/cvs1-branch-a-selection.md;
 * names: docs/port-tasks/character-names.md. Read-only, worker thread only. */

#define CVS1_STATE_SIZE  0x32AB0u
#define CVS1_PHASE_OFF   0x3E58u     /* P = R + 0x3E58                     */
#define CVS1_PHASE_SIZE  0x112u
#define CVS1_ACTOR_OFF   0x3FB8u     /* actor[side] = R + 0x3FB8 + side*0x700 */
#define CVS1_ACTOR_STEP  0x700u
#define CVS1_ACTOR_HEAD  0x38u
#define CVS1_ACTOR_OWNER 0x688u

/* The owned copy the decoder works on: only what selection needs. */
typedef struct {
    BYTE phase[CVS1_PHASE_SIZE];
    BYTE actor[2][CVS1_ACTOR_HEAD];
    BYTE owner[2];
    unsigned variant;    /* shell game-version setting: 0 Japanese ROM, 1 US ROM */
} cvs1_select_sample;

/* Native ID for `side`, or -1 not selecting / -2 selecting but unreadable. */
int cvs1_select_decode(const cvs1_select_sample *s, unsigned side);
/* Groove for `side`: 0 Capcom, 1 SNK, or -1 not choosing / -2 unreadable. */
int cvs1_groove_decode(const cvs1_select_sample *s, unsigned side);
/* Owned labels and visits for the shared selection channel. */
void cvs1_selection_copy(const cvs1_select_sample *s,
                         game_selection_choice out[GAME_SELECTION_OWNERS]);
/* NULL = CVS1 not admitted: depart. */
void cvs1_select_observe(const cvs1_select_sample *s, ULONGLONG root, unsigned rom);
void cvs1_select_tick(void);
#endif
