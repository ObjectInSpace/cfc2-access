/* See cvs1_text.h. Only the CVS1-specific part: the queue's address and
 * record layout, and the renderer's byte syntax. Buffering, publication,
 * de-duplication, speech and the ` key are shared (src/text_channel.h). */
#include "cvs1_text.h"
#include "game_text.h"
#include "log.h"
#include <string.h>
#include <wchar.h>
#include <stdio.h>

/* Producer 0x1405AF6B0 writes record = QUEUE + count*0x40: x byte, y byte,
 * style byte, then formatted text from +3 (at most 61 bytes with its NUL).
 * Reset 0x1405AF7C0 clears the count inside the phase work; the drain
 * 0x1405AF730 renders the records later in the same update. */
#define QUEUE_COUNT 0x1422D0624ull
#define QUEUE_BASE  0x1422D0630ull
#define QUEUE_MAX   0x80
#define RECORD_SIZE 0x40
#define TEXT_CAP    61
/* Style 7 goes to the shared renderer 0x140F31050, which draws 0x01..0x7F
 * as they are and has no caret escape; other styles use 0x1405C8DB0's. */
#define STYLE_PLAIN 7

/* A blinking prompt ("PRESS START BUTTON", "INSERT COIN(S)") must not repeat
 * per blink but must speak on a later visit: CVS2's measured policy. */

static game_text_source source;
static game_text_state state;       /* zeroed, so .bss: see game_text.h */
static tc_frame collecting;            /* the CVS1 update's thread only */

void cvs1_text_capture(void)
{
    int count = *(volatile int *)(ULONG_PTR)QUEUE_COUNT;
    /* The game itself only rejects count == 0x80; a reader rejects any
     * count outside 0..0x80 rather than trusting the invariant. */
    if (count < 0 || count > QUEUE_MAX) count = 0;
    for (int i = 0; i < count; ++i) {
        const unsigned char *r = (const unsigned char *)(ULONG_PTR)(QUEUE_BASE + (ULONGLONG)i * RECORD_SIZE);
        tc_frame_add(&collecting, (float)r[0], (float)r[1], r[2], 0,
                     (const char *)r + 3, TEXT_CAP);
    }
    tc_frame_publish(&state.publisher, &collecting);
}

/* 0x1405C8F40: '^' consumes the next byte -- "^Z" draws byte 0x5C, any
 * other pair draws a special glyph (next byte + 0x3C). The glyph atlas is
 * not mapped, so special glyphs are omitted rather than guessed, and 0x5C
 * itself (the copyright lines start with it) is treated as decoration.
 * Anything outside printable ASCII refuses the row. */
BOOL cvs1_text_decode(int style, const char *raw, wchar_t *out, SIZE_T cap)
{
    SIZE_T i = 0, n = 0;
    out[0] = 0;
    if (cap < 2) return FALSE;
    while (i < TC_RAW_CAP && raw[i]) {
        unsigned c = (unsigned char)raw[i++];
        if (style != STYLE_PLAIN && c == '^') {
            if (i >= TC_RAW_CAP || !raw[i]) return FALSE;
            ++i;                                  /* special glyph: omitted */
            continue;
        }
        if (c == 0x5C) continue;                  /* decoration, not a letter */
        if (c < 0x20 || c > 0x7E) return FALSE;
        if (n + 1 >= cap) return FALSE;
        out[n++] = (wchar_t)c;
    }
    out[n] = 0;
    return tc_squeeze(out) > 0;
}

static game_text_source source =
    GAME_TEXT_SOURCE("cvs1-text", "CVS1", cvs1_text_decode, NULL, &state);

static tc_read_result read_current(void) { return game_text_read_current(&source); }

void cvs1_text_observe(ULONGLONG root) { game_text_observe(&source, root); }

void cvs1_text_register(void) { tc_register("CVS1", read_current); }
