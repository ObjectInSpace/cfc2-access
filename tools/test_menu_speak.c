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
#include "../src/gmd_find.h"
#include "../src/menu_cursor.h"
/* ⚠ The generated roster table, so case 33p can check it against the exe's own
 * counts in the C build too -- a hand-edit of the generated header would
 * otherwise only be caught by re-running the generator. */
#include "../src/roster_table.h"
/* ⭐ For case 33c: planting a Game Settings screen needs the instance-table
 * seams (gt_test_settext_body) and the class constants. The suite already
 * LINKS gui_text.c; only the declarations were missing. */
#include "../src/gui_text.h"
#include "../src/gui_markup.h"

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

/* ⭐ More of the shipped prompts, verbatim from the corpus. The list grew from
 * 2 to 20 after the user reported "you also are not reading many of the yes/no
 * prompts", and a resolver that still knew only the original two would leave
 * every added index untested -- it would answer FALSE for them and the matcher
 * would look correct while covering nothing. */
#define MODAL_QUIT_TR    "Exit training mode?"
#define MODAL_SCORE_RST  "Reset high score data?"
#define MODAL_RESET_GAME "Restart the game?"
#define MODAL_LOAD_CHECK "Load quick save data?"


/* Stands in for the exe's part-ID tables, which a test process does not map:
 * keyboard row r -> part 100+r, gamepad slot s -> part 200+s. */
static const char *g_test_charset;   /* case 27: the lobby charset */

static BOOL test_exe_int(ULONGLONG addr, int *out)
{
    if (g_test_charset && (addr == 0x141206c90ULL || addr == 0x141206c94ULL)) {
        ULONGLONG v = (ULONGLONG)(ULONG_PTR)g_test_charset;
        *out = (int)(addr == 0x141206c90ULL ? (v & 0xffffffffu) : (v >> 32));
        return TRUE;
    }
    if (addr >= 0x141207620ULL && addr < 0x141207620ULL + 16 * 4) {
        *out = 100 + (int)((addr - 0x141207620ULL) / 4); return TRUE;
    }
    if (addr >= 0x141207848ULL && addr < 0x141207848ULL + 8 * 4) {
        *out = 200 + (int)((addr - 0x141207848ULL) / 4); return TRUE;
    }
    if (addr >= 0x1411fb4f0ULL && addr < 0x1411fb4f0ULL + 32 * 4) {
        *out = 300 + (int)((addr - 0x1411fb4f0ULL) / 4); return TRUE;
    }
    /* The real action-row states (0x1411fb640, read from the dump). */
    if (addr >= 0x1411fb640ULL && addr < 0x1411fb640ULL + 3 * 4) {
        static const int st[3] = { 0xf4247, 0xf4248, 0xf4249 };
        *out = st[(addr - 0x1411fb640ULL) / 4]; return TRUE;
    }
    if (addr >= 0x141207ce8ULL && addr < 0x141207ce8ULL + 16 * 4) {
        *out = 400 + (int)((addr - 0x141207ce8ULL) / 4); return TRUE;
    }
    *out = 0;
    return FALSE;
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
    /* Keyboard Settings: a fixed 16 rows (since 2026-11-24 the main menu has
     * no fixed count -- it is read from its own data). */
    m = mk(0x14009ec58ULL, 2, 0, 8);     /* 9 items, table says 16 */
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
    /* Since 2026-11-24: read through the LIST part table, label child 8. */
    check("7. the shipped table reads the main menu from its own rows",
          n >= 1 && s && s->kind == MC_PART_ROWS &&
          s->part_table == 0x141207188ULL && s->label_child == 8 &&
          s->row_off == 0x330 && s->rows_off == 0x334,
          "n=%d kind=%d table=%llx", n, s ? (int)s->kind : -1,
          s ? (unsigned long long)s->part_table : 0ull);

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
            /* ⭐⭐ Two rows may share a caller only if they are
             * DISTINGUISHABLE at lookup time.
             *
             * Positive counts must differ -- that is the original rule, and it
             * still holds: two entries claiming the same caller AND count means
             * one silently shadows the other.
             *
             * ⭐ A count == 0 entry is a deliberate CATCH-ALL (every game draws
             * a different number of settings, so Game Settings and Training
             * Options cannot enumerate their counts). It is legal beside
             * counted entries because menu_speak_lookup_n() prefers an exact
             * count match and only then the catch-all -- but ONLY ONE per
             * caller, or that choice becomes table-order-dependent.
             *
             * ⚠ This is NOT the rule being loosened to fit a change. A catch-all
             * is safe for MC_ROW_COLUMN specifically because that path
             * re-checks the LIVE COLUMN against the cursor's count on every
             * movement and refuses on a mismatch. A label map has no such
             * check -- its count IS the guard -- which is why a map entry must
             * still carry a positive count, asserted below. */
            if (a->count < 0 || b->count < 0)
                sane = 0;
            else if (a->count > 0 && b->count > 0 && a->count == b->count)
                sane = 0;                       /* same caller AND count */
            else if (a->count == 0 && b->count == 0)
                sane = 0;                       /* two catch-alls: ambiguous */
            /* ⛔ A catch-all must never carry a label map or a base: those
             * CLAIM names from a table and the count is the only thing that
             * ties the table to the screen. */
            else if ((a->count == 0 && (a->labels || a->base_index >= 0)) ||
                     (b->count == 0 && (b->labels || b->base_index >= 0)))
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
                 /* both read from the game's own rows, no map */
                 three->kind == MC_PART_ROWS && six->kind == MC_PART_ROWS &&
                 !six->labels;
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

    /* 16 tested the hand-made label/value maps, removed 2026-11-24
     * when settings screens moved to the game's own rows. */

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
              v && sib && sib->kind == MC_SETTINGS_ROW && !sib->labels &&
              sib->part_table == 0x141208408ULL,
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

    /* 20 tested the hand-made label/value maps, removed 2026-11-24
     * when settings screens moved to the game's own rows. */

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

    /* 20c tested the hand-made label/value maps, removed 2026-11-24
     * when settings screens moved to the game's own rows. */

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
        /* since 2026-11-24 read from the game's rows: no map */
        int ok = rowmap && rowmap->kind == MC_SETTINGS_ROW && !rowmap->labels;
        int linked = 0;
        for (size_t i = 0; i < sizeof VC / sizeof VC[0]; i++) {
            const mc_screen *v = menu_speak_lookup(VC[i]);
            if (v && v->kind == MC_SETTINGS_VALUE && v->values_off == 0x338 &&
                menu_speak_sibling_row(v) == rowmap)
                linked++;
        }
        check("20d. System: 4 value callers all link to the one row entry",
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
    /* 20m-20n tested the hand-picked list of 20 text-matched prompts, removed
     * 2026-11-24: every dialog is read from its own fields (run_modal_test). */

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
                 casual->count == 4 && ranked->count == 3;
        check("20i. one caller resolves to TWO screens by live row count",
              ok, "casual=%p(%d) ranked=%p(%d) val=%p",
              (const void *)casual, casual ? casual->count : -1,
              (const void *)ranked, ranked ? ranked->count : -1,
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

    /* 21 tested the removed value lists (no variant for the game row). */

    /* Case 22 tested a strip reader replaced on 2026-11-24 (caption game
     * strips, the Music position table); those strips are now read from
     * the part the game writes their title into: case 40. */

    /* 23 tested the hand-made label/value maps, removed 2026-11-24
     * when settings screens moved to the game's own rows. */

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

    /* --- cases 27-27c: ⭐⭐⭐ THE LOBBY ID IS READ FROM THE GAME'S DATA.
     *
     * The refresh routine 0x14008ad70 writes slot i as charset[screen[0x334 +
     * 4*i]], with the charset pointed to by 0x141206c90 and the current slot at
     * +0x350 of [+0x354]. The 36-item picker edits the current slot's index;
     * the slot strip moves the slot. Both speak the current slot's character.
     * ⚠ The test charset is deliberately NOT "0..9A..Z" order, so speech that
     * still maps a position through a written-down alphabet fails. No caption
     * is supplied: the old caption fallback is gone. */
    {
        static unsigned char lscr[0x400];
        static char lcs[37] = "ZYXWVUTSRQPONMLKJIHGFEDCBA9876543210";
        mc_speech ls;
        int wrong = 0;
        char first_bad[96] = { 0 };

        g_test_charset = lcs;
        menu_speak_test_set_exe_reader(test_exe_int);
        memset(lscr, 0, sizeof lscr);
        *(int *)(lscr + 0x354) = 6;                  /* six slots */
        *(int *)(lscr + 0x350) = 2;                  /* editing slot 2 */
        for (int idx = 0; idx < 36; idx++) {
            mc_move km = mk(0x14008a8efULL, (idx + 7) % 36, 0, 35);
            km.screen = lscr;
            *(int *)(lscr + 0x334 + 4 * 2) = idx;    /* slot 2's character */
            menu_speak_build(&km, &ls);
            if (!ls.named || ls.item[0] != lcs[idx] || ls.item[1]) {
                if (!wrong)
                    snprintf(first_bad, sizeof first_bad,
                             "idx %d named=%d item=\"%s\" want '%c'", idx,
                             (int)ls.named, ls.item, lcs[idx]);
                wrong++;
            }
        }
        check("27. the picker speaks the GAME's character for the slot, not "
              "the position's", wrong == 0, "%d wrong; first: %s", wrong,
              first_bad);

        /* 27b: the slot strip speaks the character in the slot it moved to. */
        *(int *)(lscr + 0x334 + 4 * 4) = 10;
        *(int *)(lscr + 0x350) = 4;
        mc_move sm = mk(0x14008a91bULL, 4, 0, 5);
        sm.vertical = FALSE;
        sm.screen = lscr;
        menu_speak_build(&sm, &ls);
        int b1 = ls.named && ls.item[0] == lcs[10] && !ls.item[1];
        check("27b. the slot strip speaks the moved-to slot's character",
              b1, "named=%d item=\"%s\" want '%c'", (int)ls.named, ls.item,
              lcs[10]);

        /* 27c: an index outside the charset, a slot outside the count, or a
         * charset that is not 36 printable characters names nothing. */
        *(int *)(lscr + 0x334 + 4 * 4) = 36;
        menu_speak_build(&sm, &ls);
        int c1 = !ls.named;
        *(int *)(lscr + 0x334 + 4 * 4) = 3;
        *(int *)(lscr + 0x350) = 6;
        menu_speak_build(&sm, &ls);
        int c2 = !ls.named;
        *(int *)(lscr + 0x350) = 4;
        lcs[20] = ' ';
        menu_speak_build(&sm, &ls);
        int c3 = !ls.named;
        lcs[20] = 'F';
        check("27c. a bad index, slot or charset names nothing", c1 && c2 && c3,
              "index=%d slot=%d charset=%d", c1, c2, c3);
        menu_speak_test_set_exe_reader(NULL);
        g_test_charset = NULL;
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

    /* 30 tested the hand-made label/value maps, removed 2026-11-24
     * when settings screens moved to the game's own rows. */

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

    /* Cases 32 tested the position-based readers (the x=320/835
     * columns, the x=362 pause column, the training value slots) that
     * Game Settings, Training Options and the Pause Menu used until
     * 2026-11-24. Those screens are now read from their own data:
     * case 39. */

    /* --- case 33: ⭐⭐ NO SCREEN IS READ BY POSITION ANY MORE.
     * Since 2026-11-24 every screen that used a coordinate column
     * (MC_ROW_COLUMN, MC_ROW_VALUE_LIVE) or coordinate order (MC_COORD_LIVE)
     * is read from its own data. A new entry of those kinds would bring the
     * guessing back, so none may ship. */
    {
        int bad = 0;
        for (int i = 0; i < menu_speak_screen_count(); i++) {
            const mc_screen *s = menu_speak_screen_at(i);
            if (s && (s->kind == MC_ROW_COLUMN || s->kind == MC_ROW_VALUE_LIVE ||
                      s->kind == MC_COORD_LIVE))
                bad++;
        }
        check("33. no screen entry reads by screen position", bad == 0,
              "%d entr%s still positional", bad, bad == 1 ? "y" : "ies");
    }

    /* Cases 33c-33i tested the position-based readers (the x=320/835
     * columns, the x=362 pause column, the training value slots) that
     * Game Settings, Training Options and the Pause Menu used until
     * 2026-11-24. Those screens are now read from their own data:
     * case 39. */

    /* --- case 33n: ⭐⭐ FIGHTER AWARDS' GRID NAMES FROM THE CORPUS, AND ONLY
     * AT 40 ITEMS.
     *
     * ⭐ The 40-item grid is named from a map (position N == achievement N,
     * proven against 16 pairs the game drew), so it does NOT depend on the game
     * having drawn a title -- measured, it draws one for only 17 of 78
     * movements while the user sweeps. That is the whole point: it works at any
     * speed.
     *
     * ⛔⛔ THE SAME CALLER ALSO DRIVES A 35-ITEM AXIS on the Player Progress
     * page (Y toggles it). The map must NOT reach that screen -- nothing here
     * knows what those 35 are, and speaking achievement names over them would
     * be the confident-wrong-name failure. The count is the guard.
     *
     * ⛔ And the 7-row LIST on the sibling caller is a scrolling window over
     * the same 40 (its position 2 showed achievement 7), so it must have no map
     * at all. */
    {
        const mc_screen *grid40 = menu_speak_lookup_n(0x140079739ULL, 40);
        /* since 2026-11-24 named from the game's data (MC_AWARD) */
        int ok = grid40 && grid40->kind == MC_AWARD && grid40->count == 40;

        /* ⛔⛔ ASSERT WHAT THE USER WOULD HEAR, not what lookup_n returns.
         *
         * lookup_n(caller, 35) falls back to the count=40 entry, because no
         * entry matches 35 -- so a table-shape assertion FAILS while the
         * behaviour is actually safe: menu_speak_build's staleness check
         * compares the entry's count against the LIVE total and refuses. That
         * refusal is the thing worth pinning, and pinning it here means a
         * future change to that check cannot silently re-open this.
         *
         * At 35 items the Player Progress page must come back UNNAMED, with
         * the reason stated -- never an achievement name over a stat row. */
        mc_speech s35;
        mc_move m35 = mk(0x140079739ULL, 3, 0, 34);   /* 35 items */
        m35.vertical = FALSE;
        menu_speak_build(&m35, &s35);
        ok &= (!s35.named && s35.why == MC_UNNAMED_COUNT_MISMATCH);

        /* And the 7-row window must carry no map of its own. */
        /* the scrolling list moves the same selection field, so it is named
         * from data too -- but never from a position map */
        const mc_screen *list7 = menu_speak_lookup_n(0x140079833ULL, 7);
        ok &= list7 && list7->kind == MC_AWARD && !list7->labels;

        check("33n. the awards grid names at 40 items, and the 35-item page "
              "REFUSES rather than borrowing its map", ok,
              "grid40 labels=%d n=%d / 35-item named=%d why=%d / list7 "
              "labels=%d", grid40 && grid40->labels,
              grid40 ? grid40->nlabels : -1, (int)s35.named, (int)s35.why,
              list7 && list7->labels);
    }

    /* --- case 33o: ⭐⭐⭐ THE PLAY STATS ROSTERS, and the MJUS COLLISION.
     *
     * Fighter Awards' achievements grid and Project Justice's Play Stats
     * roster share caller 0x140079739 AND an item count of 40. No caller+count
     * rule can separate them, so the tab index at screen+0x338 decides.
     *
     * ⛔⛔ THE FAILURE THIS PINS: on MJUS's tab, a count-keyed lookup returns
     * the achievements entry and speaks an ACHIEVEMENT TITLE where a CHARACTER
     * NAME belongs -- confidently, and undetectably to a listener.
     *
     * ⚠ gmd_text_by_index() fails offline, so the NAME cannot be asserted here.
     * What is asserted is the decision that precedes it: WHICH ENTRY CLAIMS THE
     * SCREEN. That is where the bug lives; the name is a lookup afterwards. */
    {
        /* A stand-in screen object. Only the int at +0x338 is ever read. */
        static unsigned char screen[0x400];
        int *tab = (int *)(screen + 0x338);
        const mc_screen *e;
        int ok = 1;

        /* Tab 1 = CVS2, 48 characters. The roster entry must claim it. */
        *tab = 1;
        e = menu_speak_lookup_for(0x140079739ULL, 48, screen);
        ok &= (e && e->kind == MC_GAME_ROSTER);

        /* ⭐⭐ THE COLLISION. Tab 6 = Project Justice, 40 characters, on the
         * same caller and count as the 40 achievements. The ROSTER must win. */
        *tab = 6;
        e = menu_speak_lookup_for(0x140079739ULL, 40, screen);
        ok &= (e && e->kind == MC_GAME_ROSTER);

        /* ⭐ And the achievements page must still reach its own map. Its tab
         * field does NOT read 6-with-40-items: any other value fails the
         * claim's size cross-check, so the count match stands. */
        *tab = 0;                      /* CVS1: 35 != 40, so no claim */
        e = menu_speak_lookup_for(0x140079739ULL, 40, screen);
        ok &= (e && e->kind == MC_AWARD);

        /* ⛔⛔ A tab index outside 0..7 establishes nothing. It must NOT claim,
         * and must NOT be CLAMPED into some roster.
         *
         * ⚠ THE COUNTS HERE ARE CHOSEN TO ISOLATE THE BOUNDS CHECK, and an
         * earlier version of this case did not: it used 40 items for both,
         * where clamping 99->7 (STG2, 24) and -1->0 (CVS1, 35) fails the size
         * cross-check anyway, so the suite passed with the bounds check
         * DELETED. Sabotage found that, not review.
         *
         * So: pair each out-of-range value with the item count its CLAMPED
         * roster would have. A clamp then satisfies the size check and claims
         * the screen -- and only a real bounds check refuses. */
        *tab = 99;                     /* would clamp to 7 = STG2, 24 chars */
        e = menu_speak_lookup_for(0x140079739ULL, 24, screen);
        ok &= !(e && e->kind == MC_GAME_ROSTER);
        *tab = -1;                     /* would clamp to 0 = CVS1, 35 chars */
        e = menu_speak_lookup_for(0x140079739ULL, 35, screen);
        ok &= !(e && e->kind == MC_GAME_ROSTER);

        /* ⛔⛔ A SIZE DISAGREEMENT IS A REFUSAL, not a near-miss. Tab 1 is
         * CVS2 (48); on a 35-item list the identity and the live count
         * disagree, so the roster must not claim -- this is what makes a
         * MISREAD field harmless instead of wrong. */
        *tab = 1;
        e = menu_speak_lookup_for(0x140079739ULL, 35, screen);
        ok &= !(e && e->kind == MC_GAME_ROSTER);

        /* ⚠ With no screen, nothing can be established: the roster entry must
         * not act as the caller's catch-all. The 35-item page then reports
         * COUNT_MISMATCH against the achievements entry, exactly as case 33n
         * requires -- these two cases pin the same property from both sides. */
        e = menu_speak_lookup_for(0x140079739ULL, 35, NULL);
        ok &= !(e && e->kind == MC_GAME_ROSTER);

        check("33o. the Play Stats roster is claimed by the TAB INDEX, so "
              "Project Justice's 40 characters beat the 40 achievements", ok,
              "(see the case body for which assertion failed)");
    }

    /* --- case 33p: the roster table agrees with the game's OWN count table.
     *
     * ⭐⭐ The eight counts are read from the exe at 0x1411f7b30 and are what
     * the claim cross-check compares against. They are generated from the
     * corpus by tools/gen_roster_table.py, which fails if the two sources
     * disagree -- this pins the result in the C build as well, so a hand-edit
     * of the generated header cannot slip through.
     *
     * ⚠ Order is the GAME'S index, not alphabetical: it is what +0x338 holds. */
    {
        static const int exe_counts[8] = { 35, 48, 23, 39, 10, 14, 40, 24 };
        int ok = 1;
        for (int i = 0; i < 8; i++) {
            ok &= (g_rosters[i].count == exe_counts[i]);
            ok &= (g_rosters[i].base > 0);
            ok &= (g_rosters[i].name && g_rosters[i].name[0]);
        }
        /* ⭐ Every roster must occupy its own GMD range: overlapping runs
         * would name one game's characters from another's labels. */
        for (int i = 0; i + 1 < 8; i++)
            ok &= (g_rosters[i].base + 2 * g_rosters[i].count
                   <= g_rosters[i + 1].base);

        check("33p. the 8 rosters match the exe's count table and do not "
              "overlap in the GMD", ok,
              "counts %d,%d,%d,%d,%d,%d,%d,%d",
              g_rosters[0].count, g_rosters[1].count, g_rosters[2].count,
              g_rosters[3].count, g_rosters[4].count, g_rosters[5].count,
              g_rosters[6].count, g_rosters[7].count);
    }

    /* --- case 33t: ⭐⭐ THE STANDBY SETTINGS read their own column ---------
     *
     * 0x1400991c1, 3 rows, named by the snapshot at x=494 (Basic / In-game /
     * Museum Standby). It quotes the live column, so there is no map to go
     * stale -- what this pins is that the entry EXISTS with the measured
     * column, because a wrong or missing coord_x silently names nothing.
     *
     * ⛔⛔ AND THAT IT IS NOT CONFUSED WITH THE OTHER 3-ROW CONTROL. The pair
     * 0x14009885e/0x1400988bf on the Casual Match screens is ALSO three rows
     * vertical and is STILL UNNAMED. I spent a round treating the two as one
     * problem; they are different callers on different screens. If either of
     * those ever acquires this screen's column by copy-paste, it would quote
     * Casual Match's values as though they were standby rows. */
    {
        const mc_screen *sb = menu_speak_lookup_n(0x1400991c1ULL, 3);
        const mc_screen *a  = menu_speak_lookup_n(0x14009885eULL, 3);
        const mc_screen *b  = menu_speak_lookup_n(0x1400988bfULL, 3);
        int ok = sb && sb->kind == MC_PART_ROWS &&
                 sb->part_table == 0x1411fb510ULL && sb->label_child == 7 &&
                 sb->row_off == 0x334 && sb->rows_off == 0x338 &&
                 /* the other pair must still have NO entry at all */
                 /* since 2026-11-24: identified from the code as up/down
                  * inside the game grid */
                 a && a->kind == MC_GAME_GRID && b && b->kind == MC_GAME_GRID;
        check("33t. Standby Settings reads its own rows, and the Casual "
              "Match 3-row pair is the game grid", ok,
              "standby=%d table=%llx grid=%d/%d",
              sb ? (int)sb->kind : -1,
              sb ? (unsigned long long)sb->part_table : 0ull,
              a != NULL, b != NULL);
    }

    /* Case 33q-33s tested a strip reader replaced on 2026-11-24 (caption game
     * strips, the Music position table); those strips are now read from
     * the part the game writes their title into: case 40. */

    /* --- case 33j: ⛔⛔ THE PAUSE MENU MUST NOT CLAIM NAMES FROM A MAP.
     *
     * USER-REPORTED: "In training mode it is saying quicksave/load rather than
     * training options/return to character select." A confidently WRONG name --
     * worse than silence, because a blind player cannot tell it is wrong and
     * may Load a Quick Save while trying to open the training menu.
     *
     * The cause was a LABEL MAP keyed on row count. Training mode also draws
     * EIGHT rows, so 8 did not mean "arcade" and the arcade map spoke its rows
     * 6-7 (Load Quick Save / Quick Save) over training's (Training Menu /
     * Change Character). The snapshot sweep caught five distinct variants on
     * this one caller.
     *
     * ⚠ There was NO test for this screen at all, which is how it shipped. This
     * case pins the property that was violated: on a caller whose variants
     * cannot be told apart by count, the entry must QUOTE the live column
     * rather than index a table. */
    {
        const mc_screen *p7 = menu_speak_lookup_n(0x1400a749dULL, 7);
        const mc_screen *p8 = menu_speak_lookup_n(0x1400a749dULL, 8);
        int ok = p7 && p8;
        if (ok) {
            /* Both counts must resolve to a quoting entry with a real column,
             * and NEITHER may carry a label map or a base to index. */
            /* Since 2026-11-24: read from the game's own rows (MC_PART_ROWS),
             * still with no map and no base. */
            ok &= p7->kind == MC_PART_ROWS && p7->part_table &&
                  p7->label_child == 8 && !p7->labels && p7->base_index < 0;
            ok &= p8->kind == MC_PART_ROWS && p8->part_table &&
                  p8->label_child == 8 && !p8->labels && p8->base_index < 0;
            /* ⭐ And both must be the SAME entry: two entries here would mean
             * the count was being used to tell the variants apart again, which
             * is the thing that was wrong. */
            ok &= (p7 == p8);
        }
        check("33j. the pause menu reads its own rows (count cannot separate "
              "training from arcade)", ok,
              "7-row kind=%d labels=%d / 8-row kind=%d labels=%d / same=%d",
              p7 ? (int)p7->kind : -1, p7 && p7->labels,
              p8 ? (int)p8->kind : -1, p8 && p8->labels,
              p7 && p8 && p7 == p8);
    }

    /* Cases 33b and 34 tested the position-based readers (the x=320/835
     * columns, the x=362 pause column, the training value slots) that
     * Game Settings, Training Options and the Pause Menu used until
     * 2026-11-24. Those screens are now read from their own data:
     * case 39. */

    /* --- case 35: ⛔⛔ TWO VARIANTS OF ONE SCREEN NEED TWO LEVEL MASKS, and
     * this case drives menu_speak_build() rather than reading the table.
     *
     * Display & Sound Settings has a 7-row form and a 6-row form (games with no
     * "Internal Resolution"). Music Volume and Sound Effects Volume are rows 4-5
     * in the first and rows 3-4 in the second, so ONE mask cannot serve both:
     * the 7-row 0x30 applied to the 6-row screen marks Sound Effects Volume and
     * DEFAULT -- stating a level on the row that RESETS the screen.
     *
     * ⭐ So the mask is read from the SIBLING ROW entry (selected by the live
     * row count), not from the shared value entry.
     *
     * ⚠⚠ AN EARLIER VERSION OF THIS CASE ONLY INSPECTED THE TABLE, and it
     * PASSED with the lookup sabotaged back to the shared mask -- it asserted
     * the data and never the code consuming it. This drives a real value
     * movement on each variant, which is what makes the sabotage fail. */
    {
        /* values_off is 0x398; row N's cursor is screen + 0x398 + 4*N. */
        static unsigned char av7buf[0x400], av6buf[0x400];
        const mc_screen *val = menu_speak_lookup(0x14009c7a3ULL);
        int ok = val != NULL;
        mc_speech s7d, s7v, s6d, s6v;

        memset(&s7d, 0, sizeof s7d); memset(&s7v, 0, sizeof s7v);
        memset(&s6d, 0, sizeof s6d); memset(&s6v, 0, sizeof s6v);

        if (ok) {
            mc_move mv = mk(0x14009c7a3ULL, 1, 0, 10);
            menu_speak_reset_row_total();
            menu_speak_note_row_total(av7buf, 7);
            mv.screen = av7buf;
            mv.cursor = (int *)(av7buf + 0x398 + 4 * 4);     /* row 4 = volume  */
            menu_speak_build(&mv, &s7v);
            mv.cursor = (int *)(av7buf + 0x398 + 4 * 6);     /* row 6 = Default */
            menu_speak_build(&mv, &s7d);

            mc_move mv6 = mk(0x14009c7a3ULL, 1, 0, 10);
            menu_speak_reset_row_total();
            menu_speak_note_row_total(av6buf, 6);
            mv6.screen = av6buf;
            mv6.cursor = (int *)(av6buf + 0x398 + 4 * 3);    /* row 3 = volume  */
            menu_speak_build(&mv6, &s6v);
            mv6.cursor = (int *)(av6buf + 0x398 + 4 * 5);    /* row 5 = Default */
            menu_speak_build(&mv6, &s6d);

            ok &= s7v.is_level && !s7d.is_level;
            ok &= s6v.is_level && !s6d.is_level;
        }
        check("35. each AV variant levels the right rows (7:4-5, 6:3-4)", ok,
              "7row vol=%d default=%d | 6row vol=%d default=%d",
              (int)s7v.is_level, (int)s7d.is_level,
              (int)s6v.is_level, (int)s6d.is_level);
    }

    /* --- cases 36-37: ⭐⭐⭐ THE CONTROLS SCREENS, READ FROM THE GAME'S TREE.
     *
     * ⛔ User, 2026-11-24: "remove guessing entirely". Both screens are now
     * read the way their refresh routines write them: row -> part ID from the
     * exe's table, then the part's child 7 (label) and child 8 (value) found
     * by walking the GUI tree. These fixtures build that tree -- screen+0x108
     * root, part nodes (id +0x08, child +0x68, next +0x70, objects +0x100),
     * text objects (index +0x50, child +0x70, next +0x78) -- with NO screen
     * coordinates at all, so nothing positional can pass by accident.
     * ⚠ A DECOY part with the same ID sits as the ROOT's sibling: the game
     * never searches there, and neither may we. */
    {
        static unsigned char scr[0x2000], decoy_root[0x110];
        static unsigned char parts[24][0x110], hold[24][0x100];
        static unsigned char txt[48][GT_CLASS_SIZE];
        static unsigned char root[0x110];
        int nt = 0;
        mc_speech cs;

        gt_test_reset();
        gt_test_focus_enable();
        memset(scr, 0, sizeof scr);
        memset(root, 0, sizeof root);
        memset(decoy_root, 0, sizeof decoy_root);
        *(int *)(root + 0x08) = -1;
        *(void **)(scr + 0x108) = root;

        /* Make part `id` with label (child 7) and optional value (child 8). */
        #define MKPART(i_, id_, lab_, val_) do {                              \
            unsigned char *P_ = parts[i_], *H_ = hold[i_];                   \
            memset(P_, 0, 0x110); memset(H_, 0, 0x100);                       \
            *(int *)(P_ + 0x08) = (id_);                                     \
            *(int *)(H_ + 0x50) = -1;                                        \
            *(void **)(P_ + 0x100) = H_;                                     \
            unsigned char *L_ = txt[nt++];                                   \
            memset(L_, 0, GT_CLASS_SIZE);                                    \
            *(ULONGLONG *)L_ = GT_VTABLE_ADDR;                               \
            *(int *)(L_ + 0x50) = 7;                                         \
            *(void **)(H_ + 0x70) = L_;                                      \
            gt_test_settext_body(L_, (lab_), NULL, 0);                       \
            if (val_) {                                                      \
                unsigned char *V_ = txt[nt++];                               \
                memset(V_, 0, GT_CLASS_SIZE);                                \
                *(ULONGLONG *)V_ = GT_VTABLE_ADDR;                           \
                *(int *)(V_ + 0x50) = 8;                                     \
                *(void **)(L_ + 0x78) = V_;                                  \
                gt_test_settext_body(V_, (val_), NULL, 0);                   \
            }                                                                \
        } while (0)

        /* Keyboard rows 0..15 -> parts 100+r. Row 14 is not drawn (6 keys). */
        static const char *const KL[16] = {
            "Key Bindings", "Menu", "Start", "Coin", "Left", "Up", "Right",
            "Down", "Light Punch", "Medium Punch", "Heavy Punch", "Light Kick",
            "Medium Kick", "Heavy Kick", NULL, "Default" };
        static const char *const KV[16] = {
            "Type A", "<SIZE 58><ICON KB_F2></SIZE>",
            "<SIZE 58><ICON KB_F1></SIZE>", "<SIZE 58><ICON KB_F3></SIZE>",
            "<SIZE 58><ICON KB_A></SIZE>", "<SIZE 58><ICON KB_W></SIZE>",
            "<SIZE 58><ICON KB_D></SIZE>", "<SIZE 58><ICON KB_S></SIZE>",
            "<SIZE 58><ICON KB_U></SIZE>", "<SIZE 58><ICON KB_I></SIZE>",
            "<SIZE 58><ICON KB_O></SIZE>", "<SIZE 58><ICON KB_J></SIZE>",
            "<SIZE 58><ICON KB_K></SIZE>", "<SIZE 58><ICON KB_L></SIZE>",
            NULL, NULL };
        int np = 0;
        unsigned char *prev = NULL;
        for (int r = 0; r < 16; r++) {
            if (!KL[r]) continue;
            MKPART(np, 100 + r, KL[r], KV[r]);
            if (prev) *(void **)(prev + 0x70) = parts[np];
            else      *(void **)(root + 0x68) = parts[np];
            prev = parts[np++];
        }
        /* Decoy: the ROOT's sibling holds another part 103 saying "WRONG". */
        *(int *)(decoy_root + 0x08) = -2;
        *(void **)(root + 0x70) = decoy_root;
        MKPART(np, 103, "WRONG", "WRONG");
        *(void **)(decoy_root + 0x68) = parts[np++];
        *(int *)(scr + 0x37c) = 13;              /* last drawn key row */

        menu_speak_test_set_exe_reader(test_exe_int);

        static const struct {
            ULONGLONG caller; int helper, max, game_row;
            const char *want; const char *why;
        } K[] = {
            { 0x14009ec58ULL, 1, 15, 1,  "Menu, F2", "first key row" },
            { 0x14009ec58ULL, 8, 15, 15, "Default", "helper 8, game 15: no value" },
            { 0x14009ec58ULL, 14, 15, 7, "Down, S", "helper 14, game 7" },
            { 0x14009ec58ULL, 7, 15, 0,  "Key Bindings, Type A", "helper 7, game 0" },
            { 0x14009ed68ULL, 1, 1, 9,   "Medium Punch, I", "column switch 2 -> 9" },
            { 0x14009ed20ULL, 2, 3, 0,   "Key Bindings, Type A", "type axis" },
            { 0x14009ec58ULL, 2, 15, 3,  "Coin, F3", "part 103 from the TREE, not the decoy" },
        };
        int ok = 1;
        for (int k = 0; k < (int)(sizeof K / sizeof K[0]); k++) {
            mc_move km = mk(K[k].caller, K[k].helper, 0, K[k].max);
            km.vertical = (K[k].caller == 0x14009ec58ULL);
            km.screen = scr;
            *(int *)(scr + 0x334) = K[k].game_row;
            menu_speak_build(&km, &cs);
            int good = cs.named && cs.bare && strcmp(cs.item, K[k].want) == 0;
            if (!good)
                printf("        36: %s -> named=%d item=\"%s\" (want \"%s\")\n",
                       K[k].why, (int)cs.named, cs.item, K[k].want);
            ok &= good;
        }
        check("36. the keyboard controls read the game's own part for the "
              "game's own row, on all three call sites", ok, "");

        {
            mc_move km = mk(0x14009ec58ULL, 3, 0, 15);
            km.screen = scr;
            *(int *)(scr + 0x334) = 14;          /* past the last key row */
            menu_speak_build(&km, &cs);
            int a1 = !cs.named;
            *(int *)(scr + 0x334) = 16;          /* out of range */
            menu_speak_build(&km, &cs);
            int a2 = !cs.named && cs.why == MC_UNNAMED_ROW_UNDERIVABLE;
            check("36b. an undrawn or out-of-range row names nothing", a1 && a2,
                  "undrawn=%d out=%d", a1, a2);
        }

        /* --- 37: the gamepad list. Slot s -> part 200+s. scroll 3, count 12:
         * slot 0 = item 0, slots 1..6 = items 4..9, slot 7 = item 11. */
        gt_test_reset();
        gt_test_focus_enable();
        memset(scr, 0, sizeof scr);
        memset(root, 0, sizeof root);
        *(int *)(root + 0x08) = -1;
        *(void **)(scr + 0x108) = root;
        nt = 0; np = 0; prev = NULL;
        static const char *const PL[8] = {
            "Button Layout", "Guard", "Plasma Field", "Plasma Reflect",
            "Taunt", "Vertical Slash x2", "Kick x2", "Default" };
        for (int sl = 0; sl < 8; sl++) {
            MKPART(np, 200 + sl, PL[sl], sl == 0 ? "Type A" : NULL);
            if (prev) *(void **)(prev + 0x70) = parts[np];
            else      *(void **)(root + 0x68) = parts[np];
            prev = parts[np++];
        }
        *(int *)(scr + 0x338) = 3;
        *(int *)(scr + 0x340) = 12;
        static const struct {
            ULONGLONG caller; int cursor; BOOL vert; const char *want;
            int pos; BOOL bare;
        } A[] = {
            { 0x1400a031dULL, 2, TRUE,  "Plasma Field", 6, FALSE },  /* item 5 */
            { 0x1400a031dULL, 8, TRUE,  "Default", 12, FALSE },      /* item 11 */
            { 0x1400a056fULL, 1, FALSE, "Guard", 0, TRUE },          /* item 4 */
        };
        ok = 1;
        for (int k = 0; k < (int)(sizeof A / sizeof A[0]); k++) {
            mc_move am = mk(A[k].caller, 0, 0, A[k].vert ? 7 : 158);
            am.vertical = A[k].vert;
            am.screen = scr;
            *(int *)(scr + 0x33c) = A[k].cursor;
            menu_speak_build(&am, &cs);
            int good = cs.named && strcmp(cs.item, A[k].want) == 0 &&
                       cs.bare == A[k].bare &&
                       (A[k].bare || cs.position == A[k].pos);
            if (!good)
                printf("        37[%d]: named=%d bare=%d pos=%d item=\"%s\" "
                       "(want \"%s\")\n", k, (int)cs.named, (int)cs.bare,
                       cs.position, cs.item, A[k].want);
            ok &= good;
        }
        check("37. the gamepad list names the slot the game highlights", ok, "");
        /* Slot 0 is item 0 only; with scroll 3, cursor 0 selects item 3, which
         * no slot shows -- refuse rather than name a neighbour. */
        {
            mc_move am = mk(0x1400a031dULL, 0, 0, 7);
            am.screen = scr;
            *(int *)(scr + 0x33c) = 0;
            menu_speak_build(&am, &cs);
            check("37b. an item no slot shows is refused", !cs.named,
                  "named=%d item=\"%s\"", (int)cs.named, cs.item);
        }
        menu_speak_test_set_exe_reader(NULL);
        #undef MKPART
    }

    /* --- case 38: ⭐⭐⭐ A SETTINGS VALUE FROM THE PART THE GAME WROTE.
     *
     * Options > System, the screen the user reported silent: row r's part is
     * 0x1411fb4f0[r] (stood in for as 300+r), value in child 8. The value
     * axis 0x1400a3578 drives row 2 (Start Button) at values_off 0x338. No
     * coordinates, and the objects are planted in REVERSE row order so table
     * order cannot carry the answer. ⚠ The label cross-check needs the live
     * message table, which this process lacks, so it is not exercised here. */
    {
        static unsigned char sscr[0x800], sroot[0x110];
        static unsigned char sp_[8][0x110], sh[8][0x100];
        static unsigned char so[16][GT_CLASS_SIZE];
        static const char *const SL[6] = {
            "Language", "Menu Button", "Start Button", "Coin Button",
            "Data Collection", "Default" };
        static const char *const SV[6] = {
            "English [en]", "<SIZE 58><ICON BTN_SELECT></SIZE>",
            "<SIZE 58><ICON BTN_LB></SIZE>", "<SIZE 58><ICON LSB></SIZE>",
            "Decline", NULL };
        int no = 0;
        unsigned char *prevp = NULL;
        mc_speech ss;

        gt_test_reset();
        gt_test_focus_enable();
        memset(sscr, 0, sizeof sscr);
        memset(sroot, 0, sizeof sroot);
        *(int *)(sroot + 0x08) = -1;
        *(void **)(sscr + 0x108) = sroot;
        for (int r = 5; r >= 0; r--) {
            unsigned char *P = sp_[r], *H = sh[r], *L, *V;
            memset(P, 0, 0x110); memset(H, 0, 0x100);
            *(int *)(P + 0x08) = 300 + r;
            *(int *)(H + 0x50) = -1;
            *(void **)(P + 0x100) = H;
            L = so[no++]; memset(L, 0, GT_CLASS_SIZE);
            *(ULONGLONG *)L = GT_VTABLE_ADDR;
            *(int *)(L + 0x50) = 7;
            *(void **)(H + 0x70) = L;
            gt_test_settext_body(L, SL[r], NULL, 0);
            if (SV[r]) {
                V = so[no++]; memset(V, 0, GT_CLASS_SIZE);
                *(ULONGLONG *)V = GT_VTABLE_ADDR;
                *(int *)(V + 0x50) = 8;
                *(void **)(L + 0x78) = V;
                gt_test_settext_body(V, SV[r], NULL, 0);
            }
            if (prevp) *(void **)(prevp + 0x70) = P;
            else       *(void **)(sroot + 0x68) = P;
            prevp = P;
        }
        menu_speak_test_set_exe_reader(test_exe_int);

        /* Start Button's value moves: cursor = values array slot 2. */
        mc_move sm = mk(0x1400a3578ULL, 2, 0, 7);
        sm.vertical = FALSE;
        sm.screen = sscr;
        sm.cursor = (int *)(sscr + 0x338 + 4 * 2);
        menu_speak_build(&sm, &ss);
        int good1 = ss.named && strcmp(ss.item, "LB") == 0 && ss.have_row &&
                    ss.row == 2;
        /* Coin Button (row 3) on its own value caller. */
        sm = mk(0x1400a3538ULL, 3, 2, 7);
        sm.vertical = FALSE;
        sm.screen = sscr;
        sm.cursor = (int *)(sscr + 0x338 + 4 * 3);
        menu_speak_build(&sm, &ss);
        char lsb[GM_OUT_MAX];
        gm_speak("<SIZE 58><ICON LSB></SIZE>", lsb, sizeof lsb);
        int good2 = ss.named && lsb[0] && strcmp(ss.item, lsb) == 0;
        char coin[128];
        snprintf(coin, sizeof coin, "%s", ss.item);
        check("38. a settings value is read from the part the game wrote",
              good1 && good2, "start ok=%d, coin=\"%s\"", good1, coin);

        /* 38b: delete the value object of row 2 -> nothing is borrowed from a
         * neighbour; the row speaks position only. */
        {
            /* row 2 was planted 4th (r = 5,4,3,2): its label is so[6]. */
            unsigned char *L2 = NULL;
            for (int i = 0; i < no; i++)
                if (*(int *)(so[i] + 0x50) == 7 &&
                    *(void **)(sh[2] + 0x70) == so[i])
                    L2 = so[i];
            if (L2) *(void **)(L2 + 0x78) = NULL;
        }
        sm = mk(0x1400a3578ULL, 2, 0, 7);
        sm.vertical = FALSE;
        sm.screen = sscr;
        sm.cursor = (int *)(sscr + 0x338 + 4 * 2);
        menu_speak_build(&sm, &ss);
        check("38b. a row with no value object borrows nothing", !ss.named,
              "named=%d item=\"%s\"", (int)ss.named, ss.item);
        menu_speak_test_set_exe_reader(NULL);
    }

    /* --- case 39: ⭐⭐⭐ MC_PART_ROWS -- Pause Menu, Training Options, Game
     * Settings read from their own data.
     *
     * Training: tab = [+0x338], row = [+0x350 + 4*tab], count = [+0x360 +
     * 4*tab], part = 0x1411fb4f0[row] (stood in for as 300+row), label child 7,
     * value child 8. Pause: row = [+0x338], count [+0x33c], part =
     * 0x141207ce8[row] (stood in for as 400+row), label child 8.
     * ⚠ The tab TITLE comes from the message table, which this process lacks,
     * so it is not exercised here. */
    {
        static unsigned char tscr[0x800], troot[0x110];
        static unsigned char tp[12][0x110], th[12][0x100];
        static unsigned char tobj[24][GT_CLASS_SIZE];
        int no = 0, np = 0;
        unsigned char *prev = NULL;
        mc_speech ts;

        gt_test_reset();
        gt_test_focus_enable();
        memset(tscr, 0, sizeof tscr);
        memset(troot, 0, sizeof troot);
        *(int *)(troot + 0x08) = -1;
        *(void **)(tscr + 0x108) = troot;

        #define PLANT(id_, lc_, lab_, vc_, val_) do {                          \
            unsigned char *P_ = tp[np], *H_ = th[np], *L_;                    \
            memset(P_, 0, 0x110); memset(H_, 0, 0x100);                        \
            *(int *)(P_ + 0x08) = (id_);                                      \
            *(int *)(H_ + 0x50) = -1;                                         \
            *(void **)(P_ + 0x100) = H_;                                      \
            L_ = tobj[no++]; memset(L_, 0, GT_CLASS_SIZE);                    \
            *(ULONGLONG *)L_ = GT_VTABLE_ADDR;                                \
            *(int *)(L_ + 0x50) = (lc_);                                      \
            *(void **)(H_ + 0x70) = L_;                                       \
            gt_test_settext_body(L_, (lab_), NULL, 0);                        \
            if (val_) {                                                       \
                unsigned char *V_ = tobj[no++]; memset(V_, 0, GT_CLASS_SIZE); \
                *(ULONGLONG *)V_ = GT_VTABLE_ADDR;                            \
                *(int *)(V_ + 0x50) = (vc_);                                  \
                *(void **)(L_ + 0x78) = V_;                                   \
                gt_test_settext_body(V_, (val_), NULL, 0);                    \
            }                                                                 \
            if (prev) *(void **)(prev + 0x70) = P_;                           \
            else      *(void **)(troot + 0x68) = P_;                          \
            prev = P_; np++;                                                  \
        } while (0)

        /* Training rows on screen (whatever tab): parts 300..302. */
        PLANT(300, 7, "Health Bar", 8, "Normal");
        PLANT(301, 7, "Super Meter", 8, "Infinite");
        PLANT(302, 7, "Stun Gauge", 8, "Off");
        /* Pause rows: parts 400..402, label in child 8. */
        PLANT(400, 8, "Resume", 0, NULL);
        PLANT(401, 8, "Move List", 0, NULL);
        PLANT(402, 8, "Quit", 0, NULL);
        menu_speak_test_set_exe_reader(test_exe_int);

        /* Tab 1: row cursor at +0x354 = 1, count at +0x364 = 3. Tab 0's
         * fields hold a DIFFERENT row (2) so a builder ignoring the tab names
         * the wrong row. */
        *(int *)(tscr + 0x338) = 1;
        *(int *)(tscr + 0x350) = 2;  *(int *)(tscr + 0x360) = 3;
        *(int *)(tscr + 0x354) = 1;  *(int *)(tscr + 0x364) = 3;
        mc_move tm = mk(0x1400a41efULL, 0, 0, 2);     /* helper index unused */
        tm.screen = tscr;
        menu_speak_build(&tm, &ts);
        int r1 = ts.named && strcmp(ts.item, "Super Meter, Infinite") == 0 &&
                 ts.position == 2 && ts.total == 3;
        char d1[160];
        snprintf(d1, sizeof d1, "item=\"%s\" pos=%d/%d", ts.item, ts.position,
                 ts.total);
        /* Value axis on the same tab: the row is named, the value is the item. */
        tm = mk(0x1400a4241ULL, 1, 0, 3);
        tm.vertical = FALSE;
        tm.screen = tscr;
        menu_speak_build(&tm, &ts);
        int r2 = ts.named && ts.axis == MC_AXIS_VALUE &&
                 strcmp(ts.row_name, "Super Meter") == 0 &&
                 strcmp(ts.item, "Infinite") == 0;
        char d2[160];
        snprintf(d2, sizeof d2, "row=\"%s\" item=\"%s\"", ts.row_name, ts.item);
        check("39. Training Options reads the current TAB's row from its own "
              "part, on both axes", r1 && r2, "%s | %s", d1, d2);

        /* Pause menu: row [+0x338] = 1, count [+0x33c] = 3, label child 8. */
        *(int *)(tscr + 0x338) = 1;
        *(int *)(tscr + 0x33c) = 3;
        tm = mk(0x1400a749dULL, 0, 0, 2);
        tm.screen = tscr;
        menu_speak_build(&tm, &ts);
        check("39b. the pause menu reads its label from child 8",
              ts.named && strcmp(ts.item, "Move List") == 0 && ts.position == 2,
              "item=\"%s\" pos=%d", ts.item, ts.position);

        /* A row at or past the count is not drawn: refused. */
        *(int *)(tscr + 0x338) = 3;
        menu_speak_build(&tm, &ts);
        check("39c. a row past the game's count is refused", !ts.named,
              "named=%d item=\"%s\"", (int)ts.named, ts.item);
        /* 39d: ⛔ the EX page's Default row spoke page 1's "3 (First to 2)"
         * (user run 16:18). Its value object keeps stale text; the game hides
         * it by playing an ACTION-row state. Same row, same stale text: with
         * the action state it must speak the label alone, with a value-row
         * state it must still speak the value. */
        {
            static int anim_rec;
            *(void **)(th[1] + 0xb8) = &anim_rec;     /* part 301 */
            *(int *)(tscr + 0x338) = 1;
            *(int *)(tscr + 0x354) = 1;  *(int *)(tscr + 0x364) = 3;
            mc_move dm = mk(0x1400a41efULL, 0, 0, 2);
            dm.screen = tscr;
            anim_rec = 0xf4248;                       /* action row, selected */
            menu_speak_build(&dm, &ts);
            int hid = ts.named && strcmp(ts.item, "Super Meter") == 0;
            char dh[96]; snprintf(dh, sizeof dh, "%s", ts.item);
            anim_rec = 5;                             /* value row, selected */
            menu_speak_build(&dm, &ts);
            int shown = ts.named && strcmp(ts.item, "Super Meter, Infinite") == 0;
            check("39d. an action row's hidden value is not spoken; a value "
                  "row's is", hid && shown, "action=\"%s\" value-row=\"%s\"",
                  dh, ts.item);
            *(void **)(th[1] + 0xb8) = NULL;
        }
        menu_speak_test_set_exe_reader(NULL);
        #undef PLANT
    }

    /* --- case 40: ⭐⭐ STRIPS SPEAK THE TITLE THEIR REFRESH WRITES.
     * Gallery and Music write the current title into part 0xd child 7, the
     * Ranked Leaderboard into part 0x1e child 15. The position the helper
     * reports is deliberately unrelated to the planted title, so a strip that
     * still named by position fails. */
    {
        static unsigned char gscr[0x400], groot[0x110];
        static unsigned char gp[2][0x110], gh[2][0x100];
        static unsigned char go_[2][GT_CLASS_SIZE];
        mc_speech gs;

        gt_test_reset();
        gt_test_focus_enable();
        memset(gscr, 0, sizeof gscr); memset(groot, 0, sizeof groot);
        *(int *)(groot + 0x08) = -1;
        *(void **)(gscr + 0x108) = groot;
        static const struct { int part, child; const char *t; } G[2] = {
            { 0xd, 7, "Capcom vs. SNK 2: Mark of the Millennium 2001" },
            { 0x1e, 15, "Power Stone 2" },
        };
        for (int i = 0; i < 2; i++) {
            memset(gp[i], 0, 0x110); memset(gh[i], 0, 0x100);
            memset(go_[i], 0, GT_CLASS_SIZE);
            *(int *)(gp[i] + 0x08) = G[i].part;
            *(int *)(gh[i] + 0x50) = -1;
            *(void **)(gp[i] + 0x100) = gh[i];
            *(ULONGLONG *)go_[i] = GT_VTABLE_ADDR;
            *(int *)(go_[i] + 0x50) = G[i].child;
            *(void **)(gh[i] + 0x70) = go_[i];
            gt_test_settext_body(go_[i], G[i].t, NULL, 0);
            if (i == 0) *(void **)(groot + 0x68) = gp[i];
            else        *(void **)(gp[0] + 0x70) = gp[i];
        }
        mc_move gm = mk(0x14009a482ULL, 6, 0, 8);    /* Gallery, position 7 */
        gm.vertical = FALSE; gm.screen = gscr;
        menu_speak_build(&gm, &gs);
        int g1 = gs.named && strcmp(gs.item, G[0].t) == 0;
        gm = mk(0x14009af21ULL, 0, 0, 8);            /* Music strip */
        gm.vertical = FALSE; gm.screen = gscr;
        menu_speak_build(&gm, &gs);
        int g2 = gs.named && strcmp(gs.item, G[0].t) == 0;
        gm = mk(0x1400ac059ULL, 3, 0, 7);            /* Leaderboard */
        gm.vertical = FALSE; gm.screen = gscr;
        menu_speak_build(&gm, &gs);
        int g3 = gs.named && strcmp(gs.item, G[1].t) == 0;
        check("40. Gallery, Music and the Leaderboard speak the title their "
              "refresh wrote", g1 && g2 && g3, "gallery=%d music=%d board=%d",
              g1, g2, g3);
    }

    /* --- case 41: ⭐⭐ THE MODE STRIP AND THE CONSENT DIALOG ARE NAMED BY
     * THE GAME'S OWN MESSAGE TABLES (once thought to be layout-only text).
     * The message text needs the live table, so this pins the wiring -- the
     * tables the screens' own code uses -- and the refusals. */
    {
        const mc_screen *ms = menu_speak_lookup_n(0x1400b2729ULL, 3);
        const mc_screen *cs = menu_speak_lookup_n(0x140087cdaULL, 3);
        int ok = ms && ms->kind == MC_MSG_LIST &&
                 ms->msg_table == 0x1412084d8ULL && ms->id_off == 0x340 &&
                 ms->row_off == 0x338 && ms->id_max == 4 && !ms->labels &&
                 cs && cs->kind == MC_MSG_LIST &&
                 cs->msg_table == 0x141206b98ULL && cs->row_off == 0x340 &&
                 cs->id_max == 3 && !cs->labels;
        /* an item past the count, and a mode id past the table, name nothing */
        static unsigned char ms_scr[0x400];
        mc_speech r;
        memset(ms_scr, 0, sizeof ms_scr);
        *(int *)(ms_scr + 0x33c) = 3;
        *(int *)(ms_scr + 0x338) = 3;               /* item 3 of 3: refused */
        mc_move mm = mk(0x1400b2729ULL, 2, 0, 2);
        mm.vertical = FALSE; mm.screen = ms_scr;
        menu_speak_build(&mm, &r);
        int a = !r.named;
        *(int *)(ms_scr + 0x338) = 1;
        *(int *)(ms_scr + 0x340 + 4) = 4;           /* mode id 4: refused */
        menu_speak_build(&mm, &r);
        int b = !r.named;
        check("41. the mode strip and consent dialog read their own message "
              "tables, and refuse out-of-range items", ok && a && b,
              "wiring=%d past-count=%d bad-id=%d", ok, a, b);
    }

    /* --- case 42: a dialog's filled-in text fits its template.
     * The resolution confirm (PC_SETTING_ASK, "%d seconds") and the quick-save
     * overwrite (POP_SAVE_CHECK2, "%s") were silent: no exact match could ever
     * fire. The dialog reader speaks the live text that fits the template. */
    {
        const char *T1 = "Do you want to keep the current settings? (Reverting "
                         "to previous settings in %d seconds.)";
        const char *T2 = "<CNTR>Quick save data already exists for <CNTR>\"%s\". "
                         "<CNTR>Overwrite?";
        int ok = 1;
        ok &= menu_speak_test_template_fits(T1, "Do you want to keep the current "
                 "settings? (Reverting to previous settings in 14 seconds.)");
        ok &= menu_speak_test_template_fits(T2, "<CNTR>Quick save data already "
                 "exists for <CNTR>\"Capcom vs. SNK 2\". <CNTR>Overwrite?");
        ok &= menu_speak_test_template_fits("Begin match?", "Begin match?");
        /* refusals: different wording, a missing tail, extra text */
        ok &= !menu_speak_test_template_fits(T1, "Do you want to keep the "
                  "current settings?");
        ok &= !menu_speak_test_template_fits("Begin match?", "Begin match? Now");
        ok &= !menu_speak_test_template_fits(T2, "Quick save loaded.");
        check("42. a dialog's live text is matched to its template, values and "
              "names included", ok, "");
    }

    printf("\n");
    if (failures) { printf("FAILED: %d case(s)\n", failures); return 1; }
    printf("all cases passed\n");
    return 0;
}
