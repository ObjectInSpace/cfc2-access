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

/* ⭐ The two modal prompts, verbatim from the corpus (SYS_EXIT_APP 384 and
 * SYS_BACK_TITLE 383), and a resolver standing in for gmd_text_by_index --
 * which has no table in this process.
 *
 * ⚠ The stub returns the REAL strings rather than re-deriving the match, so it
 * supplies DATA, not the mechanism under test. A stub that answered "yes, that
 * is a prompt" would leave the matcher untested. */
#define MODAL_EXIT_APP "If you exit the application, all unsaved\r\nprogress will be lost. Continue?"
#define MODAL_BACK_TITLE "If you return to the title screen,\r\nall unsaved progress will be lost.\r\nContinue?"

static BOOL test_modal_resolve(int idx, char *out, unsigned outlen)
{
    const char *s = (idx == 384) ? MODAL_EXIT_APP :
                    (idx == 383) ? MODAL_BACK_TITLE : NULL;
    if (!s || !out || outlen == 0) return FALSE;
    _snprintf(out, outlen, "%s", s);
    out[outlen - 1] = '\0';
    return TRUE;
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
        /* ⭐⭐ Provenance. A row justified by plausibility rather than by a
         * live-text discovery block shipped once and made a different screen
         * speak the main menu's labels. Only VERIFIED_LIVE_TEXT may ship.
         *
         * ⚠ An MC_USER_ACCEPTED_UNPROVEN escape hatch was added for the
         * consent dialog and removed the same day -- see menu_speak.h. The
         * rule is back to having exactly one acceptable answer. */
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
         * guessed row, arriving by a different door. That stays illegal.
         *
         * ⚠ A `heading_index` third key was added here and REMOVED: the heading
         * object is refreshed on a different schedule from the rows it was meant
         * to identify, so it named the wrong tab 2 dumps in 6. The rule is back
         * to exactly two keys, and the screen that needed a third is now served
         * by MC_ROW_COLUMN, which identifies no screen at all -- see case 32. */
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
            /* ⭐ THE CHARACTER PICKER's items (lobby-ID keypad). Single
             * characters must be ADMITTED -- they are the whole content of that
             * screen, and the same predicate gates them. ⚠ The hint strip that
             * polluted position 36 in the logs is already a NEGATIVE below, so
             * these two cases together are exactly the discrimination the
             * keypad needs. */
            "0", "9", "A", "Z",
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
        /* ⭐ The ONLINE 3-value Game Version (Japanese/English/Either). It was
         * a negative here until 2026-09-10: the row ran at a live count of 3
         * while only the 2-value variant existed, so the mod said "Game
         * Version, 2 of 3" and named nothing -- part of the "values missing on
         * the online screens" the user reported. It now resolves, and the
         * corpus check confirms 53-55 really is that run. */
        const mc_value_set *ver3 = menu_speak_values_for(v, 1, 3);
        /* ⚠ A count nobody has measured must STILL refuse -- this is the
         * discriminating half of the case and must not be lost. */
        const mc_value_set *ply7 = menu_speak_values_for(v, 2, 7);
        check("20. value variants match on live count; unmeasured counts refuse",
              ver2 && ver2->first == 53 && ply2 && ply2->first == 58 &&
              ply4 && ply4->first == 58 && ply4->count == 4 &&
              ver3 && ver3->first == 53 && ver3->count == 3 &&
              game == NULL && ply7 == NULL,
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

    /* --- cases 20l-20n: ⭐⭐ THE MODAL PROMPT (the quit dialogs) -------------
     *
     * These dialogs produce ZERO cursor movements, so every movement-driven
     * path in this file is blind to them; and their SELECTION is not observable
     * at all (measured and closed in run-2026-09-11-1322). So the mod announces
     * the QUESTION and claims nothing about which button is highlighted.
     *
     * ⚠ Only the decision logic is testable offline -- gmd_text_by_index() has
     * no table in this process -- so the matcher takes a resolver and the
     * flattener is exposed directly. The speech itself is out of scope here for
     * the same reason menu_speak() is: it calls into UniversalSpeech. */
    {
        /* 20l: the corpus strings carry "\r\n" as LAYOUT. Spoken verbatim that
         * is a pause or a literal character depending on the reader, and the
         * whitespace run it leaves reads as a stutter. */
        char out[256];
        menu_speak_flatten_prompt(
            "If you exit the application, all unsaved\r\n"
            "progress will be lost. Continue?", out, (int)sizeof out);
        int ok = strcmp(out,
            "If you exit the application, all unsaved progress will be lost. "
            "Continue?") == 0;

        char lead[64];
        menu_speak_flatten_prompt("\r\n  padded  \r\n", lead, (int)sizeof lead);
        ok = ok && strcmp(lead, "padded") == 0;

        char empty[16];
        menu_speak_flatten_prompt(NULL, empty, (int)sizeof empty);
        ok = ok && empty[0] == '\0';

        check("20l. a modal prompt's line breaks collapse to single spaces",
              ok, "got \"%s\" / \"%s\"", out, lead);
    }
    {
        /* 20m: the matcher accepts ONLY the known prompts. ⚠ Asserted in both
         * directions: a matcher that accepts everything would pass a
         * one-sided test and announce ordinary menu prose as a quit warning. */
        int ok = menu_speak_is_modal_prompt(MODAL_EXIT_APP, test_modal_resolve) &&
                 menu_speak_is_modal_prompt(MODAL_BACK_TITLE, test_modal_resolve) &&
                 !menu_speak_is_modal_prompt("Play offline.", test_modal_resolve) &&
                 !menu_speak_is_modal_prompt("Exit the application.",
                                             test_modal_resolve) &&
                 !menu_speak_is_modal_prompt("", test_modal_resolve) &&
                 !menu_speak_is_modal_prompt(NULL, test_modal_resolve);
        check("20m. only the known modal prompts match, in both directions",
              ok, "one or more classifications wrong");
    }
    {
        /* 20n: ⛔ with no resolver it must match NOTHING. In the game a missing
         * GMD table is a real state (it is built during startup), and silence
         * is correct there -- announcing an unresolved string would speak
         * whatever prose happened to be on screen. */
        int ok = !menu_speak_is_modal_prompt(MODAL_EXIT_APP, NULL);
        check("20n. no resolver means no match, not a fallback",
              ok, "matched with a NULL resolver");
    }

    /* --- cases 20i-20k: ⛔⛔ ONE CALLER, TWO SCREENS, ONE SHARED VALUE AXIS
     *
     * USER-REPORTED, and the user named the consequence exactly: "the values
     * won't be correct if the rows are wrong, and it's confusing to announce
     * the wrong thing."
     *
     * 0x14009890d serves Casual Match (4 rows) and Ranked Match (3) -- measured
     * by entry point over 17 visits, zero exceptions. They SHARE the value axis
     * 0x140098986, which can carry only one `name`. So a sibling search keyed on
     * the name returns the CASUAL row map even when the user is on RANKED, and
     * names row 3 "One-button Special Moves" when the row is actually
     * Cross-region Matchmaking: the wrong SETTING announced while it is changed.
     *
     * ⚠⚠ NOTE WHY A `matches > 1` GUARD WOULD NOT HAVE FIXED IT. The value
     * entry's name matches the Casual row entry EXACTLY ONCE -- it looks
     * unambiguous. The ambiguity is between the value axis and the row entries,
     * not among the row entries. These cases pin the real rule: the live ROW
     * COUNT decides. */
    {
        const mc_screen *casual = menu_speak_lookup_n(0x14009890dULL, 4);
        const mc_screen *ranked = menu_speak_lookup_n(0x14009890dULL, 3);
        const mc_screen *val    = menu_speak_lookup(0x140098986ULL);
        int ok = casual && ranked && val &&
                 casual != ranked &&
                 casual->nlabels == 4 && ranked->nlabels == 3;
        check("20i. one caller resolves to TWO screens by live row count",
              ok, "casual=%p(%d) ranked=%p(%d) val=%p",
              (const void *)casual, casual ? casual->nlabels : -1,
              (const void *)ranked, ranked ? ranked->nlabels : -1,
              (const void *)val);
    }
    {
        /* 20g: the shared value axis must reach the map for the screen the user
         * is actually ON, chosen by the row count seen on that screen object. */
        const mc_screen *casual = menu_speak_lookup_n(0x14009890dULL, 4);
        const mc_screen *ranked = menu_speak_lookup_n(0x14009890dULL, 3);
        const mc_screen *val    = menu_speak_lookup(0x140098986ULL);
        int scr_a = 0, scr_b = 0;      /* stand-ins for two screen objects */

        menu_speak_reset_row_total();
        menu_speak_note_row_total(&scr_a, 4);
        const mc_screen *got_casual = menu_speak_sibling_row_for(val, &scr_a);

        menu_speak_reset_row_total();
        menu_speak_note_row_total(&scr_b, 3);
        const mc_screen *got_ranked = menu_speak_sibling_row_for(val, &scr_b);

        check("20j. the shared value axis follows the LIVE row count",
              got_casual == casual && got_ranked == ranked,
              "4rows->%s  3rows->%s",
              got_casual == casual ? "Casual(ok)" :
                  (got_casual == ranked ? "RANKED(wrong)" : "none"),
              got_ranked == ranked ? "Ranked(ok)" :
                  (got_ranked == casual ? "CASUAL(wrong)" : "none"));
    }
    {
        /* 20h: ⛔ a count that matches NO map must yield NULL, not the first
         * entry. Naming a row from a map that does not describe the screen is
         * the failure mode; a bare position is always true. */
        const mc_screen *val = menu_speak_lookup(0x140098986ULL);
        int scr = 0;
        menu_speak_reset_row_total();
        menu_speak_note_row_total(&scr, 9);          /* neither 4 nor 3 */
        const mc_screen *got = menu_speak_sibling_row_for(val, &scr);
        menu_speak_reset_row_total();
        check("20k. an unmatched row count names NOTHING rather than guessing",
              got == NULL, "got=%p", (const void *)got);
    }

    /* --- case 20e: a SCREEN caption is not repeated on every row ---------
     * ⛔⛔ Fighter Awards sets ONE caption -- "View all of your challenges." --
     * for all 40 grid positions and all 7 list rows. Once ACHIEVEMENT_*_NAME
     * entered the prompt table so achievement titles could be spoken, that
     * static sentence started resolving too, and would have been appended to
     * every movement.
     *
     * ⚠ Driven as a SEQUENCE because the suppressor keeps state. The first
     * sighting on a screen must SPEAK (it is new information on arrival); an
     * identical repeat must be suppressed; a screen change must re-arm; and a
     * screen with genuinely per-row captions must be unaffected. */
    {
        const char *AW = "View all of your challenges.";
        int ok = 1;
        /* Arrive on Fighter Awards: first sighting speaks. */
        ok &= (menu_speak_test_caption_repeat(AW, "Fighter Awards") == FALSE);
        /* Every later row on the SAME screen repeats it -- suppress. */
        for (int i = 0; i < 5; i++)
            ok &= (menu_speak_test_caption_repeat(AW, "Fighter Awards") == TRUE);
        /* A different screen re-arms, even with the same words. */
        ok &= (menu_speak_test_caption_repeat(AW, "Museum") == FALSE);
        /* ⭐ A screen with REAL per-row captions is untouched: consecutive
         * rows differ, so none is suppressed. */
        ok &= (menu_speak_test_caption_repeat("Play offline.", "Main menu")
               == FALSE);
        ok &= (menu_speak_test_caption_repeat("Play online using a network "
                                              "connection.", "Main menu")
               == FALSE);
        ok &= (menu_speak_test_caption_repeat("Browse various art galleries.",
                                              "Main menu") == FALSE);
        /* ...but going back to a row you just left still repeats -> suppress,
         * which is correct: the user just heard it. */
        ok &= (menu_speak_test_caption_repeat("Browse various art galleries.",
                                              "Main menu") == TRUE);
        /* Empty is never spoken. */
        ok &= (menu_speak_test_caption_repeat("", "Main menu") == TRUE);
        ok &= (menu_speak_test_caption_repeat(NULL, "Main menu") == TRUE);
        check("20e. a static SCREEN caption is not repeated on every row",
              ok, "");
    }

    /* --- case 20f: the ROM-table branch is SELECT GAME's alone -----------
     * ⛔⛔ THE BUG THIS EXISTS FOR, reported by the user: changing screen
     * resolution said "Power Stone, 5 of 21". A branch reading "row 0 of a
     * settings screen is the game list" was true when Select Game was the ONLY
     * such screen, and became false when PC Settings, Sound, Network and System
     * joined it -- their row 0 is Resolution / Music Volume / Microphone /
     * Language, and all four read out GAME TITLES.
     *
     * ⭐ The condition described a POSITION when it meant an IDENTITY. Nothing
     * failed when it was written; it failed when the world grew around it, and
     * NO TEST NOTICED -- the suite was green while seven screens were wrong.
     *
     * ⚠ Asserted structurally, because the offline harness has no ROM table and
     * no live GMD: exactly ONE MC_SETTINGS_VALUE row may be the game list, and
     * it must be Select Game's. If another value screen is ever added, this
     * fails unless its row 0 is deliberately considered. */
    {
        int value_sites = 0;
        int game_list_sites = 0;
        for (int i = 0; i < nrows; i++) {
            const mc_screen *r = menu_speak_row(i);
            if (!r || r->kind != MC_SETTINGS_VALUE)
                continue;
            value_sites++;
            if (r->caller == 0x1400b1bd9ULL)
                game_list_sites++;
        }
        /* Several value sites exist; only one is the game list. If that count
         * is ever not 1, the branch has either lost its screen or gained one. */
        check("20f. exactly one value site is the ROM game list",
              value_sites >= 5 && game_list_sites == 1,
              "value sites=%d game-list sites=%d", value_sites,
              game_list_sites);
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

    /* --- case 22: the GAME-STRIP flag is set for Gallery, and ONLY there ---
     * ⭐⭐ sp.game_strip is the gate on a rule that speaks an unverified-by-
     * corpus caption as an ITEM NAME. Being in the ROM table is not by itself
     * permission to speak -- the screen must be declared a game strip too.
     *
     * ⚠ Both directions asserted. A flag that was always TRUE would let the
     * rule fire on the main menu, where a caption like "Play offline." is a
     * DESCRIPTION and must never become the item's name. That failure would be
     * invisible to a positive-only test.
     *
     * ⚠ Gallery's count is 0 (position 1 is the collection, not a ROM), so the
     * movement below deliberately uses a 9-item range to mirror the real
     * screen -- and case 3's staleness check must not reject it. */
    {
        mc_speech g;
        mc_move gm_ = mk(0x14009a482ULL, 1, 0, 8);
        menu_speak_build(&gm_, &g);

        mc_speech mn;
        mc_move mm = mk(MAIN_CALLER, 1, 0, 5);
        menu_speak_build(&mm, &mn);

        /* And a screen that IS named must not be flagged either. */
        mc_speech lb;
        mc_move lm = mk(0x140083e09ULL, 1, 0, 2);
        menu_speak_build(&lm, &lb);

        check("22. game_strip is set for Gallery and for nothing else",
              g.game_strip && g.position == 2 && g.total == 9 &&
              !mn.game_strip && !lb.game_strip,
              "gallery=%d (pos %d of %d), main=%d, lobby=%d",
              (int)g.game_strip, g.position, g.total,
              (int)mn.game_strip, (int)lb.game_strip);
    }

    /* --- case 23: an UNKNOWN row in a mapped screen speaks POSITION ---------
     * ⚠⚠ THIS FIXTURE ROTTED ONCE AND THAT IS THE LESSON. It originally used
     * Lobby Filters' row 2 as its "unknown row". The moment that row was named
     * from the renderer's table, the case FAILED -- a test quietly depending on
     * a screen staying unsolved. It now uses MC_TEST_CALLER, a g_screens row
     * that exists only to hold a gap, so the mechanism outlives the screens it
     * was built for.
     *
     * ⭐⭐ Join Lobby's filters have 5 of 6 rows established. Without
     * MC_LABEL_UNKNOWN a map must be all-or-nothing, so one unproven row would
     * force all six to speak position -- discarding evidence that exists to
     * avoid claiming evidence that does not.
     *
     * ⚠ BOTH halves asserted. The unknown row must NOT be named (the failure
     * that matters: naming it would speak a neighbour's label or garbage), and
     * its neighbours MUST still be named (otherwise the sentinel has silently
     * disabled the whole map and the test would pass on a broken screen). */
    {
        mc_speech u, k;
        mc_move um = mk(MC_TEST_CALLER, 1, 0, 2);   /* position 2 -- unknown */
        menu_speak_build(&um, &u);
        mc_move km = mk(MC_TEST_CALLER, 2, 0, 2);   /* position 3 -- known   */
        menu_speak_build(&km, &k);

        /* ⚠ The GMD is not live in this process, so a KNOWN row cannot resolve
         * its text here -- it reports TEXT_UNREADABLE. That is a different
         * reason from NO_LABELS, and the distinction is the whole assertion:
         * the unknown row declines because it has no label, the known row
         * because there is no live table to read. */
        check("23. an unknown mapped row declines for NO_LABELS, not by "
              "naming, while a known row reaches the text lookup",
              !u.named && u.why == MC_UNNAMED_NO_LABELS &&
              u.position == 2 && u.total == 3 &&
              !k.named && k.why == MC_UNNAMED_TEXT_UNREADABLE,
              "unknown: named=%d why=%d pos=%d; known: named=%d why=%d",
              (int)u.named, (int)u.why, u.position, (int)k.named, (int)k.why);
    }

    /* --- case 24: the TWO 8-item game strips are kept apart ----------------
     * ⛔⛔ Ranked Leaderboard (0x1400ac059) and Casual Match (0x140098986) are
     * both 8-item horizontal game strips and look identical in a log. The
     * Leaderboard CAPTIONS one title per movement; Casual Match captions NOT AT
     * ALL (0 in 20 movements) and its titles arrive only on the transcript
     * channel.
     *
     * ⚠ So exactly one of them may carry game_strip. If Casual Match ever gains
     * it, the rule would fire on a screen with no per-movement caption and
     * speak whatever stale title was last set -- and the one caption that does
     * exist near that strip belongs to a DIFFERENT axis and contradicts the
     * insertion order. This case is what stops that being added by symmetry. */
    {
        mc_speech lb, cm;
        mc_move lm = mk(0x1400ac059ULL, 2, 0, 7);
        menu_speak_build(&lm, &lb);
        mc_move cmv = mk(0x140098986ULL, 2, 0, 7);
        menu_speak_build(&cmv, &cm);

        check("24. the Leaderboard strip is a game strip; Casual Match's is NOT",
              lb.game_strip && lb.total == 8 &&
              !cm.game_strip && !cm.named,
              "leaderboard=%d (total %d), casual=%d named=%d",
              (int)lb.game_strip, lb.total, (int)cm.game_strip, (int)cm.named);
    }

    /* --- case 25: the ROM game-list path is keyed to a CALLER LIST ---------
     * ⭐⭐ Row 0 means "the game list" on Select Game and on Casual Match's
     * value axis, and something entirely different elsewhere -- Sound Settings'
     * row 0 is Music Volume, System's is Language.
     *
     * ⚠ So the guard must stay an explicit caller list. Widening it to "any
     * MC_SETTINGS_VALUE row 0" would make a volume slider announce a game
     * title. This case fails if anyone does that, which is the only reason it
     * exists -- the shipping condition is one line and easy to "simplify".
     *
     * ⚠ Asserted through menu_speak_lookup, not by re-deriving the condition:
     * the harness must not restate the mechanism under test. */
    {
        const mc_screen *sg = menu_speak_lookup(0x1400b1bd9ULL);
        const mc_screen *cm = menu_speak_lookup_n(0x140098986ULL, 8);
        const mc_screen *sv = menu_speak_lookup_n(0x1400a2771ULL, 10);

        /* The two game-list value sites exist and are VALUE sites; the sound
         * value site exists too and must NOT be one of them. */
        int ok = sg && cm && sg->kind == MC_SETTINGS_VALUE &&
                 cm->kind == MC_SETTINGS_VALUE &&
                 sg->caller != cm->caller &&
                 (!sv || (sv->caller != 0x1400b1bd9ULL &&
                          sv->caller != 0x140098986ULL));
        char d[200];
        snprintf(d, sizeof d,
                 "select-game=%d casual=%d distinct=%d sound-not-a-game-list=%d",
                 sg != NULL, cm != NULL,
                 (sg && cm) ? (sg->caller != cm->caller) : 0,
                 (!sv || sv->caller != 0x1400b1bd9ULL));
        check("25. the ROM game-list path is keyed to specific callers", ok, d);
    }

    /* --- case 26: char_picker is set for the keypad and nothing else -------
     * ⚠ Same reasoning as case 22 for game_strip: this flag gates a rule that
     * speaks an unverified-by-corpus caption as the ITEM. On any other screen a
     * short caption is a VALUE or a leftover, and quoting it as the item name
     * would be the substitution this file exists to prevent.
     *
     * ⚠ The count is asserted too: 36 is 10 digits + 26 letters, a real
     * invariant. A different live count means this is not that picker, and the
     * staleness check must reject it. */
    {
        mc_speech kp, other;
        mc_move km = mk(0x14008a8efULL, 0, 0, 35);
        menu_speak_build(&km, &kp);
        mc_move om = mk(MAIN_CALLER, 0, 0, 5);
        menu_speak_build(&om, &other);

        /* And with the WRONG count, the entry must not apply. */
        mc_speech stale;
        mc_move sm = mk(0x14008a8efULL, 0, 0, 9);   /* 10 items, not 36 */
        menu_speak_build(&sm, &stale);

        check("26. char_picker is set only for the keypad, and only at 36",
              kp.char_picker && kp.total == 36 &&
              !other.char_picker && !stale.char_picker,
              "keypad=%d(total %d) main=%d wrong-count=%d",
              (int)kp.char_picker, kp.total, (int)other.char_picker,
              (int)stale.char_picker);
    }

    /* --- case 27: the picker's character must MATCH its position -----------
     * ⛔⛔ FROM THE 21:17 RUN. The keypad wrapped to position 36 ("Z") and the
     * mod spoke "0" -- the six-slot ID display's value, set 100 ms earlier by
     * the OTHER axis on the SAME screen object. Across all runs: 36 movements
     * where the nearby character is right, 2 where it is stale. Speaking the
     * wrong character ~5% of the time is exactly the failure this file exists
     * to prevent, and is_value_shaped() alone cannot catch it -- "0" is a
     * perfectly value-shaped string.
     *
     * ⭐ So the quote is checked against the position. Two independent sources
     * -- the cursor's index and the text the game drew -- must agree, the same
     * standard the music list's numbering meets. */
    {
        int ok = 1;
        /* The real mapping, spot-checked at both ends and the digit/letter seam */
        ok &= menu_speak_test_char_picker("0", 1);
        ok &= menu_speak_test_char_picker("9", 10);
        ok &= menu_speak_test_char_picker("A", 11);
        ok &= menu_speak_test_char_picker("Z", 36);
        /* ⛔ THE MEASURED BUG: "0" at position 36 must be REFUSED. */
        ok &= !menu_speak_test_char_picker("0", 36);
        ok &= !menu_speak_test_char_picker("Z", 1);
        /* Multi-character and degenerate input */
        ok &= !menu_speak_test_char_picker("AB", 11);
        ok &= !menu_speak_test_char_picker("", 1);
        ok &= !menu_speak_test_char_picker(NULL, 1);
        /* Out-of-range positions */
        ok &= !menu_speak_test_char_picker("0", 0);
        ok &= !menu_speak_test_char_picker("Z", 37);
        check("27. the picker speaks a character ONLY if it matches the position",
              ok, "one or more mappings misclassified");
    }

    /* --- cases 27b-27d: ⛔⛔ THE PICKER MUST SPEAK WITHOUT A CAPTION --------
     *
     * USER-REPORTED: "when I press up and down to choose a value I don't hear
     * anything." Measured in run-...-1222: of 101 up/down movements, only TWO
     * spoke a character; 99 gave a bare "19 of 36", which is useless when you
     * are spelling out a code.
     *
     * THE CAUSE: the picker's speech lived in the CAPTION block, so it needed a
     * caption to corroborate the position -- and this screen SETS NO TEXT AT
     * ALL. Its 36 characters have no text objects (the layout probe finds only
     * the six lobby slots at y=556), so they are sprites. The path was gated on
     * a signal that does not exist for it.
     *
     * ⚠ THESE CASES ASSERT SPEECH WITH NO CAPTION SUPPLIED, which is the whole
     * point -- menu_speak_build() must name the item from POSITION alone. A
     * case that passed a caption in would have passed before the fix. */
    {
        /* 27b: every one of the 36 positions names its own character. */
        int wrong = 0;
        char first_bad[64] = { 0 };
        static const char *const ALPHA =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        for (int p = 1; p <= 36; p++) {
            mc_speech sp;
            mc_move m = mk(0x14008a8efULL, p - 1, 0, 35);
            menu_speak_build(&m, &sp);
            if (!sp.named || sp.item[0] != ALPHA[p - 1] || sp.item[1] != '\0') {
                if (!wrong)
                    snprintf(first_bad, sizeof first_bad,
                             "pos %d named=%d item=\"%.20s\" want '%c'",
                             p, (int)sp.named, sp.item, ALPHA[p - 1]);
                wrong++;
            }
        }
        check("27b. all 36 picker positions name their character, NO caption",
              wrong == 0, "%d wrong; first: %s", wrong, first_bad);
    }
    {
        /* 27c: ⛔ a screen with a different live count is NOT this picker and
         * must get nothing -- the count is the membership test. */
        mc_speech sp;
        mc_move m = mk(0x14008a8efULL, 0, 0, 9);     /* 10 items, not 36 */
        menu_speak_build(&m, &sp);
        check("27c. a picker-caller screen with the wrong count names nothing",
              !sp.named && sp.item[0] == '\0',
              "named=%d item=\"%s\"", (int)sp.named, sp.item);
    }
    {
        /* 27d: ⚠ THE TWO ALPHABETS MUST BE THE SAME ONE. build() speaks from
         * K_PICKER_STR and char_picker_matches() guards with K_PICKER; if those
         * ever drift, the picker says one character while its own guard checks
         * another. Asserted by requiring the spoken character to satisfy the
         * guard at every position. */
        int bad = 0;
        for (int p = 1; p <= 36; p++) {
            mc_speech sp;
            mc_move m = mk(0x14008a8efULL, p - 1, 0, 35);
            menu_speak_build(&m, &sp);
            if (!sp.named || !menu_speak_test_char_picker(sp.item, p))
                bad++;
        }
        check("27d. the spoken character satisfies the picker's OWN guard",
              bad == 0, "%d position(s) disagree between the two alphabets", bad);
    }

    /* --- case 28: the ROM probe is caller-gated, like the speaking path -----
     * ⛔⛔ MEASURED: it was firing on EIGHTEEN callers across the logs -- 16
     * times on PC Settings -- logging "IF the mapping holds, this is Capcom
     * Fighting Jam" while the user moved through display languages. Row 0 is
     * the game list on exactly TWO screens.
     *
     * ⚠ Nothing was spoken, so it never reached the ear; it polluted the logs
     * every offline audit here reads. A diagnostic that cries wolf on 16
     * screens is worse than no diagnostic.
     *
     * ⚠ Asserted through the SCREEN TABLE rather than by calling the probe:
     * menu_speak_probe_rest() needs a live GMD and a real screen pointer, so a
     * direct call would test nothing. The property that matters is that the two
     * game-list value sites are distinguishable from the other value sites, and
     * that is checkable here. */
    {
        const mc_screen *sg = menu_speak_lookup(0x1400b1bd9ULL);
        const mc_screen *cm = menu_speak_lookup_n(0x140098986ULL, 8);
        /* Three value sites that must NOT be treated as game lists. */
        const mc_screen *lang = menu_speak_lookup_n(0x1400a3431ULL, 14);
        const mc_screen *pc   = menu_speak_lookup_n(0x1400a1757ULL, 3);
        const mc_screen *net  = menu_speak_lookup_n(0x14009f4c1ULL, 2);

        int ok = sg && cm &&
                 sg->caller == 0x1400b1bd9ULL && cm->caller == 0x140098986ULL;
        /* None of the others may share those callers. */
        const mc_screen *others[3]; others[0]=lang; others[1]=pc; others[2]=net;
        for (int i = 0; i < 3; i++)
            if (others[i] && (others[i]->caller == 0x1400b1bd9ULL ||
                              others[i]->caller == 0x140098986ULL))
                ok = 0;
        check("28. only the two game-list callers can be ROM-probed", ok,
              "select=%d casual=%d", sg != NULL, cm != NULL);
    }

    /* --- case 29: the lobby game list is NINE entries, "Any" then 8 ROMs ----
     * ⭐⭐ The renderer's table gives row 0 first = 14 = GAME_NAME_ANY, a family
     * with exactly ONE member, because "Any" is the only game-list entry that
     * is not a ROM. So position 1 is a FILTER and the games start at 2.
     *
     * ⛔⛔ AN OFF-BY-ONE HERE NAMES EVERY GAME WRONGLY -- position 2 would say
     * "Capcom vs. SNK 2" where the screen shows "Capcom vs. SNK". That is the
     * confidently-wrong-value failure the user reported, so the shape is
     * asserted rather than left to the comment.
     *
     * ⚠ Asserted on the SCREEN TABLE: the two 9-entry callers must be distinct
     * from the two 8-entry ones, since the code branches on exactly that. */
    {
        const mc_screen *nine_a = menu_speak_lookup(0x140082d16ULL);
        const mc_screen *nine_b = menu_speak_lookup(0x14008e813ULL);
        const mc_screen *eight  = menu_speak_lookup_n(0x140098986ULL, 8);
        int ok = nine_a && nine_b && eight &&
                 nine_a->kind == MC_SETTINGS_VALUE &&
                 nine_b->kind == MC_SETTINGS_VALUE &&
                 nine_a->caller != eight->caller &&
                 nine_b->caller != eight->caller;
        check("29. the two 9-entry game lists are distinct from the 8-entry one",
              ok, "a=%d b=%d eight=%d", nine_a != NULL, nine_b != NULL,
              eight != NULL);
    }

    /* --- case 30: a settings row with NO value table names no value ---------
     * ⛔⛔ USER-REPORTED 2026-09-11: "the values in the options submenus are not
     * correct". Measured in the logs, the mod was appending ANOTHER ROW'S
     * value:
     *     "Coin Button, 1 of 6, English [en]"
     *     "Menu Button, 1 of 7, English [en]"
     *     "Announcer Volume, 1 of 10, Remix"
     *     "Game, 3 of 9, First to 4"
     *
     * The live-quote path claimed a guard of "a caption stamped with THIS
     * movement", which is NOT the same as "belongs to this row" -- several rows
     * write text during one movement and the channel returns whichever came
     * last.
     *
     * ⚠ Asserted on the DATA, not by staging a caption (the harness has no live
     * caption channel): System's button rows 1-3 must have NO value variant, so
     * there is nothing for a quote to be checked against and the path must stay
     * silent. If someone adds a variant for them later, this case fails and
     * they must re-establish the labels first -- the buttons are controller
     * GLYPHS with no GMD run, which is why no variant exists. */
    {
        const mc_screen *v = menu_speak_lookup(0x1400a3431ULL);
        int any = 0;
        for (int i = 0; v && i < v->nvalues; i++)
            if (v->values[i].row >= 1 && v->values[i].row <= 3)
                any = 1;
        /* Row 0 (Language) and row 4 (Data Collection) DO have variants -- the
         * positive control, so this cannot pass on an empty table. */
        const mc_value_set *lang = menu_speak_values_for(v, 0, 14);
        check("30. System's button rows have no value variant to mis-speak",
              v && !any && lang && lang->count == 14,
              "buttons=%d lang=%d", any, lang != NULL);
    }

    /* --- case 31: LEVEL rows are marked per-row, not per-screen -------------
     * ⭐⭐ User, 2026-09-11: "they might be textless but even an internal value
     * is fine". A volume slider has no label and draws no string, but the mod
     * is HANDED the index and the count -- and for a slider the index IS the
     * setting. "Music Volume, 7, of 10" says the level; "7 of 10" reads as a
     * list position and buries it.
     *
     * ⛔⛔ THE MASK MUST BE PER-ROW. These screens MIX the two kinds: Sound
     * rows 0-3 are volume levels and row 4 is Original/Remix. A per-screen flag
     * would say "Challenger Jingle, 2, of 2" -- stating a level where a NAME
     * belongs, which is the exact failure class the user reported. This case
     * fails if anyone widens the mask to cover the whole screen.
     *
     * ⚠ Counts are the MEASURED ones (cursor-address derived): Sound volumes
     * are 10 values, Network's are 11. */
    {
        const mc_screen *snd = menu_speak_lookup(0x1400a2771ULL);
        const mc_screen *net = menu_speak_lookup(0x14009f4c1ULL);
        const mc_screen *pc  = menu_speak_lookup(0x1400a1757ULL);
        int ok = snd && net && pc;
        if (ok) {
            /* Sound: rows 0-3 are levels, row 4 (Challenger Jingle) is NOT. */
            ok &= (snd->levels & 0x0Fu) == 0x0Fu;
            ok &= (snd->levels & (1u << 4)) == 0;
            /* Network: rows 1-2 are levels, row 0 (Microphone) is NOT. */
            ok &= (net->levels & (1u << 1)) != 0;
            ok &= (net->levels & (1u << 2)) != 0;
            ok &= (net->levels & (1u << 0)) == 0;
            /* PC Settings has NO level rows -- Display and VSync are names. */
            ok &= pc->levels == 0;
        }
        check("31. level rows are marked per-row, and named rows are not",
              ok, "sound=%#x net=%#x pc=%#x",
              snd ? snd->levels : 0, net ? net->levels : 0,
              pc ? pc->levels : 0);
    }

    /* --- case 32: ⛔⛔ THE TABS ARE NAMED WITHOUT IDENTIFYING THE TAB, and this
     * case exists because the first attempt did the opposite.
     *
     * FOUR tabs share caller 0x1400a41ef and ONE screen object: 6 rows, then
     * three of 5. Two 5-row tabs have identical value shapes (3,3,3,2), so
     * neither the row count nor the value counts separate them -- and the
     * HEADING cannot either: it is refreshed on a different schedule from the
     * rows and leads them by one tab (2 of 6 dumps had a heading naming a
     * different tab than its own rows; one had reverted to "Training Options").
     * A heading_index key was built, shipped, and removed.
     *
     * ⭐ MC_ROW_COLUMN reads the label the game DREW, scoped to the row column,
     * so no tab identity is needed at all. */
    {
        const mc_screen *five = menu_speak_lookup_n(0x1400a41efULL, 5);
        const mc_screen *six  = menu_speak_lookup_n(0x1400a41efULL, 6);
        int ok = five && six;

        if (ok) {
            ok &= five->kind == MC_ROW_COLUMN;
            ok &= five->coord_x > 0.0f;
            /* The 6-row tab keeps its verified label MAP -- it is unambiguous. */
            ok &= six->kind == MC_SETTINGS_ROW && six->labels &&
                  six->nlabels == 6;
        }
        check("32. the 5-row training tabs read their own column, not a map",
              ok, "5-row kind=%d coord_x=%.0f, 6-row nlabels=%d",
              five ? (int)five->kind : -1,
              five ? (double)five->coord_x : 0.0,
              six ? six->nlabels : -1);
    }

    /* --- case 33: no MC_ROW_COLUMN entry may ship without a column and count.
     * ⚠ A zero coord_x makes the path refuse SILENTLY on a screen the table
     * claims to name -- worse than no entry, because the log then says the
     * screen is known. */
    {
        int ok = 1, seen = 0;
        for (int i = 0; i < menu_speak_screen_count(); i++) {
            const mc_screen *s = menu_speak_screen_at(i);
            if (!s || s->kind != MC_ROW_COLUMN)
                continue;
            seen++;
            if (!(s->coord_x > 0.0f) || s->count <= 0)
                ok = 0;
        }
        ok &= seen > 0;
        check("33. every MC_ROW_COLUMN entry has a column and a count",
              ok, "entries=%d", seen);
    }

    /* --- case 34: ⭐⭐ THE TRAINING VALUE SLOTS ARE ABSOLUTE ACROSS TABS.
     *
     * USER: "The rows read, but not the values." The value entry existed but its
     * sets covered only the FIRST tab, so every other tab spoke "row 11, 2 of 3".
     *
     * ⭐ Each tab owns a block of EIGHT slots, so slot = tab_base + row -- and
     * this is MEASURED, not assumed: in the 17:02 run the row axis on "Guard
     * Crush, 3 of 5" produced slot 10 and "Groove Gauge, 2 of 5" produced slot
     * 9, i.e. slot - (row-1) == 8 on all six pairings.
     *
     * This asserts the slots that must exist and the ones that deliberately must
     * NOT -- a missing entry is the correct behaviour for a row whose values
     * cannot be expressed as a contiguous run. */
    {
        const mc_screen *v = menu_speak_lookup(0x1400a4241ULL);
        int have8 = 0, have10 = 0, have11 = 0, have24 = 0, have26 = 0;
        int have27 = 0, ok = v != NULL;

        if (ok) {
            for (int i = 0; i < v->nvalues; i++) {
                int r = v->values[i].row;
                if (r == 8)  have8  = 1;
                if (r == 10) have10 = 1;
                if (r == 11) have11 = 1;
                if (r == 24) have24 = 1;
                if (r == 26) have26 = 1;
                if (r == 27) have27 = 1;
            }
            ok &= have10 && have11 && have24 && have26;
            /* ⛔ Slot 8 (Health Bar) must have NO entry: VITAL_GAUGE_00..02 are
             * 715-717 but _03 is at 4136, so a {first,count} run cannot express
             * it and {8,4,715} would name the 4th value "Super Combo Gauge". */
            ok &= !have8;
            /* ⛔ Slot 27 (Network Delay) is a GENERATED level, not a label run:
             * it must carry a levels BIT and no value set. */
            ok &= !have27;
            ok &= (v->levels & (1u << 27)) != 0;
        }
        check("34. training value slots are absolute (tab_base + row)", ok,
              "s8=%d s10=%d s11=%d s24=%d s26=%d s27=%d levels=%#x",
              have8, have10, have11, have24, have26, have27,
              v ? v->levels : 0);
    }


    printf("\n");
    if (failures) { printf("FAILED: %d case(s)\n", failures); return 1; }
    printf("all cases passed\n");
    return 0;
}
