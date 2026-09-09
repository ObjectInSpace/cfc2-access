/*
 * Turning a cursor movement into speech.
 *
 * THE THREE PIECES, and where each came from
 * ------------------------------------------
 *  1. WHICH SCREEN  -- the return address of the cursor-helper call
 *     (menu_cursor.h). 68 call sites in 37 distinct functions, one per menu.
 *  2. WHICH ITEM    -- the cursor index and range, both arriving as arguments.
 *  3. WHAT IT SAYS  -- gmd_text_by_index(). Menu item labels sit in CONTIGUOUS
 *     index runs in the GMD, so a screen's BASE index plus the cursor index
 *     names the item. Verified on three menus (memory cfc2-menu-label-runs).
 *
 * ⛔⛔ THE RULE THIS FILE EXISTS TO ENFORCE
 * A wrong item name is WORSE THAN SILENCE for a blind user: it is indistinguish-
 * able from a right one, so it cannot be caught and corrected the way a sighted
 * player would catch a mislabelled screen. Therefore:
 *   - a screen is spoken by NAME only if its caller address is in the table;
 *   - an unknown screen falls back to POSITION ONLY ("item 3 of 6"), which is
 *     true regardless of which menu it is;
 *   - the item count must MATCH the table entry, or the entry is not used --
 *     that is the check that catches a stale table after a game update;
 *   - if the text cannot be read, position is still spoken, never a guess.
 *
 * ⛔⛔ A KNOWN LIMIT OF THE base+index MODEL -- SETTINGS SCREENS.
 *
 * It assumes a screen's items are a CONTIGUOUS run of GMD labels. That holds
 * for the menus verified so far (main menu 37..42, Options 170..174, pause
 * 250..256) and it does NOT hold for settings screens.
 *
 * Measured: the game-settings screen (caller 0x1400b1bd9, fn 0x1400b1b10,
 * EIGHT items) draws from the OPT_ family, where setting NAMES are interleaved
 * with their VALUE labels -- OPT_DIFFICULTY 643, OPT_DAMAGE_LEVEL 644, then
 * 645..648 are its four values, OPT_TIMER_SPEED 649, and so on. The names sit
 * at 643, 644, 649, 654, 659, 660, 661, 662, 665, 1108, 1115, 1125. No base
 * plus offset can address them, and WHICH eight appear depends on the game
 * selected.
 *
 * ⭐ So such a screen cannot be named by a g_screens row at all. It needs a
 * per-screen index->label MAP, not a base. Do not try to force a base for one:
 * any base that "looks close" will speak a value label where an item name
 * belongs -- confidently and wrongly.
 *
 * ⚠ Keying on item count alone was considered and REJECTED: several menus share
 * a count, so it would produce confident wrong names. The caller address is
 * exact.
 */
#ifndef CFC2_MENU_SPEAK_H
#define CFC2_MENU_SPEAK_H

#include <windows.h>
#include "menu_cursor.h"

/* One known screen: which call site, and where its labels start. */
/* ⛔⛔ EVERY ROW MUST RECORD HOW IT WAS ESTABLISHED, and only one answer is
 * acceptable in a shipped row.
 *
 * A row added on plausibility -- "the only 6-item vertical screen we have seen"
 * -- shipped, and made a DIFFERENT 6-item screen speak the main menu's labels.
 * It also SUPPRESSED that screen's discovery block, so the evidence that would
 * have identified it never appeared. A wrong row is worse than a missing one
 * twice over: it lies, and it hides the fix.
 *
 * The rule: a row exists only if a live-text discovery block showed the item
 * the user was standing on. VERIFIED_LIVE_TEXT is the only value that ships. */
typedef enum {
    MC_VERIFIED_LIVE_TEXT = 1,  /* a discovery block matched the real item */
} mc_verified;

typedef struct {
    ULONGLONG caller;      /* the cursor-helper call site inside this screen */
    int       base_index;  /* GMD index of item 0 */
    int       count;       /* items expected; a mismatch DISABLES the entry */
    const char *name;      /* screen name, for the log and for context speech */
    mc_verified how;       /* how this row was established -- see above */
} mc_screen;

/* What a movement should say. */
/* Why an item was NOT named. ⭐⭐ This is a reported REASON, not a bare flag,
 * for the same reason kbd_install_why() exists: the outcome "no name" arrives
 * by several routes, and a test asserting only the outcome cannot tell which
 * one produced it. Measured: with no live GMD table, the text lookup fails
 * anyway -- so deleting the STALENESS CHECK entirely left the whole suite
 * green. Exposing the reason is what makes that check testable. */
typedef enum {
    MC_NAMED_OK = 0,           /* the item was named */
    MC_UNNAMED_SCREEN_UNKNOWN, /* caller not in the table */
    MC_UNNAMED_COUNT_MISMATCH, /* table row exists but the count disagrees */
    MC_UNNAMED_TEXT_UNREADABLE /* screen known, count matched, read failed */
} mc_named_why;

typedef struct {
    BOOL  named;               /* did we resolve an item NAME? */
    mc_named_why why;          /* if not, WHICH guard declined */
    char  item[128];           /* the item text, if named */
    char  screen_name[64];     /* the screen, if known */
    int   position;            /* 1-based, for "item 3 of 6" */
    int   total;
} mc_speech;

/* Build the announcement for a movement. Always fills `position`/`total`;
 * sets `named` only when the screen is known AND the text was read. */
void menu_speak_build(const mc_move *m, mc_speech *out);

/* Build it and speak it. */
void menu_speak(const mc_move *m);

/* Log a movement from an UNKNOWN screen in the exact form needed to add it to
 * the table -- caller address, count, axis. ⭐ This is the discovery mode: the
 * next run's log becomes the table, instead of another guessing round. */
void menu_speak_log_unknown(const mc_move *m);

/* Test seam: how many screens the table holds, and lookup by caller. */
int  menu_speak_screen_count(void);
const mc_screen *menu_speak_lookup(ULONGLONG caller);

/* Test-only: row i of the shipped table, or NULL. Lets the suite check the
 * hand-edited rows for typos and duplicate callers. */
const mc_screen *menu_speak_row(int i);

#endif /* CFC2_MENU_SPEAK_H */
