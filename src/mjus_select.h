#ifndef CFC2_MJUS_SELECT_H
#define CFC2_MJUS_SELECT_H
#include <windows.h>
#include "game_selection.h"

/* Project Justice (game 6, ROMs 12 mjus / 13 pjus): the Story/Free menu, the
 * story school list and free character select. Evidence:
 * docs/port-tasks/mjus-selection.md. Read-only, worker thread only. */

#define MJUS_STATE_SIZE  0x9E8A8u
#define MJUS_TASKS_OFF   0x93F30u   /* 16 tasks, stride 0x18: +0 state word, +5 routine,
                                       +7 screen state, +8 step (dispatcher 0x140B93B60) */
#define MJUS_TASK_COUNT  16u
#define MJUS_TASK_STRIDE 0x18u
#define MJUS_SELECT_ROUTINE 6u      /* table 0x141787260 entry 6 = 0x140BA1230 */
#define MJUS_MENU_OFF    0x22F0u    /* +0 cursor, +1 option count, +2 confirm step */
#define MJUS_HUMANS_OFF  0x2029u    /* bit 0 = side 1, bit 1 = side 2 */
#define MJUS_SIDE_OFF    0x2500u    /* per-side records, stride 0x80 */
#define MJUS_SIDE_STRIDE 0x80u
#define MJUS_ID_LIMIT    0x42u      /* the shell's MJUS name bound */

#define MJUS_PHASE_OFF   0x21C0u    /* game phase: 1 menus/select/story, 2 fighting */
#define MJUS_TEAM_OFF    0x225Eu    /* story team choice: +0 human side 1/2, +2 count,
                                       +3.. the offered character IDs */
#define MJUS_TEAM_SIZE   12u
#define MJUS_PICK_OFF    0x230Bu    /* +0 picks made, +1 cursor (0xFE idle),
                                       +2.. picked flag per entry */
#define MJUS_PICK_SIZE   12u

typedef struct {
    BYTE phase;
    BYTE team[MJUS_TEAM_SIZE];
    BYTE pick[MJUS_PICK_SIZE];
    BYTE task_state, task_step;     /* routine-6 task +7, +8; 0xFF when absent */
    BYTE menu[3];                   /* R+0x22F0 cursor, count, step */
    BYTE humans;
    BYTE sides[2][0x30];            /* R+0x2500 / R+0x2580, first 0x30 bytes each */
    unsigned variant;               /* shell game-version setting */
} mjus_select_sample;

enum { MJUS_KIND_CHARACTER, MJUS_KIND_MODE, MJUS_KIND_SCHOOL };
#define MJUS_MENU_OWNER 2u

/* Native character ID highlighted by `side` in free select, or -1 not
 * selecting / -2 selecting but unreadable. */
int mjus_select_decode(const mjus_select_sample *s, unsigned side);
/* Story-chapter teammate choice: the highlighted native ID for `side`, or -1
 * not choosing / -2 choosing but unreadable. */
int mjus_team_decode(const mjus_select_sample *s, unsigned side);
/* Teammates picked so far in that choice. */
unsigned mjus_team_picked(const mjus_select_sample *s);
/* The Story/Free menu: 0 Story, 1 Free; -1 not on it. */
int mjus_mode_decode(const mjus_select_sample *s);
/* The story school list: 0..6 in the game's order; -1 not on it, -2 unreadable. */
int mjus_school_decode(const mjus_select_sample *s);
void mjus_selection_copy(const mjus_select_sample *s,
                         game_selection_choice out[GAME_SELECTION_OWNERS]);
/* NULL = not admitted: depart. */
void mjus_select_observe(const mjus_select_sample *s, ULONGLONG root, unsigned rom);
void mjus_select_tick(void);
#endif
