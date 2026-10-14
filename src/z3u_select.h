#ifndef CFC2_Z3U_SELECT_H
#define CFC2_Z3U_SELECT_H
#include <windows.h>
#include "game_selection.h"

/* Street Fighter Alpha 3 (game 3, ROMs 6 z3u / 7 a3u, the ordinary branch)
 * character, ism and speed speech. Native evidence:
 * docs/port-tasks/z3u-selection.md; names: docs/port-tasks/character-names.md.
 * Read-only, worker thread only. The alternate branch (z3uu/a3uu) has its own
 * root and layout and is not admitted. */

#define Z3U_STATE_SIZE  0x12F98u
#define Z3U_OBJ_OFF     0x1B0u       /* player objects, side 0 first           */
#define Z3U_OBJ_STEP    0x550u
#define Z3U_ID_LIMIT    38u          /* grid and random pool draw 0..37 only   */

/* Per-side fields copied from the player object. */
enum { Z3U_STATE, Z3U_SUB, Z3U_SIDE, Z3U_ID, Z3U_RANDOM, Z3U_ISM, Z3U_FIELDS };

/* The owned copy the decoder works on: only what selection needs. */
typedef struct {
    BYTE mode[6];        /* R+0x28 task mode, +0x2A game mode, +0x2C screen (words) */
    BYTE entered;        /* R+0x59: bit (1 << side) = side is in the game       */
    BYTE joined;         /* R+0x69: bit (1 << side) = side has finished select  */
    BYTE speed;          /* low byte of R+0x3C: the speed menu's cursor         */
    BYTE obj[2][Z3U_FIELDS];
    unsigned variant;    /* shell game-version setting: 0 Japanese ROM, 1 US ROM */
} z3u_select_sample;

/* Selection kinds spoken through the shared channel. */
enum { Z3U_KIND_CHARACTER, Z3U_KIND_ISM, Z3U_KIND_SPEED };

/* For `side`: the kind being chosen and its value, or -1 not choosing /
 * -2 choosing but unreadable. Character value 0xFF is the random cell. */
int z3u_select_decode(const z3u_select_sample *s, unsigned side, unsigned *kind);
/* Owned labels for the shared selection channel. */
void z3u_selection_copy(const z3u_select_sample *s,
                        game_selection_choice out[GAME_SELECTION_OWNERS]);
/* NULL = Alpha 3 not admitted: depart. */
void z3u_select_observe(const z3u_select_sample *s, ULONGLONG root, unsigned rom);
void z3u_select_tick(void);
#endif
