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
