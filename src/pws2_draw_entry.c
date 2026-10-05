#include "pws2_draw_entry.h"
#include <windows.h>

void *pws2_draw_entry_original;
pws2_draw_entry_sink_fn pws2_draw_entry_sink;

#ifdef PWS2_DRAW_ENTRY_TEST
extern const unsigned char pws2_test_after_call[];
#define REVIEWED_CALLER ((uintptr_t)pws2_test_after_call)
#else
#define REVIEWED_CALLER UINT64_C(0x140a5df81)
#endif

/* Called only by the assembly entry with synthesized observer arguments.
 * A matching return address identifies the local matrix-load stage only.
 * It does not prove current ownership, queue insertion or visible geometry. */
void pws2_draw_entry_observe(const void *matrix, const void *object,
                             const void *resource, uintptr_t caller)
{
    DWORD last_error = GetLastError();
    if (caller == REVIEWED_CALLER && pws2_draw_entry_sink)
        pws2_draw_entry_sink(matrix, object, resource, caller);
    SetLastError(last_error);
}
