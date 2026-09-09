/*
 * Turning a cursor movement into speech -- see menu_speak.h for the rules.
 */

#include "menu_speak.h"
#include "gmd_find.h"
#include "rom_table.h"
#include "memsafe.h"
#include "speech.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

/* --- The screen table ----------------------------------------------------
 *
 * ⭐ Each entry is a MEASURED fact, not a guess: `caller` comes from a live
 * run's log, `base_index`/`count` from the GMD in file order.
 *
 * ⚠ ONE entry so far. That is deliberate -- the main menu is the only screen
 * observed in a run to date, and adding rows for screens nobody has watched
 * would be exactly the "confident wrong name" failure this file exists to
 * prevent. Unknown screens speak POSITION ONLY, which is always true.
 *
 * To add a row: run the game, walk into a menu, and read the
 * "mc: [DISCOVERY]" line from the log. It prints the caller and the count.
 * Then find the label run in msg.arc (tools/dump_menu_eng.py) and add it here.
 */
/* ⭐⭐ Select Game's three row labels, by cursor index.
 *
 * GMD 51 "Game", 52 "Game Version", 56 "Player". NOT contiguous -- 53..55 and
 * 58..61 are those rows' VALUES -- which is precisely why this screen needs a
 * map and not a base index. Order is the cursor's order, top to bottom, as the
 * hook reports it. */
static const int g_select_game_rows[] = { 51, 52, 56 };

/* ⭐⭐ Select Game's VALUE labels, per row, keyed by the LIVE count.
 *
 * ⛔ Row 0 (Game) is deliberately ABSENT: the eight game titles have no clean
 * run in the GMD -- they occur only inside ACHIEVEMENT_*_DESC text -- so they
 * are drawn as logo images. ⛔ THAT WAS WRONG -- see rom_table.h: the titles
 * are in the EXE, as a ROM table. Row 0 still has no variant here, but for a
 * DIFFERENT reason: the cursor-index -> ROM-index mapping is not yet confirmed.
 * A row with no variant here names nothing, which is
 * the correct outcome, not a gap to be filled with the nearest-looking run.
 *
 * Row 1, Game Version. Labels: 53 GAME_VERSION_J "Japanese Version",
 * 54 GAME_VERSION_E "English Version", 55 GAME_VERSION_ANY "Either".
 * ⭐ "Either" is a LOBBY SEARCH FILTER, so offline the live list is J/E only --
 * which is exactly the count of 2 measured in the 23:03 run. The 3-value
 * variant is NOT listed: it has never been observed, and an unobserved variant
 * is a guess. If an online screen ever shows 3, it will name nothing and log a
 * mismatch, which is how it should be found.
 *
 * Row 2, Player. Labels: 58 PLAYER_1P "P1" .. 61 PLAYER_4P "P4".
 * ⭐ P3/P4 exist for the 4-player games (Power Stone 2, Project Justice); a
 * 2-player game offers P1/P2, the count of 2 measured in the run. Both the
 * 2-value and 4-value variants start at 58, so the 4-value case is a PREFIX
 * extension, not a different list -- but it is still listed explicitly rather
 * than inferred, so that a count of 3 (which no game should produce) refuses
 * instead of quietly reading three of the four.
 * ⚠ The 4-value row is NOT yet confirmed live -- see MC_VALUES_UNCONFIRMED. */
static const mc_value_set g_select_game_values[] = {
    { 1, 2, 53 },   /* Game Version: Japanese / English  -- MEASURED */
    { 2, 2, 58 },   /* Player: P1 / P2                   -- MEASURED */
    { 2, 4, 58 },   /* Player: P1..P4 on 4-player games  -- PREDICTED */
};

static const mc_screen g_screens[] = {
    /* ⛔⛔ REMOVED: { 0x14009ba21, 37, 6, "Main menu" }.
     *
     * This row was added from the FIRST run, before the discovery mechanism
     * existed, on the reasoning that it was the only 6-item vertical screen
     * seen. That was a guess, and it was WRONG -- it made a different 6-item
     * screen speak the main menu's labels ("Quit, 6 of 6", "Offline Play,
     * 1 of 6") while the real main menu, 0x140097a59, was in use seconds
     * earlier in the same run.
     *
     * ⭐ Exactly the failure this file's rules exist to prevent, committed by
     * me in this file. The row that survived scrutiny (0x140097a59) was the one
     * established by READING LIVE TEXT; the one that had to be retracted was
     * the one justified by item count and plausibility.
     *
     * 0x14009ba21 is now unknown again, so it speaks position only and logs a
     * discovery block -- which is what will identify it properly. */

    /* ⭐ CONFIRMED 2026-09-08 by live text, not by guessing: at this call site
     * the mod read base 37 item 6 as "Quitter" while the user stood on
     * position 6 of 6, in a French-language run. That is EXIT/FR -- the main
     * menu's last item.
     *
     * ⚠ It is a DIFFERENT function from the row above (0x1400973d0 vs
     * 0x14009b920), so these are two distinct screens that present the same six
     * choices. Both legitimately map to base 37; neither is a duplicate to be
     * deleted. */
    { 0x140097a59ULL, 37, 6, "Main menu", MC_VERIFIED_LIVE_TEXT, MC_LIST, 0,
      NULL, 0, NULL, 0 },

    /* ⭐ Options. CONFIRMED 2026-09-08 by live text: standing on position 5 of
     * 5, base 170 read "Credits" (French "Credits"), which is MENU_OP_CREDITS
     * -- the last of Systeme / Audio / Reseau / Options PC / Credits.
     *
     * ⚠ The rival candidate was base 37, whose item 5 is "Options" -- and the
     * user had just SELECTED Options to get here, so the word alone would have
     * been convincing. The item COUNT settles it: this screen has 5 items, the
     * main menu has 6. That is why the count is stored and checked, not merely
     * recorded. */
    { 0x1400a20fcULL, 170, 5, "Options", MC_VERIFIED_LIVE_TEXT, MC_LIST, 0,
      NULL, 0, NULL, 0 },

    /* --- Select Game (from Offline Play), fn 0x1400b1b10 -----------------
     *
     * ⛔⛔ THIS SCREEN WAS MISNAMED "Game settings" AND IT COST A RUN.
     * I gave `0x1400b1b69` that name in the same session that found the
     * row/value STRUCTURE, on nothing but its sharing a function with the
     * value site. The structure was right; the identity was a guess, and it
     * shipped. The user entered Offline Play and heard "setting 2 of 3" --
     * a confident claim about the screen that was simply false, and WORSE
     * than the bare "2 of 3" it replaced.
     *
     * ⭐ What it actually is, from the GMD (labels 50..66): GAME_SELECT
     * "Select Game" at 50, then three ROWS -- GAME_TITLE 51 "Game",
     * GAME_VERSION 52 "Game Version", PLAYER 56 "Player" -- with each row's
     * VALUES in the gaps between them (53..55 Japanese/English/Either,
     * 58..61 P1..P4). The HELP twins at 62, 63 and 66 confirm the grouping.
     * Row 0 showing EIGHT values matches the collection's eight games.
     *
     * ⭐⭐ 51, 52, 56 is exactly the non-contiguous case: no base+index can
     * address it. That is what `labels` exists for. */
    { 0x1400b1b69ULL, -1, 3, "Select Game", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_select_game_rows, 3, NULL, 0 },

    /* The value site of the same screen. ⚠ count 0 -- one call site drives all
     * three rows' value lists, whose lengths differ (8 games; the version and
     * player lists were both observed at 2, shorter than the 3 and 4 labels
     * that exist, so the live list is context-dependent). A fixed count would
     * reject two rows out of three.
     *
     * ⛔ NO labels here, deliberately. The row labels are known; the VALUE
     * labels are not. The eight game titles do not exist as a clean run in the
     * GMD at all -- the only occurrences are inside achievement descriptions --
     * so the game names are almost certainly drawn as logo IMAGES.
     * ⛔⛔ WRONG, corrected 2026-09-08: they are in the EXE's own data
     * (src/rom_table.h, 18 records at 0x141208b00). "Not in the GMD" was a
     * fact about the GMD; "they are images" was a claim I had no evidence for.
     * Row 0 is still unnamed because the INDEX MAPPING is unconfirmed, not
     * because the text is missing. Speaking a
     * value here would be a guess. It reports the row it belongs to, which the
     * row label then names, and the value's POSITION. */
    { 0x1400b1bd9ULL, -1, 0, "Select Game", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x338, NULL, 0,
      g_select_game_values,
      (int)(sizeof(g_select_game_values) / sizeof(g_select_game_values[0])) },
};

#define NSCREENS (int)(sizeof(g_screens) / sizeof(g_screens[0]))

int menu_speak_screen_count(void) { return NSCREENS; }

const mc_screen *menu_speak_row(int i)
{
    if (i < 0 || i >= NSCREENS)
        return NULL;
    return &g_screens[i];
}

/* Find the MC_SETTINGS_ROW entry belonging to the same screen as `v`.
 *
 * ⭐ Matched on the screen NAME, so the two halves of one screen (the row
 * selector and the value selector) stay linked through one fact rather than a
 * second hand-copied address that could drift. */
const mc_screen *menu_speak_sibling_row(const mc_screen *v)
{
    if (!v || !v->name)
        return NULL;
    for (int i = 0; i < NSCREENS; i++) {
        if (g_screens[i].kind == MC_SETTINGS_ROW &&
            g_screens[i].name && strcmp(g_screens[i].name, v->name) == 0)
            return &g_screens[i];
    }
    return NULL;
}

/* The value-label variant for `row` whose count matches the LIVE count, or
 * NULL. ⭐ Matching on the count is what makes an unmeasured list refuse
 * itself rather than borrow another row's labels. */
const mc_value_set *menu_speak_values_for(const mc_screen *s, int row,
                                          int live_count)
{
    if (!s || !s->values || row < 0 || live_count <= 0)
        return NULL;
    for (int i = 0; i < s->nvalues; i++) {
        if (s->values[i].row == row && s->values[i].count == live_count)
            return &s->values[i];
    }
    return NULL;
}

const mc_screen *menu_speak_lookup(ULONGLONG caller)
{
    for (int i = 0; i < NSCREENS; i++) {
        if (g_screens[i].caller == caller)
            return &g_screens[i];
    }
    return NULL;
}

/* ⭐⭐ THE ROW DERIVATION -- see the header for why this is arithmetic and not
 * remembered state.
 *
 * ⚠ Neither pointer is DEREFERENCED here. They are subtracted as values. That
 * matters: `screen` is caller-supplied and this project has already killed the
 * game once by trusting such a pointer, so the rule is that `screen` is never
 * read. Subtracting two addresses reads neither. */
BOOL menu_speak_derive_row(const void *screen, const void *cursor,
                           int values_off, int max_row, int *row)
{
    if (!screen || !cursor || !row)
        return FALSE;

    /* ⚠ Compare as integers, not as pointers: `cursor` may legitimately be
     * below `screen` on a screen we have modelled wrongly, and pointer
     * subtraction on unrelated objects is undefined.
     *
     * ⭐⭐ ONE bound, not two. A separate "is the cursor below the array?"
     * test was written first, in both unsigned and signed form, and BOTH were
     * proven unnecessary by sabotage: deleting either left the entire suite
     * green, because a cursor below the base yields a delta of ~2^63 which the
     * single range check below rejects on its own. That is not defence in
     * depth -- it is one property written twice, and the cost is that neither
     * copy can be tested. So the range check is the whole guard, and case 11's
     * `below` sub-case exercises it through that one path. */
    LONGLONG base = (LONGLONG)(ULONG_PTR)screen + (LONGLONG)values_off;
    LONGLONG cur  = (LONGLONG)(ULONG_PTR)cursor;
    ULONGLONG delta = (ULONGLONG)(cur - base);
    if (delta % 4u != 0)
        return FALSE;                      /* misaligned: not an int array */

    ULONGLONG r = delta / 4u;
    /* ⭐⭐ THE BOUND -- and the ONLY one, per the note above. Without it a
     * screen laid out differently would still yield SOME row number, and a
     * plausible-looking wrong row is exactly the failure mode this design
     * refuses. It rejects three things at once: a row past the end of a known
     * screen, a row absurdly far out, and (via the wraparound) a cursor sitting
     * BELOW the array entirely.
     *
     * ⚠ `max_row == 0` means "the row count is not known here" -- the ceiling
     * then falls back to MC_MAX_PLAUSIBLE_ROWS rather than to no ceiling at
     * all, because "unknown count" must not mean "accept anything". */
    ULONGLONG ceiling = (max_row > 0) ? (ULONGLONG)max_row
                                      : (ULONGLONG)MC_MAX_PLAUSIBLE_ROWS;
    if (r >= ceiling)
        return FALSE;

    *row = (int)r;
    return TRUE;
}

void menu_speak_build(const mc_move *m, mc_speech *out)
{
    memset(out, 0, sizeof(*out));

    /* Position is ALWAYS available and always true: it comes from arguments,
     * not from any table. Compute it first so every path has it. */
    out->position = m->new_index - m->min_index + 1;
    out->total    = m->max_index - m->min_index + 1;

    const mc_screen *s = menu_speak_lookup((ULONGLONG)(ULONG_PTR)m->caller);
    if (!s) {
        out->why = MC_UNNAMED_SCREEN_UNKNOWN;
        return;                        /* unknown screen -> position only */
    }

    /* ⭐⭐ THE STALENESS CHECK. If the live item count does not match what the
     * table expects, this screen is not the screen the row describes -- a game
     * update reordered a menu, or the caller was reused. Refuse the row rather
     * than index into the wrong label run. */
    /* ⭐ count == 0 means "this screen's item count VARIES legitimately", which
     * is true of a settings screen's value site: the same call site drives an
     * 8-value row and two 2-value rows. Such a row never claims a label, so
     * there is nothing for a stale count to corrupt -- checking it would only
     * reject every movement. Every LABELLED row still has a real count and is
     * still checked. */
    if (s->count > 0 && out->total != s->count) {
        out->why = MC_UNNAMED_COUNT_MISMATCH;
        static BOOL warned;
        if (!warned) {
            warned = TRUE;
            log_line("mcspeak: ⚠ screen \"%s\" expects %d items but this one "
                     "has %d -- NOT using the label table for it. Speaking "
                     "position only, which is still correct.",
                     s->name, s->count, out->total);
        }
        return;
    }

    _snprintf(out->screen_name, sizeof(out->screen_name), "%s", s->name);
    out->screen_name[sizeof(out->screen_name) - 1] = '\0';

    /* --- settings screens ------------------------------------------------
     * ⭐⭐ These are NOT flat lists (see the header): a settings screen has
     * setting ROWS, each with its own list of VALUES. base+index cannot name
     * either half, so both kinds report position with the correct FRAMING and
     * leave `named` FALSE until a verified label map exists.
     *
     * ⭐ The framing is the whole benefit available today: "row 2 of 3" and
     * "value 3 of 8" are both TRUE statements telling a blind user which axis
     * they just moved on -- which a bare "3 of 8" did not. */
    if (s->kind == MC_SETTINGS_ROW) {
        out->axis = MC_AXIS_ROW;
        /* ⭐⭐ THE MAP. A settings/select screen's row labels are scattered
         * through the GMD (Select Game: 51, 52, 56), so the label comes from
         * an explicit per-index map, never from base+index -- which would land
         * on a VALUE label in the gaps between them. */
        if (!s->labels) {
            out->why = MC_UNNAMED_NO_LABELS;
            return;
        }
        /* ⚠ The map must cover this screen exactly. A map shorter than the
         * live list would name some rows and silently mis-name the rest, so a
         * disagreement disables the map rather than indexing part of it. */
        int idx = m->new_index - m->min_index;
        if (s->nlabels != out->total || idx < 0 || idx >= s->nlabels) {
            out->why = MC_UNNAMED_COUNT_MISMATCH;
            return;
        }
        char text[128];
        if (!gmd_text_by_index(s->labels[idx], text, sizeof(text)) || !text[0]) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        _snprintf(out->item, sizeof(out->item), "%s", text);
        out->item[sizeof(out->item) - 1] = '\0';
        out->named = TRUE;
        return;
    }
    if (s->kind == MC_SETTINGS_VALUE) {
        out->axis = MC_AXIS_VALUE;
        /* ⭐ Derive which row this value belongs to, purely from the pointers
         * the hook was handed. A failure here is REPORTED, never rounded. */
        int row = 0;
        if (menu_speak_derive_row(m->screen, m->cursor, s->values_off,
                                  0, &row)) {
            out->row = row;
            out->have_row = TRUE;
            out->why = MC_UNNAMED_NO_LABELS;
            /* ⭐⭐ Name the ROW this value belongs to, from the SIBLING row
             * entry's map. "Game Version, English Version" is what a user
             * needs; "setting 2, value 2 of 2" is not.
             *
             * ⚠ The sibling is found by screen NAME, so the two halves of one
             * screen stay linked without a second hand-maintained address.
             * The VALUE itself is still not named -- see the table row. */
            const mc_screen *sib = menu_speak_sibling_row(s);
            if (sib && sib->labels && row >= 0 && row < sib->nlabels) {
                char text[128];
                if (gmd_text_by_index(sib->labels[row], text, sizeof(text)) &&
                    text[0]) {
                    _snprintf(out->row_name, sizeof(out->row_name), "%s", text);
                    out->row_name[sizeof(out->row_name) - 1] = '\0';
                }
            }
            /* ⭐⭐ NAME THE VALUE ITSELF, when this row has a variant matching
             * the LIVE count: "Game Version, English Version" rather than
             * "Game Version, 2 of 2".
             *
             * ⛔ Keyed on the count, because the live list is a SUBSET of the
             * labels that exist -- "Either" is an online lobby filter, and
             * P3/P4 belong to the 4-player games. No matching variant means no
             * name, which is the right answer: an unrecognised count is a list
             * nobody has measured, and indexing the nearest one would speak a
             * value that is not on the screen. */
            /* ⭐⭐ ROW 0: THE GAME NAME, from the exe's ROM table.
             *
             * CONFIRMED 2026-09-09. The user walked all eight slots and the
             * predicted titles came out in the collection's own order --
             * CVS1, CVS2, CFJ, SFA3 Upper, Power Stone, Power Stone 2,
             * Project Justice, Plasma Sword. Eight of eight, in sequence, is
             * not a coincidence a wrong mapping could produce.
             *
             * ⭐ Speaks the JAPANESE-side title (version 0), which is the
             * table's first entry for each game. ⚠ For three games the two
             * regions have DIFFERENT names -- "Capcom Fighting Jam" vs
             * "Evolution", "Zero 3 Upper" vs "Alpha 3 Upper", "Moero! Justice
             * Gakuen" vs "Project Justice", "Star Gladiator 2" vs "Plasma
             * Sword" -- so which one is right depends on the Game Version row
             * beside it, and reading that row is not yet wired up. The JP-side
             * name is a true name for the game either way; refining it to
             * follow the version selection is the obvious next step. */
            if (row == 0 && rom_table_ready()) {
                int game = m->new_index - m->min_index;
                int ri = rom_table_index_for(game, 0);
                char gt[128];
                if (ri >= 0 && rom_table_title(ri, gt, sizeof(gt)) && gt[0]) {
                    _snprintf(out->item, sizeof(out->item), "%s", gt);
                    out->item[sizeof(out->item) - 1] = '\0';
                    out->named = TRUE;
                    out->why = MC_NAMED_OK;
                }
                return;
            }

            const mc_value_set *vs = menu_speak_values_for(s, row, out->total);
            if (vs) {
                int vidx = m->new_index - m->min_index;
                char vtext[128];
                if (vidx >= 0 && vidx < vs->count &&
                    gmd_text_by_index(vs->first + vidx, vtext, sizeof(vtext)) &&
                    vtext[0]) {
                    _snprintf(out->item, sizeof(out->item), "%s", vtext);
                    out->item[sizeof(out->item) - 1] = '\0';
                    out->named = TRUE;
                    out->why = MC_NAMED_OK;
                } else {
                    out->why = MC_UNNAMED_TEXT_UNREADABLE;
                }
            }
        } else {
            out->why = MC_UNNAMED_ROW_UNDERIVABLE;
        }
        return;
    }

    char text[128];
    if (!gmd_text_by_index(s->base_index + (m->new_index - m->min_index),
                           text, sizeof(text)) || !text[0]) {
        out->why = MC_UNNAMED_TEXT_UNREADABLE;
        return;                        /* could not read -> position only */
    }

    _snprintf(out->item, sizeof(out->item), "%s", text);
    out->item[sizeof(out->item) - 1] = '\0';
    out->named = TRUE;
}

void menu_speak(const mc_move *m)
{
    mc_speech sp;
    menu_speak_build(m, &sp);

    /* ⚠ The item NAME leads, then the position. A screen reader user wants the
     * word first; the position is context, not the headline. */
    char line[256];
    /* ⭐ A named VALUE reads as "row name, value name" -- "Game Version,
     * English Version". The row name is the context a blind user needs in
     * order to know what the value is a value OF, and it leads for the same
     * reason the item name leads elsewhere. */
    if (sp.named && sp.axis == MC_AXIS_VALUE && sp.row_name[0])
        _snprintf(line, sizeof(line), "%s, %s, %d of %d",
                  sp.row_name, sp.item, sp.position, sp.total);
    else if (sp.named)
        _snprintf(line, sizeof(line), "%s, %d of %d",
                  sp.item, sp.position, sp.total);
    /* ⭐⭐ Settings screens: say which AXIS moved. Without this, moving Right
     * on the settings screen said "3 of 8" -- indistinguishable from having
     * moved to the third of eight menu items, when in fact the user changed
     * the focused setting to its third value. Both forms below are true with
     * no label knowledge at all. */
    else if (sp.axis == MC_AXIS_ROW)
        /* ⛔ Unnamed row: say POSITION only. It used to say "setting %d of %d",
         * which asserts the screen is a settings screen -- and that claim was
         * WRONG on the first screen it met (Select Game, reached from Offline
         * Play). A bare position is true whatever the screen turns out to be. */
        _snprintf(line, sizeof(line), "%d of %d", sp.position, sp.total);
    else if (sp.axis == MC_AXIS_VALUE && sp.row_name[0])
        /* ⭐ The useful form: "Game Version, 2 of 2" -- the row NAMED, the
         * value's position given. The value text itself is not claimed. */
        _snprintf(line, sizeof(line), "%s, %d of %d",
                  sp.row_name, sp.position, sp.total);
    else if (sp.axis == MC_AXIS_VALUE && sp.have_row)
        _snprintf(line, sizeof(line), "row %d, %d of %d",
                  sp.row + 1, sp.position, sp.total);
    else if (sp.axis == MC_AXIS_VALUE)
        _snprintf(line, sizeof(line), "%d of %d", sp.position, sp.total);
    else
        _snprintf(line, sizeof(line), "%d of %d", sp.position, sp.total);
    line[sizeof(line) - 1] = '\0';

    wchar_t w[256];
    int n = MultiByteToWideChar(CP_UTF8, 0, line, -1, w,
                                (int)(sizeof(w) / sizeof(w[0])));
    if (n <= 0) {
        /* ⭐ A conversion failure must not speak mojibake. Say nothing and log,
         * rather than emit bytes the reader will render as noise. */
        log_line("mcspeak: could not convert \"%s\" to UTF-16; not speaking.",
                 line);
        return;
    }

    /* interrupt=TRUE: the user has moved on, so a queued previous item is
     * stale. Holding a direction must not build a backlog to read out. */
    /* ⚠ The log says what ACTUALLY happened. Writing `mcspeak: "..."`
     * unconditionally is what let a completely silent mod look healthy in the
     * log for several runs. */
    if (speech_say(w, TRUE))
        log_line("mcspeak: spoke \"%s\"", line);
    else
        log_line("mcspeak: ⛔ NOT SPOKEN (backend refused): \"%s\"", line);
}


/* ⛔ REMOVED: opt_discovery_enabled() and the cfc2_discover.on gate.
 *
 * The gate existed because an early sweep cost 4.8 s on the poll thread. That
 * cost was the per-byte guarded reader (fixed in 56fa641); the gate outlived
 * the bug and then did real harm, because with the file absent -- the normal
 * state -- the only mechanism that can name a screen never ran at all.
 *
 * ⭐ The general shape: a guard added for a measured cost must be re-measured
 * when the cost is fixed, or it silently becomes a feature switch that is
 * always off. Measure, then delete the guard; do not keep it "just in case",
 * because a diagnostic nobody runs is a diagnostic that does not exist. */

/* ⭐⭐ THE GAME-NAME PROBE -- LOGGED, NEVER SPOKEN.
 *
 * The Select Game screen's Game row has no value labels in the GMD, but the
 * exe carries a ROM table with the real titles (src/rom_table.h). The mapping
 * from the cursor's 0..7 to a ROM index is a HYPOTHESIS -- rom = 2*game +
 * version, from the table's JP/US pairing -- and this prints what the
 * hypothesis WOULD say, so a run's log can be compared against the game
 * actually on screen.
 *
 * ⛔⛔ IT TAKES THE RESTING POSITION, NOT A MOVEMENT, AND THAT IS THE POINT.
 * The first version hung off menu_speak(), which only runs on a CHANGE -- so
 * it could never describe the item the user was already sitting on. Confirming
 * the FIRST game meant leaving it and coming back, and the report then named
 * whatever they had moved to. The user pointed out that this measures the
 * wrong thing, and they were right.
 *
 * ⛔ Still never spoken until the mapping is confirmed. */
void menu_speak_probe_rest(const mc_move *m)
{
    if (!m || !m->screen)
        return;

    const mc_screen *s = menu_speak_lookup((ULONGLONG)(ULONG_PTR)m->caller);
    if (!s || s->kind != MC_SETTINGS_VALUE)
        return;

    int row = 0;
    if (!menu_speak_derive_row(m->screen, m->cursor, s->values_off, 0, &row))
        return;
    if (row != 0)
        return;
    if (!rom_table_ready())
        return;

    int game  = m->new_index - m->min_index;
    int total = m->max_index - m->min_index + 1;

    char t0[128] = "", t1[128] = "", c0[16] = "", c1[16] = "";
    int i0 = rom_table_index_for(game, 0);
    int i1 = rom_table_index_for(game, 1);
    if (i0 >= 0) { rom_table_title(i0, t0, sizeof(t0));
                   rom_table_code(i0, c0, sizeof(c0)); }
    if (i1 >= 0) { rom_table_title(i1, t1, sizeof(t1));
                   rom_table_code(i1, c1, sizeof(c1)); }

    log_line("mcspeak: [ROMPROBE] RESTING on cursor index %d (position %d of "
             "%d) -- IF the mapping rom=2*game+version holds, this is \"%s\" "
             "[%s] / \"%s\" [%s]. ⚠ NOT SPOKEN: compare against the game "
             "actually on screen before trusting it.",
             game, game + 1, total, t0, c0, t1, c1);
}

void menu_speak_log_unknown(const mc_move *m)
{
    /* ⭐ DISCOVERY MODE. Print exactly what a new table row needs, so the next
     * run's log IS the table rather than another round of guessing.
     *
     * ⚠ Logged ONCE per caller: these fire on every movement, and a flood
     * would bury the distinct set that is the actual finding. */
    enum { MAX_SEEN = 64 };
    static void *seen[MAX_SEEN];
    static int   nseen;
    static BOOL  overflowed;

    for (int i = 0; i < nseen; i++) {
        if (seen[i] == m->caller)
            return;
    }
    if (nseen >= MAX_SEEN) {
        if (!overflowed) {
            overflowed = TRUE;
            log_line("mcspeak: [DISCOVERY] more than %d distinct screens seen; "
                     "no longer recording new ones. That is more menus than "
                     "the game is believed to have -- treat as suspicious.",
                     MAX_SEEN);
        }
        return;
    }
    seen[nseen++] = m->caller;

    int count = m->max_index - m->min_index + 1;
    log_line("mcspeak: [DISCOVERY] unknown screen -- caller=0x%llx  "
             "items=%d  axis=%s  (screen=%p).",
             (unsigned long long)(ULONG_PTR)m->caller, count,
             m->vertical ? "vertical" : "horizontal", m->screen);

    /* ⭐ The caller and the item COUNT are everything an offline tool needs to
     * propose a label run: menu labels sit in contiguous GMD runs, so
     * tools/find_label_runs.py <count> lists every run of that length that also
     * has a parallel HELP_ run -- the structural signature of a menu.
     *
     * ⚠ The screens do NOT name themselves: there is no identifying constant
     * near any call site (checked). And matching a caller to a run by COUNT
     * alone is the guess this design refuses -- two 6-item menus appeared in one
     * run. So the tool proposes candidates and a person confirms which screen
     * they were actually in; nothing is ever SPOKEN from a candidate. */
    /* ⭐⭐ WHAT THE LOG NEEDS IN ORDER TO NAME THIS SCREEN.
     *
     * The count alone cannot identify it (23 candidate label runs exist for a
     * 2-item menu, 7 for a 6-item one), and asking the user to narrate which
     * menu they were in is the wrong measurement -- it depends on them
     * remembering, and a blind user cannot glance at the screen to check.
     *
     * So print the LIVE text of the item the user is standing on, for a window
     * of candidate bases around each plausible run. Whoever reads the log then
     * matches words to a screen directly, in whatever language the game is set
     * to -- evidence, not recall.
     *
     * ⚠ Logged only, NEVER spoken: a candidate is a hypothesis, and speaking
     * one would be exactly the confident-wrong-name failure this file exists to
     * prevent. */
    /* \u2b50\u2b50 DISCOVERY RUNS ONCE PER SCREEN, OFF THE POLL PATH, AND CHEAPLY.
     *
     * Three versions of this have now cost the user runs, and the lesson is the
     * same each time: this is a DEBUG AID, and it was starving the thing it was
     * meant to help.
     *
     *   v1  a hand-written list of candidate bases -- missed any screen whose
     *       labels I had not anticipated.
     *   v2  swept every base testing ONE item -- 3144 matches, useless.
     *   v3  swept every base testing the WHOLE RUN -- better candidates, but it
     *       scanned ~1100 bases x 5 lookups and took 4.8 SECONDS on the poll
     *       thread while the user was still navigating. And the 40 results it
     *       printed included "\u2605" and "Stun:", so the cost bought very little.
     *
     * \u26d4 The real problem with v3 was never the per-lookup cost -- it was doing
     * the work AT ALL on the thread that has to stay responsive. So:
     *   - it is gated behind cfc2_discover.on, absent by default, so an ordinary
     *     session never pays for it;
     *   - it stops at 8 candidates rather than 40, because a list needing to be
     *     read by a person is useless past a handful;
     *   - and it is capped at 600 bases, which covers every menu label in this
     *     game (the highest real menu run is ~590) without sweeping stats and
     *     move-list text that can never be a menu.
     *
     * \u26a0 Everything here is LOGGED, NEVER SPOKEN. A candidate is a hypothesis. */
    /* ⭐⭐ ON BY DEFAULT, because the cost that justified the gate is GONE.
     *
     * This sweep was gated behind cfc2_discover.on after a version of it took
     * 4.8 SECONDS on the poll thread. That cost was the per-byte guarded
     * reader, fixed in 56fa641 -- and the gate outlived the bug.
     *
     * ⛔ The consequence was worse than the freeze it prevented: with the file
     * absent (the normal state) discovery NEVER RAN, so a run that visited 21
     * unnamed screens produced 21 "unknown screen" lines and not one candidate.
     * The only mechanism that can name a screen was off in every ordinary
     * session, and coverage could not grow. Three screens named in a week is
     * the result.
     *
     * ⭐ MEASURED, not estimated -- against the real 4165-string blob, on the
     * shipping code path (tools/bench_discovery.c): 0.10 ms for a 2-item menu,
     * 0.64 ms for 8 items, 0.82 ms worst case at 40 and 67 items. Under 1 ms
     * against a 100 ms poll, and it runs ONCE PER SCREEN, not per movement.
     *
     * ⚠ I have twice estimated this kind of cost by arithmetic and been wrong
     * both times. This number came from running it. */
    if (gmd_text_count() > 0) {
        int off = m->new_index - m->min_index;
        int total = gmd_text_count();
        if (total > 600)
            total = 600;
        int shown = 0;
        char batch[512];
        size_t used = 0;
        batch[0] = '\0';

        for (int base = 0; base + count < total && shown < 8; base++) {
            char cur[128];
            int ok = 1;
            for (int i = 0; i < count && ok; i++) {
                if (!gmd_text_by_index(base + i, cur, sizeof(cur)) || !cur[0]) {
                    ok = 0; break;
                }
                size_t L = strlen(cur);
                if (cur[0] == '<' || L > 32 ||
                    cur[L - 1] == '.' || cur[L - 1] == '?' || cur[L - 1] == '!')
                    ok = 0;
            }
            if (!ok)
                continue;
            /* ⭐⭐ REQUIRE A PARALLEL HELP RUN -- one blurb per item, not
             * merely one sentence after the run.
             *
             * The old test accepted any run of short strings followed by a
             * single sentence, and a run's log showed what that is worth: a
             * 6-item menu drew EIGHT candidates, nearly all unrelated
             * ("First to 5", "Player 3", "Connection Strength"). A list of
             * eight guesses is not evidence a person can act on.
             *
             * Menus in this GMD carry one HELP_ blurb per item, in the same
             * order -- verified on all three known menus (main 37..42,
             * Options 170..174, pause 250..256), each of which passes this.
             * ⭐ MEASURED effect on the shipped data: candidates drop from 8
             * to 4 for a 6-item menu, and to exactly ONE for counts of 8, 9
             * and 10. Checked against every known-real menu first -- main,
             * Options, pause, online modes, lobby entry -- all five survive.
             *
             * ⚠⚠ BUT UNDERSTAND WHAT WAS DISCARDED, because I first called it
             * "noise" and that was wrong. The eight candidates for a 6-item
             * menu were every one of them a COHERENT contiguous run: the
             * Victory Condition values (First to 5..10), the Number of Players
             * values, the lobby player slots, the audio settings block,
             * Connection Strength and its values. The filter was not failing;
             * the GMD genuinely contains many runs of this shape, because
             * menus and VALUE LISTS share it.
             *
             * ⛔ So this narrows an AMBIGUITY, it does not remove noise -- and
             * a screen whose labels lack per-item blurbs will now be filtered
             * OUT. That is a real cost, accepted because a list of eight is
             * unusable, not because the discarded entries were junk. The
             * "no candidate" message says so explicitly. */
            for (int i = 0; i < count && ok; i++) {
                if (!gmd_text_by_index(base + count + i, cur, sizeof(cur)) ||
                    !cur[0]) {
                    ok = 0; break;
                }
                size_t HL = strlen(cur);
                if (cur[HL - 1] != '.' && cur[HL - 1] != '?' &&
                    cur[HL - 1] != '!')
                    ok = 0;
            }
            if (!ok)
                continue;
            if (!gmd_text_by_index(base + off, cur, sizeof(cur)))
                continue;
            int k = _snprintf(batch + used, sizeof(batch) - used,
                              "%s%d:%s", used ? "  " : "", base, cur);
            if (k > 0 && (size_t)k < sizeof(batch) - used)
                used += (size_t)k;
            shown++;
        }
        /* ⚠⚠ A SURVIVING CANDIDATE IS STILL NOT A CONFIRMATION.
         *
         * The filter cannot tell a real run from a SLIDE of one: on the
         * shipped data, bases 247, 248 and 249 all survive for counts of 10,
         * 9 and 8, because they are the same pause-menu run starting one label
         * earlier each time. Only one of them is the menu; the others begin on
         * "Medium Kick" and "Heavy Kick".
         *
         * ⭐ Which is why the LIVE TEXT of the item the user is standing on is
         * printed beside each base, and why a person still has to match it to
         * the screen. That comparison is the confirmation; this list is only
         * the shortlist. */
        if (used)
            log_line("mcspeak: [DISCOVERY]   position %d of %d; candidate "
                     "bases (each shows what item %d would be called -- ⚠ "
                     "neighbouring bases can be SLIDES of one real run, so "
                     "match the text to the screen): %s",
                     off + 1, count, off + 1, batch);
        else
            log_line("mcspeak: [DISCOVERY]   position %d of %d; no candidate "
                     "run found in the first %d labels. ⚠ This screen's labels "
                     "may simply lack the one-blurb-per-item shape this looks "
                     "for -- absence here is NOT evidence the labels are "
                     "missing.", off + 1, count, total);
    }

    /* ⭐ Emit a row in the CURRENT shape, including the fields added for
     * settings screens. An older version of this line printed a 4-field row
     * that no longer compiles -- a discovery hint that produces a broken row is
     * worse than none, because it is followed. */
    log_line("mcspeak: [DISCOVERY]   -> to name it, add "
             "{ 0x%llx, <base>, %d, \"<name>\", MC_VERIFIED_LIVE_TEXT, "
             "MC_LIST, 0 } to g_screens (src/menu_speak.c) using "
             "tools/find_label_runs.py %d",
             (unsigned long long)(ULONG_PTR)m->caller, count, count);

    /* ⭐⭐ IS THIS A SETTINGS SCREEN? Say so, because the answer changes which
     * KIND of row to write, and getting it wrong means naming a VALUE where a
     * setting belongs.
     *
     * The tell needs no table and no dereference: on a settings screen the
     * value cursor sits inside an ARRAY at a fixed offset from the screen
     * object, so a horizontal site whose cursor is NOT the same distance from
     * `screen` every time is a per-row value list. Here we can only report the
     * distance seen on THIS movement -- a person comparing two lines for one
     * caller sees it move. ⚠ A hint, explicitly not a conclusion: nothing is
     * ever spoken from it. */
    if (m->screen && m->cursor) {
        LONGLONG off = (LONGLONG)(ULONG_PTR)m->cursor
                     - (LONGLONG)(ULONG_PTR)m->screen;
        log_line("mcspeak: [DISCOVERY]   cursor sits at screen+0x%llx on this "
                 "movement. ⚠ If this OFFSET CHANGES between movements at the "
                 "same caller, it is a settings VALUE list (row + per-row "
                 "values), NOT a flat menu -- use MC_SETTINGS_VALUE with "
                 "values_off set to the SMALLEST offset seen, and give it NO "
                 "base index. See memory cfc2-settings-screen-shape.",
                 (unsigned long long)off);
    }

    /* ⛔⛔ REMOVED: a dump of screen+0x340 as "option IDs".
     *
     * That field IS an option-ID array on caller 0x1400b2729, read off its
     * machine code. It is NOT one anywhere else, and printing it for every
     * screen produced exactly what generalising from one sample deserves:
     * [741845392], [1090385120], [1015004208] -- pointers and padding,
     * formatted as if they were menu semantics.
     *
     * ⭐ The lesson is this project's oldest one, committed again: a field
     * offset read out of ONE function's instructions describes that function.
     * Applying it to 21 other screens is the memory-scanning mistake wearing a
     * disassembly costume. If a per-screen field is ever wanted again, it must
     * come from a per-screen TABLE ROW, never from a constant offset applied
     * everywhere. */

}
