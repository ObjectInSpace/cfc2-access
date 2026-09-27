/*
 * text_channel -- the shared speech and read-current-text layer.
 *
 * WHY THIS EXISTS
 * ---------------
 * Four readers shipped before this file, and each one re-implemented the same
 * five mechanics with its own bugs available to be found separately:
 *
 *   cvs2_text.c    positioned records + font runs, frame-published, ` readout
 *   cvs2_story.c   story pages + the credit stream, FIFO queue
 *   cvs2_select.c  win quotes, decoded out of the guarded state copy
 *   menu_speak.c   the shell: captions, modal prompts, hints
 *
 * The five mechanics are:
 *
 *   1. narrow bytes -> UTF-16, refusing anything that does not convert
 *   2. a capture buffer written on the game's render thread and read on ours
 *   3. invalidation when the session/screen underneath the capture changes
 *   4. de-duplication, so a redrawn line is not spoken again
 *   5. a drain that advances ONLY when the backend accepted the text
 *
 * ⭐ (5) is the one worth centralising even on its own. Every reader here had
 * to learn it separately, and `speech.c`'s own comment records what it costs to
 * get wrong: a log full of tidy "spoke" lines while the user hears nothing.
 * `tc_say()` is the single place that couples "said it" to "mark it spoken".
 *
 * ⛔⛔ WHAT THIS FILE DELIBERATELY DOES NOT DO: unify the de-dup POLICY.
 * The three CVS2 readers key their suppression differently and each difference
 * is load-bearing, measured, and regression-pinned:
 *
 *   - text:  keyed on the TEXT, 2000 ms re-arm. "PRESS START" blinks; it must
 *            not repeat every blink, but it must speak again on a later visit.
 *   - story: keyed on the ENTRY INDEX, 1500 ms re-arm, and FIFO -- every page
 *            must be spoken, in order. Dropping one loses dialogue.
 *   - quote: ONE SHOT per root, and it needs two identical reads to settle,
 *            because the buffer is read while the animation fills it.
 *
 * A single policy would have to be the union of those, and the union is wrong
 * for all three. So the policy is the CALLER's, expressed by which primitive it
 * picks; this file supplies the mechanism only. That is also what makes it
 * cheap to port: a new game writes a capture method and chooses a suppressor.
 *
 * PORTING A NEW GAME (the whole cost)
 * -----------------------------------
 *   1. Hook that game's draw path and call tc_frame_* from the detours.
 *      Nothing else in this file runs on a render thread.
 *   2. Give the worker a tc_source and call tc_source_observe() from its poll.
 *   3. Register the source with tc_register() so the ` key can read it.
 *
 * The decode step is a per-game function pointer (tc_decode_fn) because that is
 * the part that is genuinely different: CP932 records, a credit glyph map, the
 * shell's UTF-8. Everything around it is shared.
 *
 * ⚠⚠ THREADING. tc_frame_begin/add/publish are the ONLY functions callable
 * from a game thread. They take no lock that can block (TryAcquire only),
 * allocate nothing, log nothing, and never speak -- see the note in speech.c
 * about why speaking on the render thread is a frozen game. Everything else on
 * this interface is worker-thread only.
 */
#ifndef CFC2_TEXT_CHANNEL_H
#define CFC2_TEXT_CHANNEL_H

#include <windows.h>

#define TC_LINE_CAP 512   /* longest single spoken line, wchars incl. NUL */
#define TC_SAY_CAP  8192  /* longest assembled utterance */
#define TC_ROW_CAP  160   /* rows one captured frame may hold */
#define TC_RAW_CAP  80    /* longest raw (narrow) row a capture may carry */

/* ---- Conversion --------------------------------------------------------- */

/* Narrow -> UTF-16 with a named code page, refusing invalid input rather than
 * emitting replacement characters. A screen reader renders those as noise, and
 * noise is indistinguishable from real text to the user.
 * Returns FALSE and leaves out[0] == 0 on any failure, including truncation. */
BOOL tc_decode_cp(unsigned code_page, const char *in, wchar_t *out, SIZE_T cap);

/* Collapse runs of spaces and trim both ends, in place. Returns the new length.
 * Presentation padding is how several native records centre their text. */
SIZE_T tc_squeeze(wchar_t *s);

/* TRUE if the text contains at least one letter (ASCII or non-ASCII). Digits
 * and punctuation alone are a HUD value, not a sentence -- callers use this to
 * keep unlabelled numbers out of automatic narration while still offering them
 * to the on-demand readout. */
BOOL tc_has_letters(const wchar_t *s);

/* ---- Speech ------------------------------------------------------------- */

/* The single speech entry point for every reader. Wraps speech_say() so that
 * "the backend accepted it" is the only thing that ever advances a cursor, a
 * latch or a queue. `what` names the channel in the log. */
BOOL tc_say(const wchar_t *text, BOOL interrupt, const char *what);

/* ---- Captured frames (render thread + worker) --------------------------- */

/* One captured row. x/y order the readout; `style` and `kind` are the capturing
 * game's own classification, passed through untouched so its filter can use it
 * without this layer needing to know what they mean. */
typedef struct {
    float x, y;
    int style, kind;
    char raw[TC_RAW_CAP];
} tc_row;

typedef struct {
    unsigned count;
    BOOL overflow;
    tc_row rows[TC_ROW_CAP];
} tc_frame;

/* A frame under construction, plus the last one published. One per game. */
typedef struct {
    SRWLOCK lock;
    tc_frame published;
    ULONGLONG published_at;
    unsigned generation;
} tc_publisher;

/* Render thread. Copies `text` defensively (one byte at a time to NUL, never
 * assuming the caller owns a full buffer) into the caller's collecting frame.
 * Rejects a non-finite coordinate, an unreadable pointer and an unterminated
 * string. Sets `overflow` rather than dropping silently past TC_ROW_CAP. */
void tc_frame_add(tc_frame *collecting, float x, float y, int style, int kind,
                  const char *text, SIZE_T cap);

/* Render thread. Publishes `collecting` and resets it for the next frame.
 * ⚠ An EMPTY frame still replaces its predecessor: the screen really did go
 * blank, and reading the previous screen's text back would be a lie. */
void tc_frame_publish(tc_publisher *p, tc_frame *collecting);

/* Worker. Copies the published frame out if it is newer than `*consumed` (or
 * `force`), and no older than `max_age_ms`. Returns FALSE when there is nothing
 * fresh to read, which is NOT the same as "the screen is empty" -- the caller
 * distinguishes those for the user. Sorts the rows into reading order. */
BOOL tc_frame_take(tc_publisher *p, tc_frame *out, unsigned *consumed,
                   BOOL force, ULONGLONG max_age_ms);

/* Worker. Discards anything published before now, so a frame captured under the
 * previous session cannot be read as the current one's. */
void tc_frame_invalidate(tc_publisher *p);

/* ---- De-duplication: keyed on the text, with a re-arm ------------------- */

/* Suppresses a line already spoken, until `rearm_ms` of not seeing it passes.
 * This is the blinking-prompt policy. Sized for one screen's worth of rows. */
typedef struct {
    struct { wchar_t text[TC_LINE_CAP]; ULONGLONG seen; BOOL spoken; }
        slot[TC_ROW_CAP];
} tc_textdedup;

void tc_textdedup_reset(tc_textdedup *d);

/* Returns the slot index for `text`, evicting the least-recently-seen entry
 * when full, and stamps it as seen at `now`. Sets *suppress to TRUE when this
 * text has already been spoken and has not been away for `rearm_ms`.
 * ⚠ The slot is NOT marked spoken here -- only tc_textdedup_mark() does that,
 * and only after the backend accepted. */
int tc_textdedup_note(tc_textdedup *d, const wchar_t *text, ULONGLONG now,
                      ULONGLONG rearm_ms, BOOL *suppress);
void tc_textdedup_mark(tc_textdedup *d, int slot);

/* ---- De-duplication: keyed on an index, with a re-arm ------------------- */

/* The story-page policy: the same page re-drawn for several frames is one
 * utterance, but returning to it later is new. `count` entries, caller-owned.
 * Generation-stamped so a session change clears it without a memset race. */
typedef struct {
    ULONGLONG seen[256];
    LONG generation;
} tc_keydedup;

/* Clears the local table when `generation` moved. Call before tc_keydedup_hit.*/
void tc_keydedup_sync(tc_keydedup *d, LONG generation);
/* TRUE when `key` was seen within `rearm_ms`; refreshes its stamp either way. */
BOOL tc_keydedup_hit(tc_keydedup *d, unsigned key, ULONGLONG now,
                     ULONGLONG rearm_ms);
void tc_keydedup_set(tc_keydedup *d, unsigned key, ULONGLONG now);

/* ---- A FIFO of finished lines ------------------------------------------- */

/* For a capture that produces complete lines on a game thread and wants every
 * one of them spoken in order (story pages, credit lines). */
#define TC_QUEUE_CAP 256
typedef struct {
    SRWLOCK lock;
    wchar_t line[TC_QUEUE_CAP][TC_LINE_CAP];
    unsigned head, tail;
} tc_queue;

/* Game thread. Never blocks: returns FALSE if the lock is held or the queue is
 * full, and the caller leaves its own cursor unchanged so the line is retried.*/
BOOL tc_queue_push(tc_queue *q, const wchar_t *text);
/* Worker. Speaks at most one line, and advances only on acceptance. */
BOOL tc_queue_drain(tc_queue *q, const char *what);
void tc_queue_clear(tc_queue *q);

/* ---- Settling a buffer that is being written while we read it ----------- */

/* The win-quote policy. A decoded value is accepted only after two consecutive
 * identical reads, which is what distinguishes a finished buffer from an
 * animation part-way through filling it. One shot per `epoch` (e.g. the session
 * root); a new epoch re-arms. */
typedef struct {
    ULONGLONG epoch;
    BOOL spoken;
    wchar_t pending[TC_LINE_CAP];
} tc_settle;

void tc_settle_reset(tc_settle *s);
/* Returns TRUE when `text` is settled and not yet spoken for this epoch. */
BOOL tc_settle_ready(tc_settle *s, ULONGLONG epoch, const wchar_t *text);
void tc_settle_mark(tc_settle *s);
/* Forgets the pending copy WITHOUT clearing the spoken latch or the epoch.
 * ⚠ Needed when a read fails to decode: otherwise a torn copy could pair with
 * an identical one from after the interruption and be treated as settled, which
 * is exactly the state the two-read rule exists to reject. */
void tc_settle_break(tc_settle *s);

/* ---- The read-current-text registry (the ` key) -------------------------------- */

/* ⭐⭐ WHY A REGISTRY AND NOT A CALL PER GAME. The ` key must mean "read what is on
 * the screen right now" everywhere -- the shell and all eight arcade titles.
 * With a call per game, mod.c grows a branch per port and the KEY's behaviour
 * becomes a property of that branch list. Here a source is either present and
 * active or it is not, and its own code never changes again.
 *
 * ⚠ And the failure messages live here for the same reason: "nothing is
 * active" and "nothing readable is on screen" are DIFFERENT answers, and a
 * blind user cannot tell a key that did nothing from a key that is not bound.
 * Silence is never an acceptable response to the ` key. */
typedef enum {
    TC_READ_SPOKE,      /* text was assembled and the backend took it */
    TC_READ_EMPTY,      /* the source is live but has nothing readable */
    TC_READ_STALE,      /* the source is live but its capture is too old */
    TC_READ_INACTIVE,   /* this source is not the one currently running */
    TC_READ_REFUSED     /* text was assembled and the backend refused it */
} tc_read_result;

/* A source is asked, in registration order, to read its current screen. */
typedef tc_read_result (*tc_read_fn)(void);

/* Registers a source under a spoken name ("CVS2", "the menus"). Returns FALSE
 * if the table is full, which is logged -- a source that silently failed to
 * register is a key that silently does nothing. */
BOOL tc_register(const char *name, tc_read_fn read);

/* Worker. Asks each registered source in turn and speaks a reason if none
 * could read anything. Safe to call with no sources registered. */
void tc_read_current(void);

/* Worker-thread request flag, set from the hotkey poll. Kept here so every
 * game's reader observes the same request rather than each polling its own. */
void tc_request_read(void);
BOOL tc_read_requested(void);

/* ---- Test seams --------------------------------------------------------- */

/* The registry is process-global; the offline suites need it empty per case. */
void tc_test_reset(void);
int  tc_test_source_count(void);

#endif /* CFC2_TEXT_CHANNEL_H */
