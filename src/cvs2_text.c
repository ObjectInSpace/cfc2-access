/* CVS2's native positioned-text records and ordinary font runs.
 * Native evidence: docs/cvs2-text-source-audit.md.
 *
 * ⭐ This file is now ONLY the CVS2-specific part: the three hook addresses,
 * their signatures, the record header layout, and the CP932 record syntax.
 * Capture buffering, publication, de-duplication, speech and the ` key registry
 * are shared -- see src/text_channel.h. Porting another arcade title means
 * writing a file this shape, not repeating that machinery.
 *
 * Capture only draw calls, publish only at the end of the text frame. */
#include "cvs2_text.h"
#include "text_channel.h"
#include "memsafe.h"
#include "log.h"
#include "../libs/minhook/include/MinHook.h"
#include <string.h>
#include <wchar.h>

#define RECORD_FN 0x1406B7E00ull
#define STRING_FN 0x1406AD730ull
#define END_FN    0x1406B09F0ull

/* CVS2's own row classification, passed through tc_row unchanged:
 *   kind 0 = an ordinary font run   kind 1 = a positioned record
 *   style 16 = the two HUD name records (0x1406C5759 / 0x1406C5803) */
#define KIND_STRING 0
#define KIND_RECORD 1
#define STYLE_HUD_NAME 16

/* A blinking prompt must not repeat per blink, but must speak on a later
 * visit. Measured against "PRESS START BUTTON", which blinks about 1 Hz. */
#define REARM_MS 2000
/* A frame older than this is not what is on screen now. */
#define FRESH_MS 500

static __thread tc_frame collecting;
static __thread unsigned record_depth;
static tc_publisher publisher = { SRWLOCK_INIT, {0}, 0, 0 };
static BOOL text_installed;
static void (*real_record)(const void *, float);
static void (*real_string)(float, float, float, const char *);
static void (*real_end)(void);

static void record_detour(const void *record, float depth)
{
    volatile struct { short x, y; int style; } header;
    volatile BOOL faulted;
    MEM_GUARDED(faulted, {
        header.x = ((const short *)record)[0];
        header.y = ((const short *)record)[1];
        header.style = ((const int *)record)[1];
    });
    if (!faulted)
        tc_frame_add(&collecting, (float)header.x, (float)header.y,
                     header.style, KIND_RECORD, (const char *)record + 8, 56);
    ++record_depth;
    real_record(record, depth);
    --record_depth;
}

static void string_detour(float x, float y, float depth, const char *text)
{
    /* Embedded font runs are already represented by their parent record. */
    if (!record_depth)
        tc_frame_add(&collecting, x, y, 0, KIND_STRING, text, TC_RAW_CAP);
    real_string(x, y, depth, text);
}

static void end_detour(void)
{
    real_end();
    tc_frame_publish(&publisher, &collecting);
}

/* Positioned-record syntax comes from 6B7E00. Ordinary font runs are CP932.
 * Unsupported sprite/button runs are rejected, never interpreted as letters.
 * Escaped decorative font glyphs are omitted. */
static BOOL decode_row(const tc_row *r, wchar_t *out)
{
    char bytes[TC_RAW_CAP];
    SIZE_T i = 0, n = 0;
    BOOL embedded = FALSE;
    if (r->kind == KIND_STRING)
        return tc_decode_cp(932, r->raw, out, TC_LINE_CAP);
    while (i < sizeof r->raw && r->raw[i]) {
        unsigned c = (BYTE)r->raw[i++];
        if (c == 0xFE || c == 0xFF) { embedded = !embedded; continue; }
        if (!embedded && c == '^') {
            if (!r->raw[i]) return FALSE;
            c = (BYTE)r->raw[i++];
            if (c == 'Y') c = '^';
            else if (c == 'Z') c = '\\';
            else continue;
        } else if (!embedded && c >= 0x80) {
            /* The Japanese ROM's font runs are the same CP932: a lead byte
             * takes its trail byte whole, before any escape test, because a
             * trail byte can equal '^'. Anything else outside CP932 (and the
             * decode below) still refuses the row. */
            BOOL lead = (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC);
            if (lead) {
                unsigned t;
                if (i >= sizeof r->raw) return FALSE;
                t = (BYTE)r->raw[i];
                if (!(t >= 0x40 && t <= 0xFC && t != 0x7F)) return FALSE;
                if (n + 2 >= sizeof bytes) return FALSE;
                bytes[n++] = (char)c;
                bytes[n++] = (char)t;
                i++;
                continue;
            }
            if (!(c >= 0xA1 && c <= 0xDF)) return FALSE;   /* half-width kana only */
        } else if (!embedded && c < 32) {
            return FALSE; /* Icon-only/mixed command text needs its own map. */
        }
        if (n + 1 >= sizeof bytes) return FALSE;
        bytes[n++] = (char)c;
    }
    bytes[n] = 0;
    if (!tc_decode_cp(932, bytes, out, TC_LINE_CAP)) return FALSE;
    /* Collapse the presentation padding in title/territory records. */
    return tc_squeeze(out) > 0;
}

static tc_textdedup dedup;
static ULONGLONG active_root;
static unsigned consumed;

void cvs2_text_request_read(void) { tc_request_read(); } /* Worker only. */

/* Assembles the current frame's readable rows.
 *
 * `manual` selects the on-demand readout, which differs from automatic
 * narration in three ways, each for its own reason:
 *   - it includes rows the automatic path leaves to their own readers
 *     (story/quote font runs, and the two HUD name records);
 *   - it includes rows with no letters, i.e. unlabelled HUD digits;
 *   - it ignores de-duplication, because the user asked for what is there NOW.
 */
static tc_read_result assemble(BOOL manual, wchar_t *say, SIZE_T say_cap)
{
    static tc_frame frame; /* Worker only; avoid a large stack frame. */
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
        if (!decode_row(&frame.rows[i], decoded[i])) continue;
        for (j = 0; j < i; ++j) if (!wcscmp(decoded[j], decoded[i])) break;
        if (j < i) continue;
        if (!manual) {
            /* Existing readers own stories/quotes and the two HUD name
             * records. The ` key includes them, without duplicating that speech. */
            if (frame.rows[i].kind == KIND_STRING ||
                frame.rows[i].style == STYLE_HUD_NAME) continue;
            if (!tc_has_letters(decoded[i])) continue; /* HUD digits: on demand */
            slot = tc_textdedup_note(&dedup, decoded[i], now, REARM_MS,
                                     &suppress);
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
    if (!tc_say(say, manual, manual ? "cvs2-text (read current)" : "cvs2-text"))
        return TC_READ_REFUSED;
    if (!manual)
        for (i = 0; i < count; ++i) tc_textdedup_mark(&dedup, accepted[i]);
    if (frame.overflow)
        log_line("cvs2-text: capture limit reached; some rows were dropped");
    return TC_READ_SPOKE;
}

/* The ` key source. Returns INACTIVE unless CVS2 is the running game. */
static tc_read_result read_current(void)
{
    wchar_t say[TC_SAY_CAP];
    if (!active_root || !text_installed) return TC_READ_INACTIVE;
    return assemble(TRUE, say, TC_SAY_CAP);
}

void cvs2_text_observe(ULONGLONG root)
{
    wchar_t say[TC_SAY_CAP];
    if (root != active_root) {
        tc_textdedup_reset(&dedup);
        active_root = root;
        tc_frame_invalidate(&publisher);
    }
    if (!root || !text_installed) return;
    assemble(FALSE, say, TC_SAY_CAP);
}

BOOL cvs2_text_install(void)
{
    static const struct { ULONGLONG address; BYTE sig[16]; unsigned size; } sites[] = {
        { RECORD_FN, {0x40,0x55,0x53,0x57,0x48,0x8d,0x6c,0x24,0x90,0x48,0x81,0xec,0x70,0x01,0,0}, 16 },
        { STRING_FN, {0x48,0x83,0xec,0x38,0x48,0x8b,0x0d,0x7d,0x67,0xc2,0x01,0x0f,0x28,0xda}, 14 },
        { END_FN, {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x8b,0x1d,0xcc,0x34,0xc2,0x01}, 16 }
    };
    void *detours[] = {(void *)record_detour, (void *)string_detour, (void *)end_detour};
    void **originals[] = {(void **)&real_record, (void **)&real_string, (void **)&real_end};
    BYTE bytes[16];
    unsigned i, created = 0;
    if (text_installed) return TRUE;
    if (!mem_guard_install()) return FALSE;
    for (i = 0; i < 3; ++i) {
        if (!mem_copy(bytes, (void *)(ULONG_PTR)sites[i].address, sites[i].size) ||
            memcmp(bytes, sites[i].sig, sites[i].size)) {
            log_line("cvs2-text: signature mismatch at 0x%llx; disabled", sites[i].address);
            return FALSE;
        }
    }
    for (i = 0; i < 3; ++i) {
        if (MH_CreateHook((void *)(ULONG_PTR)sites[i].address, detours[i], originals[i]) != MH_OK) goto fail;
        ++created;
    }
    for (i = 0; i < 3; ++i)
        if (MH_EnableHook((void *)(ULONG_PTR)sites[i].address) != MH_OK) goto fail;
    text_installed = TRUE;
    tc_register("CVS2", read_current);
    log_line("cvs2-text: record, string, and end-of-text-frame hooks installed");
    return TRUE;
fail:
    for (i = 0; i < created; ++i) {
        MH_DisableHook((void *)(ULONG_PTR)sites[i].address);
        MH_RemoveHook((void *)(ULONG_PTR)sites[i].address);
    }
    log_line("cvs2-text: hook installation failed; disabled");
    return FALSE;
}
