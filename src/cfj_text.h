#ifndef CFC2_CFJ_TEXT_H
#define CFC2_CFJ_TEXT_H
#include <windows.h>
#include "text_channel.h"

/* CFJ's positioned text-record queue: coin/credit and start prompts,
 * CONTINUE, GAME OVER, NO CHALLENGER, mode titles, copyright. Evidence:
 * docs/port-tasks/cfj-text.md. Other on-screen text, if drawn another way,
 * is not covered here. */

/* Game thread, from the CFJ frame hook AFTER the original update returns:
 * the update resets (0x1407D1440), fills and drains (0x1407D13C0) the queue,
 * so it now holds exactly this frame's records. */
void cfj_text_capture(void);

/* Worker. Root of the admitted CFJ block, or 0 when CFJ is not running. */
void cfj_text_observe(ULONGLONG root);

/* Worker. Registers the ` key source; call once at startup, in priority order. */
void cfj_text_register(void);

/* Pure decode of one record's text, exposed for the offline test. Returns
 * FALSE for anything it cannot render faithfully, and for style 0, which the
 * renderer does not draw. */
BOOL cfj_text_decode(int style, const char *raw, wchar_t *out, SIZE_T cap);
#endif
