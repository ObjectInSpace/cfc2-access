/*
 * Turning a cursor movement into speech -- see menu_speak.h for the rules.
 */

#include "menu_speak.h"
#include "gmd_find.h"
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
    { 0x140097a59ULL, 37, 6, "Main menu", MC_VERIFIED_LIVE_TEXT, MC_LIST, 0 },

    /* ⭐ Options. CONFIRMED 2026-09-08 by live text: standing on position 5 of
     * 5, base 170 read "Credits" (French "Credits"), which is MENU_OP_CREDITS
     * -- the last of Systeme / Audio / Reseau / Options PC / Credits.
     *
     * ⚠ The rival candidate was base 37, whose item 5 is "Options" -- and the
     * user had just SELECTED Options to get here, so the word alone would have
     * been convincing. The item COUNT settles it: this screen has 5 items, the
     * main menu has 6. That is why the count is stored and checked, not merely
     * recorded. */
    { 0x1400a20fcULL, 170, 5, "Options", MC_VERIFIED_LIVE_TEXT, MC_LIST, 0 },

    /* --- the per-game settings screen, fn 0x1400b1b10 --------------------
     *
     * ⭐⭐ SHAPE MEASURED, LABELS NOT. Both rows below carry base_index -1 and
     * are NEVER named: they exist to give the movement its correct FRAMING
     * ("setting 2 of 3" / "setting 1, value 3 of 8") rather than the bare and
     * misleading "3 of 8" this screen used to produce.
     *
     * ⚠ `how` is VERIFIED_LIVE_TEXT for the SHAPE: the 22:46 run walked this
     * screen and the log's cursor addresses match the disassembly's array
     * exactly (row 0 -> +0x338, row 1 -> +0x33c, row 2 -> +0x340, 22 movements,
     * zero exceptions). No LABEL is claimed by either row, so the provenance
     * rule -- never ship a label that live text has not shown -- is not being
     * bent here; there is simply no label to ship yet.
     *
     * ⛔ Do NOT give these a base_index until a discovery pass proves one.
     * Which setting each row names depends on the GAME selected, and the OPT_
     * family interleaves setting names with their value labels, so any base
     * that "looks close" will read out a value where a name belongs. */
    { 0x1400b1b69ULL, -1, 3, "Game settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0 },
    /* ⚠ count is 0 -- this site's item count is the VALUE count of whichever
     * row is focused (8, 2 and 2 were all observed at this one call site), so
     * a fixed expected count would be wrong for two rows out of three. 0 means
     * "do not check the count", which is safe here precisely BECAUSE the row
     * never claims a label. */
    { 0x1400b1bd9ULL, -1, 0, "Game settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x338 },
};

#define NSCREENS (int)(sizeof(g_screens) / sizeof(g_screens[0]))

int menu_speak_screen_count(void) { return NSCREENS; }

const mc_screen *menu_speak_row(int i)
{
    if (i < 0 || i >= NSCREENS)
        return NULL;
    return &g_screens[i];
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
        out->why  = MC_UNNAMED_NO_LABELS;
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
    if (sp.named)
        _snprintf(line, sizeof(line), "%s, %d of %d",
                  sp.item, sp.position, sp.total);
    /* ⭐⭐ Settings screens: say which AXIS moved. Without this, moving Right
     * on the settings screen said "3 of 8" -- indistinguishable from having
     * moved to the third of eight menu items, when in fact the user changed
     * the focused setting to its third value. Both forms below are true with
     * no label knowledge at all. */
    else if (sp.axis == MC_AXIS_ROW)
        _snprintf(line, sizeof(line), "setting %d of %d",
                  sp.position, sp.total);
    else if (sp.axis == MC_AXIS_VALUE && sp.have_row)
        _snprintf(line, sizeof(line), "setting %d, value %d of %d",
                  sp.row + 1, sp.position, sp.total);
    else if (sp.axis == MC_AXIS_VALUE)
        _snprintf(line, sizeof(line), "value %d of %d",
                  sp.position, sp.total);
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


/* Is discovery turned on? Create `cfc2_discover.on` next to the exe.
 *
 * \u26d4 OFF BY DEFAULT because discovery is a debug aid that runs on the poll
 * thread, and three versions of it have starved the mod during real play. A
 * diagnostic must not be able to degrade the thing it diagnoses in an ordinary
 * session. */
static BOOL opt_discovery_enabled(void)
{
    static int cached = -1;
    if (cached >= 0)
        return cached != 0;
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { cached = 0; return FALSE; }
    char *slash = strrchr(path, '\\');
    if (!slash) { cached = 0; return FALSE; }
    slash[1] = '\0';
    strncat(path, "cfc2_discover.on", MAX_PATH - strlen(path) - 1);
    cached = (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES);
    return cached != 0;
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
    if (gmd_text_count() > 0 && opt_discovery_enabled()) {
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
            if (!gmd_text_by_index(base + count, cur, sizeof(cur)) || !cur[0])
                continue;
            size_t L = strlen(cur);
            if (cur[L - 1] != '.' && cur[L - 1] != '?' && cur[L - 1] != '!')
                continue;
            if (!gmd_text_by_index(base + off, cur, sizeof(cur)))
                continue;
            int k = _snprintf(batch + used, sizeof(batch) - used,
                              "%s%d:%s", used ? "  " : "", base, cur);
            if (k > 0 && (size_t)k < sizeof(batch) - used)
                used += (size_t)k;
            shown++;
        }
        if (used)
            log_line("mcspeak: [DISCOVERY]   position %d of %d; candidate "
                     "bases: %s", off + 1, count, batch);
        else
            log_line("mcspeak: [DISCOVERY]   position %d of %d; no candidate "
                     "run found in the first %d labels.", off + 1, count, total);
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
}
