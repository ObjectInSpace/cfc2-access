/* See z3u_text.h. Only the Alpha 3-specific part: the queue's address and
 * record layout, and the drawer's byte syntax. Buffering, publication,
 * de-duplication, speech and the ` key are shared (src/text_channel.h). */
#include "z3u_text.h"
#include "log.h"
#include <string.h>
#include <wchar.h>
#include <stdio.h>

/* Print 0x140879770: the first free record of R+0x9D70 (0x60 of 0x18 bytes)
 * gets +0 in use, +1 font (dl & 0x7F), +2 colour, +4 x word, +6 y word,
 * +8 a POINTER to the string. The text is not copied, so it is copied here. */
#define ROOT_PTR    0x1422FB960ull
#define STATE_SIZE  0x12F98ull
#define QUEUE_OFF   0x9D70u
#define QUEUE_MAX   0x60
#define RECORD_SIZE 0x18
#define TEXT_CAP    56
/* Strings live in the game image (literals and its own buffers) or in the
 * Alpha 3 block (the credit line at R+5, the score at R+0x9D58). Anything
 * else is not read at all. */
#define IMAGE_LO    0x140001000ull
#define IMAGE_HI    0x142E75000ull

#define REARM_MS 2000
#define FRESH_MS 500

static tc_frame collecting;            /* the Alpha 3 update's thread only */
static tc_publisher publisher = { SRWLOCK_INIT, {0}, 0, 0 };

static SIZE_T room(ULONGLONG p, ULONGLONG root)
{
    ULONGLONG end = 0;
    if (p >= IMAGE_LO && p < IMAGE_HI) end = IMAGE_HI;
    else if (p >= root && p < root + STATE_SIZE) end = root + STATE_SIZE;
    if (!end) return 0;
    return end - p < TEXT_CAP ? (SIZE_T)(end - p) : TEXT_CAP;
}

void z3u_text_capture(void)
{
    ULONGLONG root = *(volatile ULONGLONG *)(ULONG_PTR)ROOT_PTR;
    if (root && root < 0x00007FFF00000000ull) {
        for (int i = 0; i < QUEUE_MAX; ++i) {
            const unsigned char *r = (const unsigned char *)(ULONG_PTR)(root + QUEUE_OFF + (ULONGLONG)i * RECORD_SIZE);
            short x, y;
            ULONGLONG p;
            SIZE_T n;
            if (!r[0]) continue;
            memcpy(&x, r + 4, 2);
            memcpy(&y, r + 6, 2);
            memcpy(&p, r + 8, 8);
            n = room(p, root);
            if (n) tc_frame_add(&collecting, (float)x, (float)y, r[1], 0, (const char *)(ULONG_PTR)p, n);
        }
    }
    tc_frame_publish(&publisher, &collecting);
}

/* In-game drawer 0x14087F5B0: each byte above 0x20 draws glyph (byte - 0x21)
 * from the record's font, so fonts 1 to 3, whose strings are the game's own
 * ASCII literals, read as ASCII; bytes up to 0x20 advance one cell. Font 4
 * draws picture words from tile codes (table 0x1412DA840); their meaning is
 * in the image, so they are refused rather than spelled out. '@' leads the
 * copyright line and is not a word: omitted. Bytes from 0x7F refuse the row. */
BOOL z3u_text_decode(int font, const char *raw, wchar_t *out, SIZE_T cap)
{
    SIZE_T i = 0, n = 0;
    out[0] = 0;
    if (font < 1 || font > 3 || cap < 2) return FALSE;
    while (i < TC_RAW_CAP && raw[i]) {
        unsigned c = (unsigned char)raw[i++];
        if (c >= 0x7F) return FALSE;
        if (c <= 0x20) c = ' ';
        else if (c == '@') continue;
        if (n + 1 >= cap) return FALSE;
        out[n++] = (wchar_t)c;
    }
    out[n] = 0;
    return tc_squeeze(out) > 0;
}

#define TAG "z3u-text"

/* Rows the decoder refused, logged once each (at most 32 per run) with
 * their raw bytes, so a picture word or unmapped glyph is visible in the log
 * rather than silently missing from speech. Worker only. */
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
    log_line("%s: refused row font=%d bytes: %s", TAG, r->style, hex);
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
        if (!z3u_text_decode(frame.rows[i].style, frame.rows[i].raw, decoded[i], TC_LINE_CAP)) {
            if (frame.rows[i].raw[0]) note_refused(&frame.rows[i]);
            continue;
        }
        /* The queue can hold the same line more than once (a screen that
         * prints every frame between clears): one reading each. */
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
    if (!tc_say(say, manual, manual ? "z3u-text (read current)" : "z3u-text"))
        return TC_READ_REFUSED;
    if (!manual)
        for (i = 0; i < count; ++i) tc_textdedup_mark(&dedup, accepted[i]);
    if (frame.overflow)
        log_line("z3u-text: capture limit reached; some rows were dropped");
    return TC_READ_SPOKE;
}

static tc_read_result read_current(void)
{
    wchar_t say[TC_SAY_CAP];
    if (!active_root) return TC_READ_INACTIVE;
    return assemble(TRUE, say, TC_SAY_CAP);
}

void z3u_text_observe(ULONGLONG root)
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

void z3u_text_register(void) { tc_register("Alpha 3", read_current); }
