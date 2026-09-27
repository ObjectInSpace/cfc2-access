/* The render-thread half of the shared text layer: tc_frame_add().
 *
 * ⭐ WHY THIS IS A SEPARATE TRANSLATION UNIT. It is the only part of
 * text_channel that needs memsafe's per-thread fault guard, and it is needed
 * only by a reader that copies strings out of a live render callback. A reader
 * that merely queues finished lines links this file not at all -- so its
 * offline suite keeps its own mem_copy seam without colliding with memsafe.
 *
 * ⚠⚠ EVERYTHING HERE RUNS ON A GAME THREAD. No VirtualQuery, no logging, no
 * allocation, no speech, and no lock that can block. This project has already
 * paid twice for a stall on the render thread (see speech.c) and once for
 * dereferencing a caller-supplied pointer (memory:
 * hooked-function-pointers-are-untrusted).
 */
#include "text_channel.h"
#include "memsafe.h"
#include <math.h>

/* One byte at a time up to NUL: never assume the caller owns a full
 * maximum-length buffer. The process-wide fault handler is already installed.
 * Returns FALSE unless a NUL was actually reached inside `limit`, so a
 * truncated or unreadable string is rejected rather than half-spoken. */
static BOOL copy_narrow(char *dst, const char *src, SIZE_T limit)
{
    volatile SIZE_T i = 0;
    volatile BOOL faulted;
    MEM_GUARDED(faulted, {
        while (i < limit) {
            char c = src[i];
            dst[i++] = c;
            if (!c) break;
        }
    });
    return !faulted && i && !dst[i-1];
}

void tc_frame_add(tc_frame *collecting, float x, float y, int style, int kind,
                  const char *text, SIZE_T cap)
{
    tc_row *r;
    if (!collecting || !text) return;
    /* A non-finite coordinate cannot be sorted into reading order, and NaN
     * compares false against everything -- it would corrupt the row order
     * rather than merely misplace one row. */
    if (!isfinite(x) || !isfinite(y)) return;
    if (cap > TC_RAW_CAP) cap = TC_RAW_CAP;
    /* Counted, not silently dropped: a frame that lost rows must be able to
     * say so, because "the screen had nothing else on it" is a different fact. */
    if (collecting->count >= TC_ROW_CAP) { collecting->overflow = TRUE; return; }
    r = &collecting->rows[collecting->count];
    if (!copy_narrow(r->raw, text, cap) || !r->raw[0]) return;
    r->x = x; r->y = y; r->style = style; r->kind = kind;
    ++collecting->count;
}
