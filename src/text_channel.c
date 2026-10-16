/* The shared speech and read-current-text layer. Rationale in the header. */
#include "text_channel.h"
#include "speech.h"
#include "mod_text.h"
#include "log.h"
#include <string.h>
#include <wchar.h>
#include <stdlib.h>
#include <math.h>

/* ---- Conversion --------------------------------------------------------- */

BOOL tc_decode_cp(unsigned code_page, const char *in, wchar_t *out, SIZE_T cap)
{
    int got;
    if (!out || !cap) return FALSE;
    out[0] = 0;
    if (!in) return FALSE;
    /* MB_ERR_INVALID_CHARS: refuse rather than emit U+FFFD. A reader speaks
     * those as noise, and noise cannot be told from real text by ear. */
    got = MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, in, -1,
                              out, (int)cap);
    if (got <= 0) { out[0] = 0; return FALSE; }
    return out[0] != 0;
}

SIZE_T tc_squeeze(wchar_t *s)
{
    SIZE_T i, n = 0;
    if (!s) return 0;
    for (i = 0; s[i]; ++i) {
        if (s[i] == L' ' && (!n || s[n-1] == L' ')) continue;
        s[n++] = s[i];
    }
    while (n && s[n-1] == L' ') --n;
    s[n] = 0;
    return n;
}

BOOL tc_has_letters(const wchar_t *s)
{
    SIZE_T i;
    if (!s) return FALSE;
    for (i = 0; s[i]; ++i)
        if ((s[i] >= L'A' && s[i] <= L'Z') || (s[i] >= L'a' && s[i] <= L'z') ||
            s[i] > 127)
            return TRUE;
    return FALSE;
}

/* ---- Speech ------------------------------------------------------------- */

BOOL tc_say(const wchar_t *text, BOOL interrupt, const char *what)
{
    /* The log is UTF-8. "%ls" converts through the C locale and printed
     * NOTHING for a Japanese line (2026-10-15: "cvs2-quote: spoke: " with the
     * quote missing), so the text is converted here. */
    char utf8[1024];
    if (!text || !text[0]) return FALSE;
    if (!WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, (int)sizeof utf8, NULL, NULL))
        lstrcpynA(utf8, "(text too long to log)", sizeof utf8);
    if (speech_say(text, interrupt)) {
        log_line("%s: spoke: %s", what ? what : "text", utf8);
        return TRUE;
    }
    /* ⚠ Not a warning-and-carry-on. The caller must NOT advance its cursor on
     * this, which is the whole reason this function returns a value. */
    log_line("%s: ⛔ NOT SPOKEN (backend refused): %s", what ? what : "text",
             utf8);
    return FALSE;
}

/* ---- Captured frames ---------------------------------------------------- */

/* ⚠ The fault-guarded narrow copy lives in text_capture.c, NOT here.
 *
 * It is the one piece of this layer that needs memsafe's TLS guard, and it is
 * needed ONLY by a reader that captures frames off a render thread. Keeping it
 * in its own translation unit means a reader that merely queues finished lines
 * (the story/credits shape) links neither the guard nor a stub for it -- and
 * an offline suite for such a reader keeps its own mem_copy seam intact. */

void tc_frame_publish(tc_publisher *p, tc_frame *collecting)
{
    if (!p || !collecting) return;
    /* TryAcquire: a render thread must never wait on our worker. Losing the
     * race costs one frame, and the next frame republishes. */
    if (TryAcquireSRWLockExclusive(&p->lock)) {
        p->published = *collecting;
        p->published_at = GetTickCount64();
        ++p->generation;
        ReleaseSRWLockExclusive(&p->lock);
    }
    /* Even an empty frame replaces the old screen -- see the header. */
    collecting->count = 0;
    collecting->overflow = FALSE;
}

static int row_order(const void *aa, const void *bb)
{
    const tc_row *a = aa, *b = bb;
    if (a->y != b->y) return a->y < b->y ? -1 : 1;
    if (a->x != b->x) return a->x < b->x ? -1 : 1;
    return 0;
}

BOOL tc_frame_take(tc_publisher *p, tc_frame *out, unsigned *consumed,
                   BOOL force, ULONGLONG max_age_ms)
{
    unsigned seq;
    ULONGLONG at, now = GetTickCount64();
    BOOL fresh, got = FALSE;
    if (!p || !out || !consumed) return FALSE;
    AcquireSRWLockShared(&p->lock);
    seq = p->generation;
    at = p->published_at;
    fresh = seq && at && now - at <= max_age_ms;
    if (fresh && (force || seq != *consumed)) { *out = p->published; got = TRUE; }
    ReleaseSRWLockShared(&p->lock);
    if (!got) return FALSE;
    *consumed = seq;
    qsort(out->rows, out->count, sizeof out->rows[0], row_order);
    return TRUE;
}

void tc_frame_invalidate(tc_publisher *p)
{
    if (!p) return;
    AcquireSRWLockExclusive(&p->lock);
    p->published_at = 0; /* Require a frame produced after this change. */
    ReleaseSRWLockExclusive(&p->lock);
}

/* ---- Text-keyed de-duplication ------------------------------------------ */

void tc_textdedup_reset(tc_textdedup *d)
{
    if (d) memset(d, 0, sizeof *d);
}

int tc_textdedup_note(tc_textdedup *d, const wchar_t *text, ULONGLONG now,
                      ULONGLONG rearm_ms, BOOL *suppress)
{
    int slot = -1, oldest = 0;
    unsigned i;
    if (suppress) *suppress = FALSE;
    if (!d || !text) return -1;
    for (i = 0; i < TC_ROW_CAP; ++i) {
        if (!wcscmp(d->slot[i].text, text)) { slot = (int)i; break; }
        if (d->slot[i].seen < d->slot[oldest].seen) oldest = (int)i;
    }
    if (slot < 0) {
        slot = oldest;
        lstrcpynW(d->slot[slot].text, text, TC_LINE_CAP);
        d->slot[slot].spoken = FALSE;
    } else if (now - d->slot[slot].seen > rearm_ms) {
        d->slot[slot].spoken = FALSE;
    }
    d->slot[slot].seen = now;
    if (suppress) *suppress = d->slot[slot].spoken;
    return slot;
}

void tc_textdedup_mark(tc_textdedup *d, int slot)
{
    if (d && slot >= 0 && slot < TC_ROW_CAP) d->slot[slot].spoken = TRUE;
}

/* ---- Index-keyed de-duplication ----------------------------------------- */

void tc_keydedup_sync(tc_keydedup *d, LONG generation)
{
    if (!d) return;
    if (d->generation != generation) {
        memset(d->seen, 0, sizeof d->seen);
        d->generation = generation;
    }
}

BOOL tc_keydedup_hit(tc_keydedup *d, unsigned key, ULONGLONG now,
                     ULONGLONG rearm_ms)
{
    if (!d || key >= 256) return FALSE;
    if (d->seen[key] && now - d->seen[key] < rearm_ms) {
        d->seen[key] = now;
        return TRUE;
    }
    return FALSE;
}

void tc_keydedup_set(tc_keydedup *d, unsigned key, ULONGLONG now)
{
    if (d && key < 256) d->seen[key] = now;
}

/* ---- FIFO -------------------------------------------------------------- */

BOOL tc_queue_push(tc_queue *q, const wchar_t *text)
{
    BOOL ok = FALSE;
    if (!q || !text || !text[0]) return FALSE;
    /* Never wait on a game thread. A refused push leaves the producer's cursor
     * unchanged, so the line is offered again rather than lost. */
    if (!TryAcquireSRWLockExclusive(&q->lock)) return FALSE;
    if (q->head - q->tail < TC_QUEUE_CAP) {
        lstrcpynW(q->line[q->head % TC_QUEUE_CAP], text, TC_LINE_CAP);
        ++q->head;
        ok = TRUE;
    }
    ReleaseSRWLockExclusive(&q->lock);
    return ok;
}

BOOL tc_queue_drain(tc_queue *q, const char *what)
{
    wchar_t text[TC_LINE_CAP];
    unsigned item;
    if (!q) return FALSE;
    AcquireSRWLockExclusive(&q->lock);
    if (q->head == q->tail) { ReleaseSRWLockExclusive(&q->lock); return FALSE; }
    item = q->tail;
    memcpy(text, q->line[q->tail % TC_QUEUE_CAP], sizeof text);
    ReleaseSRWLockExclusive(&q->lock);
    /* Spoken outside the lock; advanced only on acceptance. */
    if (!tc_say(text, FALSE, what)) return FALSE;
    AcquireSRWLockExclusive(&q->lock);
    if (q->tail == item) ++q->tail;
    ReleaseSRWLockExclusive(&q->lock);
    return TRUE;
}

void tc_queue_clear(tc_queue *q)
{
    if (!q) return;
    AcquireSRWLockExclusive(&q->lock);
    q->tail = q->head;
    ReleaseSRWLockExclusive(&q->lock);
}

/* ---- Settling ---------------------------------------------------------- */

void tc_settle_reset(tc_settle *s)
{
    if (!s) return;
    s->epoch = 0;
    s->spoken = FALSE;
    s->pending[0] = 0;
}

BOOL tc_settle_ready(tc_settle *s, ULONGLONG epoch, const wchar_t *text)
{
    if (!s || !text || !text[0]) return FALSE;
    if (epoch != s->epoch) {
        s->epoch = epoch;
        s->spoken = FALSE;
        s->pending[0] = 0;
    }
    if (s->spoken) return FALSE;
    /* Two consecutive identical reads: one read can catch a buffer mid-write. */
    if (wcscmp(text, s->pending)) {
        lstrcpynW(s->pending, text, TC_LINE_CAP);
        return FALSE;
    }
    return TRUE;
}

void tc_settle_mark(tc_settle *s)
{
    if (s) s->spoken = TRUE;
}

void tc_settle_break(tc_settle *s)
{
    if (s) s->pending[0] = 0;
}

/* ---- The read-current-text registry ------------------------------------ */

#define TC_MAX_SOURCES 12
static struct { const char *name; tc_read_fn read; } g_sources[TC_MAX_SOURCES];
static int g_source_count;
static volatile LONG g_requested;

BOOL tc_register(const char *name, tc_read_fn read)
{
    if (!name || !read) return FALSE;
    if (g_source_count >= TC_MAX_SOURCES) {
        /* ⚠ Loud: a source that failed to register is a key that silently does
         * nothing for that game, which is exactly what cannot be diagnosed by
         * ear. Raise TC_MAX_SOURCES rather than letting this pass. */
        log_line("text-channel: ⛔ cannot register '%s' -- the source table is "
                 "full (%d). Read-current-text will not cover it.",
                 name, TC_MAX_SOURCES);
        return FALSE;
    }
    g_sources[g_source_count].name = name;
    g_sources[g_source_count].read = read;
    ++g_source_count;
    /* ⚠ Name the KEY in the log, not just the source. A hotkey that does
     * nothing is undiagnosable by ear, so the log must state which key is bound
     * -- and by POSITION as well as by character, since VK_OEM_3 is the key left
     * of `1` and does not produce ` on every layout. */
    log_line("text-channel: registered '%s' as read-current-text source %d "
             "(key: ` / the key left of 1)", name, g_source_count);
    return TRUE;
}

void tc_request_read(void) { InterlockedExchange(&g_requested, 1); }

BOOL tc_read_requested(void)
{
    return InterlockedExchange(&g_requested, 0) != 0;
}

void tc_read_current(void)
{
    int i, inactive = 0, empty = 0, stale = 0, refused = 0;
    if (!g_source_count) {
        speech_say(mt(MT_TEXT_NO_READER), TRUE);
        return;
    }
    for (i = 0; i < g_source_count; ++i) {
        switch (g_sources[i].read()) {
        case TC_READ_SPOKE:    return;
        case TC_READ_EMPTY:    ++empty; break;
        case TC_READ_STALE:    ++stale; break;
        case TC_READ_INACTIVE: ++inactive; break;
        case TC_READ_REFUSED:  ++refused; break;
        }
    }
    /* ⭐ Every outcome gets a DIFFERENT sentence. "Nothing is running", "there
     * is text but it is stale" and "the screen genuinely has no words on it"
     * are three distinct facts, and collapsing them into silence -- or into one
     * message -- is what makes a hotkey undiagnosable without sight. */
    if (refused)
        log_line("text-channel: a source assembled text the backend refused");
    else if (empty)
        speech_say(mt(MT_TEXT_NOTHING), TRUE);
    else if (stale)
        speech_say(mt(MT_TEXT_STALE), TRUE);
    else if (inactive == g_source_count)
        speech_say(mt(MT_TEXT_INACTIVE), TRUE);
}

void tc_test_reset(void)
{
    memset(g_sources, 0, sizeof g_sources);
    g_source_count = 0;
    g_requested = 0;
}

int tc_test_source_count(void) { return g_source_count; }
