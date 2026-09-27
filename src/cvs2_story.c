/* CVS2's story pages and credit roll.
 * Native renderer evidence and validation limits: docs/cvs2-story-reader.md.
 *
 * ⭐ Only the CVS2-specific parts live here: the two hook addresses and their
 * signatures, the 80-byte substituted-line buffer, the credit stream's glyph
 * map and its cursor. The FIFO, the per-entry suppression and the speech drain
 * are shared -- see src/text_channel.h. */
#include "cvs2_story.h"
#include "cvs2_select.h"
#include "text_channel.h"
#include "memsafe.h"
#include "log.h"
#include "../libs/minhook/include/MinHook.h"
#include <string.h>

#define STORY_FN 0x140701170ull
#define LINE_FN 0x140700EA0ull
#define CREDIT_BASE 0x1412A0B70ull
#define CREDIT_SIZE 2482u
#define TEXT_CAP TC_LINE_CAP
/* A page re-drawn for consecutive frames is one utterance; returning to it
 * later is new. Every page must eventually be spoken, hence a FIFO. */
#define STORY_REARM_MS 1500
typedef void (*story_fn)(int, int, const float *);
typedef void (*line_fn)(char *, int, int);
static story_fn real_story;
static line_fn real_line;
static BOOL installed;
static tc_queue queue;
static LONG epoch;
static __thread struct {
    BOOL capture, bad;
    char text[TEXT_CAP];
    SIZE_T length;
    tc_keydedup seen;
} local;

static void line_detour(char *text, int x, int y)
{
    char line[80];
    SIZE_T n;
    real_line(text, x, y);
    if (!local.capture || local.bad) return;
    /* This is the renderer's 80-byte stack buffer, AFTER name substitution. */
    if (!mem_copy(line, text, sizeof line)) { local.bad = TRUE; return; }
    for (n = 0; n < sizeof line && line[n]; ++n) {}
    if (n == sizeof line || local.length + n + 2 > sizeof local.text) {
        local.bad = TRUE;
        return;
    }
    if (local.length && n) local.text[local.length++] = ' ';
    memcpy(local.text + local.length, line, n);
    local.length += n;
    local.text[local.length] = 0;
}

static void story_detour(int entry, int palette, const float *position)
{
    ULONGLONG now = GetTickCount64();
    wchar_t text[TEXT_CAP];
    tc_keydedup_sync(&local.seen, InterlockedCompareExchange(&epoch, 0, 0));
    if (entry < 0 || entry >= 250 || local.capture) {
        real_story(entry, palette, position);
        return;
    }
    if (tc_keydedup_hit(&local.seen, (unsigned)entry, now, STORY_REARM_MS)) {
        real_story(entry, palette, position);
        return;
    }
    local.capture = TRUE;
    local.bad = FALSE;
    local.length = 0;
    local.text[0] = 0;
    real_story(entry, palette, position);
    local.capture = FALSE;
    /* ASCII is unchanged by CP932; native Japanese/name glyph pairs also
     * decode. Never emit a partially captured or invalid encoded page. */
    if (!local.bad && local.length &&
        tc_decode_cp(932, local.text, text, TEXT_CAP) &&
        tc_queue_push(&queue, text))
        tc_keydedup_set(&local.seen, (unsigned)entry, now);
}

BOOL cvs2_credit_line(const BYTE *b, SIZE_T length, wchar_t *out, SIZE_T cap)
{
    SIZE_T i, n = 0;
    if (!b || !out || !cap || !length || b[length-1] != 0xFE) return FALSE;
    out[0] = 0;
    for (i = 0; i + 1 < length; ++i) {
        unsigned c = b[i];
        wchar_t w = 0;
        if (c < 26) w = (wchar_t)(L'A' + c);
        else if (c < 52) w = (wchar_t)(L'a' + c - 26);
        else if (c < 62) w = (wchar_t)(L'0' + c - 52);
        else if (c == 63) w = L'.';
        else if (c == 70) w = L'(';
        else if (c == 71) w = L')';
        else if (c == 75) w = L'-';
        else if (c < 128) w = L' '; /* Unverified decorative glyphs. */
        else if (c >= 0xF0 && c != 0xF1) return FALSE;
        /* 80..EF palette/size/position and F1 animation controls. */
        if (!w || (w == L' ' && (!n || out[n-1] == L' '))) continue;
        if (n + 1 >= cap) { out[0] = 0; return FALSE; }
        out[n++] = w;
    }
    while (n && out[n-1] == L' ') --n;
    out[n] = 0;
    return TRUE;
}

BOOL cvs2_story_install(void)
{
    static const BYTE story_sig[] = {0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x57,0x41,0x54,0x41,0x55};
    static const BYTE line_sig[] = {0x44,0x89,0x44,0x24,0x18,0x53,0x56,0x57,0x41,0x55,0x41,0x56,0x48,0x83,0xec,0x60};
    BYTE bytes[16];
    if (installed) return TRUE;
    if (!mem_copy(bytes, (void *)(ULONG_PTR)STORY_FN, sizeof story_sig) ||
        memcmp(bytes, story_sig, sizeof story_sig) ||
        !mem_copy(bytes, (void *)(ULONG_PTR)LINE_FN, sizeof line_sig) ||
        memcmp(bytes, line_sig, sizeof line_sig)) {
        log_line("story: native signatures differ; reader disabled");
        return FALSE;
    }
    if (MH_CreateHook((void *)(ULONG_PTR)LINE_FN, (void *)line_detour,
                      (void **)&real_line) != MH_OK) return FALSE;
    if (MH_CreateHook((void *)(ULONG_PTR)STORY_FN, (void *)story_detour,
                      (void **)&real_story) != MH_OK) {
        MH_RemoveHook((void *)(ULONG_PTR)LINE_FN);
        return FALSE;
    }
    if (MH_EnableHook((void *)(ULONG_PTR)LINE_FN) != MH_OK ||
        MH_EnableHook((void *)(ULONG_PTR)STORY_FN) != MH_OK) {
        MH_DisableHook((void *)(ULONG_PTR)LINE_FN);
        MH_RemoveHook((void *)(ULONG_PTR)LINE_FN);
        MH_RemoveHook((void *)(ULONG_PTR)STORY_FN);
        return FALSE;
    }
    installed = TRUE;
    log_line("story: native story page and substituted-line hooks installed");
    return TRUE;
}

void cvs2_story_observe(const BYTE *s, SIZE_T size, ULONGLONG root)
{
    static ULONGLONG last_root;
    static BYTE stream[CREDIT_SIZE];
    static BOOL loaded, credits;
    static SIZE_T cursor;
    ULONGLONG ptr;
    wchar_t text[TEXT_CAP];
    SIZE_T end, next;
    if (!s || size < CVS2_STATE_SIZE || !root) {
        if (last_root) {
            tc_queue_clear(&queue);
            InterlockedIncrement(&epoch);
        }
        last_root = 0;
        credits = FALSE;
        return;
    }
    if (last_root && last_root != root) {
        cvs2_story_observe(NULL, 0, 0);
    }
    last_root = root;
    if (!memcmp(s + 0x78, "\x02\x01\x04\x02\x01", 5)) {
        memcpy(&ptr, s + 0x21650, sizeof ptr);
        if (!loaded) {
            static const BYTE prefix[] = {0xFE,0x91,0xB0,2,0,15,2,14,12};
            loaded = mem_copy(stream, (void *)(ULONG_PTR)CREDIT_BASE, sizeof stream)
                && !memcmp(stream, prefix, sizeof prefix) && stream[CREDIT_SIZE-1] == 0xFF;
        }
        if (loaded && ptr >= CREDIT_BASE && ptr < CREDIT_BASE + CREDIT_SIZE) {
            end = (SIZE_T)(ptr - CREDIT_BASE);
            /* A poll may catch an increment inside the character loop. Only
             * FE boundaries represent complete lines submitted to rendering. */
            if (!end || stream[end-1] == 0xFE) {
                if (!credits || end < cursor) {
                    cursor = end;
                    /* Attaching mid-roll reads only the most recent line. */
                    if (cursor) {
                        --cursor;
                        while (cursor && stream[cursor-1] != 0xFE) --cursor;
                    }
                    credits = TRUE;
                }
                while (cursor < end) {
                    next = cursor;
                    while (next < end && stream[next] != 0xFE) ++next;
                    if (next == end) break;
                    ++next;
                    if (!cvs2_credit_line(stream + cursor, next - cursor, text, TEXT_CAP)) break;
                    if (text[0] && !tc_queue_push(&queue, text)) break;
                    cursor = next;
                }
            }
        }
    } else credits = FALSE;
    tc_queue_drain(&queue, "story/credits");
}
