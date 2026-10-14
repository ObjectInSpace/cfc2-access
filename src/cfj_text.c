/* See cfj_text.h. Only the CFJ-specific part: the queue's address and record
 * layout, and the renderer's byte syntax. Buffering, publication,
 * de-duplication, speech and the ` key are shared (src/text_channel.h). */
#include "cfj_text.h"
#include "game_text.h"
#include "log.h"
#include <string.h>
#include <wchar.h>
#include <stdio.h>

/* Producer 0x1407D1320: record = QUEUE + count*0x40, x word, y word, style
 * dword, then formatted text from +8; it refuses work at count 0x80. */
#define QUEUE_COUNT 0x1422F1844ull
#define QUEUE_BASE  0x1422F1860ull
#define QUEUE_MAX   0x80
#define RECORD_SIZE 0x40
#define TEXT_CAP    56


static game_text_source source;
static tc_frame collecting;            /* the CFJ update's thread only */

void cfj_text_capture(void)
{
    int count = *(volatile int *)(ULONG_PTR)QUEUE_COUNT;
    if (count < 0 || count > QUEUE_MAX) count = 0;
    for (int i = 0; i < count; ++i) {
        const unsigned char *r = (const unsigned char *)(ULONG_PTR)(QUEUE_BASE + (ULONGLONG)i * RECORD_SIZE);
        short x, y;
        int style;
        memcpy(&x, r, 2);
        memcpy(&y, r + 2, 2);
        memcpy(&style, r + 4, 4);
        tc_frame_add(&collecting, (float)x, (float)y, style, 0, (const char *)r + 8, TEXT_CAP);
    }
    tc_frame_publish(&source.publisher, &collecting);
}

/* Renderer 0x1407DB720: style 0 returns before drawing. '^' consumes the
 * next byte: ^Z draws '\', ^Y '^', ^M '"', ^N '`'; any other pair draws a
 * special glyph (byte + 0x3C), which is not mapped and so is omitted (the
 * copyright line begins with one). Bytes from 0x90 take a separate glyph
 * path and refuse the row, as does anything else outside printable ASCII. */
BOOL cfj_text_decode(int style, const char *raw, wchar_t *out, SIZE_T cap)
{
    SIZE_T i = 0, n = 0;
    out[0] = 0;
    if (style == 0 || cap < 2) return FALSE;
    while (i < TC_RAW_CAP && raw[i]) {
        unsigned c = (unsigned char)raw[i++];
        if (c == '^') {
            if (i >= TC_RAW_CAP || !raw[i]) return FALSE;
            c = (unsigned char)raw[i++];
            if (c == 'Z') c = '\\';
            else if (c == 'Y') c = '^';
            else if (c == 'M') c = '"';
            else if (c == 'N') c = '`';
            else continue;                        /* special glyph: omitted */
        }
        if (c < 0x20 || c > 0x7E) return FALSE;
        if (n + 1 >= cap) return FALSE;
        out[n++] = (wchar_t)c;
    }
    out[n] = 0;
    return tc_squeeze(out) > 0;
}

/* The renderer returns before drawing style 0, so a refused style-0 row
 * is expected and not logged. */
static BOOL quiet_style0(const tc_row *r) { return r->style == 0; }

static game_text_source source =
    GAME_TEXT_SOURCE("cfj-text", "CFJ", cfj_text_decode, quiet_style0);

static tc_read_result read_current(void) { return game_text_read_current(&source); }

void cfj_text_observe(ULONGLONG root) { game_text_observe(&source, root); }

void cfj_text_register(void) { tc_register("CFJ", read_current); }
