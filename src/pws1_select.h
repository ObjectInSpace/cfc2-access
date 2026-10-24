#ifndef CFC2_PWS1_SELECT_H
#define CFC2_PWS1_SELECT_H
#include <windows.h>
#include "pws_select_decode.h"
/* Power Stone (game 4, ROMs 8 pws1 / 9 pws1u) character-highlight speech and
 * match-audio ownership. The decoder is shared with PWS2
 * (pws_select_decode.c); this file is the acquisition and gating. Native
 * evidence: docs/port-tasks/pws-selection.md. Read-only, worker thread only. */
#define PWS1_STATE_SIZE  0x428F0u
#define PWS1_WINDOW_OFF  0x27DC2u     /* side states .. owner mask, one read  */
#define PWS1_WINDOW_SIZE 0x230u       /* R+0x27DC2 .. R+0x27FF1 inclusive     */
/* The Versus stage select: stage ID for the cursor word R+0x27FC2 while
 * phase/sub-state/step read 3/1/1, -1 off that screen, -2 unreadable. */
int pws1_stage_decode(BYTE phase, BYTE sub, BYTE step, int cursor);
/* The stage's name in the game's language, from its theme's title. */
BOOL pws1_stage_name(int id, wchar_t *out, unsigned cap);
/* NULL = PWS1 not admitted: depart. `stage` from pws1_stage_decode. */
void pws1_select_observe(const pws_select_sample *s, int stage, ULONGLONG root, unsigned rom);
void pws1_select_tick(void);
#endif
