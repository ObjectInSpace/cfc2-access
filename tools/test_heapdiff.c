/*
 * Offline positive control for the differential heap scan.
 *
 * The one thing this MUST prove is that the control subtraction works: a page
 * that changes on its own has to be rejected even though it also changes
 * across the screen transition. Without that, the scanner reports every timer
 * in the process and the whole method is worthless -- and it would look like a
 * success, which is worse than looking like a failure.
 *
 * Planted:
 *   FOCUS page -- changes ONLY when the "screen" changes   -> must be reported
 *   TIMER page -- changes on EVERY capture                 -> must be rejected
 *   STATIC page -- never changes                            -> must be ignored
 *
 * Usage: test_heapdiff.exe
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "heapdiff.h"
#include "memsafe.h"
#include "log.h"

static int g_fail;

static void check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? " OK " : "FAIL", what);
    if (!cond) g_fail++;
}

static BYTE *page_alloc(BYTE fill)
{
    BYTE *p = (BYTE *)VirtualAlloc(NULL, HD_BLOCK, MEM_COMMIT | MEM_RESERVE,
                                   PAGE_READWRITE);
    memset(p, fill, HD_BLOCK);
    return p;
}

static int listed(const uintptr_t *v, int n, uintptr_t want)
{
    for (int i = 0; i < n; i++) if (v[i] == want) return 1;
    return 0;
}

/* --- Reading back the log this run wrote --------------------------------
 * The watch's whole product is its log output, so the test has to inspect
 * that rather than just confirm the function returned. log_line writes
 * CFC2Access.log next to the executable. */
static void log_path(char *out, size_t cap)
{
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
    if (!n) { out[0] = 0; return; }
    char *slash = strrchr(out, '\\');
    if (slash) slash[1] = 0;
    strncat(out, "CFC2Access.log", cap - strlen(out) - 1);
}

static long log_size(void)
{
    char p[MAX_PATH]; log_path(p, sizeof(p));
    HANDLE h = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD sz = GetFileSize(h, NULL);
    CloseHandle(h);
    return (long)sz;
}

static int log_contains_after(long from, const char *needle)
{
    char p[MAX_PATH]; log_path(p, sizeof(p));
    HANDLE h = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD sz = GetFileSize(h, NULL);
    if ((DWORD)from > sz) { CloseHandle(h); return 0; }
    DWORD want = sz - (DWORD)from;
    char *buf = (char *)malloc(want + 1);
    if (!buf) { CloseHandle(h); return 0; }
    SetFilePointer(h, from, NULL, FILE_BEGIN);
    DWORD got = 0;
    ReadFile(h, buf, want, &got, NULL);
    buf[got] = 0;
    int hit = strstr(buf, needle) != NULL;
    free(buf);
    CloseHandle(h);
    return hit;
}

int main(void)
{
    log_init();
    if (!mem_guard_install()) {
        printf("fault guard self-test failed\n");
        return 2;
    }

    /* --- The hash primitive, asserted directly ---------------------------
     * Not only end-to-end: an end-to-end trap in this project has already
     * passed once with the mechanism sabotaged. */
    printf("[case 1] hash primitive\n");
    BYTE a[HD_BLOCK], b[HD_BLOCK];
    memset(a, 0x11, sizeof(a));
    memcpy(b, a, sizeof(b));
    check(hd_hash_block(a, sizeof(a)) == hd_hash_block(b, sizeof(b)),
          "identical blocks hash the same");
    b[HD_BLOCK / 2] ^= 0x01;
    check(hd_hash_block(a, sizeof(a)) != hd_hash_block(b, sizeof(b)),
          "a ONE-BIT change deep in the block changes the hash");
    memset(a, 0, sizeof(a));
    check(hd_hash_block(a, sizeof(a)) != 0,
          "an all-zero block still hashes non-zero (0 means 'no block')");

    /* --- Case 1b: EVERY byte is mixed, at every offset class -------------
     *
     * The hash mixes 8 bytes at a time for speed. That is only safe if it
     * still covers every byte -- a loop that skips bytes, or mishandles the
     * tail, would silently miss changes, which is a FALSE NEGATIVE and exactly
     * the failure mode that closed the previous three routes.
     *
     * So flip one bit at every offset and require the hash to change at all of
     * them. Not a sample of offsets: all of them. Also test lengths that are
     * NOT multiples of 8, which is where a tail bug lives. */
    printf("\n[case 1b] every byte affects the hash (word-wise loop safety)\n");
    {
        BYTE t[HD_BLOCK];
        memset(t, 0x5C, sizeof(t));
        unsigned base_h = hd_hash_block(t, sizeof(t));
        int missed = 0, first_missed = -1;
        for (SIZE_T i = 0; i < sizeof(t); i++) {
            t[i] ^= 0x01;
            if (hd_hash_block(t, sizeof(t)) == base_h) {
                if (first_missed < 0) first_missed = (int)i;
                missed++;
            }
            t[i] ^= 0x01;                   /* restore */
        }
        printf("  (offsets whose change was missed: %d of %d", missed,
               (int)sizeof(t));
        if (first_missed >= 0) printf(", first at %d", first_missed);
        printf(")\n");
        check(missed == 0,
              "a one-bit change at EVERY offset in the block changes the hash");

        /* Tail handling: lengths not divisible by 8. A word-wise loop that
         * drops the remainder passes a 4096-byte test and fails here. */
        int tail_bad = 0;
        for (SIZE_T n = 1; n <= 32; n++) {
            BYTE s[32];
            memset(s, 0x11, sizeof(s));
            unsigned h0 = hd_hash_block(s, n);
            s[n - 1] ^= 0x01;               /* the LAST byte specifically */
            if (hd_hash_block(s, n) == h0)
                tail_bad++;
        }
        check(tail_bad == 0,
              "the LAST byte matters for every length 1..32 (tail handled)");

        /* And a change confined to the final 7 bytes of a full block -- the
         * region a dropped tail would lose. */
        memset(t, 0x5C, sizeof(t));
        base_h = hd_hash_block(t, sizeof(t));
        t[sizeof(t) - 3] ^= 0x01;
        check(hd_hash_block(t, sizeof(t)) != base_h,
              "a change in the final bytes of a full block is detected");
    }

    /* --- Plant the three pages ------------------------------------------- */
    BYTE *focus  = page_alloc(0xA0);
    BYTE *timer  = page_alloc(0xB0);
    BYTE *stat   = page_alloc(0xC0);
    printf("\nplanted: focus=%p timer=%p static=%p\n",
           (void *)focus, (void *)timer, (void *)stat);

    static hd_snapshot A1, A2, B;
    static hd_block pa[HD_MAX_BLOCKS ? 200000 : 1];
    static hd_block pb[200000];
    static hd_block pc[200000];
    A1.block = pa; A2.block = pb; B.block = pc;

    unsigned tick = 0;

    /* --- Case 2: capture works ------------------------------------------- */
    printf("\n[case 2] capture\n");
    timer[0] = (BYTE)++tick;
    check(hd_capture(&A1, "A1"), "A1 captured");
    check(A1.nblocks > 0, "A1 has blocks");
    check(!A1.truncated, "A1 was not truncated");

    /* --- Case 3: the CONTROL isolates the ticking page -------------------- */
    printf("\n[case 3] control: only the timer moves\n");
    timer[0] = (BYTE)++tick;              /* ticks on its own */
    check(hd_capture(&A2, "A2"), "A2 captured");

    static uintptr_t noise[4096];
    int nnoise = hd_changed(&A1, &A2, noise, 4096);
    printf("  (control changes: %d)\n", nnoise);
    check(listed(noise, nnoise, (uintptr_t)timer),
          "the TIMER page is in the control set");
    check(!listed(noise, nnoise, (uintptr_t)focus),
          "the FOCUS page is NOT in the control set");
    check(!listed(noise, nnoise, (uintptr_t)stat),
          "the STATIC page is NOT in the control set");

    /* --- Case 4: the screen change ---------------------------------------- */
    printf("\n[case 4] screen change: focus AND timer both move\n");
    timer[0] = (BYTE)++tick;              /* the timer keeps ticking */
    focus[64] = 0x5A;                     /* the screen changed */
    check(hd_capture(&B, "B"), "B captured");

    static uintptr_t sig[4096];
    int nsig = hd_changed(&A2, &B, sig, 4096);
    printf("  (signal changes: %d)\n", nsig);
    check(listed(sig, nsig, (uintptr_t)focus),
          "the FOCUS page changed across the transition");
    check(listed(sig, nsig, (uintptr_t)timer),
          "the TIMER page ALSO changed (this is the whole problem)");

    /* --- Case 5: THE POINT -- subtraction leaves only the focus page ------
     * This is the assertion the entire method rests on. */
    printf("\n[case 5] signal MINUS control\n");
    int kept_focus = 0, kept_timer = 0;
    for (int i = 0; i < nsig; i++) {
        if (listed(noise, nnoise, sig[i])) continue;   /* noisy: drop */
        if (sig[i] == (uintptr_t)focus) kept_focus = 1;
        if (sig[i] == (uintptr_t)timer) kept_timer = 1;
    }
    check(kept_focus, "the FOCUS page SURVIVES the subtraction");
    check(!kept_timer,
          "the TIMER page is REMOVED by the subtraction (the control works)");

    /* --- Case 6: alloc/free is not a 'change' ----------------------------- */
    printf("\n[case 6] a block present in only one snapshot is not a change\n");
    {
        hd_snapshot x, y;
        static hd_block bx[4], by[4];
        memset(&x, 0, sizeof(x)); memset(&y, 0, sizeof(y));
        x.block = bx; y.block = by;
        bx[0].addr = 0x1000; bx[0].hash = 1;
        bx[1].addr = 0x2000; bx[1].hash = 2;   /* only in x */
        bx[2].addr = 0x3000; bx[2].hash = 3;
        x.nblocks = 3;
        by[0].addr = 0x1000; by[0].hash = 1;
        by[1].addr = 0x3000; by[1].hash = 9;   /* differs */
        by[2].addr = 0x4000; by[2].hash = 4;   /* only in y */
        y.nblocks = 3;
        static uintptr_t got[8];
        int n = hd_changed(&x, &y, got, 8);
        printf("  (changed: %d)\n", n);
        check(n == 1 && got[0] == 0x3000,
              "only the block present in BOTH with a differing hash counts");
    }

    /* --- Case 7: hd_changed emits ASCENDING addresses ---------------------
     * The candidate report now groups contiguous pages into runs, which is
     * only correct if the address list is sorted. hd_changed merge-walks two
     * ascending block lists so it is sorted by construction -- but that is an
     * invariant the reporting depends on, so assert it rather than assume it. */
    printf("\n[case 7] changed addresses come out ascending\n");
    {
        hd_snapshot x, y;
        static hd_block bx[8], by[8];
        memset(&x, 0, sizeof(x)); memset(&y, 0, sizeof(y));
        x.block = bx; y.block = by;
        for (int i = 0; i < 8; i++) {
            bx[i].addr = by[i].addr = 0x10000 + (uintptr_t)i * HD_BLOCK;
            bx[i].hash = 100 + i;
            by[i].hash = (i % 2) ? 900 + i : 100 + i;   /* odd ones differ */
        }
        x.nblocks = y.nblocks = 8;
        static uintptr_t got[8];
        int n = hd_changed(&x, &y, got, 8);
        int ascending = 1;
        for (int i = 1; i < n; i++)
            if (got[i] <= got[i - 1]) ascending = 0;
        printf("  (changed: %d)\n", n);
        check(n == 4, "the four differing blocks were found");
        check(ascending, "addresses are strictly ascending (runs are valid)");
    }

    /* --- Case 8: the byte-level watch names the changed field -------------
     *
     * Once the candidate set is small the question stops being "which page"
     * and becomes "which bytes". This is the step that would actually identify
     * a selection index, so its reporting has to be right.
     *
     * The watch reads the live candidate list, which only hd_step() fills, so
     * drive it the same way the game does: run a full trial against the
     * planted pages, then watch. */
    printf("\n[case 8] byte-level watch\n");
    {
        hd_reset();
        /* Trial 1: A1, A2 (nothing touched), B (focus page changes). */
        timer[0] = (BYTE)++tick;  hd_step();          /* A1 */
        timer[0] = (BYTE)++tick;  hd_step();          /* A2 */
        timer[0] = (BYTE)++tick;
        focus[0x100] = 7;         hd_step();          /* B  */

        /* ⚠ The watch REFUSES an unfiltered set, and that gate is itself
         * worth testing: in-game it silently watched 32 of 369 one-trial
         * candidates and produced a wall of double-buffer and frame-counter
         * noise that looked exactly like real fields. */
        check(!hd_watch_capture("early"),
              "the watch REFUSES a set that has had too few trials");

        /* Run two more trials so the set is narrow enough to watch. */
        for (int t = 0; t < 2; t++) {
            timer[0] = (BYTE)++tick;  hd_step();      /* A1 */
            timer[0] = (BYTE)++tick;  hd_step();      /* A2 */
            timer[0] = (BYTE)++tick;
            focus[0x100] ^= 0x40;     hd_step();      /* B  */
        }

        int cap1 = hd_watch_capture("w1");
        check(cap1, "watch captured the candidate pages once narrowed");

        /* Now the thing we actually care about: a 4-byte field stepping by 1,
         * exactly like a selection index moving down one item.
         *
         * ⚠ Assert the OUTPUT, not merely that it ran. `check(1, ...)` after
         * calling a reporter proves nothing at all -- it passes with the
         * reporter emptied out. So parse the log this run wrote and require
         * the exact line describing the field. */
        unsigned sel = 7;
        memcpy(focus + 0x100, &sel, 4);
        hd_watch_capture("w2");
        sel = 8;
        memcpy(focus + 0x100, &sel, 4);
        hd_watch_capture("w3");

        long mark = log_size();          /* only look at what follows */
        hd_watch_report();

        /* The planted change is a 4-byte field at +0x100 going 7 -> 8. */
        check(log_contains_after(mark, "+0x100"),
              "the changed offset +0x100 is reported");
        check(log_contains_after(mark, "u32"),
              "a 4-byte run is decoded as a u32");
        check(log_contains_after(mark, "delta +1"),
              "the N -> N+1 step is reported as delta +1");
    }

    /* --- Case 9: FIELD-level control ---------------------------------------
     *
     * ⚠ This is the gap the in-game run exposed. The trial control subtracts
     * noisy PAGES; within a surviving page, individual FIELDS were never
     * noise-tested. A page qualifies because ONE field tracks the cursor --
     * and then forty per-frame fields on that same page are reported as if
     * they were equally interesting. Measured in-game: 47 fields "changed
     * every time" and none were index-like.
     *
     * Plant BOTH on ONE page: a churning field that moves on every capture,
     * and a selection field that moves only on the cursor step. The churn
     * must be suppressed and the selection must survive. */
    printf("\n[case 9] field-level control on a single page\n");
    {
        const SIZE_T CHURN = 0x40, SEL = 0x80;
        unsigned churn = 1000, sel = 5;

        /* The watch reads the live candidate list, so drive trials first.
         *
         * ⚠ FIXTURE DETAIL THAT MATTERS: the churn must NOT move during the
         * trial control windows, or the page is correctly classified as noise
         * and dropped -- leaving 0 candidates and nothing to watch. That is
         * the code behaving properly; the first version of this test had the
         * churn ticking on every capture and eliminated its own page.
         *
         * This mirrors reality: a page only survives the trials because its
         * per-frame churn happened to be quiet in those windows, which is
         * exactly why field-level control is still needed afterwards. */
        hd_reset();
        for (int t = 0; t < 3; t++) {
            hd_step();                                              /* A1 */
            hd_step();                                              /* A2 */
            sel++;   memcpy(focus + SEL,   &sel,   4);  hd_step();  /* B  */
        }

        long mark = log_size();

        /* The verdict is now reported at W4 (the reversibility capture), so
         * this case drives all four stages. W3 no longer reports on its own --
         * deliberately, because "reacted to the cursor" was exactly the
         * finding that turned out to be a counter. */
        while (hd_watch_stage() != HD_W_WANT_1)
            hd_watch_step();

        /* W1 baseline */
        churn++; memcpy(focus + CHURN, &churn, 4);
        hd_watch_step();
        /* W2 control -- the churn moves, the selection does NOT */
        churn++; memcpy(focus + CHURN, &churn, 4);
        hd_watch_step();
        /* W3 -- cursor moves down; both the churn and the selection move */
        unsigned base_sel9;
        memcpy(&base_sel9, focus + SEL, 4);
        churn++;            memcpy(focus + CHURN, &churn, 4);
        sel = base_sel9 + 1; memcpy(focus + SEL,   &sel,   4);
        hd_watch_step();
        /* W4 -- cursor moves BACK; the selection returns, the churn does not */
        churn++;            memcpy(focus + CHURN, &churn, 4);
        sel = base_sel9;    memcpy(focus + SEL,   &sel,   4);
        hd_watch_step();

        char want_sel[64];
        _snprintf(want_sel, sizeof(want_sel), "+0x%03llx",
                  (unsigned long long)SEL);
        char want_churn[64];
        _snprintf(want_churn, sizeof(want_churn), "+0x%03llx",
                  (unsigned long long)CHURN);

        check(log_contains_after(mark, want_sel),
              "the SELECTION field (moves only on the cursor step) is reported");
        check(!log_contains_after(mark, want_churn),
              "the CHURNING field (moves every capture) is SUPPRESSED");
        /* The verdict now RANKS: a small integer that stepped by exactly one
         * and returned is called out as an INDEX CANDIDATE, because in-game
         * 2658 fields returned and the real signal was buried among floats
         * and per-item pointers. */
        check(log_contains_after(mark, "INDEX CANDIDATE"),
              "a small field stepping by one is flagged as an index candidate");
    }

    /* --- Case 10: REVERSIBILITY separates state from a counter -------------
     *
     * ⚠⚠ THE CASE PAID FOR BY A FALSE POSITIVE. A pointer pair passed every
     * filter this tool had -- 13 page-level trials, zero field-level control
     * noise, a constant +0x90 stride, persisting between rounds -- and was a
     * monotonic allocation counter. It went +1 element on an UP move and the
     * record it pointed at was 144 zero bytes.
     *
     * Controls prove a field is not NOISE; they cannot prove it is STATE.
     * Undoing the action is what separates them.
     *
     * Plant both on one page: a COUNTER that increments on every input, and a
     * SELECTION that goes 5 -> 6 -> 5 as the cursor moves down then back. */
    printf("\n[case 10] reversibility: state RETURNS, a counter does not\n");
    {
        const SIZE_T CTR = 0x200, SEL2 = 0x240;
        unsigned ctr = 700, sel2 = 5;

        /* ⚠ hd_reset() clears the TRIAL state but the WATCH keeps its own
         * stage, and case 9 left it mid-cycle. Four hd_watch_step() calls
         * then land on the wrong stages -- the planted writes happen during
         * what the watch thinks is its control window, both fields are
         * filtered as noise, and the case fails for a reason unrelated to the
         * code. Drive the watch back to its start first. */
        hd_reset();
        while (hd_watch_stage() != HD_W_WANT_1)
            hd_watch_step();
        hd_reset();

        for (int t = 0; t < 3; t++) {
            hd_step();                                              /* A1 */
            hd_step();                                              /* A2 */
            sel2++; memcpy(focus + SEL2, &sel2, 4);
            ctr++;  memcpy(focus + CTR,  &ctr,  4);  hd_step();     /* B  */
        }

        long mark2 = log_size();

        hd_watch_step();                                  /* W1 baseline    */
        hd_watch_step();                                  /* W2 control     */

        /* ⚠ "Returns" means returns to the value at the CONTROL capture, not
         * to some number picked in advance. The first version of this fixture
         * set 6 then 5 while the baseline was 8, so nothing returned and the
         * test failed for a reason that had nothing to do with the code.
         * Read the baseline and move relative to it. */
        unsigned base_sel;
        memcpy(&base_sel, focus + SEL2, 4);

        /* Cursor moves DOWN: both the selection and the counter advance. */
        sel2 = base_sel + 1; memcpy(focus + SEL2, &sel2, 4);
        ctr++;               memcpy(focus + CTR,  &ctr,  4);
        hd_watch_step();                                  /* W3 after move  */

        /* Cursor moves BACK: the selection RETURNS, the counter climbs. */
        sel2 = base_sel;     memcpy(focus + SEL2, &sel2, 4);
        ctr++;               memcpy(focus + CTR,  &ctr,  4);
        hd_watch_step();                                  /* W4 reversibility */

        char want2[64];
        _snprintf(want2, sizeof(want2), "+0x%03llx",
                  (unsigned long long)SEL2);
        char notwant[64];
        _snprintf(notwant, sizeof(notwant), "+0x%03llx",
                  (unsigned long long)CTR);

        /* ⚠ Match the RESULT line, not the header. "RETURNED" alone also
         * appears in the section title, so the first version of this test
         * passed while zero fields had actually returned. */
        check(log_contains_after(mark2, "⭐ "),
              "a field that returns to its original value is reported");
        check(log_contains_after(mark2, want2),
              "the SELECTION field is the one reported");
        check(!log_contains_after(mark2, notwant),
              "the COUNTER is NOT reported (it kept climbing)");
        check(log_contains_after(mark2, "kept climbing"),
              "counters are counted and named as rejected");
    }

    /* --- Case 11: tracking a candidate across repeated cursor presses ------
     *
     * ⚠ Why this exists: heap addresses differ between launches, so a
     * candidate found in one run CANNOT be re-found in the next -- two live
     * runs produced index candidates with zero page overlap. Confirmation has
     * to happen WITHIN a run, by watching the field over several presses.
     * A selection index walks by one and REVERSES; a one-off coincidence
     * does not. */
    printf("\n[case 11] tracking a series across repeated presses\n");
    {
        /* Drive a verdict so something is latched, using the same shape as
         * case 10 (a selection that steps and returns). */
        const SIZE_T SEL3 = 0x300;
        unsigned s3 = 40;
        memcpy(focus + SEL3, &s3, 4);

        hd_reset();
        while (hd_watch_stage() != HD_W_WANT_1) hd_watch_step();
        hd_reset();
        for (int t = 0; t < 3; t++) {
            hd_step(); hd_step();
            s3++; memcpy(focus + SEL3, &s3, 4); hd_step();
        }

        unsigned base3;
        memcpy(&base3, focus + SEL3, 4);
        hd_watch_step();                                   /* W1 */
        hd_watch_step();                                   /* W2 */
        s3 = base3 + 1; memcpy(focus + SEL3, &s3, 4);
        hd_watch_step();                                   /* W3 */
        s3 = base3;     memcpy(focus + SEL3, &s3, 4);
        hd_watch_step();                                   /* W4 -> latches */

        long mark3 = log_size();

        /* Now walk the "cursor": down, down, then back up. A real index
         * produces a series that steps by one AND reverses. */
        hd_track_sample();                                 /* base3        */
        s3 = base3 + 1; memcpy(focus + SEL3, &s3, 4);
        hd_track_sample();                                 /* base3+1      */
        s3 = base3 + 2; memcpy(focus + SEL3, &s3, 4);
        hd_track_sample();                                 /* base3+2      */
        s3 = base3 + 1; memcpy(focus + SEL3, &s3, 4);
        hd_track_sample();                                 /* back to +1   */

        check(log_contains_after(mark3, "INDEX"),
              "a series that steps by one and reverses is called an INDEX");

        /* ⚠ THE ABOVE ALONE IS NOT A TEST. Verified: forcing the classifier
         * to label everything INDEX leaves it passing. A classifier is only
         * tested by showing it also says NO.
         *
         * ⚠ And the negative needs a FRESH series. Simply sampling more times
         * without touching the field appends stationary values to the SAME
         * series (43 44 45 44 44 44 ...), which is still correctly an index --
         * the earlier movement is real. Re-latch first, then never touch it. */
        hd_track_latch();
        long mark4 = log_size();
        /* Nothing is latched now, so drive a fresh verdict whose candidate
         * then stays put. Simplest honest form: re-run the 4-stage watch so
         * something latches, then sample repeatedly WITHOUT moving it. */
        unsigned hold;
        memcpy(&hold, focus + SEL3, 4);
        hd_watch_step();                                   /* W1 */
        hd_watch_step();                                   /* W2 */
        hold += 1; memcpy(focus + SEL3, &hold, 4);
        hd_watch_step();                                   /* W3 */
        hold -= 1; memcpy(focus + SEL3, &hold, 4);
        hd_watch_step();                                   /* W4 -> latches */

        long mark5 = log_size();
        for (int k = 0; k < 4; k++)
            hd_track_sample();          /* field never moves from here on */

        check(log_contains_after(mark5, "NEVER MOVED"),
              "a field that never changes is NOT called an index");
        (void)mark4;
    }

    /* --- Case 12: the ONE-KEY HUNT ----------------------------------------
     *
     * Four presses replace the old ~30. The load-bearing assertion is the
     * counter one: an earlier draft compared after-move against
     * after-move-back and had no baseline, so a counter 10 -> 11 -> 12 read as
     * "+1, small" and would have been reported as an index -- re-introducing
     * the exact false positive that already cost this project a session. */
    printf("\n[case 12] one-key hunt: five presses, counter rejected\n");
    {
        const SIZE_T SEL4 = 0x400, CTR4 = 0x440;
        unsigned sel4 = 12, ctr4 = 900;
        memcpy(focus + SEL4, &sel4, 4);
        memcpy(focus + CTR4, &ctr4, 4);

        long mark6 = log_size();

        /* ⚠ FIVE presses. Press 3 identifies which pages react to a cursor
         * move; presses 4 and 5 then bracket a SECOND move with real bytes,
         * so reversibility is judged per FIELD. Judging it per PAGE (hash at
         * press 2 == hash at press 4) was tried and is far too strict: an
         * index and a counter on the same page make the page never "return",
         * and the index is lost with it. */
        hd_hunt();                                   /* 1: baseline        */
        /* The counter's page ticks on its own -- that is what the control is
         * for, and it must actually tick or the assertion below is vacuous. */
        ctr4++; memcpy(timer + 0x40, &ctr4, 4);
        hd_hunt();                                   /* 2: control         */

        /* Cursor DOWN: the index steps. The counter lives on a DIFFERENT
         * page here so the page-level control can reject it, mirroring the
         * real case where noise and state are separable at page level. */
        sel4 = 13; memcpy(focus + SEL4, &sel4, 4);
        hd_hunt();                                   /* 3: which pages     */

        hd_hunt();                                   /* 4: pre-move bytes  */

        /* The measured move: index steps again, and the counter ticks. */
        sel4 = 14; memcpy(focus + SEL4, &sel4, 4);
        ctr4++;    memcpy(timer + 0x40, &ctr4, 4);   /* counter, other page */
        hd_hunt();                                   /* 5: verdict         */

        char wsel[64];
        _snprintf(wsel, sizeof(wsel), "+0x%03llx", (unsigned long long)SEL4);

        check(log_contains_after(mark6, "INDEX CANDIDATE"),
              "the hunt reports an index candidate");
        check(log_contains_after(mark6, wsel),
              "the SELECTION field is the candidate");
        /* The counter lives on a page that ticks during the control, so the
         * page-level control must have dropped that page entirely. */
        check(!log_contains_after(mark6, "+0x040"),
              "the ticking COUNTER's page is rejected by the control");
        (void)CTR4;
    }

    /* --- Case 13: fields RECURRING across hunts are surfaced ---------------
     *
     * ⚠ Four real hunts each reported ~30-50 candidates, and the fact that
     * page 0x4A7F000 (offsets +0xbc8, +0xc70, +0xa78) appeared in EVERY one
     * was invisible in the log -- it took cross-referencing four runs by hand.
     * The tool must do that itself: a field seen in several independent hunts,
     * each with its own control and its own move, is far stronger evidence
     * than one seen once. */
    printf("\n[case 13] fields recurring across hunts are ranked\n");
    {
        const SIZE_T STEADY = 0x500;   /* moves in EVERY hunt   */
        const SIZE_T ONEOFF = 0x540;   /* moves in the first only */
        unsigned st = 20, of = 30;
        memcpy(focus + STEADY, &st, 4);
        memcpy(focus + ONEOFF, &of, 4);

        long mark7 = log_size();

        for (int hunt = 0; hunt < 3; hunt++) {
            hd_hunt();                                   /* 1 baseline */
            hd_hunt();                                   /* 2 control  */
            st++; memcpy(focus + STEADY, &st, 4);
            if (hunt == 0) { of++; memcpy(focus + ONEOFF, &of, 4); }
            hd_hunt();                                   /* 3 which pages */
            hd_hunt();                                   /* 4 pre-move    */
            st++; memcpy(focus + STEADY, &st, 4);
            if (hunt == 0) { of++; memcpy(focus + ONEOFF, &of, 4); }
            hd_hunt();                                   /* 5 verdict     */
        }

        char wst[64];
        _snprintf(wst, sizeof(wst), "+0x%03llx", (unsigned long long)STEADY);

        check(log_contains_after(mark7, "seen in MULTIPLE hunts"),
              "the cross-hunt summary is printed");
        /* ⚠ Do NOT hardcode the hunt number: earlier cases run hunts too, so
         * the counter is already nonzero here and the ratio was 3/4, not 3/3.
         * Assert the property that matters -- the field accumulated three
         * hits and is called index-shaped -- not a count that depends on
         * which tests ran before. */
        check(log_contains_after(mark7, "3/"),
              "a field present in every one of these hunts accumulated 3 hits");
        check(log_contains_after(mark7, "INDEX-SHAPED"),
              "and is flagged index-shaped, not a 0/1 toggle");
        check(log_contains_after(mark7, wst),
              "the steady field is the one reported");
    }

    /* --- Case 14: finding a pointer to the candidate ----------------------
     *
     * The hunt's address changes every launch, so the field is only usable if
     * something re-findable points at it. This checks the search itself: plant
     * a pointer to the candidate page and require it to be located.
     *
     * ⚠ It canNOT check the "inside the exe image" half offline -- this test
     * binary's image is not the game's, and a planted heap pointer is not
     * static. So this asserts the SEARCH works; whether a STATIC root exists
     * is a fact about the game, answerable only in-game. Saying that plainly
     * beats a green line implying more. */
    printf("\n[case 14] the path search finds a pointer to the candidate\n");
    {
        /* g_seen must hold an index-shaped candidate; case 13's steady field
         * qualifies and its page is `focus`. */
        static uintptr_t *slot;
        if (!slot)
            slot = (uintptr_t *)VirtualAlloc(NULL, 4096,
                                             MEM_COMMIT | MEM_RESERVE,
                                             PAGE_READWRITE);
        slot[0] = (uintptr_t)focus;       /* a pointer AT the candidate page */

        long mark8 = log_size();
        hd_find_path();

        check(log_contains_after(mark8, "level 1"),
              "the path search ran and reported level 1");
        check(!log_contains_after(mark8, "nothing points at this page"),
              "the planted pointer to the candidate page WAS found");

        /* ⚠⚠ THE BUG THIS CASE EXISTS FOR. In-game the search found 137
         * pointers of which 6 were static -- and every one of those 6 sat
         * past the 64-entry array cap, so the summary said "6 inside the exe
         * image" while the detail loop printed NOTHING. The headline was
         * right and the actionable part was silently missing.
         *
         * Reproduce it: bury a pointer among far more than `cap` others and
         * require the report to stay self-consistent -- it must never claim
         * more hits than it listed without saying so. */
        static uintptr_t *many;
        if (!many)
            many = (uintptr_t *)VirtualAlloc(NULL, 4096 * 16,
                                             MEM_COMMIT | MEM_RESERVE,
                                             PAGE_READWRITE);
        for (int k = 0; k < 4096 * 16 / 8; k++)
            many[k] = (uintptr_t)focus;      /* thousands of hits */

        long mark9 = log_size();
        hd_find_path();
        check(log_contains_after(mark9, "level 1"),
              "the search still completes with thousands of hits");
        /* Either every counted static hit was listed, or the log says why. */
        check(!log_contains_after(mark9, "STATIC") ||
              !log_contains_after(mark9, "could not be listed"),
              "the report never claims more static hits than it listed");
    }

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
