#ifndef CFC2_CFJ_SELECT_H
#define CFC2_CFJ_SELECT_H
#include <windows.h>
#include "game_selection.h"

/* Capcom Fighting Evolution (game 2, ROMs 4 cfj / 5 cfe) character-highlight
 * speech. Native evidence: docs/port-tasks/cfj-selection.md; names:
 * docs/port-tasks/character-names.md. Read-only, worker thread only. */

#define CFJ_STATE_SIZE  0x26E80u
#define CFJ_PHASE_OFF   0x21960u     /* phase R+0x21960, select sub-state +0x21961 */
#define CFJ_HUMANS_OFF  0x2197Cu     /* bit (1 << side) = side is human-controlled */
#define CFJ_SIDES_OFF   0x26E00u     /* per-side select records, stride 0x10        */
#define CFJ_SIDES_SIZE  0x20u
#define CFJ_SELECT_COUNT 23u         /* select numbers 0..22                        */

/* The owned copy the decoder works on: only what selection needs. */
typedef struct {
    BYTE phase[2];
    BYTE humans;
    BYTE sides[CFJ_SIDES_SIZE];
    unsigned variant;    /* shell game-version setting: 0 Japanese ROM, 1 US ROM */
} cfj_select_sample;

/* Native character ID for `side`, or -1 not selecting / -2 selecting but
 * unreadable. */
int cfj_select_decode(const cfj_select_sample *s, unsigned side);
/* Stage select after both picks (Versus, Training): the menu entry, 0 Random
 * or 1..10 a stage, or -1 not on it / -2 unreadable. Shared by both sides,
 * spoken on owner CFJ_STAGE_OWNER. */
#define CFJ_STAGE_OWNER 2u
enum { CFJ_KIND_CHARACTER, CFJ_KIND_STAGE };
int cfj_stage_decode(const cfj_select_sample *s);
/* Owned labels and visits for the shared selection channel. */
void cfj_selection_copy(const cfj_select_sample *s,
                        game_selection_choice out[GAME_SELECTION_OWNERS]);
/* NULL = CFJ not admitted: depart. */
void cfj_select_observe(const cfj_select_sample *s, ULONGLONG root, unsigned rom);
void cfj_select_tick(void);
#endif
