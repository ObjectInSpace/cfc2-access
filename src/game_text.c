/* See game_text.h. Moved, unchanged in behaviour, out of cvs1_text.c,
 * cfj_text.c and z3u_text.c, which each carried a copy. */
#include "game_text.h"
#include "log.h"
#include <string.h>
#include <wchar.h>
#include <stdio.h>

#define REARM_MS 2000
#define FRESH_MS 500

/* Rows the decoder refused, logged once each (at most 32 per run) with
 * their raw bytes, so an unmapped glyph or picture word is visible in the
 * log rather than silently missing from speech. Worker only. */
static void note_refused(game_text_source *s, const tc_row *r)
{
    game_text_state *st = s->state;
    unsigned h = 2166136261u, i;
    char hex[3 * TC_RAW_CAP + 1];
    SIZE_T n = 0;
    for (i = 0; i < TC_RAW_CAP && r->raw[i]; ++i) h = (h ^ (unsigned char)r->raw[i]) * 16777619u;
    h ^= (unsigned)r->style;
    for (i = 0; i < st->refused_used; ++i) if (st->refused_hash[i] == h) return;
    if (st->refused_used == 32) return;
    st->refused_hash[st->refused_used++] = h;
    for (i = 0; i < TC_RAW_CAP && r->raw[i] && n + 4 < sizeof hex; ++i)
        n += (SIZE_T)snprintf(hex + n, sizeof hex - n, "%02x ", (unsigned char)r->raw[i]);
    hex[n] = 0;
    log_line("%s: refused row style=%d bytes: %s", s->tag, r->style, hex);
}

/* The arcade board's credit display: FREE PLAY, or CREDIT(S) and a count.
 * Every game draws it on every screen between fights, so with the 2 s re-arm
 * it was narrated at each screen change -- seven times in one CVS1 session,
 * user-reported as "saying free play sometimes and it's not clear why"
 * (2026-10-14). It is board status, not screen content: never narrated,
 * still read by the ` key. */
static BOOL coin_status(const wchar_t *t)
{
    if (!wcscmp(t, L"FREE PLAY")) return TRUE;
    if (wcsncmp(t, L"CREDIT", 6)) return FALSE;
    t += 6;
    if (*t == L'S') ++t;
    else if (!wcsncmp(t, L"(S)", 3)) t += 3;     /* Project Justice: "CREDIT(S) 3" */
    while (*t == L' ' || *t == L':') ++t;
    if (!*t) return FALSE;
    /* A count, and Project Justice's coin fraction "1/2" after it. */
    if (*t < L'0' || *t > L'9') return FALSE;
    while ((*t >= L'0' && *t <= L'9') || *t == L'/' || *t == L' ') ++t;
    return !*t;
}

/* Automatic narration speaks new lines with letters, except the credit
 * display; the ` key reads the whole current frame, digits included,
 * ignoring de-duplication. */
static tc_read_result assemble(game_text_source *s, BOOL manual, wchar_t *say, SIZE_T say_cap)
{
    static tc_frame frame;                /* worker only */
    static wchar_t decoded[TC_ROW_CAP][TC_LINE_CAP];
    game_text_state *st = s->state;
    int accepted[TC_ROW_CAP];
    unsigned i, j, count = 0;
    ULONGLONG now = GetTickCount64();
    SIZE_T used = 0;
    char source[64];

    say[0] = 0;
    if (!tc_frame_take(&st->publisher, &frame, &st->consumed, manual, FRESH_MS))
        return TC_READ_STALE;
    memset(decoded, 0, sizeof decoded);
    for (i = 0; i < frame.count; ++i) {
        BOOL suppress = FALSE;
        int slot = -1;
        SIZE_T length;
        if (!s->decode(frame.rows[i].style, frame.rows[i].raw, decoded[i], TC_LINE_CAP)) {
            if (frame.rows[i].raw[0] && !(s->quiet && s->quiet(&frame.rows[i])))
                note_refused(s, &frame.rows[i]);
            continue;
        }
        /* A queue can hold the same line more than once: one reading each. */
        for (j = 0; j < i; ++j) if (!wcscmp(decoded[j], decoded[i])) break;
        if (j < i) continue;
        if (!manual) {
            if (!tc_has_letters(decoded[i]) || coin_status(decoded[i])) continue;
            slot = tc_textdedup_note(&st->dedup, decoded[i], now, REARM_MS, &suppress);
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
    snprintf(source, sizeof source, manual ? "%s (read current)" : "%s", s->tag);
    if (!tc_say(say, manual, source))
        return TC_READ_REFUSED;
    if (!manual)
        for (i = 0; i < count; ++i) tc_textdedup_mark(&st->dedup, accepted[i]);
    if (frame.overflow)
        log_line("%s: capture limit reached; some rows were dropped", s->tag);
    return TC_READ_SPOKE;
}

tc_read_result game_text_read_current(game_text_source *s)
{
    wchar_t say[TC_SAY_CAP];
    if (!s->state->active_root) return TC_READ_INACTIVE;
    return assemble(s, TRUE, say, TC_SAY_CAP);
}

void game_text_observe(game_text_source *s, ULONGLONG root)
{
    game_text_state *st = s->state;
    wchar_t say[TC_SAY_CAP];
    if (root != st->active_root) {
        tc_textdedup_reset(&st->dedup);
        st->active_root = root;
        tc_frame_invalidate(&st->publisher);
    }
    if (!root) return;
    assemble(s, FALSE, say, TC_SAY_CAP);
}
