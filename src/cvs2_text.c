/* CVS2's native positioned-text records and ordinary font runs.
 * Native evidence: docs/cvs2-text-source-audit.md.
 *
 * ⭐ This file is now ONLY the CVS2-specific part: the three hook addresses,
 * their signatures, the record header layout, and the CP932 record syntax.
 * Capture buffering, publication, de-duplication, speech and the ` key registry
 * are shared -- see src/text_channel.h -- and the worker half (narration,
 * the ` key readout, refused-row logging, the credit-display filter) is the
 * one every emulated game uses, src/game_text.c. Porting another arcade title means
 * writing a file this shape, not repeating that machinery.
 *
 * Capture only draw calls, publish only at the end of the text frame. */
#include "cvs2_text.h"
#include "game_text.h"
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

static __thread tc_frame collecting;
static __thread unsigned record_depth;
static game_text_state state;       /* zeroed, so .bss: see game_text.h */
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
    tc_frame_publish(&state.publisher, &collecting);
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

/* Automatic narration leaves these to the readers that already speak them:
 * the story and win-quote font runs (cvs2_story.c, cvs2_select.c) and the
 * two HUD name records. The ` key still reads them. */
static BOOL key_only(const tc_row *r)
{ return r->kind == KIND_STRING || r->style == STYLE_HUD_NAME; }

static BOOL decode_text_row(const tc_row *r, wchar_t *out, SIZE_T cap)
{ return cap >= TC_LINE_CAP && decode_row(r, out); }

static game_text_source source = {
    .tag = "cvs2-text", .name = "CVS2", .decode_row = decode_text_row,
    .key_only = key_only, .state = &state,
};

/* The ` key source. INACTIVE unless CVS2 is the running game. */
static tc_read_result read_current(void) { return game_text_read_current(&source); }

/* Without the hooks nothing is captured, so CVS2 is "not active here" for
 * the ` key rather than a stale-frame answer. */
void cvs2_text_observe(ULONGLONG root) { game_text_observe(&source, text_installed ? root : 0); }

void cvs2_text_register(void) { tc_register("CVS2", read_current); }

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
