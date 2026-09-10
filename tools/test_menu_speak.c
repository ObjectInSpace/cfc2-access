/*
 * Offline self-test for the movement -> speech logic.
 *
 * ⛔⛔ WHAT THIS FILE IS PROTECTING
 * A WRONG item name is worse than silence for a blind user: it is
 * indistinguishable from a right one, so unlike a sighted player seeing a
 * mislabelled screen, they cannot catch and correct it. Every case below exists
 * to keep the mod on the safe side of that line -- speak position (always true)
 * rather than a name that might not be.
 *
 * ⚠ menu_speak_build() is tested, NOT menu_speak(): the latter calls into
 * UniversalSpeech. The split is deliberate -- all the decisions live in
 * _build(), and the part that cannot run offline does no deciding.
 *
 * ⚠ gmd_text_by_index() returns FALSE here (no game, no table). That is itself
 * one of the cases: text unavailable must degrade to position, not to silence
 * or a guess.
 *
 * SABOTAGE RESULTS -- each mechanism broken deliberately, suite re-run:
 *   delete the staleness check   -> ONLY case 3 fails (why=TEXT_UNREADABLE
 *                                   instead of COUNT_MISMATCH)
 *   fuzzy caller comparison      -> ONLY case 6 fails
 *   absolute (not min-relative)  -> ONLY case 5 fails
 *   name an item with no text    -> ONLY case 4 fails, item="Offline Play"
 *                                   -- the exact wrong-name failure this file
 *                                   exists to prevent
 *
 * ⚠⚠ AND THE ONE THAT TAUGHT ME SOMETHING. Asserting only `!named` for case 3
 * passed with the staleness check ENTIRELY DELETED, because with no live GMD
 * table the text lookup fails anyway and produces the same outcome by a
 * different route. The fix was to report WHICH guard declined (mc_named_why)
 * and have the case demand that specific reason. Same shape as the
 * kbd_install_why() lesson: when two layers can produce one outcome, asserting
 * the outcome tests neither.
 *
 * SABOTAGE RESULTS FOR THE SETTINGS-SCREEN CASES (10-15), same method:
 *   drop the alignment check    -> 11 and 14 fail. ⭐⭐ And 14 shows the real
 *                                  danger: without it a MISALIGNED cursor is
 *                                  silently rounded into "row 0", i.e. the mod
 *                                  would name the wrong setting confidently.
 *   drop the range ceiling      -> ONLY 11 fails (row 6 on a 3-row screen)
 *   let a settings ROW be named -> ONLY 12 fails
 *   never report a derived row  -> ONLY 13 fails
 *   apply the count check to the
 *     varying-count site        -> 13, 14 and 15 fail
 *
 * ⭐⭐ AND ONE MECHANISM THAT WAS DELETED RATHER THAN TESTED. derive_row first
 * carried a separate "is the cursor below the array?" check beside the range
 * ceiling. Sabotage proved it REDUNDANT, not protective: deleting it -- in both
 * unsigned and signed form -- left the entire suite green, because a cursor
 * below the base wraps to a delta of ~2^63 that the ceiling rejects anyway.
 * Two expressions of one property are not two layers; they just make the
 * property untestable. The check was removed and the ceiling now carries it
 * alone, which is why case 11's `below` sub-case is meaningful.
 *
 * ⚠ A fifth sabotage (drop the unknown-screen guard) does NOT discriminate --
 * it also falls through to the failing text lookup. The risk it models (an
 * unknown menu borrowing row 0's labels) is covered by case 2 demanding
 * why == MC_UNNAMED_SCREEN_UNKNOWN. Recorded rather than left as a gap
 * someone later mistakes for coverage.
 */

#include "../src/menu_speak.h"
#include "../src/menu_cursor.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int failures;

static void check(const char *name, int cond, const char *fmt, ...)
{
    if (cond) { printf("  PASS  %s\n", name); return; }
    printf("  FAIL  %s", name);
    if (fmt && *fmt) {
        va_list ap; va_start(ap, fmt);
        printf("  "); vprintf(fmt, ap); va_end(ap);
    }
    printf("\n");
    failures++;
}

static mc_move mk(ULONGLONG caller, int newi, int mini, int maxi)
{
    mc_move m;
    memset(&m, 0, sizeof(m));
    m.caller    = (void *)(ULONG_PTR)caller;
    m.new_index = newi;
    m.old_index = newi;
    m.min_index = mini;
    m.max_index = maxi;
    m.vertical  = TRUE;
    return m;
}

/* The main menu's call site.
 *
 * ⛔ This used to be 0x14009ba21 -- a row added from the first run on the
 * reasoning that it was the only 6-item vertical screen seen. That guess was
 * WRONG and had to be retracted: it made a DIFFERENT 6-item screen speak the
 * main menu's labels. 0x140097a59 is the one established by reading LIVE TEXT
 * ("Quitter" at position 6 of 6), and it is the one that survived contact with
 * a real run. */
#define MAIN_CALLER 0x140097a59ULL

int main(void)
{
    mc_speech sp;

    /* --- case 1: position is computed for a KNOWN screen ------------------ */
    mc_move m = mk(MAIN_CALLER, 2, 0, 5);
    menu_speak_build(&m, &sp);
    check("1. a known screen yields 1-based position and total",
          sp.position == 3 && sp.total == 6,
          "position=%d total=%d", sp.position, sp.total);

    /* --- case 2: an UNKNOWN screen still yields position ------------------
     * ⭐ The fallback that makes an incomplete table safe. Every screen not in
     * g_screens must still be navigable by position. */
    m = mk(0xDEADBEEFULL, 4, 0, 9);
    menu_speak_build(&m, &sp);
    check("2. an unknown screen still yields position, and is NOT named",
          sp.position == 5 && sp.total == 10 && !sp.named &&
          sp.screen_name[0] == '\0',
          "position=%d total=%d named=%d screen=%s",
          sp.position, sp.total, (int)sp.named, sp.screen_name);

    /* --- case 3: THE STALENESS CHECK --------------------------------------
     * ⭐⭐ THE MOST IMPORTANT CASE. The right caller but the WRONG item count
     * means this is not the screen the table row describes -- a game update
     * reordered the menu, or the call site was reused. Indexing the label run
     * anyway would speak confident wrong names. */
    m = mk(MAIN_CALLER, 2, 0, 8);        /* 9 items, table says 6 */
    menu_speak_build(&m, &sp);
    /* ⭐⭐ It demands the REASON, not just "no name". Asserting only !named
     * passed with this whole check DELETED, because the text lookup fails in
     * this process anyway and produced the same outcome by another route. */
    check("3. a count mismatch REFUSES the table row, and SAYS so",
          !sp.named && sp.why == MC_UNNAMED_COUNT_MISMATCH &&
          sp.position == 3 && sp.total == 9,
          "named=%d why=%d position=%d total=%d",
          (int)sp.named, (int)sp.why, sp.position, sp.total);

    /* --- case 4: text unavailable degrades to position, never a guess -----
     * There is no live GMD table in this process, so gmd_text_by_index() fails.
     * The screen IS known and the count DOES match -- so this isolates the
     * "could not read the text" path specifically. */
    m = mk(MAIN_CALLER, 0, 0, 5);
    menu_speak_build(&m, &sp);
    check("4. unreadable text degrades to position, and `named` stays FALSE",
          !sp.named && sp.item[0] == '\0' && sp.position == 1 && sp.total == 6,
          "named=%d item=\"%s\"", (int)sp.named, sp.item);

    /* --- case 5: a non-zero min_index is handled -------------------------
     * Some menus will not start at 0. Position must be relative to min, or the
     * readout would be off by min on every such screen. */
    m = mk(0xDEADBEEFULL, 5, 3, 7);
    menu_speak_build(&m, &sp);
    check("5. position is relative to min_index, not absolute",
          sp.position == 3 && sp.total == 5,
          "position=%d total=%d", sp.position, sp.total);

    /* --- case 6: the table lookup is exact, not fuzzy --------------------
     * An address one byte off must NOT match. Call sites sit close together
     * (19 functions contain several), so a sloppy comparison would attribute a
     * movement to the wrong list within the right screen. */
    check("6. lookup is exact -- a nearby address does not match",
          menu_speak_lookup(MAIN_CALLER) != NULL &&
          menu_speak_lookup(MAIN_CALLER + 1) == NULL &&
          menu_speak_lookup(MAIN_CALLER - 1) == NULL, "");

    /* --- case 7: the shipped table is not empty and is self-consistent ---- */
    int n = menu_speak_screen_count();
    const mc_screen *s = menu_speak_lookup(MAIN_CALLER);
    check("7. the shipped table has the main menu with base 37, count 6",
          n >= 1 && s && s->base_index == 37 && s->count == 6,
          "n=%d base=%d count=%d", n, s ? s->base_index : -1,
          s ? s->count : -1);

    /* --- case 8: the table holds CALL SITES, not return addresses ---------
     * ⭐@@S@ REGRESSION GUARD FOR A BUG THAT SHIPPED. The hook takes
     * __builtin_return_address(0), which points at the instruction AFTER the
     * call -- 5 bytes on for `call rel32`. The table was written from a
     * disassembler (call sites), so nothing ever matched and EVERY screen fell
     * back to position-only. Confirmed on all six screens of the 2026-09-08
     * run: every observed return address minus 5 was a known call site, and no
     * raw return address was itself one.
     *
     * The hook now normalises to the call site. This case pins the convention
     * so the table and the lookup cannot drift apart again: the shipped entry
     * must be the CALL SITE, and the return address 5 bytes on must NOT match.
     */
    check("8. the table key is the call site, not the return address",
          menu_speak_lookup(MAIN_CALLER) != NULL &&
          menu_speak_lookup(MAIN_CALLER + 5) == NULL,
          "call-site=%p  ret-addr=%p",
          (const void *)menu_speak_lookup(MAIN_CALLER),
          (const void *)menu_speak_lookup(MAIN_CALLER + 5));

    /* --- case 9: every shipped row is internally sane --------------------
     * ⭐ The table is hand-edited after each run now, so the likeliest future
     * defect is a typo'd row rather than a logic bug. Check what can be checked
     * without the game: a plausible base, a positive count, a name, and -- the
     * one that matters -- NO DUPLICATE CALLER. Two rows sharing a caller means
     * one silently shadows the other, and the shadowed screen would speak
     * another menu's labels, which is the confident-wrong-name failure.
     *
     * ⚠ Two rows MAY share a BASE: the shell has two distinct screens
     * presenting the same six choices. Duplicate bases are legal, not checked.
     */
    int nrows = menu_speak_screen_count();
    int sane = (nrows >= 1);
    for (int i = 0; i < nrows; i++) {
        const mc_screen *a = menu_speak_row(i);
        if (!a || !a->name || !a->name[0])
            sane = 0;
        /* ⭐⭐ The sanity rule is per-KIND, not one rule loosened to fit.
         * A flat list MUST have a real base and count -- that is what gets
         * indexed, and a bad one speaks a wrong name. A settings row must have
         * NO base (-1), because it is forbidden from naming anything until a
         * discovery pass proves its labels; a base here would be the exact
         * confident-wrong-name failure. */
        else if (a->kind == MC_LIST) {
            /* ⭐ A flat list needs EITHER a base OR a label map -- and never
             * both. Online Play is a plain vertical menu whose items are
             * 68,69,70,71,1076,1078, so a base cannot address it; its row
             * carries `labels` and base_index -1.
             *
             * ⛔ "Both" must stay illegal: a map exists precisely because
             * base+index would land on the wrong string, so a row offering a
             * fallback base would reintroduce the bug the map prevents. */
            if (a->count <= 0)
                sane = 0;
            if (a->labels) {
                if (a->base_index != -1 || a->nlabels != a->count)
                    sane = 0;
            } else if (a->base_index < 0) {
                sane = 0;
            }
        } else {
            if (a->base_index != -1)
                sane = 0;             /* a settings row must claim NO label */
            if (a->count < 0)
                sane = 0;
            if (a->kind == MC_SETTINGS_VALUE && a->values_off <= 0)
                sane = 0;             /* the row cannot be derived without it */
        }
        /* ⭐⭐ Provenance. A row justified by plausibility rather than by a
         * live-text discovery block shipped once and made a different screen
         * speak the main menu's labels. Only VERIFIED_LIVE_TEXT may ship. */
        if (a && a->how != MC_VERIFIED_LIVE_TEXT)
            sane = 0;
        /* ⭐⭐ TWO ROWS MAY SHARE A CALLER -- BUT ONLY IF THE COUNT TELLS THEM
         * APART. Measured: 0x14009ba21 is "Select Mode" at 3 items AND the
         * Online Play menu at 6; the generic cursor helper is shared, so a
         * call site is not a unique screen identity.
         *
         * ⛔ This LOOSENS nothing that matters. If two rows shared a caller
         * AND a count, menu_speak_lookup_n() could not choose between them and
         * would silently return the first -- the same class of bug as the
         * guessed row, arriving by a different door. That stays illegal. */
        for (int j = i + 1; j < nrows; j++) {
            const mc_screen *b = menu_speak_row(j);
            if (!a || !b || a->caller != b->caller)
                continue;
            if (a->count <= 0 || b->count <= 0 || a->count == b->count)
                sane = 0;
        }
    }
    check("9. every row is verified, sane, and distinguishable "
          "(caller+count unique)", sane, "rows=%d", nrows);

    /* --- case 9a: the DISCOVERY sweep is OFF by default -------------------
     * ⛔⛔ This is a user-facing safety property, not a preference. That sweep
     * runs on the poll thread and stalled a live session TWICE -- 37 s, then
     * 11.9 s -- during which the mod stopped speaking and the user navigated
     * blind. It has named ZERO shipped screens (every g_screens row cites live
     * text, the disassembly, or the caption channel).
     *
     * ⚠ Asserted by TIMING the call, because that is the property that matters:
     * "returns quickly" is what the user experiences. With the gate removed
     * this takes hundreds of ms against a real GMD; here there is no live
     * table, so the assertion is simply that nothing expensive is attempted.
     *
     * ⭐ The default must be OFF at process start with no opt-in file, which is
     * the state an ordinary session is in. */
    /* ⛔ Asserted on the GATE'S STATE, not by timing the call.
     *
     * The first version of this case timed menu_speak_log_unknown() and PASSED
     * with the gate deleted -- the offline harness has no live GMD, so the
     * sweep exits early on `gmd_text_count() > 0` regardless. A second guard
     * was producing the effect the test attributed to the first. */
    {
        BOOL def = menu_speak_discovery_enabled();
        check("9a. the DISCOVERY sweep is OFF by default", !def,
              def ? "enabled with no opt-in file -- an ordinary session would "
                    "pay for it" : "off");

        /* And the switch must actually move, or the default above is just a
         * variable nobody reads. */
        menu_speak_set_discovery(TRUE);
        BOOL on = menu_speak_discovery_enabled();
        menu_speak_set_discovery(FALSE);
        check("9a2. the discovery switch responds to being set",
              on && !menu_speak_discovery_enabled(), "");
    }

    /* --- case 9d: is_value_shaped -- the ONLY guard on unverified speech ---
     * ⭐⭐ On a value axis the mod speaks the captured string WITHOUT matching
     * it to the corpus, because identification is impossible there ("Off"
     * occurs 22 times in the GMD; "1024x768" is generated and occurs zero
     * times). This predicate is what keeps prose and button strips out of that
     * path, so its NEGATIVES carry the weight.
     *
     * ⚠ Real strings from the run of 2026-09-10, both sides. */
    {
        static const char *const VALUES[] = {
            "Off", "On", "1024x768", "English [en]", "Original",
            "Agree", "Any", "2+", "Borderless Windowed",
        };
        static const char *const NOT_VALUES[] = {
            /* captions -- prose, and they end in a period */
            "Adjust the volume of background music.",
            "Change the resolution.",
            "Play offline.",
            /* a button strip -- markup */
            "<SIZE 58><ICON DECIDE></SIZE>Confirm <SIZE 58><ICON CANCEL></SIZE>Back",
            /* degenerate */
            "", " ", "\r\n",
            /* ⛔ EACH GUARD NEEDS A CASE ONLY IT REJECTS. Sabotage showed the
             * length and markup checks could both be deleted with this suite
             * still green -- every negative above also ends in a period, so
             * the punctuation check alone was carrying them. These three end
             * in no punctuation, so exactly one guard stands between each of
             * them and the user's ear. */
            /* long, no punctuation -> only the length cap rejects it */
            "Set the multiplayer input delay for online matches and lobbies",
            /* markup, no punctuation, and SHORT enough to pass the length cap
             * -> only the markup check rejects it. ⚠ The first attempt here
             * was 41 chars and the cap caught it first, so the markup guard
             * stayed untested and the sabotage sweep said so. */
            "<ICON BTN_X>Settings",
            /* multi-line, no punctuation -> only the newline check rejects it */
            "Windowed\r\nBorderless",
        };
        int bad = 0;
        for (size_t i = 0; i < sizeof VALUES / sizeof VALUES[0]; i++)
            if (!menu_speak_test_value_shaped(VALUES[i])) bad++;
        for (size_t i = 0; i < sizeof NOT_VALUES / sizeof NOT_VALUES[0]; i++)
            if (menu_speak_test_value_shaped(NOT_VALUES[i])) bad++;
        check("9d. value-shaped text is admitted, prose and strips are not",
              !bad && !menu_speak_test_value_shaped(NULL),
              "%d misclassified", bad);
    }

    /* --- case 9b: the SHARED CALLER is resolved by item count -------------
     * ⭐⭐ 0x14009ba21 serves two screens. The count is what tells them apart,
     * and this asserts the lookup actually uses it -- otherwise case 9's
     * loosened uniqueness rule would be permission without a mechanism.
     *
     * ⚠ Asserted on the NAMES, not merely on "two different pointers": a
     * lookup that returned the wrong row for each count would still return
     * two distinct pointers and pass a weaker form. */
    {
        const mc_screen *three = menu_speak_lookup_n(0x14009ba21ULL, 3);
        const mc_screen *six   = menu_speak_lookup_n(0x14009ba21ULL, 6);
        int ok = three && six && three != six &&
                 strcmp(three->name, "Select Mode") == 0 &&
                 strcmp(six->name, "Online Play") == 0 &&
                 six->labels && six->nlabels == 6 &&
                 /* the map's first and last entries, from the run's captions */
                 six->labels[0] == 68 && six->labels[5] == 1078;
        check("9b. a shared caller resolves by item count", ok,
              "3->%s  6->%s",
              three ? three->name : "(none)", six ? six->name : "(none)");
    }

    /* --- case 9c: an UNKNOWN count still yields a row, not nothing --------
     * ⚠ A count we have never seen must fall back to the caller match rather
     * than returning NULL -- the screen is then refused by the count guard in
     * menu_speak_build(), which reports WHY. Returning NULL here would collapse
     * "unknown screen" and "known screen, unexpected count" into one outcome. */
    {
        const mc_screen *odd = menu_speak_lookup_n(0x14009ba21ULL, 99);
        check("9c. an unseen count falls back to the caller match",
              odd != NULL, "got %s", odd ? odd->name : "(none)");
    }



    /* --- case 10: the row derivation, on the REAL numbers ----------------
     * ⭐⭐ These are the addresses from the 22:46 run's log, not invented ones:
     * screen 0x2C3B1D00, value cursors 0x2C3B2038 / 203C / 2040 for rows 0/1/2,
     * with the array at +0x338 read out of fn 0x1400b1b10. If the arithmetic in
     * the shipping code disagrees with the game's own layout, this fails. */
    {
        const void *screen = (const void *)(ULONG_PTR)0x2C3B1D00ULL;
        int row = -1;
        int ok0 = menu_speak_derive_row(screen,
                    (const void *)(ULONG_PTR)0x2C3B2038ULL, 0x338, 3, &row)
                  && row == 0;
        int ok1 = menu_speak_derive_row(screen,
                    (const void *)(ULONG_PTR)0x2C3B203CULL, 0x338, 3, &row)
                  && row == 1;
        int ok2 = menu_speak_derive_row(screen,
                    (const void *)(ULONG_PTR)0x2C3B2040ULL, 0x338, 3, &row)
                  && row == 2;
        check("10. the row is derived correctly from the real run's pointers",
              ok0 && ok1 && ok2, "ok0=%d ok1=%d ok2=%d", ok0, ok1, ok2);
    }

    /* --- case 11: a derivation that does NOT fit is REFUSED ---------------
     * ⭐⭐ THE CASE THAT MATTERS MOST HERE. The whole justification for deriving
     * the row by arithmetic is that a wrong model FAILS rather than yielding a
     * plausible row number. Three ways it can be wrong, all of which must
     * return FALSE and not a rounded answer:
     *   - the cursor lies BELOW the array (a different screen layout);
     *   - the difference is MISALIGNED (not an int array at all);
     *   - the row is OUT OF RANGE (past the end of a 3-row screen).
     * ⚠ `row` is left at a poison value to prove nothing was written. */
    {
        const void *screen = (const void *)(ULONG_PTR)0x2C3B1D00ULL;
        int row = -999;
        /* ⚠⚠ AN HONEST NOTE ON THIS SUB-CASE. With max_row passed, deleting
         * the `cur < base` check alone leaves the suite GREEN: the unsigned
         * subtraction wraps to a huge delta which the range bound then rejects
         * anyway -- two layers producing one outcome, which is the trap this
         * project has now hit four times. So `below` is tested with max_row 0,
         * where the range bound is explicitly NOT in play and only the lower
         * bound can produce the refusal. */
        int below = !menu_speak_derive_row(screen,
                      (const void *)(ULONG_PTR)0x2C3B2000ULL, 0x338, 0, &row);
        int misaligned = !menu_speak_derive_row(screen,
                      (const void *)(ULONG_PTR)0x2C3B203AULL, 0x338, 3, &row);
        int too_far = !menu_speak_derive_row(screen,
                      (const void *)(ULONG_PTR)0x2C3B2050ULL, 0x338, 3, &row);
        check("11. a cursor that does not fit the model is REFUSED, not rounded",
              below && misaligned && too_far && row == -999,
              "below=%d misaligned=%d too_far=%d row=%d",
              below, misaligned, too_far, row);
    }

    /* --- case 12: a settings ROW speaks its axis and is NEVER named -------
     * ⭐ base+index cannot name a settings row, so the row must report
     * MC_AXIS_ROW with named=FALSE and the reason NO_LABELS. Demanding the
     * REASON (not merely !named) is what keeps this from passing for the wrong
     * cause -- the lesson case 3 already paid for. */
    m = mk(0x1400b1b69ULL, 1, 0, 2);
    menu_speak_build(&m, &sp);
    /* ⚠ There is no live GMD table in this process, so the MAP is consulted
     * and the text lookup then fails -- TEXT_UNREADABLE, not NO_LABELS. That
     * distinction is the point: it proves the map was reached. */
    check("12. a mapped ROW consults the map and reports position",
          !sp.named && sp.axis == MC_AXIS_ROW &&
          sp.why == MC_UNNAMED_TEXT_UNREADABLE &&
          sp.position == 2 && sp.total == 3,
          "named=%d axis=%d why=%d %d of %d", (int)sp.named, (int)sp.axis,
          (int)sp.why, sp.position, sp.total);

    /* --- case 13: a settings VALUE carries the row it belongs to ---------- */
    m = mk(0x1400b1bd9ULL, 2, 0, 7);
    m.screen = (void *)(ULONG_PTR)0x2C3B1D00ULL;
    m.cursor = (int *)(ULONG_PTR)0x2C3B2038ULL;   /* row 0 */
    menu_speak_build(&m, &sp);
    check("13. a settings VALUE reports its axis and derives its row",
          !sp.named && sp.axis == MC_AXIS_VALUE && sp.have_row && sp.row == 0 &&
          sp.position == 3 && sp.total == 8,
          "axis=%d have_row=%d row=%d why=%d %d of %d", (int)sp.axis,
          (int)sp.have_row, sp.row, (int)sp.why, sp.position, sp.total);

    /* --- case 14: an underivable row still speaks POSITION ----------------
     * ⭐⭐ The safety property. When the row cannot be derived the mod must
     * still say something TRUE ("value 3 of 8"), not fall silent and not invent
     * a row. `have_row` FALSE is what the speech path keys on. */
    m = mk(0x1400b1bd9ULL, 2, 0, 7);
    m.screen = (void *)(ULONG_PTR)0x2C3B1D00ULL;
    m.cursor = (int *)(ULONG_PTR)0x2C3B2039ULL;   /* misaligned on purpose */
    menu_speak_build(&m, &sp);
    check("14. an underivable row still yields a true position, and says why",
          !sp.named && !sp.have_row && sp.axis == MC_AXIS_VALUE &&
          sp.why == MC_UNNAMED_ROW_UNDERIVABLE &&
          sp.position == 3 && sp.total == 8,
          "have_row=%d why=%d %d of %d", (int)sp.have_row, (int)sp.why,
          sp.position, sp.total);

    /* --- case 15: the varying-count site is NOT rejected by staleness -----
     * ⭐ The same call site legitimately drives an 8-value row and two 2-value
     * rows, so its table row carries count 0 ("do not check"). This pins that:
     * all three counts must be accepted. A regression to a fixed count would
     * silently reject two rows out of three. */
    {
        int accepted = 1;
        const int counts[3] = { 8, 2, 2 };
        for (int i = 0; i < 3; i++) {
            mc_move v = mk(0x1400b1bd9ULL, 0, 0, counts[i] - 1);
            v.screen = (void *)(ULONG_PTR)0x2C3B1D00ULL;
            v.cursor = (int *)(ULONG_PTR)(0x2C3B2038ULL + 4u * (unsigned)i);
            mc_speech vs;
            menu_speak_build(&v, &vs);
            if (vs.why == MC_UNNAMED_COUNT_MISMATCH || vs.total != counts[i])
                accepted = 0;
        }
        check("15. a varying-count settings site is never rejected on count",
              accepted, "");
    }

    /* --- case 16: the Select Game map is the RIGHT map --------------------
     * ⭐⭐ THE CASE THAT WOULD HAVE CAUGHT THE SHIPPED BUG. The screen reached
     * from Offline Play was named "Game settings" on a guess and spoke
     * "setting 2 of 3". It is Select Game, and its three rows are GMD 51
     * (Game), 52 (Game Version) and 56 (Player).
     *
     * ⭐ 51, 52, 56 is NOT contiguous, and that is the whole reason a map
     * exists: base+index would land on 53 ("Japanese Version") for row 2 --
     * a VALUE label where a row name belongs. This pins the exact indices, so
     * a future edit that "tidies" them into a run fails here. */
    {
        const mc_screen *sg = menu_speak_lookup(0x1400b1b69ULL);
        int ok = sg && sg->labels && sg->nlabels == 3 &&
                 sg->labels[0] == 51 && sg->labels[1] == 52 &&
                 sg->labels[2] == 56 &&
                 sg->kind == MC_SETTINGS_ROW &&
                 sg->name && strcmp(sg->name, "Select Game") == 0;
        /* ⛔ And it must NOT carry a base index: a row with a map that also
         * had a base could silently fall back to the wrong labels. */
        int no_base = sg && sg->base_index == -1;
        check("16. Select Game maps rows 0,1,2 to GMD 51,52,56 and has no base",
              ok && no_base, "ok=%d no_base=%d", ok, no_base);
    }

    /* --- case 17: a mapped screen NEVER uses base+index -------------------
     * ⭐⭐ The safety property, stated directly. Even if some future edit gives
     * a mapped row a base_index, the map must win -- because on this screen
     * base+index names a value, confidently and wrongly. Checked structurally:
     * no shipped row may carry BOTH a map and a base. */
    {
        int both = 0;
        for (int i = 0; i < menu_speak_screen_count(); i++) {
            const mc_screen *r = menu_speak_row(i);
            if (r && r->labels && r->base_index >= 0)
                both = 1;
        }
        check("17. no row carries both a label map and a base index", !both, "");
    }

    /* --- case 18: a value names the ROW it belongs to ---------------------
     * ⭐ "Game Version, 2 of 2" is the useful readout; "setting 2, value 2 of
     * 2" is not. The value site has no map of its own, so it finds its
     * sibling row entry and uses that. */
    {
        const mc_screen *v = menu_speak_lookup(0x1400b1bd9ULL);
        const mc_screen *sib = menu_speak_sibling_row(v);
        check("18. the value site finds its sibling row entry by screen name",
              v && sib && sib->kind == MC_SETTINGS_ROW && sib->labels &&
              sib->nlabels == 3,
              "v=%p sib=%p", (const void *)v, (const void *)sib);
    }

    /* --- case 19: a map that disagrees with the live count is REFUSED -----
     * ⭐⭐ The staleness check, for maps. If the screen shows 4 rows and the
     * map holds 3, the game changed and the map is stale -- naming rows from
     * it would mis-label every row past the first. Position only. */
    m = mk(0x1400b1b69ULL, 1, 0, 3);          /* 4 rows live, map holds 3 */
    menu_speak_build(&m, &sp);
    check("19. a map that does not match the live row count is refused",
          !sp.named && sp.why == MC_UNNAMED_COUNT_MISMATCH &&
          sp.position == 2 && sp.total == 4,
          "named=%d why=%d %d of %d", (int)sp.named, (int)sp.why,
          sp.position, sp.total);

    /* --- case 20: value variants are keyed on the LIVE count -------------
     * ⭐⭐ The live value list is a SUBSET of the labels that exist: "Either"
     * is an online lobby filter, P3/P4 belong to the 4-player games. So a
     * variant is selected by the count actually on screen, and a count nobody
     * has measured selects NOTHING rather than the nearest list. */
    {
        const mc_screen *v = menu_speak_lookup(0x1400b1bd9ULL);
        const mc_value_set *ver2 = menu_speak_values_for(v, 1, 2);
        const mc_value_set *ply2 = menu_speak_values_for(v, 2, 2);
        const mc_value_set *ply4 = menu_speak_values_for(v, 2, 4);
        /* Row 0 is the games -- deliberately absent until the ROM index
         * mapping is confirmed (rom_table.h). The titles DO exist. */
        const mc_value_set *game = menu_speak_values_for(v, 0, 8);
        /* A count nobody has measured must not borrow another variant. */
        const mc_value_set *ver3 = menu_speak_values_for(v, 1, 3);
        const mc_value_set *ply7 = menu_speak_values_for(v, 2, 7);
        check("20. value variants match on live count; unmeasured counts refuse",
              ver2 && ver2->first == 53 && ply2 && ply2->first == 58 &&
              ply4 && ply4->first == 58 && ply4->count == 4 &&
              game == NULL && ver3 == NULL && ply7 == NULL,
              "ver2=%p ply2=%p ply4=%p game=%p ver3=%p ply7=%p",
              (const void *)ver2, (const void *)ply2, (const void *)ply4,
              (const void *)game, (const void *)ver3, (const void *)ply7);
    }

    /* --- case 20b: the three settings screens are wired correctly --------
     * ⭐⭐ Each is a ROW half and a VALUE half linked by an identical `name`
     * string (menu_speak_sibling_row). If that link breaks, the value site
     * still speaks a value but loses the row name -- "Windowed, 1 of 3"
     * instead of "Display, Windowed, 1 of 3" -- which is a silent degradation
     * a count check would never catch.
     *
     * ⚠ values_off is asserted per screen because all three DIFFER (0x338 /
     * 0x358 / 0x35c). Copying one screen's offset to another is the mistake
     * the header warns about, and it would derive the wrong ROW. */
    {
        const mc_screen *pcr = menu_speak_lookup_n(0x1400a1715ULL, 4);
        const mc_screen *pcv = menu_speak_lookup(0x1400a1757ULL);
        const mc_screen *sdr = menu_speak_lookup_n(0x1400a2729ULL, 6);
        const mc_screen *sdv = menu_speak_lookup(0x1400a2771ULL);
        const mc_screen *ntr = menu_speak_lookup_n(0x14009f48eULL, 5);
        const mc_screen *ntv = menu_speak_lookup(0x14009f4c1ULL);

        int ok = pcr && pcv && sdr && sdv && ntr && ntv;
        /* Both halves present, correctly typed, and linked by name. */
        ok = ok && pcr->kind == MC_SETTINGS_ROW && pcv->kind == MC_SETTINGS_VALUE
                && sdr->kind == MC_SETTINGS_ROW && sdv->kind == MC_SETTINGS_VALUE
                && ntr->kind == MC_SETTINGS_ROW && ntv->kind == MC_SETTINGS_VALUE;
        ok = ok && menu_speak_sibling_row(pcv) == pcr
                && menu_speak_sibling_row(sdv) == sdr
                && menu_speak_sibling_row(ntv) == ntr;
        /* Each screen's OWN offset. */
        ok = ok && pcv->values_off == 0x338
                && sdv->values_off == 0x358
                && ntv->values_off == 0x35c;
        check("20b. settings screens: both halves, linked by name, own offsets",
              ok, "pc=%p/%p snd=%p/%p net=%p/%p",
              (const void *)pcr, (const void *)pcv, (const void *)sdr,
              (const void *)sdv, (const void *)ntr, (const void *)ntv);
    }

    /* --- case 20c: the new value variants match on their MEASURED count ---
     * ⛔ And an UNMEASURED count must refuse, not borrow the nearest list.
     * The volume rows show 10-11 generated steps with no labels at all; if a
     * lookup ever returned a variant for those, it would read a neighbouring
     * setting's words as if they were volume levels. */
    {
        const mc_screen *ntv = menu_speak_lookup(0x14009f4c1ULL);
        const mc_screen *sdv = menu_speak_lookup(0x1400a2771ULL);
        const mc_value_set *mic  = menu_speak_values_for(ntv, 0, 2);
        const mc_value_set *conn = menu_speak_values_for(ntv, 3, 5);
        const mc_value_set *jing = menu_speak_values_for(sdv, 4, 2);
        /* Measured-but-unlabelled rows must name nothing. */
        const mc_value_set *chat = menu_speak_values_for(ntv, 1, 11);
        const mc_value_set *dly  = menu_speak_values_for(ntv, 2, 11);
        const mc_value_set *vol  = menu_speak_values_for(sdv, 0, 10);
        /* Default rows have no value slot at all. */
        const mc_value_set *dflt = menu_speak_values_for(sdv, 5, 2);
        check("20c. new value variants match measured counts; the rest refuse",
              mic && mic->first == 203 && conn && conn->first == 212 &&
              jing && jing->first == 195 &&
              chat == NULL && dly == NULL && vol == NULL && dflt == NULL,
              "mic=%p conn=%p jing=%p chat=%p dly=%p vol=%p dflt=%p",
              (const void *)mic, (const void *)conn, (const void *)jing,
              (const void *)chat, (const void *)dly, (const void *)vol,
              (const void *)dflt);
    }

    /* --- case 20d: System's FOUR value callers all link to one row map ----
     * ⭐⭐ New shape: this screen's values are driven by four separate callers
     * (one per row group) that share `values_off` and the screen object. Each
     * needs its own g_screens entry, and all four must carry the SAME name so
     * menu_speak_sibling_row finds the row map -- otherwise a value move names
     * the value but loses the row ("Agree, 2 of 2" instead of "Data
     * Collection, Agree, 2 of 2"), a silent degradation nothing else catches.
     *
     * ⚠ Also asserts the row map is reachable from every one of them, not just
     * the first: a name typo on the fourth would be invisible to a test that
     * only checked one. */
    {
        static const ULONGLONG VC[] = {
            0x1400a3431ULL, 0x1400a3538ULL, 0x1400a3578ULL, 0x1400a34a8ULL
        };
        const mc_screen *rowmap = menu_speak_lookup_n(0x1400a33b8ULL, 6);
        int ok = rowmap && rowmap->kind == MC_SETTINGS_ROW &&
                 rowmap->nlabels == 6 && rowmap->labels &&
                 rowmap->labels[0] == 180 && rowmap->labels[5] == 168;
        int linked = 0;
        for (size_t i = 0; i < sizeof VC / sizeof VC[0]; i++) {
            const mc_screen *v = menu_speak_lookup(VC[i]);
            if (v && v->kind == MC_SETTINGS_VALUE && v->values_off == 0x338 &&
                menu_speak_sibling_row(v) == rowmap)
                linked++;
        }
        check("20d. System: 4 value callers all link to the one row map",
              ok && linked == 4, "rowmap=%p linked=%d of 4",
              (const void *)rowmap, linked);
    }

    /* --- case 21: row 0's values are never named ------------------------
     * ⛔ Row 0 must have NO GMD value variant. The eight game titles have no
     * clean run in the GMD, so any base that "looks close" names something
     * else -- the confident-wrong-name failure.
     *
     * ⚠ This case does NOT say the titles are unavailable. They are in the
     * exe (src/rom_table.h) and readable; what is missing is confirmation that
     * cursor index N corresponds to ROM index 2N. When a run confirms that,
     * the fix is a ROM-backed name, NOT a GMD variant -- so this case stays
     * as it is even then.
     *
     * ⚠ Asserted STRUCTURALLY, not through menu_speak_build(). With no live
     * GMD table in this process the text read fails anyway, so a behavioural
     * assertion would pass even with a games variant added -- two layers, one
     * outcome, the trap this suite has hit before. Checking the TABLE is what
     * isolates it: no variant may exist for row 0. Verified by sabotage --
     * adding { 0, 8, 51 } fails this case. */
    {
        const mc_screen *v = menu_speak_lookup(0x1400b1bd9ULL);
        int any_row0 = 0;
        for (int i = 0; v && i < v->nvalues; i++)
            if (v->values[i].row == 0)
                any_row0 = 1;
        check("21. row 0 (the games) has NO value variant -- they are images",
              v && !any_row0, "any_row0=%d", any_row0);
    }
    printf("\n");
    if (failures) { printf("FAILED: %d case(s)\n", failures); return 1; }
    printf("all cases passed\n");
    return 0;
}
