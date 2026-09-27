/* The shared text layer's own mechanisms, in isolation.
 *
 * ⚠ This suite tests the MECHANISM, not any game's capture. The per-game
 * readers keep their own suites (run_cvs2_text_test, run_cvs2_story_test,
 * run_cvs2_test); this one exists because those three now share code, and a
 * fault in the shared half would otherwise present as three unrelated bugs.
 *
 * Every case below was run with its mechanism deliberately sabotaged and seen
 * to FAIL -- the sabotage that isolates each one is named in its comment. */
#include "../src/text_channel.c"
#include "../src/text_capture.c"
#include <assert.h>
#include <stdio.h>

static int calls, accepts = 1;
static BOOL last_interrupt;
static wchar_t last[TC_SAY_CAP];
BOOL speech_say(const wchar_t *text, BOOL interrupt)
{ ++calls; wcscpy(last, text); last_interrupt = interrupt; return accepts; }
void log_line(const char *format, ...) { (void)format; }

/* ---- Registry sources, scripted per case ------------------------------- */
static tc_read_result r1, r2;
static int asked1, asked2;
static tc_read_result src1(void) { ++asked1; return r1; }
static tc_read_result src2(void) { ++asked2; return r2; }

static void case_conversion(void)
{
    wchar_t w[TC_LINE_CAP];
    /* Sabotage: drop MB_ERR_INVALID_CHARS -> the invalid byte converts to
     * U+FFFD and this assertion fails. That is the whole point of refusing:
     * a reader speaks U+FFFD as noise, indistinguishable from real text. */
    assert(!tc_decode_cp(932, "A\x90\x90\x90", w, TC_LINE_CAP));
    assert(!w[0]);
    assert(tc_decode_cp(932, "READY", w, TC_LINE_CAP) && !wcscmp(w, L"READY"));
    /* An empty source string is not text. */
    assert(!tc_decode_cp(932, "", w, TC_LINE_CAP));

    wcscpy(w, L"   ROUND    ONE   ");
    /* Sabotage: make tc_squeeze a no-op -> fails. Padding is how several
     * native records centre their text; spoken, it becomes long pauses. */
    assert(tc_squeeze(w) == 9 && !wcscmp(w, L"ROUND ONE"));
    wcscpy(w, L"   ");
    assert(tc_squeeze(w) == 0 && !w[0]);

    assert(tc_has_letters(L"CONTINUE 9"));
    assert(tc_has_letters(L"\x30ea\x30e5\x30a6"));   /* non-ASCII counts */
    /* Sabotage: return TRUE unconditionally -> fails. Unlabelled HUD digits
     * would then enter automatic narration and read out every frame. */
    assert(!tc_has_letters(L"12345"));
    assert(!tc_has_letters(L"00:00"));
    puts("  ok 1  conversion refuses invalid input, squeezes padding, finds letters");
}

static void case_say_couples_to_acceptance(void)
{
    calls = 0; accepts = 1;
    assert(tc_say(L"hello", TRUE, "t") && calls == 1 && last_interrupt);
    accepts = 0;
    /* ⭐⭐ THE CASE THIS WHOLE FILE EXISTS FOR. Sabotage: make tc_say return
     * TRUE regardless -> fails. Every caller advances a cursor/latch/queue on
     * this value, so a tc_say that lies makes a silent mod look healthy. */
    assert(!tc_say(L"hello", TRUE, "t") && calls == 2);
    accepts = 1;
    /* Empty text is never spoken, and never counts as a refusal either. */
    assert(!tc_say(L"", TRUE, "t") && !tc_say(NULL, TRUE, "t") && calls == 2);
    puts("  ok 2  tc_say reports what the backend did, and refuses empty text");
}

static void case_frames(void)
{
    static tc_publisher pub;
    tc_frame collecting = {0}, got = {0};
    unsigned consumed = 0;
    tc_frame_add(&collecting, 10, 20, 0, 1, "SECOND", 8);
    tc_frame_add(&collecting, 10, 10, 0, 1, "FIRST", 8);
    assert(collecting.count == 2);
    /* Nothing is visible before publication: a half-built frame is not a screen. */
    assert(!tc_frame_take(&pub, &got, &consumed, TRUE, 500));
    tc_frame_publish(&pub, &collecting);
    assert(collecting.count == 0); /* reset for the next frame */
    assert(tc_frame_take(&pub, &got, &consumed, FALSE, 500) && got.count == 2);
    /* Sabotage: drop the qsort in tc_frame_take -> fails. Rows arrive in draw
     * order, which is not reading order. */
    assert(!strcmp(got.rows[0].raw, "FIRST") && !strcmp(got.rows[1].raw, "SECOND"));
    /* The same generation is not served twice unless forced. */
    assert(!tc_frame_take(&pub, &got, &consumed, FALSE, 500));
    assert(tc_frame_take(&pub, &got, &consumed, TRUE, 500));

    /* An EMPTY frame replaces its predecessor rather than leaving the old text
     * readable. Sabotage: skip publishing an empty frame -> fails, and the ` key would
     * report the previous screen as current. */
    tc_frame_publish(&pub, &collecting);
    assert(tc_frame_take(&pub, &got, &consumed, TRUE, 500) && got.count == 0);

    /* Staleness and invalidation are distinct from emptiness. */
    tc_frame_add(&collecting, 0, 0, 0, 1, "X", 2);
    tc_frame_publish(&pub, &collecting);
    pub.published_at = GetTickCount64() - 5000;
    assert(!tc_frame_take(&pub, &got, &consumed, TRUE, 500));
    tc_frame_add(&collecting, 0, 0, 0, 1, "X", 2);
    tc_frame_publish(&pub, &collecting);
    assert(tc_frame_take(&pub, &got, &consumed, TRUE, 500));
    tc_frame_invalidate(&pub);
    /* Sabotage: make tc_frame_invalidate a no-op -> fails, and a frame captured
     * under the PREVIOUS session reads as the current one's. */
    assert(!tc_frame_take(&pub, &got, &consumed, TRUE, 500));
    puts("  ok 3  frames publish atomically, sort, expire, and invalidate");
}

static void case_frame_add_guards(void)
{
    tc_frame f = {0};
    char unterminated[4];
    memset(unterminated, 'A', sizeof unterminated);
    assert(mem_guard_install());
    /* An unreadable pointer is contained, not fatal. */
    tc_frame_add(&f, 0, 0, 0, 0, (const char *)(ULONG_PTR)1, 8);
    assert(f.count == 0);
    /* No NUL inside the cap -> rejected rather than half-spoken. */
    tc_frame_add(&f, 0, 0, 0, 0, unterminated, sizeof unterminated);
    assert(f.count == 0);
    /* A NaN coordinate cannot be ordered; NaN compares false against
     * everything, so admitting it would corrupt the sort, not misplace a row. */
    tc_frame_add(&f, (float)(0.0 / 0.0), 0, 0, 0, "X", 2);
    assert(f.count == 0);
    /* Overflow is COUNTED. Sabotage: drop the overflow flag -> fails, and a
     * truncated screen becomes indistinguishable from a short one. */
    for (int i = 0; i < TC_ROW_CAP + 4; ++i) tc_frame_add(&f, 0, (float)i, 0, 0, "X", 2);
    assert(f.count == TC_ROW_CAP && f.overflow);
    puts("  ok 4  tc_frame_add contains bad pointers, NaN, truncation; counts overflow");
}

static void case_textdedup(void)
{
    static tc_textdedup d;
    ULONGLONG t = 100000;
    BOOL sup = FALSE;
    int slot = tc_textdedup_note(&d, L"PRESS START", t, 2000, &sup);
    assert(slot >= 0 && !sup);
    /* Not suppressed until MARKED: the mark is what couples suppression to a
     * line the backend actually accepted. Sabotage: mark inside note() ->
     * a refused line is never retried and is lost silently. */
    assert(tc_textdedup_note(&d, L"PRESS START", t, 2000, &sup) >= 0 && !sup);
    tc_textdedup_mark(&d, slot);
    assert(tc_textdedup_note(&d, L"PRESS START", t + 500, 2000, &sup) >= 0 && sup);
    /* Still suppressed while it keeps being seen -- this is the blink case. */
    for (int i = 0; i < 20; ++i)
        assert(tc_textdedup_note(&d, L"PRESS START", t + 500 + i * 100, 2000, &sup) >= 0 && sup);
    /* Away for longer than the re-arm -> new again. Sabotage: never re-arm ->
     * fails, and a prompt is announced once per session however often the
     * user returns to that screen. */
    assert(tc_textdedup_note(&d, L"PRESS START", t + 30000, 2000, &sup) >= 0 && !sup);
    /* A different line is independent. */
    assert(tc_textdedup_note(&d, L"GAME OVER", t + 30000, 2000, &sup) >= 0 && !sup);
    tc_textdedup_reset(&d);
    assert(tc_textdedup_note(&d, L"PRESS START", t + 30000, 2000, &sup) >= 0 && !sup);
    puts("  ok 5  text de-dup suppresses a blink, re-arms after absence, resets");
}

static void case_keydedup(void)
{
    static tc_keydedup d;
    ULONGLONG t = 50000;
    tc_keydedup_sync(&d, 1);
    assert(!tc_keydedup_hit(&d, 7, t, 1500));
    tc_keydedup_set(&d, 7, t);
    assert(tc_keydedup_hit(&d, 7, t + 100, 1500));
    /* Sabotage: do not refresh the stamp on a hit -> a page re-drawn steadily
     * for longer than the re-arm repeats itself mid-display. */
    assert(tc_keydedup_hit(&d, 7, t + 1400, 1500));
    assert(tc_keydedup_hit(&d, 7, t + 2600, 1500));
    /* A gap longer than the re-arm makes it new. */
    assert(!tc_keydedup_hit(&d, 7, t + 20000, 1500));
    /* A generation bump clears it: leaving the game must not carry suppression
     * into the next session. Sabotage: ignore the generation -> fails. */
    tc_keydedup_set(&d, 7, t + 20000);
    assert(tc_keydedup_hit(&d, 7, t + 20100, 1500));
    tc_keydedup_sync(&d, 2);
    assert(!tc_keydedup_hit(&d, 7, t + 20100, 1500));
    /* Out-of-range keys are rejected, never written past the table. */
    assert(!tc_keydedup_hit(&d, 999, t, 1500));
    tc_keydedup_set(&d, 999, t);
    puts("  ok 6  index de-dup holds a redrawn page, re-arms, clears per session");
}

static void case_queue(void)
{
    static tc_queue q;
    calls = 0; accepts = 1;
    assert(tc_queue_push(&q, L"line one"));
    assert(tc_queue_push(&q, L"line two"));
    assert(!tc_queue_push(&q, L""));              /* nothing to say */
    assert(tc_queue_drain(&q, "t") && !wcscmp(last, L"line one"));
    /* FIFO: order is the content here, not a preference. Sabotage: drain from
     * head -> dialogue is spoken backwards. */
    assert(tc_queue_drain(&q, "t") && !wcscmp(last, L"line two"));
    assert(!tc_queue_drain(&q, "t"));             /* empty */

    /* A refused line is RETRIED, not dropped. Sabotage: advance tail before
     * checking tc_say -> the line is lost with no trace. */
    assert(tc_queue_push(&q, L"retry me"));
    accepts = 0;
    assert(!tc_queue_drain(&q, "t"));
    accepts = 1;
    assert(tc_queue_drain(&q, "t") && !wcscmp(last, L"retry me"));

    /* Full is reported, never overwritten: a wrapped queue would speak the
     * newest lines while claiming to be reading in order. */
    for (int i = 0; i < TC_QUEUE_CAP; ++i) assert(tc_queue_push(&q, L"x"));
    assert(!tc_queue_push(&q, L"overflow"));
    tc_queue_clear(&q);
    assert(!tc_queue_drain(&q, "t"));
    puts("  ok 7  queue is FIFO, retries a refusal, reports full, clears");
}

static void case_settle(void)
{
    static tc_settle s;
    /* One read is never enough: the buffer is filled by an animation, so a
     * single read can catch it part-way. Sabotage: return TRUE on the first
     * read -> a half-written quote is spoken. */
    assert(!tc_settle_ready(&s, 1, L"You cannot win."));
    assert(tc_settle_ready(&s, 1, L"You cannot win."));
    /* Not latched until MARKED, so a refusal is retried. */
    assert(tc_settle_ready(&s, 1, L"You cannot win."));
    tc_settle_mark(&s);
    assert(!tc_settle_ready(&s, 1, L"You cannot win."));
    /* A new epoch re-arms -- the next match must speak its own quote. */
    assert(!tc_settle_ready(&s, 2, L"You cannot win."));
    assert(tc_settle_ready(&s, 2, L"You cannot win."));
    tc_settle_mark(&s);
    /* tc_settle_break stops a torn read pairing ACROSS an interruption.
     * Sabotage: make it a no-op -> two reads separated by a failed decode
     * count as consecutive, which is exactly what the rule must reject. */
    tc_settle_reset(&s);
    assert(!tc_settle_ready(&s, 3, L"Round two."));
    tc_settle_break(&s);
    assert(!tc_settle_ready(&s, 3, L"Round two."));
    assert(tc_settle_ready(&s, 3, L"Round two."));
    puts("  ok 8  settle needs two consecutive reads, latches once, breaks a pair");
}

static void case_registry(void)
{
    tc_test_reset();
    calls = 0; accepts = 1;
    /* With no source at all the key STILL answers. Sabotage: return silently
     * when the table is empty -> the user cannot tell an unbound key from a
     * screen with no text, which is undiagnosable without sight. */
    tc_read_current();
    assert(calls == 1 && wcsstr(last, L"No text reader"));

    assert(tc_register("one", src1) && tc_register("two", src2));
    assert(tc_test_source_count() == 2);
    assert(!tc_register(NULL, src1) && !tc_register("x", NULL));

    /* The FIRST source that speaks wins, and later sources are not asked --
     * registration order is the priority order. Sabotage: keep asking after a
     * success -> two games narrate over each other. */
    asked1 = asked2 = 0; r1 = TC_READ_SPOKE; r2 = TC_READ_SPOKE;
    tc_read_current();
    assert(asked1 == 1 && asked2 == 0);

    /* Every distinct outcome gets its own sentence. */
    calls = 0; asked1 = asked2 = 0;
    r1 = TC_READ_INACTIVE; r2 = TC_READ_EMPTY;
    tc_read_current();
    assert(asked1 == 1 && asked2 == 1 && calls == 1 &&
           wcsstr(last, L"No readable text"));

    calls = 0; r1 = TC_READ_INACTIVE; r2 = TC_READ_STALE;
    tc_read_current();
    assert(calls == 1 && wcsstr(last, L"No fresh"));

    /* ⭐ EMPTY outranks STALE: "there is nothing written on this screen" is a
     * more useful answer than "the capture is old", when one source says each. */
    calls = 0; r1 = TC_READ_EMPTY; r2 = TC_READ_STALE;
    tc_read_current();
    assert(calls == 1 && wcsstr(last, L"No readable text"));

    /* All inactive -> say so, rather than claiming the screen is blank. */
    calls = 0; r1 = TC_READ_INACTIVE; r2 = TC_READ_INACTIVE;
    tc_read_current();
    assert(calls == 1 && wcsstr(last, L"No text reader is active here"));

    /* A REFUSAL must not be papered over with a cheerful fallback sentence:
     * the backend already has text it would not speak, and announcing
     * "no readable text" on top of that would be false. */
    calls = 0; r1 = TC_READ_REFUSED; r2 = TC_READ_INACTIVE;
    tc_read_current();
    assert(calls == 0);
    puts("  ok 9  registry: priority order, one answer per outcome, never silent");
}

static void case_request_flag(void)
{
    tc_test_reset();
    assert(!tc_read_requested());
    tc_request_read();
    /* Taken exactly once: sabotage by not clearing it -> the ` key re-reads the
     * screen on every tick thereafter, which is a continuous readout. */
    assert(tc_read_requested());
    assert(!tc_read_requested());
    puts("  ok 10 the read request is consumed exactly once");
}

static void case_source_table_full(void)
{
    int i;
    tc_test_reset();
    for (i = 0; i < 12; ++i) assert(tc_register("x", src1));
    /* Refused and LOGGED, never silently ignored: a source that failed to
     * register is a key that does nothing for that game. */
    assert(!tc_register("overflow", src1));
    assert(tc_test_source_count() == 12);
    tc_test_reset();
    assert(tc_test_source_count() == 0);
    puts("  ok 11 a full source table refuses loudly and stays consistent");
}

int main(void)
{
    puts("shared text channel:");
    case_conversion();
    case_say_couples_to_acceptance();
    case_frames();
    case_frame_add_guards();
    case_textdedup();
    case_keydedup();
    case_queue();
    case_settle();
    case_registry();
    case_request_flag();
    case_source_table_full();
    puts("PASS: conversion, speech/acceptance coupling, frame publication and "
         "expiry, capture guards, both de-dup policies, FIFO retry, settling, "
         "registry priority and every failure sentence");
    mem_guard_remove();
    return 0;
}
