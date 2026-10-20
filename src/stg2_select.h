#ifndef CFC2_STG2_SELECT_H
#define CFC2_STG2_SELECT_H
#include <windows.h>
#include "game_selection.h"
/* Plasma Sword (game 7, ROMs 14 stg2 / 15 pswd) character-highlight speech.
 * Native evidence: docs/port-tasks/stg2-selection.md; names:
 * docs/port-tasks/character-names.md. Read-only, worker thread only. */
#define STG2_STATE_SIZE   0x511B8u
#define STG2_PLAYER_STEP  0xA40u      /* per-side records, P1 at R+0          */
#define STG2_ROULETTE_OFF 0xAB4u      /* +side*0xA40: held-direction shuffle  */
#define STG2_SELECT_OFF   0x4E470u    /* window R+0x4E470 .. R+0x4E5F6        */
#define STG2_SELECT_SIZE  0x186u
#define STG2_SELECT_COUNT 25u         /* select numbers 0..24 (0 unused)      */
/* The owned copy the decoder works on: only what selection needs. */
typedef struct {
    BYTE phase;                       /* R+0: 3 = in game (update 0x140CCCB92) */
    BYTE window[STG2_SELECT_SIZE];    /* R+0x4E470 .. +0x185                  */
    DWORD roulette[2];                /* R + side*0xA40 + 0xAB4               */
} stg2_select_sample;
/* The window's fields, as offsets into it. */
#define STG2_W_STATE    0x000u        /* +side: 0 picking, 1 chosen, 3 done   */
#define STG2_W_NUMBER   0x002u        /* +side: select number                 */
#define STG2_W_HUMANS   0x049u        /* R+0x4E4B9: bit (1 << side) human     */
#define STG2_W_MODE     0x17Cu        /* R+0x4E5EC word: 1 = character select */
#define STG2_W_SUB      0x184u        /* R+0x4E5F4 word: 1 = picking          */
/* Shell character ID (the move-list ID space) for `side`, or -1 not selecting,
 * -2 selecting but unreadable (also while the held-direction shuffle runs). */
int stg2_select_decode(const stg2_select_sample *s, unsigned side);
/* Owned labels and visits for the shared selection channel. */
void stg2_selection_copy(const stg2_select_sample *s,
                         game_selection_choice out[GAME_SELECTION_OWNERS]);
/* NULL = STG2 not admitted: depart. */
void stg2_select_observe(const stg2_select_sample *s, ULONGLONG root, unsigned rom);
void stg2_select_tick(void);
#endif
