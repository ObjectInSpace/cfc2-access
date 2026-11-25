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

/* Case 56's gt_screen_texts() callback: count what it was handed. */
static BOOL screen_text_cb(const char *text, void *ctx)
{
    struct { int n; int legend; int foreign; } *s = ctx;
    s->n++;
    if (strstr(text, "Vertical Slash")) s->legend++;
    if (strstr(text, "Any Attack"))     s->foreign++;
    return TRUE;
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

        /* case 53: ⭐⭐ A BLANKED PAIR OBJECT MUST NOT SWALLOW THE NAME, and
         * this is MEASURED behaviour, not a hypothetical.
         *
         * The game POOLS one x=633 object per row and BLANKS it -- setting a
         * single space -- when that move has no condition, rather than
         * destroying it. Across four runs, all three x=633 objects ever seen
         * holding " " were ALSO seen holding real text (2C3DD4A0, 2C3DD840,
         * 2C513E50). So a blanked mate is a live, resident, Y-aligned widget
         * with non-empty text[0], and widgets_of() admits it: the `text[0]`
         * filter rejects "" but a space passes.
         *
         * ⚠ The row must still read its NAME. A pair column that is present
         * but blank is the common case (153 plain inputs against 23
         * condition-prefixed), so getting this wrong would silence most of
         * the panel while the sparse rows kept working -- the hardest failure
         * to hear, because the screen still talks. */
        {
            static unsigned char blank[2][GT_CLASS_SIZE];
            static const float BX[2] = { 84.f, 633.f };
            static const float BY[2] = { 526.f, 526.f };
            static const char *const BT[2] = { " Jinchu Watari", " " };
            for (int i = 0; i < 2; i++) {
                memset(blank[i], 0, sizeof blank[i]);
                *(ULONGLONG *)blank[i] = GT_VTABLE_ADDR;
                memcpy(blank[i] + 0x40, &BX[i], sizeof(float));
                memcpy(blank[i] + 0x44, &BY[i], sizeof(float));
                memcpy(blank[i] + 0x88, &P, sizeof(void *));
                gt_test_settext_body(blank[i], BT[i], NULL, 0);
            }
            /* Row 4 by Y order: 234, 307, 380, 526. */
            char v[128] = { 0 };
            BOOL ok = gt_column_item(P, 84.f, 633.f, 4, v, sizeof v);
            char d[200];
            snprintf(d, sizeof d, "row4 with a BLANKED x=633 mate -> %d \"%s\" "
                     "(the name must survive; a blank mate may not erase it)",
                     (int)ok, v);
            check("53. a blanked pair object does not swallow the row's name",
                  ok && strstr(v, "Jinchu Watari") != NULL, d);
        }

        /* case 54b: ⭐⭐ THE COLUMN IS WHAT SEPARATES A ROW LABEL FROM ITS VALUE,
         * and no text rule can.
         *
         * ⛔⛔ Training Options draws row labels at X=320 and their values at
         * X=835, and the value strings are just as short, markup-free and
         * non-sentence as the labels ("Normal", "Off", "Auto-Recovery"). A
         * text-only candidate filter (MC_COORD_LIVE's) admitted TEN candidates
         * where FIVE were needed on the real dumps, so the count check refused
         * the screen outright -- correct, but silent. MC_ROW_COLUMN scopes by X
         * instead, which is why this case plants BOTH columns and requires the
         * count to see only the labels. */
        {
            static unsigned char tro[8][GT_CLASS_SIZE];
            void *T = (void *)0x2C29E210ULL;      /* the real screen pointer */
            static const float TX[8] = { 320.f, 835.f, 320.f, 835.f,
                                         320.f, 835.f, 320.f, 960.f };
            static const float TY[8] = { 366.f, 366.f, 446.f, 446.f,
                                         526.f, 526.f, 606.f, 305.f };
            static const char *const TT[8] = {
                "Health Bar", "Normal", "Groove Gauge", "Auto-Recovery",
                "Guard Crush", "Normal", "Stun", "Player Settings"
            };
            for (int i = 0; i < 8; i++) {
                memset(tro[i], 0, sizeof tro[i]);
                *(ULONGLONG *)tro[i] = GT_VTABLE_ADDR;
                memcpy(tro[i] + 0x40, &TX[i], sizeof(float));
                memcpy(tro[i] + 0x44, &TY[i], sizeof(float));
                memcpy(tro[i] + 0x88, &T, sizeof(void *));
                gt_test_settext_body(tro[i], TT[i], NULL, 0);
            }
            int nrows = gt_column_count(T, 320.0f);
            int nvals = gt_column_count(T, 835.0f);
            char r2[GT_CAPTION_MAX] = { 0 };
            BOOL got = gt_column_item(T, 320.0f, -1.0f, 2, r2, sizeof r2);
            char d[200];
            snprintf(d, sizeof d, "x=320 -> %d label(s), x=835 -> %d value(s); "
                     "row 2 = \"%s\" (want Groove Gauge)", nrows, nvals, r2);
            check("54b. a row column excludes the value column and the heading",
                  nrows == 4 && nvals == 3 && got &&
                  strcmp(r2, "Groove Gauge") == 0, d);

            /* --- cases 54i-54k: ⭐⭐ QUOTING THE ROW'S VALUE ----------------
             *
             * MC_ROW_COLUMN's pair_x appends the value sharing a row's Y, so a
             * settings row reads "Game Difficulty, Free Play". The values are
             * QUOTED rather than mapped because the drawn text does NOT
             * identify a corpus label: on the real screen "Normal" has 13 stems
             * and two of them are rows of that same screen, and "Free Play"'s
             * only stem belongs to a different row than the one it is drawn on.
             *
             * ⚠ This fixture has FOUR labels and THREE values -- "Stun" has no
             * partner -- which is exactly the asymmetry that makes ordinal
             * pairing wrong. */

            /* case 54i: the value joins its OWN row, by shared Y. */
            {
                char v2[GT_CAPTION_MAX] = { 0 };
                BOOL g2 = gt_column_item(T, 320.0f, 835.0f, 2, v2, sizeof v2);
                snprintf(d, sizeof d, "row 2 with pair_x=835 -> \"%s\" "
                         "(want \"Groove Gauge, Auto-Recovery\")", v2);
                check("54i. a row quotes the value sharing its Y",
                      g2 && strcmp(v2, "Groove Gauge, Auto-Recovery") == 0, d);
            }

            /* case 54j: ⛔⛔ THE ASYMMETRY CASE, and the reason for shared-Y.
             * "Stun" (row 4, y=606) has NO value. Ordinal pairing would hand it
             * the 3rd value ("Normal", y=526) -- a confidently wrong SETTING
             * for a row that has none. It must come back as the label alone. */
            {
                char v4[GT_CAPTION_MAX] = { 0 };
                BOOL g4 = gt_column_item(T, 320.0f, 835.0f, 4, v4, sizeof v4);
                size_t ln = strlen(v4);
                BOOL dangling = ln > 0 && (v4[ln-1] == ',' || v4[ln-1] == ' ');
                snprintf(d, sizeof d, "row 4 (no value) -> \"%s\"; dangling "
                         "separator: %s", v4, dangling ? "YES" : "no");
                check("54j. a row with no value quotes the label ALONE, never "
                      "another row's value",
                      g4 && strcmp(v4, "Stun") == 0 && !dangling, d);
            }

            /* case 54k: pair_x unset (< 0) keeps the label-only behaviour every
             * MC_ROW_COLUMN screen shipped with before this field existed. */
            {
                char v1[GT_CAPTION_MAX] = { 0 };
                BOOL g1 = gt_column_item(T, 320.0f, -1.0f, 1, v1, sizeof v1);
                snprintf(d, sizeof d, "row 1 with no pair_x -> \"%s\" "
                         "(want \"Health Bar\", NOT \"Health Bar, Normal\")",
                         v1);
                check("54k. no pair_x still reads the label alone",
                      g1 && strcmp(v1, "Health Bar") == 0, d);
            }

            /* case 54x: ⭐⭐ THE PAGE NOW SHOWING, by write stamp. The live CVS1
             * EX page (build/captures/stamps-20261015): the cursor has 4 rows,
             * the column 6 labels. The page change wrote its four rows at frame
             * 1766; page 1's "Battle Type" (at the SAME Y as the real "Default")
             * and "Default" keep 1700, as does page 1's value "Normal" on the
             * Default row. Current values were written at 1798.
             *
             * ⛔ The stamp is a WRITE stamp, not "drawn this frame": a visible
             * heading carried 1692. Old does not mean hidden; only labels older
             * than THIS column's latest write are another page's. */
            {
                static unsigned char ex[9][GT_CLASS_SIZE];
                void *E = (void *)0x2C5D2A80ULL;          /* the real screen */
                static const float EX[9] = { 320.f, 320.f, 320.f, 320.f, 320.f, 320.f,
                                             835.f, 835.f, 835.f };
                static const float EY[9] = { 406.f, 486.f, 566.f, 646.f, 646.f, 726.f,
                                             486.f, 566.f, 646.f };
                static const unsigned ES[9] = { 1766, 1766, 1766, 1766, 1700, 1700,
                                                1798, 1798, 1700 };
                static const char *const ET[9] = {
                    "Coin Mode", "Boot Display", "Stage Music", "Default",
                    "Battle Type", "Default", "None", "Original", "Normal"
                };
                char r3[GT_CAPTION_MAX] = { 0 }, r4[GT_CAPTION_MAX] = { 0 }, r5[GT_CAPTION_MAX] = { 0 };
                int n_all, n_page, n_unknown, n_agrees;
                BOOL g3, g4, g5, g_old;
                for (int i = 0; i < 9; i++) {
                    memset(ex[i], 0, sizeof ex[i]);
                    *(ULONGLONG *)ex[i] = GT_VTABLE_ADDR;
                    memcpy(ex[i] + 0x40, &EX[i], sizeof(float));
                    memcpy(ex[i] + 0x44, &EY[i], sizeof(float));
                    memcpy(ex[i] + 0x88, &E, sizeof(void *));
                    memcpy(ex[i] + GT_STAMP_OFF, &ES[i], sizeof(unsigned));
                    gt_test_settext_body(ex[i], ET[i], NULL, 0);
                }
                n_all = gt_column_count(E, 320.0f);             /* every label: 6 */
                n_page = gt_page_count(E, 320.0f, 4);           /* this page's: 4 */
                n_unknown = gt_page_count(E, 320.0f, -1);       /* no row total: as is */
                n_agrees = gt_page_count(E, 320.0f, 6);         /* count agrees: as is */
                g3 = gt_page_item(E, 320.0f, 835.0f, 4, 3, r3, sizeof r3);
                g4 = gt_page_item(E, 320.0f, 835.0f, 4, 4, r4, sizeof r4);
                g5 = gt_page_item(E, 320.0f, 835.0f, 5, 2, r5, sizeof r5);   /* 4 != 5 */
                {
                    char r[GT_CAPTION_MAX];
                    g_old = gt_column_item(E, 320.0f, 835.0f, 4, r, sizeof r);  /* the tie */
                }
                snprintf(d, sizeof d, "all %d (want 6), page %d (want 4), no total %d "
                         "(want 6), agreeing %d (want 6); row 3 \"%s\", row 4 \"%s\"; "
                         "a 5-row cursor %s (want refused); the unselected read %s "
                         "(want refused: two labels at y=646)",
                         n_all, n_page, n_unknown, n_agrees, r3, r4,
                         g5 ? "named" : "refused", g_old ? "named" : "refused");
                check("54x. a shorter page's rows are the labels its page change wrote; "
                      "the previous page's leftovers and stale value are not read",
                      n_all == 6 && n_page == 4 && n_unknown == 6 && n_agrees == 6 &&
                      g3 && strcmp(r3, "Stage Music, Original") == 0 &&
                      g4 && strcmp(r4, "Default") == 0 && !g5 && !g_old, d);
            }

            /* case 54y: BACK ON PAGE 1, the EX page's Coin Mode value "Free
             * Play" is still alive on Game Difficulty's line -- the one row
             * whose own value is a graphic, so nothing collides with it and the
             * row count agrees. User, 2026-10-15: Game Difficulty read "Free
             * Play". A value written before its row label is not its value. */
            {
                static unsigned char pg[4][GT_CLASS_SIZE];
                void *G = (void *)0x2C5D2A81ULL;
                static const float GX[4] = { 320.f, 320.f, 835.f, 835.f };
                static const float GY[4] = { 366.f, 446.f, 366.f, 446.f };
                static const unsigned GS[4] = { 2000, 2000, 1900, 2001 };
                static const char *const GT[4] = {
                    "Game Difficulty", "Attack Power", "Free Play", "Normal"
                };
                char a1[GT_CAPTION_MAX] = { 0 }, a2[GT_CAPTION_MAX] = { 0 },
                     o1[GT_CAPTION_MAX] = { 0 };
                BOOL h1, h2, k1;
                for (int i = 0; i < 4; i++) {
                    memset(pg[i], 0, sizeof pg[i]);
                    *(ULONGLONG *)pg[i] = GT_VTABLE_ADDR;
                    memcpy(pg[i] + 0x40, &GX[i], sizeof(float));
                    memcpy(pg[i] + 0x44, &GY[i], sizeof(float));
                    memcpy(pg[i] + 0x88, &G, sizeof(void *));
                    memcpy(pg[i] + GT_STAMP_OFF, &GS[i], sizeof(unsigned));
                    gt_test_settext_body(pg[i], GT[i], NULL, 0);
                }
                h1 = gt_page_item(G, 320.0f, 835.0f, 2, 1, a1, sizeof a1);
                h2 = gt_page_item(G, 320.0f, 835.0f, 2, 2, a2, sizeof a2);
                k1 = gt_column_item(G, 320.0f, 835.0f, 1, o1, sizeof o1);
                snprintf(d, sizeof d, "row 1 \"%s\" (want \"Game Difficulty\"), row 2 "
                         "\"%s\" (want \"Attack Power, Normal\"); unstamped read \"%s\"",
                         a1, a2, o1);
                check("54y. a value written before its row is the previous page's, "
                      "not this row's",
                      h1 && strcmp(a1, "Game Difficulty") == 0 &&
                      h2 && strcmp(a2, "Attack Power, Normal") == 0 &&
                      k1 && strcmp(o1, "Game Difficulty, Free Play") == 0, d);
            }


            /* case 54l: ⭐⭐⭐ A MISALIGNED COLUMN -- the case that actually
             * discriminates shared-Y from ordinal pairing.
             *
             * ⛔⛔ THE SABOTAGE SWEEP EXPOSED 54i-54k AS BLIND TO THIS. With the
             * mate chosen by ORDINAL instead of shared Y, all three passed --
             * because the 54b fixture is ALIGNED: row N and value N happen to
             * share a Y for every row that has a value. Two rules that agree on
             * every input a case supplies are untested by that case.
             *
             * Here the value column SKIPS row 2 (nothing at y=446), which is
             * the real shape of an action row sitting mid-screen. Shared-Y must
             * give row 3 its OWN value; ordinal would hand row 3 the SECOND
             * value -- the value belonging to a different setting, spoken
             * confidently. That is the confusion this pairing rule exists to
             * prevent. */
            {
                static unsigned char mo[5][GT_CLASS_SIZE];
                void *M = (void *)0x2C29E300ULL;
                static const float MX[5] = { 320.f, 320.f, 320.f,
                                             835.f, 835.f };
                static const float MY[5] = { 366.f, 446.f, 526.f,
                                             366.f, 526.f };   /* no 446 */
                static const char *const MT[5] = {
                    "First Row", "Action Row", "Third Row",
                    "First Value", "Third Value" };
                for (int i = 0; i < 5; i++) {
                    memset(mo[i], 0, sizeof mo[i]);
                    *(ULONGLONG *)mo[i] = GT_VTABLE_ADDR;
                    memcpy(mo[i] + 0x40, &MX[i], sizeof(float));
                    memcpy(mo[i] + 0x44, &MY[i], sizeof(float));
                    memcpy(mo[i] + 0x88, &M, sizeof(void *));
                    gt_test_settext_body(mo[i], MT[i], NULL, 0);
                }
                char r3[GT_CAPTION_MAX] = { 0 }, r2[GT_CAPTION_MAX] = { 0 };
                BOOL g3 = gt_column_item(M, 320.0f, 835.0f, 3, r3, sizeof r3);
                BOOL g2b = gt_column_item(M, 320.0f, 835.0f, 2, r2, sizeof r2);
                snprintf(d, sizeof d, "row 3 -> \"%s\" (want \"Third Row, "
                         "Third Value\", ordinal would give \"Third Value\" to "
                         "row 2); row 2 -> \"%s\" (want \"Action Row\")",
                         r3, r2);
                check("54l. a SKIPPED row does not shift every value up one",
                      g3 && strcmp(r3, "Third Row, Third Value") == 0 &&
                      g2b && strcmp(r2, "Action Row") == 0, d);
            }
        }


        /* case 54: and the blanked mate must not be spoken AS a condition.
         * "Jinchu Watari,  " read aloud is a name followed by a pause the
         * player cannot interpret. Whatever the join does, the result must
         * not end in a dangling separator. */
        {
            char v[128] = { 0 };
            BOOL ok = gt_column_item(P, 84.f, 633.f, 4, v, sizeof v);
            size_t n = strlen(v);
            BOOL trailing = n > 0 && (v[n-1] == ',' || v[n-1] == ' ');
            char d[200];
            snprintf(d, sizeof d, "\"%s\" -- trailing separator/space: %s",
                     v, trailing ? "YES (a dangling pause)" : "no");
            check("54. a blanked mate leaves no dangling separator",
                  ok && !trailing, d);
        }

        /* case 55: ⭐⭐ A 7-ROW PAUSE MENU HAS AN 8TH, BLANKED, POOLED LABEL.
         * Real layout, run 2026-09-11 15:17: seven labels at x=362 plus " " at
         * y=785 -- the slot the 8-row variant fills with "Quit". Counted, the
         * column read 8 against the cursor's 7 and the CVS2 intro's pause
         * menu spoke bare positions. Planted in SCRAMBLED order so a lookup by
         * insertion order cannot pass. */
        {
            static unsigned char pm[8][GT_CLASS_SIZE];
            void *Q = (void *)0x2BBEB930ULL;      /* the real screen pointer */
            static const float PY[8] = { 785.f, 465.f, 225.f, 705.f,
                                         305.f, 625.f, 545.f, 385.f };
            static const char *const PT[8] = {
                " ", "Controller Settings", "Resume", "Quit", "Marquee Card",
                "Change Character", "Display & Sound Settings", "Move List"
            };
            const float px = 362.f;
            for (int i = 0; i < 8; i++) {
                memset(pm[i], 0, sizeof pm[i]);
                *(ULONGLONG *)pm[i] = GT_VTABLE_ADDR;
                memcpy(pm[i] + 0x40, &px, sizeof(float));
                memcpy(pm[i] + 0x44, &PY[i], sizeof(float));
                memcpy(pm[i] + 0x88, &Q, sizeof(void *));
                gt_test_settext_body(pm[i], PT[i], NULL, 0);
            }
            int n = gt_column_count(Q, px);
            char first[64] = { 0 }, last[64] = { 0 };
            BOOL ok1 = gt_column_item(Q, px, -1.f, 1, first, sizeof first);
            BOOL ok7 = gt_column_item(Q, px, -1.f, 7, last, sizeof last);
            char d[200];
            snprintf(d, sizeof d, "column count %d (want 7); row 1 \"%s\", "
                     "row 7 \"%s\"", n, first, last);
            check("55. a blanked pooled slot is not counted as a row",
                  n == 7 && ok1 && strcmp(first, "Resume") == 0 &&
                  ok7 && strcmp(last, "Quit") == 0, d);
        }
    }

    /* --- case 56: THE SCREEN'S LIVE TEXT, markup intact ---------------------
     * ⛔⛔ The move list's legend reader walked the focus record through a
     * prose-only reader, and a legend is nothing BUT markup
     * ("<ICON C_A>Vertical Slash<ICON C_B>..."), so it never learned a single
     * name in shipping. gt_screen_texts() replaces it: the live objects whose
     * parent is this screen, text untouched -- and NOT another screen's. */
    {
        static unsigned char obj[3][GT_CLASS_SIZE];
        void *P = (void *)0x2C48D1C0ULL;
        void *Q = (void *)0x2BE55550ULL;
        static const char *const T[3] = {
            "<SIZE 58><ICON C_A></SIZE>Vertical Slash<SIZE 58><ICON C_B></SIZE>Horizontal Slash",
            " Hadoken",
            "<SIZE 58><ICON C_A></SIZE>Any Attack<SIZE 58><ICON C_B></SIZE>Shot",
        };
        gt_test_reset();
        gt_test_focus_enable();
        for (int i = 0; i < 3; i++) {
            float x = 84.f, y = 100.f + 50.f * (float)i;
            memset(obj[i], 0, sizeof obj[i]);
            *(ULONGLONG *)obj[i] = GT_VTABLE_ADDR;
            memcpy(obj[i] + 0x40, &x, sizeof x);
            memcpy(obj[i] + 0x44, &y, sizeof y);
            void *par = (i == 2) ? Q : P;       /* index 2: another game's legend */
            memcpy(obj[i] + 0x88, &par, sizeof(void *));
            gt_test_settext_body(obj[i], T[i], NULL, 0);
        }
        struct { int n; int legend; int foreign; } seen = { 0, 0, 0 };
        int n = gt_screen_texts(P, screen_text_cb, &seen);
        char d[200];
        snprintf(d, sizeof d, "returned %d, callbacks %d, saw P's legend %d, "
                 "saw Q's legend %d (want 2, 2, 1, 0)",
                 n, seen.n, seen.legend, seen.foreign);
        check("56. a screen's live text, markup intact, and only that screen's",
              n == 2 && seen.n == 2 && seen.legend == 1 && seen.foreign == 0, d);
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
