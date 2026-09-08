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
    /* Main menu. Caller 0x14009ba21 (fn 0x14009b920), confirmed in the
     * 2026-09-08 run: 6 items, range 0..5, wrapping both ways.
     * Labels: GMD 37..42 = OFFLINE_PLAY ONLINE_PLAY MUSEUM FIGHTER_AWARD
     * OPTIONS EXIT ("Offline Play" ... "Quit"). */
    { 0x14009ba21ULL, 37, 6, "Main menu" },
};

#define NSCREENS (int)(sizeof(g_screens) / sizeof(g_screens[0]))

int menu_speak_screen_count(void) { return NSCREENS; }

const mc_screen *menu_speak_lookup(ULONGLONG caller)
{
    for (int i = 0; i < NSCREENS; i++) {
        if (g_screens[i].caller == caller)
            return &g_screens[i];
    }
    return NULL;
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
    if (out->total != s->count) {
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
    speech_say(w, TRUE);
    log_line("mcspeak: \"%s\"", line);
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

    log_line("mcspeak: [DISCOVERY] unknown screen -- caller=0x%llx  "
             "items=%d  axis=%s  (screen=%p). To name it: find its label run "
             "in msg.arc and add { 0x%llx, <base>, %d, \"<name>\" } to "
             "g_screens in src/menu_speak.c.",
             (unsigned long long)(ULONG_PTR)m->caller,
             m->max_index - m->min_index + 1,
             m->vertical ? "vertical" : "horizontal",
             m->screen,
             (unsigned long long)(ULONG_PTR)m->caller,
             m->max_index - m->min_index + 1);
}
