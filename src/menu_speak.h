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
 * ⚠ Keying on item count alone was considered and REJECTED: several menus share
 * a count, so it would produce confident wrong names. The caller address is
 * exact.
 */
#ifndef CFC2_MENU_SPEAK_H
#define CFC2_MENU_SPEAK_H

#include <windows.h>
#include "menu_cursor.h"

/* One known screen: which call site, and where its labels start. */
typedef struct {
    ULONGLONG caller;      /* the cursor-helper call site inside this screen */
    int       base_index;  /* GMD index of item 0 */
    int       count;       /* items expected; a mismatch DISABLES the entry */
    const char *name;      /* screen name, for the log and for context speech */
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

#endif /* CFC2_MENU_SPEAK_H */
