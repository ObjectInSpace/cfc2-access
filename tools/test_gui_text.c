/*
 * Offline self-test for the cGUIObjText reader.
 *
 * It drives the SHIPPING detour body (gt_test_body -> detour_body) and the
 * SHIPPING inspector (gt_test_inspect -> inspect_body) with synthetic objects,
 * so what is tested is what ships. No game needed.
 *
 * ⭐⭐ THE PROJECT'S RULE, PAID FOR SEVERAL TIMES: a test that has never been
 * SEEN to fail is not evidence. Every mechanism below was deliberately
 * sabotaged and the suite re-run; the results are recorded at the bottom of
 * this file, next to the sabotage that produced them.
 *
 * ⚠ What this test canNOT check, stated so a green run is not over-read:
 *
 *  - whether 0x1402f9720 really is cGUIObjText's update method. That rests on
 *    the offline disassembly (the class-name string sits immediately before the
 *    vtable, and this address is in exactly one 8-aligned data slot in the
 *    whole image, that vtable's slot 5) and on the runtime prologue + vtable
 *    guards in check_target(). This test checks the hook's BEHAVIOUR given
 *    those addresses are right.
 *
 *  - ⭐⭐ WHERE THE STRING ACTUALLY LIVES. That is the open question the whole
 *    module exists to answer, and it cannot be answered offline: the layout
 *    has to come from a live object. What the test establishes is that the
 *    inspector correctly reports whatever is there -- including reporting
 *    NOTHING when there is nothing, which is the case a "found it" test would
 *    quietly skip.
 */

#include "../src/gui_text.h"
#include "../src/memsafe.h"
#include "../src/log.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

static int failures;

static void check(const char *name, int cond, const char *detail)
{
    if (cond) {
        printf("  PASS  %s\n", name);
        return;
    }
    printf("  FAIL  %s -- %s\n", name, detail);
    failures++;
}

/* Build a synthetic 528-byte object. Pointer fields are written as addresses
 * inside the buffer itself (base 0x100000), which is exactly the convention
 * gt_test_inspect's resolver implements. */
#define TEST_BASE 0x100000ULL

static void put_ptr(unsigned char *obj, int off, int points_to)
{
    ULONGLONG v = TEST_BASE + (ULONGLONG)points_to;
    memcpy(obj + off, &v, 8);
}

int main(void)
{
    printf("cGUIObjText reader self-test\n");

    /* The inspector reads foreign memory through mem_copy in the live path;
     * the test path does not, but installing the guard keeps the harness
     * honest about what the shipping code needs. */
    if (!mem_guard_install()) {
        printf("  FAIL  the fault guard did not fault during its own "
               "self-test -- refusing to run\n");
        return 1;
    }

    /* The detour filters on the vptr, so the objects handed to it must be
     * REAL memory whose first qword is (or is not) cGUIObjText's vtable.
     * ⭐ ours[] stands in for one of our elements, theirs[] for the
     * OutlineFont the first live run actually caught. */
    static ULONGLONG ours[4], theirs[4];
    ours[0]   = GT_VTABLE_ADDR;
    theirs[0] = 0x141101b78ULL;      /* nGUI::OutlineFont, from run 1 */

    /* --- case 1: the detour passes the original's return value through ----
     * ⚠ The value is chosen by the stand-in, not computed, so "passed through"
     * is provable rather than merely plausible.
     * ⭐ Asserted for a FILTERED-OUT object too (case 1b): the game's control
     * flow must not depend on whether we were interested in the object. */
    gt_test_reset();
    {
        void *r = gt_test_body(ours, (void *)0x22220000ULL);
        check("case 1: the original's return value is passed through untouched",
              r == (void *)(ULONG_PTR)0xABCDEF0012345678ULL,
              "the detour altered or replaced the game's return value");

        void *r2 = gt_test_body(theirs, NULL);
        check("case 1b: ... and also for an object we filter OUT",
              r2 == (void *)(ULONG_PTR)0xABCDEF0012345678ULL,
              "a filtered call did not pass the original's return value");
    }

    /* --- case 2: the detour captures OUR instance ------------------------ */
    check("case 2: an object with our vptr is captured",
          gt_test_last_captured() == (void *)ours,
          "the pointer the game handed us was not recorded");

    /* --- case 2b: ⭐⭐ AND ANOTHER CLASS'S OBJECT IS NOT ------------------
     * THE case this whole revision exists for. Slot 5 is a SHARED helper: it
     * really does receive OutlineFonts, and run 1 dumped one as though it were
     * a text element. Capturing must be selective, or the worker thread spends
     * every poll inspecting objects that were never ours.
     *
     * ⚠ Asserted as "still the previous capture", not merely "not theirs" --
     * a detour that captured nothing at all would pass the weaker form. */
    {
        gt_stats st;
        gt_get_stats(&st);
        char d[160];
        snprintf(d, sizeof d, "captured=%p (wanted %p, unchanged); ours=%ld "
                 "other_class=%ld", gt_test_last_captured(), (void *)ours,
                 st.ours, st.other_class);
        check("case 2b: another class's object is filtered out, not captured",
              gt_test_last_captured() == (void *)ours &&
              st.ours == 1 && st.other_class == 1, d);
    }

    /* --- case 3: it counts calls -----------------------------------------
     * ⭐ The call count is what makes "0 instances" interpretable. With calls
     * == 0 the hook is not intercepting and nothing can be concluded. And it
     * counts EVERY call, filtered or not -- filtering is our business, not a
     * reason to under-report what the game did. */
    {
        gt_stats st;
        gt_get_stats(&st);
        char d[128];
        snprintf(d, sizeof d, "expected 2 calls, got %ld", st.calls);
        check("case 3: invocations are counted, filtered ones included",
              st.calls == 2, d);
    }

    /* --- case 4: a pointer field holding text is found ------------------- */
    gt_test_reset();
    {
        unsigned char obj[GT_CLASS_SIZE];
        memset(obj, 0, sizeof obj);
        strcpy((char *)obj + 0x100, "CONTINUE");
        put_ptr(obj, GT_CAND_B, 0x100);

        int off = -1;
        BOOL ok = gt_test_inspect(obj, sizeof obj, &off);
        char d[128];
        snprintf(d, sizeof d, "ran=%d, offset=%d (wanted 0x%x)",
                 (int)ok, off, GT_CAND_B);
        check("case 4: a pointer field holding text is located", ok && off == GT_CAND_B, d);
    }

    /* --- case 5: NOTHING is reported when there is nothing ---------------
     * ⭐⭐ THE CASE THAT MATTERS MOST, and the one a "did it find the string"
     * test would never write. An inspector that always names some offset is
     * useless: the whole point of running it in-game is to believe it when it
     * says a field IS the string. If it cannot say "no", its yes means
     * nothing. -1, not 0 -- "could not tell" and "offset 0" must not look
     * alike. */
    {
        unsigned char obj[GT_CLASS_SIZE];
        memset(obj, 0, sizeof obj);
        /* Pointer-shaped fields that resolve to binary noise, not text. */
        put_ptr(obj, GT_CAND_A, 0x080);
        put_ptr(obj, GT_CAND_B, 0x090);
        for (int i = 0x80; i < 0xa0; i++)
            obj[i] = (unsigned char)(0x80 + (i & 0x3f));   /* non-ASCII */

        int off = -1;
        BOOL ok = gt_test_inspect(obj, sizeof obj, &off);
        char d[128];
        snprintf(d, sizeof d, "ran=%d, offset=%d (wanted -1)", (int)ok, off);
        check("case 5: an object with no text yields NO conclusion (-1)",
              ok && off == -1, d);
    }

    /* --- case 6: an inline string is found too ---------------------------
     * ⚠ A sweep that only followed pointers would miss a fixed char array in
     * the object entirely, and a fixed array is just as likely a design. */
    {
        unsigned char obj[GT_CLASS_SIZE];
        memset(obj, 0, sizeof obj);
        strcpy((char *)obj + 0x120, "OPTIONS");

        int off = -1;
        BOOL ok = gt_test_inspect(obj, sizeof obj, &off);
        char d[128];
        snprintf(d, sizeof d, "ran=%d, offset=%d (wanted 0x120)", (int)ok, off);
        check("case 6: a string stored INLINE is located", ok && off == 0x120, d);
    }

    /* --- case 7: a longer string beats a candidate offset ----------------
     * ⭐ The two offline candidate offsets earned a look, not a verdict. If
     * another offset holds better text, it wins -- otherwise a wrong guess
     * from the offline reading would outrank the real field forever. */
    {
        unsigned char obj[GT_CLASS_SIZE];
        memset(obj, 0, sizeof obj);
        strcpy((char *)obj + 0x0c0, "ab");                 /* short, candidate */
        put_ptr(obj, GT_CAND_A, 0x0c0);
        strcpy((char *)obj + 0x0e0, "PLAYER SELECT MODE");  /* long, elsewhere */
        put_ptr(obj, 0x150, 0x0e0);

        int off = -1;
        gt_test_inspect(obj, sizeof obj, &off);
        char d[128];
        snprintf(d, sizeof d, "offset=%d (wanted 0x150, the longer string)", off);
        check("case 7: a clearly longer string outranks a candidate offset",
              off == 0x150, d);
    }

    /* --- case 8: a wide (UTF-16) string is recognised --------------------
     * The shell is a Japanese-authored engine; a widened copy in the element
     * would be unsurprising, so it is checked rather than assumed absent. */
    {
        unsigned char obj[GT_CLASS_SIZE];
        memset(obj, 0, sizeof obj);
        const char *w = "MUSEUM";
        for (int i = 0; w[i]; i++) obj[0x0b0 + i * 2] = (unsigned char)w[i];
        put_ptr(obj, GT_CAND_B, 0x0b0);

        int off = -1;
        gt_test_inspect(obj, sizeof obj, &off);
        char d[128];
        snprintf(d, sizeof d, "offset=%d (wanted 0x%x)", off, GT_CAND_B);
        check("case 8: a UTF-16 string is recognised", off == GT_CAND_B, d);
    }

    /* --- case 9: the WRONG CLASS is refused, not dumped ------------------
     * ⛔⛔ THE CASE THE FIRST LIVE RUN BOUGHT. The hook target was a shared
     * helper three sites reach by a direct call, so it received an
     * nGUI::OutlineFont -- and the module dumped 41 lines of Adobe glyph
     * names, with BOTH "candidate" offsets holding plausible strings
     * ("uni0623", "uniFE84"). Nothing there was a menu label.
     *
     * ⭐ The vptr check saved it from being a false finding, but only as a
     * WARNING above a full dump. If the code prints a warning, something must
     * FAIL on it -- so a foreign vptr now REFUSES, and this case pins that.
     *
     * ⚠ Checked through the stats counter rather than by inspecting the log,
     * because a log line is not a mechanism. This asserts the refusal HAPPENED
     * and was COUNTED.
     *
     * ⛔ NOTE the limit: gt_poll_and_log() needs an installed hook, which no
     * offline test has. So this case asserts the counter's contract -- that a
     * fresh module reports zero wrong-class sightings and that the field is
     * actually plumbed through gt_get_stats() -- while the REFUSAL ITSELF is
     * proven by the live run recorded in the sabotage log, not here. Said
     * plainly so a green suite is not read as covering it. */
    {
        gt_test_reset();
        gt_stats st;
        gt_get_stats(&st);
        char d[128];
        snprintf(d, sizeof d, "a fresh module reported %ld wrong-class "
                 "sighting(s)", st.wrong_class);
        check("case 9: the wrong-class counter is plumbed and starts at zero",
              st.wrong_class == 0, d);
    }

    /* --- case 9b: ⭐⭐ the CTOR probe captures UNCONDITIONALLY --------------
     * The opposite property to case 2b, and it must be asserted separately
     * because the two detours are deliberately different.
     *
     * At constructor entry the object is raw allocation -- the factory stamps
     * the vptr in only AFTER the ctor returns. So a vptr filter on the ctor
     * would reject every object and manufacture precisely the false zero this
     * probe exists to escape.
     *
     * ⚠ The object handed in here carries the OutlineFont vptr, i.e. a value
     * the update detour is required to reject. The ctor detour must capture it
     * anyway. Anyone later "tidying up" by sharing one filter between the two
     * detours breaks this case, which is the point of writing it. */
    {
        gt_test_reset();
        static ULONGLONG raw[4];
        raw[0] = 0x141101b78ULL;        /* NOT ours -- and captured regardless */

        void *r = gt_test_ctor_body(raw);
        gt_stats st;
        gt_get_stats(&st);
        char d[192];
        snprintf(d, sizeof d, "ret=%p captured=%p (wanted %p) ctors=%ld "
                 "(wanted 1)", r, gt_test_last_captured(), (void *)raw,
                 st.ctors);
        check("case 9b: the ctor probe counts and captures with NO vptr filter",
              r == (void *)(ULONG_PTR)0xC0FFEE0011223344ULL &&
              gt_test_last_captured() == (void *)raw && st.ctors == 1, d);
    }

    /* --- case 9d: ⭐⭐⭐ THE TEXT SETTER CAPTURES THE STRING ----------------
     * The product case. setText receives the label as an ARGUMENT, so this is
     * the whole mechanism the module now rests on: our object in, string out.
     *
     * ⚠ Asserted through what the detour PUBLISHED, not by re-reading the
     * argument -- a test that re-derives the answer from its own input would
     * pass with the capture deleted. */
    {
        gt_test_reset();
        static ULONGLONG obj[4];
        obj[0] = GT_VTABLE_ADDR;
        char got[64] = { 1 };
        void *r = gt_test_settext_body(obj, "PLAYER SELECT", got, sizeof got);
        gt_stats st;
        gt_get_stats(&st);
        char d[192];
        snprintf(d, sizeof d, "ret=%p captured=\"%s\" calls=%ld with_text=%ld",
                 r, got, st.settext_calls, st.with_text);
        check("case 9d: the text setter captures the string it is handed",
              r == (void *)(ULONG_PTR)0xFEEDFACE00112233ULL &&
              strcmp(got, "PLAYER SELECT") == 0 &&
              st.settext_calls == 1 && st.with_text == 1, d);
    }

    /* --- case 9e: ⛔ AND IGNORES ANOTHER CLASS'S CALL ---------------------
     * Slot 26 is SHARED: at least one caller (0x1400f93ca) passes a 32-bit
     * value in edx, not a char*. Dereferencing that as a string would read an
     * integer as a pointer. The vptr filter is the only thing preventing it,
     * so it is asserted directly.
     *
     * ⚠ The string here is deliberately valid -- the object is what must be
     * rejected. A filter keyed on the string instead of the object would pass
     * this and still be wrong in the game. */
    {
        gt_test_reset();
        static ULONGLONG other[4];
        other[0] = 0x141101b78ULL;      /* nGUI::OutlineFont */
        char got[64] = { 0 };
        gt_test_settext_body(other, "SHOULD NOT BE CAPTURED", got, sizeof got);
        gt_stats st;
        gt_get_stats(&st);
        char d[192];
        snprintf(d, sizeof d, "captured=\"%s\" (wanted empty) calls=%ld "
                 "with_text=%ld other_class=%ld", got, st.settext_calls,
                 st.with_text, st.other_class);
        check("case 9e: a foreign object's setText is counted but NOT captured",
              got[0] == '\0' && st.settext_calls == 1 && st.with_text == 0 &&
              st.other_class == 1, d);
    }

    /* --- case 9f: ⛔⛔ THE HIGH-FREQUENCY CASE THAT BROKE THE FIRST DESIGN --
     * The first version published through a seq-bracket, copied from
     * menu_cursor. That channel ticks a few times per keypress; THIS one fired
     * 167,081 times in four minutes (~650/s) while the reader polls at 10 Hz.
     * The reader therefore sampled the sequence mid-write almost every time,
     * discarded the snapshot, and logged NOTHING -- 616 captured strings, zero
     * lines. The counters said it worked; the log was empty.
     *
     * ⭐ The lesson is about copying a pattern across a frequency difference of
     * four orders of magnitude, and this case pins the fix: after MANY rapid
     * sets, the reader must still be able to take the LATEST value.
     *
     * ⚠ Asserted on the last value specifically, not merely "something was
     * read" -- a mailbox that hands back a stale entry would pass the weaker
     * form and be useless in exactly the way the original was. */
    {
        gt_test_reset();
        static ULONGLONG obj[4];
        obj[0] = GT_VTABLE_ADDR;
        char got[64] = { 0 };
        for (int i = 0; i < 500; i++) {
            char s[32];
            snprintf(s, sizeof s, "LABEL %d", i);
            gt_test_settext_body(obj, s, NULL, 0);
        }
        /* Drain: the mailbox holds one slot, so this is the most recent set
         * that the writer managed to publish. */
        gt_test_take_text(got, sizeof got);
        gt_stats st;
        gt_get_stats(&st);
        char d[192];
        snprintf(d, sizeof d, "took=\"%s\" (wanted \"LABEL 499\") calls=%ld",
                 got, st.settext_calls);
        check("case 9f: 500 rapid sets still deliver the LATEST to the reader",
              strcmp(got, "LABEL 499") == 0 && st.settext_calls == 500, d);
    }

    /* --- case 9c: ⛔⛔ THE TWO PROLOGUE CONSTANTS ARE NOT THE SAME ---------
     * A run was spent on this. The ctor check reused the update method's
     * prologue constant, and the two differ in ONE byte -- both are
     * `push rbx; sub rsp,N` but with N = 0x20 and 0x30. The ctor hook silently
     * declined to install.
     *
     * ⭐ The offline suite could not have caught it as written, because
     * nothing here asserted what the constants ARE. This case fixes that: it
     * pins the expected bytes for both targets against the values read from
     * the dump. If someone "simplifies" the two constants back into one, or
     * edits a target address without re-reading its prologue, this fails on
     * the bench instead of costing a launch.
     *
     * ⚠ It cannot verify the addresses are the RIGHT functions -- only that
     * the recorded bytes match what was disassembled. That distinction is the
     * same one stated at the top of this file. */
    {
        extern const BYTE *gt_test_prologue(int which, int *len);
        int ul = 0, cl = 0;
        const BYTE *up = gt_test_prologue(0, &ul);
        const BYTE *cp = gt_test_prologue(1, &cl);
        /* ⚠ These two are now EQUAL, and that is correct: after the vtable was
         * fixed, slot 13 of the real instance vtable (0x1402fd6a0) has the
         * same frame size as the ctor. An earlier version of this case
         * asserted they must DIFFER -- which was true of the addresses then in
         * use and became a false requirement the moment one address changed.
         *
         * ⭐ So pin the VALUES against the dump, never the relationship
         * between them. What matters is that each constant matches the bytes
         * at its own target, not how the two compare. */
        const BYTE want_u[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20 };
        const BYTE want_c[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20 };
        BOOL ok = up && cp && ul == 6 && cl == 6 &&
                  memcmp(up, want_u, 6) == 0 && memcmp(cp, want_c, 6) == 0;
        char d[192];
        snprintf(d, sizeof d,
                 "update=%02x %02x %02x %02x %02x %02x  ctor=%02x %02x %02x "
                 "%02x %02x %02x", up[0], up[1], up[2], up[3], up[4], up[5],
                 cp[0], cp[1], cp[2], cp[3], cp[4], cp[5]);
        check("case 9c: both prologue constants match the dump's bytes",
              ok, d);
    }

    /* --- case 10: install REFUSES when the target is not the expected code -
     * ⭐⭐ Reported as a REASON, not a bare FALSE. A test asserting only
     * "install failed" passes with every guard deleted, because MinHook
     * independently refuses an unmapped address -- the outcome would be right
     * and the assertion worthless. Asking WHICH layer refused is what makes
     * the guard observable.
     *
     * Here nothing is mapped at 0x1402f9720 in the test process, so the
     * protection check must be the one that fires -- before any byte is
     * written to any address. */
    {
        gt_stats st;
        BOOL ok = gt_install();
        gt_install_status why = gt_install_why();
        gt_get_stats(&st);
        char d[160];
        snprintf(d, sizeof d, "install returned %d, reason %d (wanted 0 and "
                 "GT_INSTALL_BAD_PROTECT=%d), installed=%d",
                 (int)ok, (int)why, (int)GT_INSTALL_BAD_PROTECT,
                 (int)st.installed);
        check("case 10: install refuses at the PROTECTION check, and says so",
              !ok && why == GT_INSTALL_BAD_PROTECT && !st.installed, d);
    }

    /* --- case 11: polling without an install reports nothing -------------
     * ⚠ Not "returns quietly": it must return FALSE, so a caller can tell
     * "no new instance" from "inspected and found nothing". */
    check("case 11: gt_poll_and_log reports FALSE when not installed",
          !gt_poll_and_log(),
          "it claimed to have something to report with no hook installed");

    /* ================= THE FOCUS CHANNEL (items 3 + 4) ==================
     * ⭐⭐ These guard the one thing that must never happen: speaking a
     * caption that belongs to a DIFFERENT row. A wrong description is
     * indistinguishable from a right one to a blind user. */

    /* --- case 12: a caption arriving after a move is returned ----------- */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG obj[4];
        obj[0] = GT_VTABLE_ADDR;

        gt_note_focus_move();                       /* the cursor moved */
        gt_test_settext_body(obj, "Play offline.", NULL, 0);

        char cap[GT_CAPTION_MAX] = { 0 };
        BOOL got = gt_take_caption(cap, (int)sizeof cap);
        char d[160];
        snprintf(d, sizeof d, "got=%d cap=\"%s\"", got, cap);
        check("case 12: a caption set after a move IS returned",
              got && strcmp(cap, "Play offline.") == 0, d);
    }

    /* --- case 13: ⛔⛔ A STALE CAPTION IS NOT RETURNED -------------------
     * THE case this channel exists to get right. Text set for the PREVIOUS
     * row must not be spoken for the current one. Without the move-sequence
     * stamp, "the most recent string" would sail through -- and that is
     * exactly the timing join docs/rethink-after-gui-text.md forbids. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG obj[4];
        obj[0] = GT_VTABLE_ADDR;

        gt_note_focus_move();
        gt_test_settext_body(obj, "Play offline.", NULL, 0);  /* row 1's */
        gt_note_focus_move();                    /* cursor moved to row 2 */
        /* ...and the game has not set row 2's caption yet. */

        char cap[GT_CAPTION_MAX] = { 0 };
        BOOL got = gt_take_caption(cap, (int)sizeof cap);
        char d[160];
        snprintf(d, sizeof d, "got=%d cap=\"%s\" (must be empty)", got, cap);
        check("case 13: a caption from the PREVIOUS row is NOT returned",
              !got && cap[0] == '\0', d);
    }

    /* --- case 14: a hint STRIP is not spoken as a row caption ------------
     * Strips describe the SCREEN and do not change per row, so returning one
     * on every movement would be constant noise. They carry markup; captions
     * do not. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG obj[4];
        obj[0] = GT_VTABLE_ADDR;

        gt_note_focus_move();
        gt_test_settext_body(obj,
            "<SIZE 58><ICON DECIDE></SIZE>Confirm "
            "<SIZE 58><ICON CANCEL></SIZE>Back", NULL, 0);

        char cap[GT_CAPTION_MAX] = { 0 };
        BOOL got = gt_take_caption(cap, (int)sizeof cap);
        char d[160];
        snprintf(d, sizeof d, "got=%d cap=\"%s\" (a strip must be skipped)",
                 got, cap);
        check("case 14: a markup hint STRIP is not returned as a caption",
              !got && cap[0] == '\0', d);
    }

    /* --- case 15: the NEWEST caption of this move wins -------------------
     * If the shell sets several strings for one row, the last one is the one
     * describing it. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG obj[4];
        obj[0] = GT_VTABLE_ADDR;

        gt_note_focus_move();
        gt_test_settext_body(obj, "Play offline.", NULL, 0);
        gt_test_settext_body(obj, "Exit the application.", NULL, 0);

        char cap[GT_CAPTION_MAX] = { 0 };
        BOOL got = gt_take_caption(cap, (int)sizeof cap);
        char d[160];
        snprintf(d, sizeof d, "got=%d cap=\"%s\"", got, cap);
        check("case 15: the NEWEST caption of the current move wins",
              got && strcmp(cap, "Exit the application.") == 0, d);
    }

    /* --- case 16: no caption without a move ------------------------------
     * ⚠ Liveness: text captured before the cursor ever moved carries seq 0,
     * and speaking it would attribute a startup string to a row the user
     * never navigated to. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG obj[4];
        obj[0] = GT_VTABLE_ADDR;

        gt_test_settext_body(obj, "Play offline.", NULL, 0);  /* no move */

        char cap[GT_CAPTION_MAX] = { 0 };
        BOOL got = gt_take_caption(cap, (int)sizeof cap);
        /* seq is 0 for both the record and the reader, so it MATCHES -- this
         * documents that a pre-move capture is returned once the reader also
         * sits at seq 0. It is not wrong (nothing has moved, so nothing is
         * stale), and it is asserted so a future change to the seq rule shows
         * up here rather than in the user's ear. */
        char d[160];
        snprintf(d, sizeof d, "got=%d cap=\"%s\"", got, cap);
        check("case 16: a pre-move capture matches the initial sequence",
              got && strcmp(cap, "Play offline.") == 0, d);
    }

    /* --- case 17: the recorder keeps ORDER across several moves ----------
     * ⭐⭐ This is what item 4's answer is read from. If the record did not
     * preserve which strings arrived under which move number, the whole
     * measurement would be a timing correlation again -- the thing the rethink
     * note forbids. Three moves, one distinct caption each: the reader must
     * see each caption only while its own move is current.
     *
     * ⛔ NOT asserted with a bare TRUE. An earlier draft of this case did
     * exactly that and tested nothing at all. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG obj[4];
        obj[0] = GT_VTABLE_ADDR;

        static const char *const CAPS[3] = {
            "Play offline.",
            "Play online using a network connection.",
            "Exit the application."
        };
        int wrong = 0;
        char seen[3][GT_CAPTION_MAX];
        for (int i = 0; i < 3; i++) {
            gt_note_focus_move();
            gt_test_settext_body(obj, (char *)CAPS[i], NULL, 0);
            seen[i][0] = '\0';
            if (!gt_take_caption(seen[i], (int)sizeof seen[i]) ||
                strcmp(seen[i], CAPS[i]) != 0)
                wrong++;
        }
        char d[900];
        snprintf(d, sizeof d, "wrong=%d  got [\"%s\", \"%s\", \"%s\"]",
                 wrong, seen[0], seen[1], seen[2]);
        check("case 17: each move returns ITS OWN caption, in order",
              wrong == 0, d);
    }

    /* --- case 18: ⭐⭐ "COULD NOT MEASURE" IS A RETURN VALUE --------------
     * Item 4's whole point is a measurement, and a measurement that cannot
     * distinguish "no captions tracked the cursor" from "the cursor hook never
     * notified us" answers nothing. Both states are exercised here.
     *
     * ⛔ This case exists because the sabotage sweep found the gap: deleting
     * the notification counter left the suite GREEN, since nothing offline
     * could reach the report's refusal. (memory:
     * inconclusive-must-be-a-return-value) */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG obj[4];
        obj[0] = GT_VTABLE_ADDR;

        /* Records but NO movements: must be INCONCLUSIVE. */
        gt_test_settext_body(obj, "Play offline.", NULL, 0);
        long r0 = -1, m0 = -1;
        BOOL conc0 = gt_focus_conclusive(&r0, &m0);

        /* Now a real movement: must become conclusive. */
        gt_note_focus_move();
        long r1 = -1, m1 = -1;
        BOOL conc1 = gt_focus_conclusive(&r1, &m1);

        char d[220];
        snprintf(d, sizeof d,
                 "no-moves: conclusive=%d records=%ld moves=%ld | "
                 "after a move: conclusive=%d records=%ld moves=%ld",
                 conc0, r0, m0, conc1, r1, m1);
        check("case 18: records with 0 movements report INCONCLUSIVE",
              !conc0 && r0 == 1 && m0 == 0 && conc1 && m1 == 1, d);
    }

    /* --- case 19: the recorder does not go blind when full ---------------
     * ⛔ FROM A REAL RUN. GT_FOCUS_MAX records, then nothing: every later
     * screen silently missing. The 22:50 session did not hit the cap only
     * because it ended after 40 s. The report clears its window, so the
     * recorder must accept new records afterwards.
     *
     * ⚠ Asserted by overfilling and then checking a NEW string still lands --
     * "dropped > 0" alone would pass with a permanently dead recorder. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG obj[4];
        obj[0] = GT_VTABLE_ADDR;
        gt_note_focus_move();

        /* Overfill: distinct strings so none are deduped. */
        for (int i = 0; i < 80; i++) {
            char s[48];
            snprintf(s, sizeof s, "Caption number %d.", i);
            gt_test_settext_body(obj, s, NULL, 0);
        }
        long r_full = -1, m_full = -1;
        gt_focus_conclusive(&r_full, &m_full);

        /* Simulate what the report does: clear the window. */
        gt_test_focus_clear();

        gt_note_focus_move();
        gt_test_settext_body(obj, "Play offline.", NULL, 0);
        char cap[GT_CAPTION_MAX] = { 0 };
        BOOL got = gt_take_caption(cap, (int)sizeof cap);

        char d[220];
        snprintf(d, sizeof d,
                 "at cap: records=%ld (max %d); after clear: got=%d cap=\"%s\"",
                 r_full, 48, got, cap);
        check("case 19: the recorder accepts records again after a report",
              r_full == 48 && got && strcmp(cap, "Play offline.") == 0, d);
    }

    /* --- case 20: the INSTANCE SET is keyed by OBJECT, not by string -------
     * ⭐⭐ This is what makes it a different measurement from the focus record.
     * The transcript collapses repeats and keeps no identity; a screen reader
     * needs "what does each live element currently say?", which only an
     * object-keyed table answers.
     *
     * ⚠ Asserted on the SHAPE: three objects setting text repeatedly must
     * yield three entries, each holding its LATEST string -- not nine entries,
     * and not the first value. Getting either wrong makes the whole route's
     * measurement unreadable. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG a[4], b[4], c[4];
        a[0] = b[0] = c[0] = GT_VTABLE_ADDR;

        /* Three rows, each written twice -- the second write wins. */
        gt_test_settext_body(a, "Offline Play", NULL, 0);
        gt_test_settext_body(b, "Online Play", NULL, 0);
        gt_test_settext_body(c, "Museum", NULL, 0);
        gt_test_settext_body(a, "Offline Play", NULL, 0);
        gt_test_settext_body(b, "Online Play", NULL, 0);
        gt_test_settext_body(c, "Music", NULL, 0);      /* c CHANGED */

        int n = gt_test_instance_count();
        /* ⚠ GT_CAPTION_MAX, not GT_TEXT_MAX: the latter is private to the .c
         * and a compile-time check keeps the two equal. */
        char t_a[GT_CAPTION_MAX] = { 0 }, t_c[GT_CAPTION_MAX] = { 0 };
        int sets_a = gt_test_instance_text(a, t_a, sizeof t_a);
        int sets_c = gt_test_instance_text(c, t_c, sizeof t_c);

        char d[220];
        snprintf(d, sizeof d,
                 "objects=%d a=\"%s\"(%d sets) c=\"%s\"(%d sets)",
                 n, t_a, sets_a, t_c, sets_c);
        check("case 20: instance set keyed by object, holding the LATEST text",
              n == 3 && strcmp(t_a, "Offline Play") == 0 && sets_a == 2 &&
              strcmp(t_c, "Music") == 0 && sets_c == 2, d);
    }

    /* --- case 21: the instance table FORGETS the oldest ------------------
     * ⛔⛔ FROM A REAL RUN. The first version filled all 64 slots and then
     * dropped 3600 objects -- and the ones dropped were the NEWEST, i.e. the
     * screen the user was actually on. 30 of 64 entries were stale objects
     * from screens left long ago. A table that never forgets measures the past.
     *
     * ⚠ Asserted on the PROPERTY that matters: after overflowing, the most
     * RECENT object must still be findable. "It evicted something" is not the
     * claim; "the current screen survives" is. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG objs[200];
        int kept_recent = 1;

        /* Fill well past the cap, bumping the move sequence as a real session
         * does, so evictions have an age to sort on. */
        for (int i = 0; i < 200; i++) {
            objs[i] = GT_VTABLE_ADDR;
            if ((i % 8) == 0)
                gt_note_focus_move();
            char s[32];
            snprintf(s, sizeof s, "Item %d", i);
            gt_test_settext_body(&objs[i], s, NULL, 0);
        }

        /* The LAST few objects written must still be present. */
        for (int i = 195; i < 200; i++) {
            char got[GT_CAPTION_MAX] = { 0 }, want[32];
            snprintf(want, sizeof want, "Item %d", i);
            if (!gt_test_instance_text(&objs[i], got, sizeof got) ||
                strcmp(got, want) != 0)
                kept_recent = 0;
        }
        /* And the very first must be gone -- otherwise nothing was evicted. */
        char first[GT_CAPTION_MAX] = { 0 };
        int first_sets = gt_test_instance_text(&objs[0], first, sizeof first);

        char d[200];
        snprintf(d, sizeof d, "recent kept=%d, oldest still present=%d (\"%s\")",
                 kept_recent, first_sets, first);
        check("case 21: the table forgets the OLDEST, never the newest",
              kept_recent && first_sets == 0, d);
    }

    /* --- case 22: a numbered item is found BY ITS NUMBER ------------------
     * The music player's five tracks, in the exact form the run captured them.
     *
     * ⛔⛔ THE OBJECTS ARE INSERTED IN THE WRONG ORDER ON PURPOSE. In the real
     * run insertion order happened to equal row order, so a lookup that simply
     * returned the Nth entry would have passed every honest-looking test and
     * shipped. Scrambling the order here means only a lookup that reads the
     * PRINTED NUMBER can pass -- which is the entire safety argument of this
     * channel, and it must be the thing under test rather than a coincidence
     * the fixture reproduces. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG t1[4], t2[4], t3[4], t4[4], t5[4];
        t1[0] = t2[0] = t3[0] = t4[0] = t5[0] = GT_VTABLE_ADDR;

        gt_test_settext_body(t3, "03. Gallery: Swayin' Flame", NULL, 0);
        gt_test_settext_body(t5, "05. Result: The Fire's Respite", NULL, 0);
        gt_test_settext_body(t1, "01. Title: Take Your Best Shot", NULL, 0);
        gt_test_settext_body(t4, "04. Credits: Ode to Fighting Mirage", NULL, 0);
        gt_test_settext_body(t2, "02. Game Select: Overdrive Blaze", NULL, 0);
        /* The durations sit in their own objects alongside the titles, exactly
         * as the run shows. They must not be mistaken for items. */
        static ULONGLONG d1[4];
        d1[0] = GT_VTABLE_ADDR;
        gt_test_settext_body(d1, "2:51", NULL, 0);

        char got1[GT_CAPTION_MAX] = { 0 }, got4[GT_CAPTION_MAX] = { 0 };
        BOOL ok1 = gt_numbered_item(1, got1, sizeof got1);
        BOOL ok4 = gt_numbered_item(4, got4, sizeof got4);

        char d[240];
        snprintf(d, sizeof d, "1 -> %d \"%s\"; 4 -> %d \"%s\"",
                 (int)ok1, got1, (int)ok4, got4);
        check("case 22: a numbered item is found by its PRINTED number",
              ok1 && strcmp(got1, "01. Title: Take Your Best Shot") == 0 &&
              ok4 && strcmp(got4, "04. Credits: Ode to Fighting Mirage") == 0,
              d);
    }

    /* --- case 23: a number that is not present names NOTHING --------------
     * ⭐⭐ THE DISCRIMINATING CASE. The music list's length varies per game (a
     * 9-item strip selects the game), so asking for position 6 of a 5-track list
     * is a real situation, not a contrived one. It MUST come back empty: the
     * caller then speaks position only, which is what the screen said before
     * this work. A lookup falling back to "nearest" or "last" would name the
     * wrong track with total confidence -- the exact failure this project has
     * shipped twice and now tests against. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG t1[4], t2[4];
        t1[0] = t2[0] = GT_VTABLE_ADDR;
        gt_test_settext_body(t1, "01. Title: Take Your Best Shot", NULL, 0);
        gt_test_settext_body(t2, "02. Game Select: Overdrive Blaze", NULL, 0);

        char got[GT_CAPTION_MAX] = { 0 };
        BOOL ok = gt_numbered_item(6, got, sizeof got);

        char d[200];
        snprintf(d, sizeof d, "asked for 6 of a 2-item list -> %d \"%s\"",
                 (int)ok, got);
        check("case 23: an absent number names NOTHING", !ok && !got[0], d);
    }

    /* --- case 24: "1." must not match inside "11." -------------------------
     * ⚠ A prefix compare against "1." matches "11. ..." and would name track 11
     * as track 1. Real soundtracks in this collection run well past ten tracks,
     * so this is a live hazard rather than a theoretical one. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG e[4];
        e[0] = GT_VTABLE_ADDR;
        gt_test_settext_body(e, "11. Eleventh Track", NULL, 0);

        char got[GT_CAPTION_MAX] = { 0 };
        BOOL ok = gt_numbered_item(1, got, sizeof got);

        char d[200];
        snprintf(d, sizeof d, "asked for 1 with only \"11.\" present -> %d \"%s\"",
                 (int)ok, got);
        check("case 24: \"1.\" does not match inside \"11.\"", !ok && !got[0], d);
    }

    /* --- case 25: TWO objects claiming one number name NOTHING -------------
     * ⛔ The table holds objects from screens already left (30 of 64 entries
     * were stale in the run this was built from). If a previous screen's
     * numbered list is still resident, two live objects can claim "01." and
     * there is no evidence saying which is on screen. A contradiction must
     * produce silence, not a coin flip.
     *
     * ⚠ Asserted as a SEPARATE case from 23 because the two failures arrive by
     * different routes: 23 is "no evidence", this is "conflicting evidence",
     * and a lookup could easily get one right and the other wrong. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG x[4], y[4];
        x[0] = y[0] = GT_VTABLE_ADDR;
        gt_test_settext_body(x, "01. Title: Take Your Best Shot", NULL, 0);
        gt_test_settext_body(y, "01. A Different Screen's First Track", NULL, 0);

        char got[GT_CAPTION_MAX] = { 0 };
        BOOL ok = gt_numbered_item(1, got, sizeof got);

        char d[200];
        snprintf(d, sizeof d, "two objects claim 01. -> %d \"%s\"", (int)ok, got);
        check("case 25: an ambiguous number names NOTHING", !ok && !got[0], d);
    }

    /* --- case 26: a STALE object's text is not returned --------------------
     * ⚠ The entry survives until eviction, so the pointer must be re-validated
     * on every lookup. An object whose vptr is no longer ours has been freed and
     * its slot reused; its text is a leftover, not the screen's content.
     *
     * ⭐ This is the property gt_instance_report already prints as LIVE/STALE.
     * The lookup must ACT on it, not merely be able to report it. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG z[4];
        z[0] = GT_VTABLE_ADDR;
        gt_test_settext_body(z, "01. Title: Take Your Best Shot", NULL, 0);
        /* The object is freed and its memory reused by something else. */
        z[0] = 0xDEADBEEFULL;

        char got[GT_CAPTION_MAX] = { 0 };
        BOOL ok = gt_numbered_item(1, got, sizeof got);

        char d[200];
        snprintf(d, sizeof d, "stale object -> %d \"%s\"", (int)ok, got);
        check("case 26: a STALE object's text is not returned", !ok && !got[0],
              d);
    }

    /* --- case 27: a DEAD slot is reclaimed before a LIVE one ---------------
     * ⛔⛔ FROM THE 13:43 RUN. The table ran 100% full with up to 6670
     * evictions while every `move 0` entry was STALE -- freed title-screen and
     * main-menu objects still holding slots ~40 minutes in. gt_instance_report
     * could already SEE they were dead and nothing acted on it.
     *
     * ⚠ THE FIXTURE MAKES THE DEAD ENTRY THE *NEWEST*, which is what makes this
     * a test of deadness rather than of age. Under the old age-only rule the
     * dead entry is the LAST thing evicted; only a rule that checks liveness
     * reclaims it first. So an implementation that regressed to pure LRU fails
     * here even though it still "evicts something".
     *
     * The live entries are deliberately given IDENTICAL move_seq values too --
     * the real table arrives in ties (15 at move 16, 15 at move 10), which is
     * the second half of why age alone did not work. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        /* ⚠ The cap is private to gui_text.c, so -- exactly as case 21 does --
         * the fixture OVERSHOOTS it rather than referencing it. The table must
         * be FULL for this to test anything: with a free slot no eviction
         * happens at all and the case would pass without ever reaching the
         * reclaim path. */
        enum { N_LIVE = 200 };
        static ULONGLONG live[N_LIVE];
        static ULONGLONG dead[4];

        /* Saturate with live objects, all at the SAME move seq -- the tie
         * condition the real table exhibits (15 entries at move 16, 15 at
         * move 10), which is half of why age alone did not work. */
        for (int k = 0; k < N_LIVE; k++) {
            live[k] = GT_VTABLE_ADDR;
            char s[32];
            snprintf(s, sizeof s, "live %d", k);
            gt_test_settext_body(&live[k], s, NULL, 0);
        }

        /* ⭐ SNAPSHOT WHAT THE FULL TABLE ACTUALLY HOLDS, rather than assuming.
         * With every entry live and tied on move_seq the age fallback has no
         * ordering to work with, so WHICH objects survive saturation is an
         * implementation detail -- and an assertion that assumed LRU semantics
         * would be testing the fixture's guess, not the reclaim rule. Asking
         * the table is the measurement; predicting it is not. */
        int resident_before = 0;
        static char held_before[N_LIVE];
        for (int k = 0; k < N_LIVE; k++) {
            char got[GT_CAPTION_MAX] = { 0 }, want[32];
            snprintf(want, sizeof want, "live %d", k);
            held_before[k] = (gt_test_instance_text(&live[k], got,
                                                   sizeof got) &&
                              strcmp(got, want) == 0);
            if (held_before[k]) resident_before++;
        }

        /* Advance time, add a NEWER object, then kill it. It is now the newest
         * entry in the table AND it is dead -- so under an age-only rule it is
         * the LAST thing that would ever be evicted. That inversion is what
         * makes this a test of deadness rather than of age.
         *
         * ⚠⚠ THIS INSERT ITSELF COSTS ONE LIVE ENTRY, LEGITIMATELY. The table
         * is full and every entry is live at this moment, so there is no dead
         * slot to reclaim and the age fallback must take a live one. Counting
         * that loss against the reclaim rule would be measuring the wrong
         * insert -- which is exactly what an earlier version of this case did.
         * So the snapshot is retaken HERE, after the unavoidable eviction. */
        gt_note_focus_move();
        dead[0] = GT_VTABLE_ADDR;
        gt_test_settext_body(dead, "doomed", NULL, 0);
        dead[0] = 0xDEADBEEFULL;          /* freed and reused */

        int resident_mid = 0;
        for (int k = 0; k < N_LIVE; k++) {
            char got[GT_CAPTION_MAX] = { 0 }, want[32];
            snprintf(want, sizeof want, "live %d", k);
            held_before[k] = (gt_test_instance_text(&live[k], got,
                                                   sizeof got) &&
                              strcmp(got, want) == 0);
            if (held_before[k]) resident_mid++;
        }

        /* One more new object must land in the DEAD slot, not on a live one. */
        static ULONGLONG fresh[4];
        fresh[0] = GT_VTABLE_ADDR;
        gt_test_settext_body(fresh, "01. Fresh Arrival", NULL, 0);

        /* ⭐ THE ASSERTION: nothing that was resident BEFORE is gone AFTER. The
         * newcomer had a dead slot available, so no live entry needed to pay for
         * it. That is the property, stated without any assumption about which
         * objects saturation happened to keep.
         *
         * ⚠ `resident_before` is asserted non-trivial too: if the snapshot had
         * found nothing, "nothing was lost" would be vacuously true -- a zero
         * that looks identical to success. */
        int lost = 0;
        for (int k = 0; k < N_LIVE; k++) {
            if (!held_before[k]) continue;
            char got[GT_CAPTION_MAX] = { 0 }, want[32];
            snprintf(want, sizeof want, "live %d", k);
            if (!gt_test_instance_text(&live[k], got, sizeof got) ||
                strcmp(got, want) != 0)
                lost++;
        }
        char found[GT_CAPTION_MAX] = { 0 };
        int fresh_ok = gt_test_instance_text(fresh, found, sizeof found);

        char d[240];
        snprintf(d, sizeof d,
                 "resident before=%d, after the dead insert=%d, lost to the "
                 "RECLAIM insert=%d, fresh stored=%d (\"%s\")",
                 resident_before, resident_mid, lost, fresh_ok, found);
        check("case 27: a DEAD slot is reclaimed before any LIVE one",
              resident_mid > 8 && lost == 0 && fresh_ok > 0, d);
    }

    /* --- case 28: the focus differ is OFF unless enabled -------------------
     * ⚠ It reads 528 bytes per tracked object per movement. An opt-in that
     * silently defaults ON would put that cost in every player's session, which
     * is the mistake the DISCOVERY sweep made twice (it stalled live sessions
     * for 37 s and then 11.9 s). Asserted, not assumed. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        gt_focus_diff_enable(FALSE);
        static ULONGLONG a[GT_CLASS_SIZE / 8];
        a[0] = GT_VTABLE_ADDR;
        gt_test_settext_body(a, "Power Stone", NULL, 0);
        gt_note_focus_move();
        gt_focus_diff_note_move();
        /* ⭐ -1 means it never ran. Asserting "TRUE" here would pass whether or
         * not the gate worked, which is the shape of green this suite exists to
         * refuse. */
        char d28[80];
        snprintf(d28, sizeof d28, "last=%d (-1 = never ran)",
                 gt_test_focus_diff_changed());
        check("28. the focus differ does NOT run when disabled",
              gt_test_focus_diff_changed() == -1, d28);
    }

    /* --- case 29: a TICKER is distinguishable from a FOCUS marker ----------
     * ⭐⭐ THE WHOLE POINT OF THE DESIGN, and route 3's direct lesson: without a
     * same-screen control, "everything that merely ticks looks like a signal".
     *
     * Two objects. One field changes on BOTH (a ticker -- animation phase, a
     * frame counter); another changes on ONE only (a focus candidate). The
     * differ must report both objects as changed in the ticker case, so a
     * reader can tell the two apart from the counts alone.
     *
     * ⚠ Asserted through the SHIPPING entry point, not a reimplementation --
     * the harness must not supply the mechanism under test. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        gt_focus_diff_enable(TRUE);

        static ULONGLONG o1[GT_CLASS_SIZE / 8], o2[GT_CLASS_SIZE / 8];
        memset(o1, 0, sizeof o1); memset(o2, 0, sizeof o2);
        o1[0] = o2[0] = GT_VTABLE_ADDR;
        gt_test_settext_body(o1, "Power Stone", NULL, 0);
        gt_test_settext_body(o2, "Plasma Sword", NULL, 0);

        /* First look: establishes baselines, reports nothing. */
        gt_note_focus_move();
        gt_focus_diff_note_move();

        /* A ticker: the same field moves on BOTH objects -> 2 changed. */
        o1[8] = 1; o2[8] = 1;
        gt_note_focus_move();
        gt_focus_diff_note_move();
        int ticker = gt_test_focus_diff_changed();

        /* A focus marker: one field moves on ONE object only -> 1 changed. */
        o1[12] = 0xFF;
        gt_note_focus_move();
        gt_focus_diff_note_move();
        int focus = gt_test_focus_diff_changed();

        /* And no change at all -> 0 changed, distinct from "never ran". */
        gt_note_focus_move();
        gt_focus_diff_note_move();
        int quiet = gt_test_focus_diff_changed();

        gt_focus_diff_enable(FALSE);

        /* ⭐⭐ THE DISCRIMINATION IS THE ASSERTION. 2 / 1 / 0 are the three
         * readings a real run must be able to produce, and they are exactly
         * what tells a ticker from a focus marker from an absent signal. A case
         * asserting only "it did not crash" cannot tell them apart -- and the
         * first version of this case did precisely that, passing while the
         * differ never executed at all (the shipping gate required g_installed,
         * which no test sets). */
        char d29[120];
        snprintf(d29, sizeof d29, "ticker=%d focus=%d quiet=%d (want 2/1/0)",
                 ticker, focus, quiet);
        check("29. a ticker (2), a focus marker (1) and silence (0) are "
              "distinguishable",
              ticker == 2 && focus == 1 && quiet == 0, d29);
    }

    /* --- case 30: the REPORT reclaims dead slots, not just counts them -----
     * ⛔⛔ MEASURED ON THE LOBBY SETTINGS SCREEN: 64 objects, 31 LIVE, 33 STALE,
     * on a screen that draws 16 objects of its own. Eviction only fires when a
     * NEW object needs a slot, so a screen that keeps re-setting its RESIDENT
     * objects never triggers it and the dead pile up -- and a label that screen
     * really did draw ("Passcode") was missing from the set.
     *
     * ⚠ The assertion is that the slot becomes REUSABLE, not merely that a
     * counter moved. Reclaiming into a slot nothing can allocate from would
     * satisfy a count-only test and fix nothing. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static ULONGLONG d1[4], d2[4], keep[4];
        d1[0] = d2[0] = keep[0] = GT_VTABLE_ADDR;
        gt_test_settext_body(d1,   "doomed one", NULL, 0);
        gt_test_settext_body(d2,   "doomed two", NULL, 0);
        gt_test_settext_body(keep, "survivor",   NULL, 0);
        int before = gt_test_instance_count();

        /* Two of the three are freed and their memory reused. */
        d1[0] = 0xDEADBEEFULL;
        d2[0] = 0xDEADBEEFULL;

        gt_instance_report();          /* must reclaim both dead slots */

        /* The survivor is untouched... */
        char got[GT_CAPTION_MAX] = { 0 };
        int kept = gt_test_instance_text(keep, got, sizeof got);
        /* ...and the dead ones no longer answer. */
        char g1[GT_CAPTION_MAX] = { 0 };
        int d1_gone = (gt_test_instance_text(d1, g1, sizeof g1) == 0);

        char d30[200];
        snprintf(d30, sizeof d30,
                 "before=%d survivor=%d(\"%s\") dead-entry-gone=%d",
                 before, kept, got, d1_gone);
        check("30. the report RECLAIMS dead slots and spares the live one",
              before == 3 && kept > 0 && strcmp(got, "survivor") == 0 &&
              d1_gone, d30);
    }

    /* --- case 31: a settings row's VALUE from the live object list ----------
     * ⛔ USER-REPORTED: "it still doesn't say anything when I change
     * resolution." The join was verified offline and never wired in; the data
     * was there the whole time. This fixture is the REAL PC Settings block from
     * run-2026-09-11-0230, verbatim.
     *
     * ⚠ Note the block deliberately contains the traps: a caption ending in a
     * period, a Confirm/Back hint strip, menu items BEFORE the row labels, and
     * a duplicate "PC Settings" -- all of which must be handled. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static const char *const block[] = {
            "PC Settings", "Options", "Quit", "Change the display mode.",
            "<SIZE 58><ICON DECIDE></SIZE>Confirm <SIZE 58><ICON CANCEL></SIZE>Back",
            "Music", "Network", "Credits", "PC Settings",
            "Resolution", "Display", "VSync", "Default",
            "1280x720", "Fullscreen", "Off",
        };
        static ULONGLONG objs[32];
        for (size_t i = 0; i < sizeof block / sizeof block[0]; i++) {
            objs[i] = GT_VTABLE_ADDR;
            gt_test_settext_body(&objs[i], block[i], NULL, 0);
        }
        static const char *const labels[] = {
            "Resolution", "Display", "VSync", "Default"
        };
        char v0[64] = { 0 }, v1[64] = { 0 }, v2[64] = { 0 }, v3[64] = { 0 };
        BOOL r0 = gt_row_value(labels, 4, 0, v0, sizeof v0);
        BOOL r1 = gt_row_value(labels, 4, 1, v1, sizeof v1);
        BOOL r2 = gt_row_value(labels, 4, 2, v2, sizeof v2);
        /* ⚠ "Default" has NO value; it must refuse rather than overrun onto
         * whatever object follows. */
        BOOL r3 = gt_row_value(labels, 4, 3, v3, sizeof v3);

        char d[220];
        snprintf(d, sizeof d, "0=%d'%s' 1=%d'%s' 2=%d'%s' 3=%d'%s'",
                 (int)r0, v0, (int)r1, v1, (int)r2, v2, (int)r3, v3);
        check("31. a settings row's value comes from the live object list",
              r0 && !strcmp(v0, "1280x720") &&
              r1 && !strcmp(v1, "Fullscreen") &&
              r2 && !strcmp(v2, "Off") && !r3, d);
    }

    /* --- case 32: labels merely IN ORDER are REFUSED ------------------------
     * ⛔⛔ THE SOUND SETTINGS BUG, as a test. Its row labels sit at positions
     * [0,4,5,6,7,8] -- in order, but NOT consecutive, with a caption and menu
     * items in between. Accepting that read from the wrong offset and produced
     * "Music Volume -> Remix", "Announcer Volume -> Options": every value
     * wrong, all plausible, none detectable by ear.
     *
     * ⚠ So this asserts a REFUSAL. Requiring consecutiveness is what makes the
     * channel safe, and a change that relaxes it fails here. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        static const char *const block[] = {
            "Music Volume", "Adjust the volume of background music.",
            "<SIZE 58><ICON CANCEL></SIZE>Back", "System",
            "Sound Effects Volume", "Announcer Volume", "Game Volume",
            "Challenger Jingle", "Default", "Remix", "Music", "Options",
        };
        static ULONGLONG objs[32];
        for (size_t i = 0; i < sizeof block / sizeof block[0]; i++) {
            objs[i] = GT_VTABLE_ADDR;
            gt_test_settext_body(&objs[i], block[i], NULL, 0);
        }
        static const char *const labels[] = {
            "Music Volume", "Sound Effects Volume", "Announcer Volume",
            "Game Volume", "Challenger Jingle", "Default"
        };
        char v[64] = { 0 };
        BOOL got = gt_row_value(labels, 6, 0, v, sizeof v);
        char d[120];
        snprintf(d, sizeof d, "returned %d, %s", (int)got, v);
        check("32. labels in order but NOT consecutive are refused", !got, d);
    }

    /* --- cases 33-37: ⭐⭐⭐ POSITION -> OBJECT BY SCREEN COORDINATES -------
     *
     * The join the lobby-ID field and the music strip were blocked on.
     * EVERY FIXTURE BELOW IS REAL DATA from run-2026-09-11-0322: the six lobby
     * slots at y=556.0, x = 800..1120 stepping 64.0.
     *
     * ⛔⛔ CASE 34 IS THE ONE THAT MATTERS. In that run the slots' +0x050 field
     * reads 0a, 0c, 0b, 0d, 0e, 0f -- slots 2 and 3 INVERTED -- and insertion
     * order put the edited slot FIRST in one run and LAST in another. So the
     * fixture plants the objects in a DELIBERATELY WRONG order with that exact
     * inversion: any implementation that sorts by arrival, or by +0x050, or
     * that does not sort at all, returns the wrong character and fails. A
     * fixture in screen order would pass with the sort deleted entirely.
     */
    {
        /* A cGUIObjText must be at least GT_CLASS_SIZE for the coordinate read
         * to be in bounds; these stand in for the real objects. */
        static unsigned char slot[6][GT_CLASS_SIZE];
        /* Planted in SCRAMBLED order -- see above. Screen order is the X. */
        static const float SX[6] = { 928.f, 800.f, 1120.f, 864.f, 1056.f, 992.f };
        static const char *const SC[6] = { "C", "A", "F", "B", "E", "D" };

        gt_test_reset();
        gt_test_focus_enable();
        for (int i = 0; i < 6; i++) {
            memset(slot[i], 0, sizeof slot[i]);
            *(ULONGLONG *)slot[i] = GT_VTABLE_ADDR;
            memcpy(slot[i] + 0x40, &SX[i], sizeof(float));
            float y = 556.f;
            memcpy(slot[i] + 0x44, &y, sizeof(float));
            gt_test_settext_body(slot[i], SC[i], NULL, 0);
        }

        /* case 33: every position resolves to the character at that X. */
        {
            static const char *const WANT[6] = { "A","B","C","D","E","F" };
            int wrong = 0;
            char d[200]; int used = 0;
            for (int p = 1; p <= 6; p++) {
                char v[32] = { 0 };
                BOOL ok = gt_item_at_position(p, 6, NULL, NULL, v, sizeof v);
                if (!ok || strcmp(v, WANT[p - 1]) != 0) wrong++;
                used += snprintf(d + used, sizeof d - used, "%s%d:%s",
                                 used ? " " : "", p, v[0] ? v : "-");
            }
            check("33. six slots resolve left-to-right by X", wrong == 0, d);
        }

        /* case 34: ⛔ THE SORT IS LOAD-BEARING. Position 2 is "B" (x=864),
         * which is the FOURTH object planted. Insertion order yields "A";
         * +0x050 order would swap slots 2 and 3. Only the coordinate is right. */
        {
            char v[32] = { 0 };
            BOOL ok = gt_item_at_position(2, 6, NULL, NULL, v, sizeof v);
            char d[120];
            snprintf(d, sizeof d, "returned %d, \"%s\" (insertion order gives "
                     "\"B\" only by accident if unsorted: planted 4th)",
                     (int)ok, v);
            check("34. position 2 is the 2nd BY X, not the 2nd planted",
                  ok && strcmp(v, "B") == 0, d);
        }

        /* case 35: ⛔ A COUNT MISMATCH IS REFUSED. Six objects on screen but a
         * cursor reporting nine means the set is not this screen's; naming the
         * 9th of six would be a read past the candidates. */
        {
            char v[32] = { 1 };
            BOOL ok = gt_item_at_position(3, 9, NULL, NULL, v, sizeof v);
            char d[120];
            snprintf(d, sizeof d, "returned %d, \"%s\"", (int)ok, v);
            check("35. candidate count disagreeing with the cursor is refused",
                  !ok && v[0] == 0, d);
        }
    }

    /* case 36: ⭐ THE AXIS IS DERIVED, not assumed. The same function must
     * order a vertical COLUMN by Y -- the shape of every menu -- using the real
     * Online Play coordinates (x=376 constant, y 192..592 stepping 80).
     * ⚠ Planted bottom-to-top so an unsorted implementation fails. */
    {
        static unsigned char row[6][GT_CLASS_SIZE];
        static const float RY[6] = { 592.f, 512.f, 432.f, 352.f, 272.f, 192.f };
        static const char *const RC[6] = {
            "High Score Challenge Leaderboard", "High Score Challenge",
            "Ranked Leaderboard", "Custom Match", "Ranked Match", "Casual Match"
        };
        gt_test_reset();
        gt_test_focus_enable();
        for (int i = 0; i < 6; i++) {
            memset(row[i], 0, sizeof row[i]);
            *(ULONGLONG *)row[i] = GT_VTABLE_ADDR;
            float x = 376.f;
            memcpy(row[i] + 0x40, &x, sizeof(float));
            memcpy(row[i] + 0x44, &RY[i], sizeof(float));
            gt_test_settext_body(row[i], RC[i], NULL, 0);
        }
        char v[64] = { 0 };
        BOOL ok = gt_item_at_position(1, 6, NULL, NULL, v, sizeof v);
        char d[160];
        snprintf(d, sizeof d, "returned %d, \"%s\"", (int)ok, v);
        check("36. a vertical column sorts by Y (row 1 = topmost)",
              ok && strcmp(v, "Casual Match") == 0, d);
    }

    /* case 37: ⛔ TWO OBJECTS AT ONE COORDINATE CANNOT BE ORDERED. Speaking
     * either is a coin flip on which row the user is standing on, so refuse. */
    {
        static unsigned char dup[2][GT_CLASS_SIZE];
        gt_test_reset();
        gt_test_focus_enable();
        for (int i = 0; i < 2; i++) {
            memset(dup[i], 0, sizeof dup[i]);
            *(ULONGLONG *)dup[i] = GT_VTABLE_ADDR;
            float x = 400.f, y = 300.f;      /* identical on purpose */
            memcpy(dup[i] + 0x40, &x, sizeof(float));
            memcpy(dup[i] + 0x44, &y, sizeof(float));
            gt_test_settext_body(dup[i], i ? "Second" : "First", NULL, 0);
        }
        char v[32] = { 1 };
        BOOL ok = gt_item_at_position(1, 2, NULL, NULL, v, sizeof v);
        char d[120];
        snprintf(d, sizeof d, "returned %d, \"%s\"", (int)ok, v);
        check("37. objects sharing a coordinate are refused, not guessed",
              !ok && v[0] == 0, d);
    }

    /* --- case 38: ⛔⛔ THE DIFFER MUST WORK WITHOUT ANY CURSOR MOVEMENT ------
     *
     * MEASURED, and it is why this exists: the quit dialogs move their
     * selection WITHOUT going through the cursor helpers -- 3 arrow presses on
     * one and 8 on the other, ZERO cursor movements logged either time. So
     * gt_focus_diff_note_move(), which is gated on the movement sequence,
     * records nothing there however long the dialog is open.
     *
     * A differ that is structurally blind to the one screen it is needed for is
     * not a measurement tool. gt_focus_diff_note_text() runs the same
     * comparison off the screen REDRAW instead.
     *
     * ⚠ THE ASSERTION IS DELIBERATELY NOT "it did not crash". The movement
     * sequence is held FIXED across the whole case -- gt_note_focus_move() is
     * never called -- so a text-triggered path that secretly still depended on
     * a movement would report 0 and fail here. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        gt_focus_diff_enable(TRUE);

        static ULONGLONG y[GT_CLASS_SIZE / 8], n[GT_CLASS_SIZE / 8];
        memset(y, 0, sizeof y); memset(n, 0, sizeof n);
        y[0] = n[0] = GT_VTABLE_ADDR;
        gt_test_settext_body(y, "Yes", NULL, 0);
        gt_test_settext_body(n, "No", NULL, 0);

        /* Baseline, WITHOUT advancing the movement sequence. */
        gt_focus_diff_note_text();

        /* The selection moves to "Yes": one field changes on ONE object. Still
         * no cursor movement -- exactly the quit dialog's behaviour. */
        y[12] = 0xFF;
        gt_focus_diff_note_text();
        int selected = gt_test_focus_diff_changed();

        /* And with nothing changing, 0 -- distinct from "never ran". */
        gt_focus_diff_note_text();
        int quiet = gt_test_focus_diff_changed();

        gt_focus_diff_enable(FALSE);

        char d38[96];
        snprintf(d38, sizeof d38, "selected=%d (want 1), quiet=%d (want 0)",
                 selected, quiet);
        check("38. the differ samples on REDRAW, with no cursor movement at all",
              selected == 1 && quiet == 0, d38);
    }

    /* case 39: ⚠ and it still respects the switch. A probe that runs when it
     * was not asked to is how a diagnostic ends up in a user's play session. */
    {
        gt_test_reset();
        gt_test_focus_enable();
        gt_focus_diff_enable(FALSE);
        static ULONGLONG o[GT_CLASS_SIZE / 8];
        memset(o, 0, sizeof o);
        o[0] = GT_VTABLE_ADDR;
        gt_test_settext_body(o, "Yes", NULL, 0);
        gt_focus_diff_note_text();
        o[12] = 0xFF;
        gt_focus_diff_note_text();
        char d39[96];
        snprintf(d39, sizeof d39, "changed=%d (want <0 = never ran)",
                 gt_test_focus_diff_changed());
        check("39. the redraw-triggered differ stays OFF when disabled",
              gt_test_focus_diff_changed() < 0, d39);
    }

    /* --- cases 40-47: THE MOVE LIST MEMBERSHIP PROBE ---------------------
     *
     * ⭐⭐⭐ THE FIXTURE IS REAL DATA. From run-2026-09-11-1626 (screen
     * 2c3874d0) and -1635 (screen 2c316d70): every move-NAME object sits at
     * X=84.0 and every INPUT-NOTATION object at X=1565.0, and BOTH carry
     * +0x088 == the cursor helper's `screen` argument. The condition column
     * (" During Hyakkishu") sits at X=633.0 and is ALSO a member -- which is
     * exactly why membership cannot key on text shape: a leading-space
     * capitalised string is indistinguishable from a move name.
     *
     * ⚠ Two parents are planted so "filter on the screen pointer" and "count
     * everything live" give different answers. With one parent they would not. */
    {
        /* A member of the screen under test, and a member of another screen. */
        static unsigned char ours[7][GT_CLASS_SIZE];
        static unsigned char other[2][GT_CLASS_SIZE];
        void *P = (void *)0x2C3874D0ULL;      /* the move list's screen  */
        void *Q = (void *)0x2C251E60ULL;      /* the pause menu's screen */
        /* 3 names at 84.0, 1 condition at 633.0, 3 notations at 1565.0. */
        static const float OX[7] = { 84.f, 84.f, 84.f, 633.f,
                                     1565.f, 1565.f, 1565.f };
        static const char *const OT[7] = {
            " Hadoken", " Shoryuken", " Tatsumaki Senpu-kyaku",
            " During Hyakkishu", "<RGHT>A", "<RGHT>B", "<RGHT>C"
        };
        static const float QX[2] = { 362.f, 362.f };
        static const char *const QT[2] = { "Resume", "Quit" };

        /* Plant the fixture once; the cases below re-feed it. */
        #define ML_PLANT()                                                    \
            do {                                                              \
                gt_test_reset();                                              \
                gt_test_focus_enable();                                       \
                gt_movelist_enable(TRUE);                                     \
                for (int i = 0; i < 7; i++) {                                 \
                    memset(ours[i], 0, sizeof ours[i]);                       \
                    *(ULONGLONG *)ours[i] = GT_VTABLE_ADDR;                   \
                    memcpy(ours[i] + 0x40, &OX[i], sizeof(float));            \
                    float yy = 234.f + 73.f * (float)i;                       \
                    memcpy(ours[i] + 0x44, &yy, sizeof(float));               \
                    memcpy(ours[i] + 0x88, &P, sizeof(void *));               \
                    gt_test_settext_body(ours[i], OT[i], NULL, 0);            \
                }                                                             \
                for (int i = 0; i < 2; i++) {                                 \
                    memset(other[i], 0, sizeof other[i]);                     \
                    *(ULONGLONG *)other[i] = GT_VTABLE_ADDR;                  \
                    memcpy(other[i] + 0x40, &QX[i], sizeof(float));           \
                    float yy = 225.f + 80.f * (float)i;                       \
                    memcpy(other[i] + 0x44, &yy, sizeof(float));              \
                    memcpy(other[i] + 0x88, &Q, sizeof(void *));              \
                    gt_test_settext_body(other[i], QT[i], NULL, 0);           \
                }                                                             \
            } while (0)

        /* case 40: ⭐⭐ MEMBERSHIP KEYS ON +0x088, NOT ON TEXT SHAPE.
         * 9 live objects, 7 of them this screen's; 3 of those are names. */
        {
            ML_PLANT();
            gt_test_ml_feed(3, P);
            int tot = 0, nm = 0, mem = 0; long h = 0;
            BOOL got = gt_test_ml_bucket_at(0, &tot, &nm, &mem, &h);
            char d[160];
            snprintf(d, sizeof d, "buckets=%d total=%d names=%d members=%d",
                     gt_test_ml_buckets(), tot, nm, mem);
            check("40. members key on the screen pointer, names on the column",
                  got && mem == 7 && nm == 3, d);
        }

        /* case 41: ⭐ THE COLUMN X IS DERIVED, not hardcoded. The same shape
         * shifted by +400 must give the same verdict -- and it must, because
         * that constant has been written down as 1265, then 1268, and measured
         * as 633.0. Three values for one number is why nothing bakes it in. */
        {
            gt_test_reset();
            gt_test_focus_enable();
            gt_movelist_enable(TRUE);
            for (int i = 0; i < 7; i++) {
                memset(ours[i], 0, sizeof ours[i]);
                *(ULONGLONG *)ours[i] = GT_VTABLE_ADDR;
                float x = OX[i] + 400.f;
                memcpy(ours[i] + 0x40, &x, sizeof(float));
                float yy = 234.f + 73.f * (float)i;
                memcpy(ours[i] + 0x44, &yy, sizeof(float));
                memcpy(ours[i] + 0x88, &P, sizeof(void *));
                gt_test_settext_body(ours[i], OT[i], NULL, 0);
            }
            gt_test_ml_feed(3, P);
            int nm = 0, mem = 0;
            gt_test_ml_bucket_at(0, NULL, &nm, &mem, NULL);
            char d[140];
            snprintf(d, sizeof d, "shifted +400: names=%d members=%d", nm, mem);
            check("41. the name column's X is DERIVED, not a constant",
                  nm == 3 && mem == 7, d);
        }

        /* case 42: ⛔⛔ A MISMATCH IS RECORDED, NOT SILENCED. This is the
         * outcome the logs suggest is REAL (the vertical total read 3 while ~20
         * name objects were live), so discarding non-matching samples would
         * print an empty histogram that reads as "no data". Refusing to NAME on
         * a mismatch is right; refusing to MEASURE one is not. */
        {
            ML_PLANT();
            gt_test_ml_feed(9, P);           /* cursor says 9, only 3 names */
            int tot = 0, nm = 0;
            BOOL got = gt_test_ml_bucket_at(0, &tot, &nm, NULL, NULL);
            char d[140];
            snprintf(d, sizeof d, "buckets=%d total=%d names=%d",
                     gt_test_ml_buckets(), tot, nm);
            check("42. a total/names MISMATCH is recorded, not discarded",
                  got && tot == 9 && nm == 3, d);
        }

        /* case 43: repeats AGGREGATE. 200 identical movements must occupy one
         * bucket -- that is what makes hundreds of fast movements survivable
         * against a 24-slot table and a per-line-fopen log. */
        {
            ML_PLANT();
            for (int i = 0; i < 200; i++)
                gt_test_ml_feed(3, P);
            long h = 0;
            gt_test_ml_bucket_at(0, NULL, NULL, NULL, &h);
            char d[140];
            snprintf(d, sizeof d, "buckets=%d hits=%ld overflow=%ld",
                     gt_test_ml_buckets(), h, gt_test_ml_overflow());
            check("43. 200 identical outcomes aggregate into ONE bucket",
                  gt_test_ml_buckets() == 1 && h == 200 &&
                  gt_test_ml_overflow() == 0, d);
        }

        /* case 44: ⛔ the 25th DISTINCT outcome is COUNTED, not wrapped away.
         * A wrapping ring would discard the rare outcome -- the very evidence
         * being sought -- and show a full table either way. */
        {
            ML_PLANT();
            for (int i = 0; i < 30; i++)
                gt_test_ml_feed(i, P);       /* 30 distinct totals */
            char d[140];
            snprintf(d, sizeof d, "buckets=%d overflow=%ld",
                     gt_test_ml_buckets(), gt_test_ml_overflow());
            check("44. outcomes past the cap are COUNTED, not wrapped",
                  gt_test_ml_buckets() == 24 && gt_test_ml_overflow() >= 1, d);
        }

        /* case 45: ⭐⭐ A MISSED MOVEMENT IS COUNTED. The probe samples at
         * 100 ms against ~0.6 s movements, so some pass between polls -- and a
         * thin sample must not be able to masquerade as a complete one.
         *
         * Drive the real entry point twice with three movements in between:
         * the sequence jumped by 3, so 2 were missed. */
        {
            ML_PLANT();
            void *S = (void *)0x2C3874D0ULL;
            gt_test_ml_note_move(1);
            gt_movelist_probe(S, 3, TRUE);       /* first sample */
            gt_test_ml_note_move(3);             /* 3 movements, 1 poll */
            gt_movelist_probe(S, 3, TRUE);       /* second sample */
            char d[140];
            snprintf(d, sizeof d, "samples=%d missed=%ld (want missed=2)",
                     gt_test_ml_buckets(), gt_test_ml_missed());
            check("45. movements passing between polls are COUNTED as missed",
                  gt_test_ml_missed() == 2, d);
        }

        /* case 46: ⭐⭐ OFF must not look like FOUND NOTHING. -1 and 0 are
         * completely different facts and an assertion of "it did not crash"
         * passes on both -- the shape that left case 29 green while executing
         * none of the code it named. */
        {
            ML_PLANT();
            gt_movelist_enable(FALSE);
            gt_test_ml_feed(3, P);
            char d[140];
            snprintf(d, sizeof d, "buckets=%d (want <0 = never ran)",
                     gt_test_ml_buckets());
            check("46. gated OFF reports -1, never a tidy 0",
                  gt_test_ml_buckets() < 0, d);
        }

        /* case 47: ⛔⛔ A STALE OBJECT IS NOT A MEMBER, and this is the worst
         * failure direction: validating the vptr only at insert time would
         * count a freed widget and answer YES when the truth is NO. */
        {
            ML_PLANT();
            *(ULONGLONG *)ours[1] = GT_VTABLE2_ADDR;   /* no longer ours */
            gt_test_ml_feed(3, P);
            int nm = 0, mem = 0;
            gt_test_ml_bucket_at(0, NULL, &nm, &mem, NULL);
            char d[140];
            snprintf(d, sizeof d, "names=%d members=%d (one name went stale)",
                     nm, mem);
            check("47. a stale object is re-validated OUT of the census",
                  nm == 2 && mem == 6, d);
        }

        #undef ML_PLANT
    }

    /* --- cases 48-52: gt_widget_of / gt_column_y ------------------------
     *
     * ⭐⭐ THE MOVE LIST'S THREE COLUMNS, real coordinates from
     * run-2026-09-11-2228: names at x=84, condition text at x=633, the heading
     * at x=960, input notation at x=1565, rows 73 px apart. A move's name and
     * its notation SHARE the row's Y, which is the pairing under test.
     *
     * ⛔⛔ THE FAILURE THIS GUARDS: pairing a name to the WRONG row's notation
     * would tell the player the wrong input for a move they can see named --
     * confidently, and undetectably. Case 50 is that case. */
    {
        static unsigned char col[8][GT_CLASS_SIZE];
        void *P = (void *)0x2C48D1C0ULL;      /* the move list's screen  */
        void *Q = (void *)0x2BE55550ULL;      /* another screen entirely */
        /* name/notation pairs on shared Ys, plus chrome and a foreign heading */
        static const float CX[8] = { 84.f, 1565.f, 84.f, 1565.f,
                                     633.f, 960.f, 960.f, 84.f };
        static const float CY[8] = { 234.f, 234.f, 307.f, 307.f,
                                     380.f, 142.f, 142.f, 380.f };
        static const char *const CT[8] = {
            " Hadoken", "<RGHT>236P", " Shoryuken", "<RGHT>623P",
            " During Hyakkishu", "Ken Moves", "Pause Menu", " Tatsumaki"
        };
        /* index 6 ("Pause Menu") belongs to Q -- the shared-chrome trap. */
        gt_test_reset();
        gt_test_focus_enable();
        for (int i = 0; i < 8; i++) {
            memset(col[i], 0, sizeof col[i]);
            *(ULONGLONG *)col[i] = GT_VTABLE_ADDR;
            memcpy(col[i] + 0x40, &CX[i], sizeof(float));
            memcpy(col[i] + 0x44, &CY[i], sizeof(float));
            void *par = (i == 6) ? Q : P;
            memcpy(col[i] + 0x88, &par, sizeof(void *));
            gt_test_settext_body(col[i], CT[i], NULL, 0);
        }

        /* case 48: the heading resolves, and the FOREIGN one at the same
         * coordinate does not win. ⚠ x=960 is shared chrome: in one run its
         * sightings carried SIX different parents. */
        {
            char v[64] = { 0 };
            BOOL ok = gt_widget_of(P, 960.f, -1.f, v, sizeof v);
            char d[140];
            snprintf(d, sizeof d, "returned %d, \"%s\" (want \"Ken Moves\"; "
                     "another screen has a heading at the same x/y)", (int)ok, v);
            check("48. a column query is scoped by the PARENT, not the coordinate",
                  ok && strcmp(v, "Ken Moves") == 0, d);
        }

        /* case 49: the Nth row of a column, sorted by Y. */
        {
            float y1 = 0.f, y2 = 0.f;
            BOOL a = gt_column_y(P, 84.f, 1, &y1);
            BOOL b = gt_column_y(P, 84.f, 3, &y2);
            char d[140];
            snprintf(d, sizeof d, "row1 y=%.0f (want 234) row3 y=%.0f (want 380)",
                     (double)y1, (double)y2);
            check("49. gt_column_y returns the Nth row's Y, top to bottom",
                  a && b && y1 == 234.f && y2 == 380.f, d);
        }

        /* case 50: ⛔⛔ THE PAIRING. Row 2's name is "Shoryuken" at y=307, so
         * its notation must be the y=307 one -- NOT row 1's. An implementation
         * that took the Nth notation without checking Y would pass whenever
         * both columns are complete and fail silently when they are not. */
        {
            float y = 0.f;
            char v[64] = { 0 };
            BOOL ok = gt_column_y(P, 84.f, 2, &y) &&
                      gt_widget_of(P, 1565.f, y, v, sizeof v);
            char d[140];
            snprintf(d, sizeof d, "row2 y=%.0f -> \"%s\" (want <RGHT>623P)",
                     (double)y, v);
            check("50. a row's notation is found by its own Y, not by ordinal",
                  ok && strcmp(v, "<RGHT>623P") == 0, d);
        }

        /* case 51: a row with NO notation refuses rather than borrowing one.
         * Row 3 (" Tatsumaki", y=380) has no x=1565 partner -- the 10-row draw
         * cap means a long list's lower rows genuinely lack one. */
        {
            float y = 0.f;
            char v[64] = { 1 };
            BOOL got = gt_column_y(P, 84.f, 3, &y);
            BOOL ok = gt_widget_of(P, 1565.f, y, v, sizeof v);
            char d[140];
            snprintf(d, sizeof d, "row3 y=%.0f, notation returned %d \"%s\"",
                     (double)y, (int)ok, v);
            check("51. a row with no notation refuses (never borrows a neighbour's)",
                  got && !ok && v[0] == 0, d);
        }

        /* case 52: the CONDITION column is not mistaken for a name. It is a
         * leading-space capitalised string at x=633 -- indistinguishable from a
         * move name by text, separable only by coordinate. */
        {
            char v[64] = { 0 };
            BOOL ok = gt_widget_of(P, 633.f, 380.f, v, sizeof v);
            char d[140];
            snprintf(d, sizeof d, "x=633 gives \"%s\"; the name column at that "
                     "y gives a different string", v);
            check("52. the condition column is addressable separately from names",
                  ok && strcmp(v, " During Hyakkishu") == 0, d);
        }
    }

    mem_guard_remove();

    printf("\n%s (%d failure(s))\n", failures ? "FAILED" : "ALL PASSED",
           failures);
    return failures ? 1 : 0;
}

/*
 * ⭐⭐ SABOTAGE LOG -- every mechanism above was broken on purpose and the
 * suite re-run. A test that has never been seen to fail is not evidence.
 * Run 2026-09-09; results verbatim, each sabotage applied alone:
 *
 *   detour records NULL instead of the pointer   -> ONLY case 2 fails
 *   never count invocations                      -> ONLY case 3 fails
 *   inspector's `best` starts at 0, not -1       -> cases 5 and 6 fail
 *   return NULL instead of the original's value  -> ONLY case 1 fails
 *   delete the install protection guard          -> ONLY case 9 fails
 *   delete the inline-string sweep               -> ONLY case 6 fails
 *   filtering the CTOR detour on vptr too        -> ONLY case 9b fails
 *   ctor count never incremented                 -> ONLY case 9b fails
 *   ctor reuses the update method's prologue     -> ONLY case 9c fails
 *   setter never copies the string               -> ONLY case 9d fails
 *   vptr filter removed from the setter          -> ONLY case 9e fails
 *   writer leaves the busy flag held             -> ONLY case 9f fails, IN
 *                                                   SECONDS. Before the spin
 *                                                   bound it HUNG the suite --
 *                                                   which in-game is a frozen
 *                                                   mod, not a missed line.
 *                                                   (in-game this would read
 *                                                    an integer as a char*)
 *                                                   (the real bug, replayed)
 *
 * ⭐⭐ THE ONE WORTH READING TWICE is the install guard. With it deleted,
 * gt_install() STILL returns FALSE -- MinHook refuses an unmapped address on
 * its own -- so an assertion of "install failed" stays green with the guard
 * gone. What changes is the REASON: 2 (BAD_PROTECT) becomes 3 (BAD_PROLOGUE).
 * Case 9 asserts the reason, which is why it catches this and the obvious
 * assertion would not. That asymmetry is the entire point of gt_install_why().
 *
 * ⚠ AND ONE HONEST NEGATIVE, recorded rather than hidden: no case here
 * isolates the VirtualQuery/fault-guard pairing in resolve_live(), because the
 * test path resolves pointers inside the harness's own buffer and never
 * touches foreign memory. That pairing is covered by test_memsafe_threads.c
 * and by menu_cursor's cases, not here. A green run of this suite is NOT
 * evidence that the live read is safe -- it is evidence that the inspector
 * draws the right conclusions from bytes it was given.
 */
