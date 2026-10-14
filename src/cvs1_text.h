#ifndef CFC2_CVS1_TEXT_H
#define CFC2_CVS1_TEXT_H
#include <windows.h>
#include "text_channel.h"

/* CVS1's positioned text-record queue: prompts, coin/credit text, win counts,
 * GAME OVER, copyright. Evidence: docs/cvs1-acquisition-trace.md (F17/F18)
 * and docs/port-tasks/cvs1-text.md. Other on-screen text is drawn through
 * other glyph paths and is not covered here. */

/* Game thread, from the CVS1 frame hook AFTER the original update returns:
 * the update resets, fills and draws the queue, so it now holds exactly this
 * frame's records. Copies them and publishes the frame. */
void cvs1_text_capture(void);

/* Worker. Root of the admitted CVS1 block, or 0 when CVS1 is not running. */
void cvs1_text_observe(ULONGLONG root);

/* Worker. Registers the ` key source; call once at startup, in priority order. */
void cvs1_text_register(void);

/* Pure decode of one record's text (after its 3-byte header), exposed for the
 * offline test. Returns FALSE for anything it cannot render faithfully. */
BOOL cvs1_text_decode(int style, const char *raw, wchar_t *out, SIZE_T cap);
#endif
