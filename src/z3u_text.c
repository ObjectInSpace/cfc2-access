/* See z3u_text.h. Only the Alpha 3-specific part: the queue's address and
 * record layout, and the drawer's byte syntax. Buffering, publication,
 * de-duplication, speech and the ` key are shared (src/text_channel.h). */
#include "z3u_text.h"
#include "memsafe.h"
#include "game_text.h"
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


/* Typewriter messages (the story screen before each fight, and the other
 * users of allocator 0x1408761E0): three objects at R+0x9C68, +0x9CC0 and
 * +0x9D18, stride 0x58, +0 in use, +0x38 the entry's start POINTER into the
 * game's data (0x1408A92E8: [context+8] + 0xA398B4 + line*0x45 on the US
 * version). An entry is line NUL indent line NUL ... ending at a zero indent.
 * The whole entry is read as soon as typing starts. */
#define MESSAGE_OFF  0x9C68u
#define MESSAGE_STEP 0x58u
#define MESSAGE_MAX  3
#define ENTRY_CAP    (TC_RAW_CAP - 1)

static game_text_source source;
static game_text_state state;       /* zeroed, so .bss: see game_text.h */
static tc_frame collecting;            /* the Alpha 3 update's thread only */

static SIZE_T room(ULONGLONG p, ULONGLONG root)
{
    ULONGLONG end = 0;
    if (p >= IMAGE_LO && p < IMAGE_HI) end = IMAGE_HI;
    else if (p >= root && p < root + STATE_SIZE) end = root + STATE_SIZE;
    if (!end) return 0;
    return end - p < TEXT_CAP ? (SIZE_T)(end - p) : TEXT_CAP;
}

/* Game thread. Adds the queue's in-use records to this frame. Called from
 * the attract walker's hook too, BEFORE it draws and clears them. */
void z3u_text_collect(void)
{
    ULONGLONG root = *(volatile ULONGLONG *)(ULONG_PTR)ROOT_PTR;
    if (!root || root >= 0x00007FFF00000000ull) return;
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

/* Joins an entry's lines with spaces: line NUL indent line NUL ... until an
 * indent of 0. Pure, so the offline test runs it on the shipped data. */
SIZE_T z3u_message_join(const unsigned char *entry, SIZE_T len, char *out, SIZE_T cap)
{
    SIZE_T i = 0, n = 0;
    if (!cap) return 0;
    while (i < len) {
        while (i < len && entry[i]) {
            if (n + 2 >= cap) { out[0] = 0; return 0; }
            out[n++] = (char)entry[i++];
        }
        if (i >= len) { out[0] = 0; return 0; }      /* unterminated */
        ++i;                                          /* the NUL */
        if (i >= len || !entry[i]) break;             /* indent 0: end */
        ++i;                                          /* the indent */
        out[n++] = ' ';
    }
    out[n] = 0;
    return n;
}

/* A byte loop under the guard: a guarded memcpy across a bad page is not
 * caught (CLAUDE.md, run_span_test case C). Its own frame, so nothing the
 * caller keeps in registers can be clobbered by the guard's longjmp. */
static __attribute__((noinline)) BOOL guarded_bytes(unsigned char *dst, ULONGLONG p, SIZE_T n)
{
    const volatile unsigned char *src = (const volatile unsigned char *)(ULONG_PTR)p;
    volatile SIZE_T got = 0;
    volatile BOOL faulted;
    MEM_GUARDED(faulted, {
        while (got < n) { dst[got] = src[got]; got++; }
    });
    return !faulted;
}

static void collect_messages(ULONGLONG root)
{
    for (unsigned m = 0; m < MESSAGE_MAX; ++m) {
        const unsigned char *o = (const unsigned char *)(ULONG_PTR)(root + MESSAGE_OFF + m * MESSAGE_STEP);
        unsigned char entry[0x90];
        char text[TC_RAW_CAP];
        ULONGLONG p;
        if (!o[0]) continue;
        memcpy(&p, o + 0x38, 8);
        if (!p || p >= 0x00007FFF00000000ull) continue;
        if (!guarded_bytes(entry, p, sizeof entry) ||
            !z3u_message_join(entry, sizeof entry, text, ENTRY_CAP))
            continue;
        tc_frame_add(&collecting, 0.0f, (float)m, Z3U_STYLE_MESSAGE, 1, text, sizeof text);
    }
}

void z3u_text_capture(void)
{
    ULONGLONG root = *(volatile ULONGLONG *)(ULONG_PTR)ROOT_PTR;
    z3u_text_collect();
    if (root && root < 0x00007FFF00000000ull) collect_messages(root);
    tc_frame_publish(&state.publisher, &collecting);
}

/* In-game drawer 0x14087F5B0 and attract walker 0x1408797D0: each byte above
 * 0x20 draws glyph (byte - 0x21) from the record's font, so fonts 0 to 3,
 * whose strings are the game's own ASCII literals (font 0: the opening
 * screen's title, date and region), read as ASCII; bytes up to 0x20 advance
 * one cell. Font 4 draws picture words from tile codes (table 0x1412DA840);
 * their meaning is in the image, so they are refused rather than spelled out.
 * '@' leads the copyright line and is not a word: omitted. Bytes from 0x7F
 * refuse the row. Messages (Z3U_STYLE_MESSAGE) are the game's ASCII text. */
BOOL z3u_text_decode(int font, const char *raw, wchar_t *out, SIZE_T cap)
{
    SIZE_T i = 0, n = 0;
    out[0] = 0;
    if ((font < 0 || font > 3) && font != Z3U_STYLE_MESSAGE) return FALSE;
    if (cap < 2) return FALSE;
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

static game_text_source source =
    GAME_TEXT_SOURCE("z3u-text", "Alpha 3", z3u_text_decode, NULL, &state);

static tc_read_result read_current(void) { return game_text_read_current(&source); }

void z3u_text_observe(ULONGLONG root) { game_text_observe(&source, root); }

void z3u_text_register(void) { tc_register("Alpha 3", read_current); }
