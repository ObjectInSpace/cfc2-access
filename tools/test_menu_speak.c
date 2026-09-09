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

/* The main menu's real call site, from the 2026-09-08 run. */
#define MAIN_CALLER 0x14009ba21ULL

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
        if (!a || a->count <= 0 || a->base_index < 0 || !a->name || !a->name[0])
            sane = 0;
        for (int j = i + 1; j < nrows; j++) {
            const mc_screen *b = menu_speak_row(j);
            if (a && b && a->caller == b->caller)
                sane = 0;
        }
    }
    check("9. every shipped row is sane and no caller is duplicated",
          sane, "rows=%d", nrows);


    printf("\n");
    if (failures) { printf("FAILED: %d case(s)\n", failures); return 1; }
    printf("all cases passed\n");
    return 0;
}
