#ifndef CFC2_CVS2_TEXT_H
#define CFC2_CVS2_TEXT_H
#include <windows.h>

/* CVS2's positioned text records and font runs. Unlike the queue games,
 * CVS2 is captured by its own renderer hooks (record, string, end of text
 * frame), so it installs them; the worker half is the shared game_text.c. */
BOOL cvs2_text_install(void);
/* Worker. Root of the admitted CVS2 block, or 0 when CVS2 is not running. */
void cvs2_text_observe(ULONGLONG root);
/* Worker. Registers the ` key source; call once at startup, in priority order. */
void cvs2_text_register(void);
#endif
