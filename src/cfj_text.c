/* See cfj_text.h. Only the CFJ-specific part: the queue's address and record
 * layout, and the renderer's byte syntax. Buffering, publication,
 * de-duplication, speech and the ` key are shared (src/text_channel.h). */
#include "cfj_text.h"
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

#define REARM_MS 2000
#define FRESH_MS 500

static tc_frame collecting;            /* the CFJ update's thread only */
static tc_publisher publisher = { SRWLOCK_INIT, {0}, 0, 0 };

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
    tc_frame_publish(&publisher, &collecting);
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

#define TAG "cfj-text"

/* Rows the decoder refused, logged once each (at most 32 per run) with
 * their raw bytes, so an unmapped glyph is visible in the log rather than
 * silently missing from speech. Worker only. */
static void note_refused(const tc_row *r)
{
    static unsigned hashes[32], used;
    unsigned h = 2166136261u, i;
    char hex[3 * TC_RAW_CAP + 1];
    SIZE_T n = 0;
    for (i = 0; i < TC_RAW_CAP && r->raw[i]; ++i) h = (h ^ (unsigned char)r->raw[i]) * 16777619u;
    h ^= (unsigned)r->style;
    for (i = 0; i < used; ++i) if (hashes[i] == h) return;
    if (used == 32) return;
    hashes[used++] = h;
    for (i = 0; i < TC_RAW_CAP && r->raw[i] && n + 4 < sizeof hex; ++i)
        n += (SIZE_T)snprintf(hex + n, sizeof hex - n, "%02x ", (unsigned char)r->raw[i]);
    hex[n] = 0;
    log_line("%s: refused row style=%d bytes: %s", TAG, r->style, hex);
}

static tc_textdedup dedup;
static ULONGLONG active_root;
static unsigned consumed;

static tc_read_result assemble(BOOL manual, wchar_t *say, SIZE_T say_cap)
{
    static tc_frame frame;                /* worker only */
    static wchar_t decoded[TC_ROW_CAP][TC_LINE_CAP];
    int accepted[TC_ROW_CAP];
    unsigned i, j, count = 0;
    ULONGLONG now = GetTickCount64();
    SIZE_T used = 0;

    say[0] = 0;
    if (!tc_frame_take(&publisher, &frame, &consumed, manual, FRESH_MS))
        return TC_READ_STALE;
    memset(decoded, 0, sizeof decoded);
    for (i = 0; i < frame.count; ++i) {
        BOOL suppress = FALSE;
        int slot = -1;
        SIZE_T length;
        if (!cfj_text_decode(frame.rows[i].style, frame.rows[i].raw, decoded[i], TC_LINE_CAP)) {
            if (frame.rows[i].style != 0 && frame.rows[i].raw[0]) note_refused(&frame.rows[i]);
            continue;
        }
        for (j = 0; j < i; ++j) if (!wcscmp(decoded[j], decoded[i])) break;
        if (j < i) continue;
        if (!manual) {
            if (!tc_has_letters(decoded[i])) continue;
            slot = tc_textdedup_note(&dedup, decoded[i], now, REARM_MS, &suppress);
            if (suppress) continue;
        }
        length = wcslen(decoded[i]);
        if (used + length + 3 >= say_cap) break;
        if (used) { say[used++] = L' '; say[used++] = L'\n'; }
        memcpy(say + used, decoded[i], (length + 1) * sizeof(wchar_t));
        used += length;
        accepted[count++] = slot;
    }
    if (!used) return TC_READ_EMPTY;
    if (!tc_say(say, manual, manual ? "cfj-text (read current)" : "cfj-text"))
        return TC_READ_REFUSED;
    if (!manual)
        for (i = 0; i < count; ++i) tc_textdedup_mark(&dedup, accepted[i]);
    if (frame.overflow)
        log_line("cfj-text: capture limit reached; some rows were dropped");
    return TC_READ_SPOKE;
}

static tc_read_result read_current(void)
{
    wchar_t say[TC_SAY_CAP];
    if (!active_root) return TC_READ_INACTIVE;
    return assemble(TRUE, say, TC_SAY_CAP);
}

void cfj_text_observe(ULONGLONG root)
{
    wchar_t say[TC_SAY_CAP];
    if (root != active_root) {
        tc_textdedup_reset(&dedup);
        active_root = root;
        tc_frame_invalidate(&publisher);
    }
    if (!root) return;
    assemble(FALSE, say, TC_SAY_CAP);
}

void cfj_text_register(void) { tc_register("CFJ", read_current); }
