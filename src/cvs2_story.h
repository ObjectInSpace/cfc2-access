#ifndef CFC2_CVS2_STORY_H
#define CFC2_CVS2_STORY_H
#include <windows.h>
BOOL cvs2_story_install(void);
void cvs2_story_observe(const BYTE *state, SIZE_T size, ULONGLONG root);
/* Pure, bounded decoder for one FE-terminated credits line. */
BOOL cvs2_credit_line(const BYTE *bytes, SIZE_T length, wchar_t *out, SIZE_T capacity);
#endif
