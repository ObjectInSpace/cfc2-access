#ifndef CFC2_MJUS_TEXT_H
#define CFC2_MJUS_TEXT_H
#include <windows.h>
#include "text_channel.h"

/* Project Justice's ASCII text: the immediate draw 0x140B96130 (x, y, scale,
 * colour, string), 7 call sites: the credit task (INSERT %d MORE COIN(S),
 * CREDIT(S) with counts), the copyright line, ROUND BONUS and its score.
 * Story dialogue, names and menus are pictures and are not covered here.
 * Evidence: docs/port-tasks/mjus-text.md. */

#define MJUS_TEXT_FN 0x140B96130ull

/* Game thread: installed with the frame hook; collects each drawn string. */
BOOL mjus_text_install(void);
void mjus_text_remove(void);
/* Game thread, from the frame hook AFTER the update returns: publishes the
 * strings drawn during this update. */
void mjus_text_capture(void);

/* Worker. Root of the admitted block, or 0 when MJUS is not running. */
void mjus_text_observe(ULONGLONG root);
/* Worker. Registers the ` key source; once at startup, in priority order. */
void mjus_text_register(void);

/* Pure decode of one drawn string, for the offline test. */
BOOL mjus_text_decode(int style, const char *raw, wchar_t *out, SIZE_T cap);
#endif
