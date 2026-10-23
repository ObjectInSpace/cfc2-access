#ifndef CFC2_PWS_SELECT_DECODE_H
#define CFC2_PWS_SELECT_DECODE_H
#include "game_selection.h"
#include <stddef.h>

/* Offline-tested selection candidate decoder. NOT wired into the DLL yet.
 * Caller must separately establish live execution, ROM, shell/overlay ownership
 * and code/table signatures. Root bytes alone do not establish eligibility.
 * Native evidence and pending live checks: docs/port-tasks/pws-selection.md. */
typedef struct {
    unsigned game, variant;
    unsigned char phase[8], gates[2], mask;
    unsigned char state[4], cursor[4], slot[4];
    unsigned shuffle[4];
} pws_select_sample;

/* Extract the bounded fields from a full owned state block; fail closed on
 * wrong game/version/length. This helper also supports future fixture replay. */
bool pws_select_from_state(unsigned game, unsigned variant, const void *state,
                           size_t size, pws_select_sample *out);
/* Shell name ID, -1 outside the traced picking branch, -2 unavailable/shuffle. */
int pws_select_decode(const pws_select_sample *sample, unsigned slot);
void pws_selection_copy(const pws_select_sample *sample,
                        game_selection_choice out[GAME_SELECTION_OWNERS]);
#endif
