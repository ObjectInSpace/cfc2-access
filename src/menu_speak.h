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
 * ⭐⭐ WHAT A SETTINGS SCREEN ACTUALLY IS (read off fn 0x1400b1b10; memory
 * cfc2-settings-screen-shape). It is NOT a flat list. It is a set of setting
 * ROWS, each row carrying its OWN list of values:
 *
 *     0x1400b1b53  lea rdi,[rbx+0x330]          ; the ROW cursor
 *     0x1400b1b3d  mov r9d,[rbx+0x334]          ; the ROW count
 *     0x1400b1b69  call <vertical>              ; move between rows
 *
 *     0x1400b1b9f  movsxd rax,[rdi]             ; rax = the current ROW
 *     0x1400b1ba2  lea rcx,[rbx+0x338]          ; base of the VALUE-cursor array
 *     0x1400b1bbf  mov r9d,[rbx+rax*4+0x348]    ; value count FOR THIS ROW
 *     0x1400b1bc7  lea rdx,[rcx+rax*4]          ; &value_cursor[row]
 *     0x1400b1bd9  call <horizontal>            ; change that row's value
 *
 * So the VERTICAL site moves the row and the HORIZONTAL site changes a value.
 * ⛔⛔ An earlier note read the horizontal site as "an 8-item screen". It is
 * not: those eight are row 0's eight VALUES. Speaking base+index there would
 * read out a VALUE label where an item name belongs.
 *
 * ⭐⭐ AND THE ROW IS DERIVABLE, NOT REMEMBERED. Because the value cursor is
 * `screen + values_off + 4*row`, the hook can recover the row from its OWN
 * arguments by arithmetic:  row = (cursor - screen - values_off) / 4.
 * That is why this needs no state shared between the two hooks and no
 * assumption about which fires first -- and why a bad case is DETECTABLE
 * (a misaligned or out-of-range result is refused, not rounded).
 * Confirmed against a live run: 22 movements, zero exceptions.
 *
 * ⚠ The label side is still NOT solved: which setting a row names depends on
 * the game selected, and the OPT_ family interleaves setting names with their
 * value labels (names at 643, 644, 649, 654, 659, 660, 661, 662, 665, 1108,
 * 1115, 1125). So a settings screen ships with NO labels until a live-text
 * discovery block proves them -- it speaks the row position only.
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


    /* ⛔ MC_USER_ACCEPTED_UNPROVEN was added and REMOVED on 2026-09-10.
     *
     * It existed for one row -- the consent dialog -- shipped with guessed
     * labels after the user approved doing so. The user then said they were
     * not confident in those labels, and they were right: the screen sets no
     * item text, so nothing had ever shown those words on it.
     *
     * ⭐ Kept as a note because the temptation will recur: an escape hatch
     * for "unproven but probably right" is exactly how VERIFIED_LIVE_TEXT
     * stops meaning anything. If it is needed again, the question to answer
     * first is what would DISPROVE the guess -- not who approved it. */
} mc_verified;

/* What SHAPE of list a call site drives.
 *
 * ⭐ Measured offline over all 68 cursor call sites, by how each computes the
 * cursor pointer it passes: 54 pass a fixed `lea [reg+imm]` (a flat list) and
 * 14 pass an INDEXED `lea [reg+reg*4]` (a per-row value list). So the shape is
 * knowable from the code for every site, without visiting the screen. */
typedef enum {
    MC_LIST = 0,      /* a flat list: base_index + index names the item */
    MC_SETTINGS_ROW,  /* the VERTICAL site of a settings screen: moves rows */
    MC_SETTINGS_VALUE,/* the HORIZONTAL site: changes the focused row's value */
    /* ⭐⭐ A list whose items name THEMSELVES in live text, numbered on screen.
     *
     * The music player is the first: its five tracks are "01. Title: Take Your
     * Best Shot" and so on. They are NOT in the GMD -- track titles are not
     * message labels -- so no base and no map can ever name them, and the
     * caption channel cannot either (the list's text is set once when the screen
     * opens and cursor movement sets nothing).
     *
     * The item is named by asking the live instance set for the object whose
     * text starts with THIS position's number, so the game's own numbering and
     * the cursor's index must agree. No `base_index`, no `labels`. */
    MC_NUMBERED_LIVE
} mc_kind;

typedef struct {
    ULONGLONG caller;      /* the cursor-helper call site inside this screen */
    int       base_index;  /* GMD index of item 0 */
    int       count;       /* items expected; a mismatch DISABLES the entry */
    const char *name;      /* screen name, for the log and for context speech */
    mc_verified how;       /* how this row was established -- see above */
    mc_kind   kind;        /* flat list, or half of a settings screen */
    /* For MC_SETTINGS_VALUE only: the offset of the value-cursor ARRAY within
     * the screen object, so the focused row can be derived by arithmetic.
     * 0x338 for fn 0x1400b1b10. ⚠ Read from the DISASSEMBLY of the specific
     * screen, never assumed -- other settings screens index different fields. */
    int       values_off;
    /* ⭐⭐ THE index -> label MAP. This is what makes a screen whose labels are
     * NOT a contiguous run addressable at all.
     *
     * The Select Game screen's three rows are GMD 51 (Game), 52 (Game Version)
     * and 56 (Player) -- 51, 52, 56, with each row's VALUES sitting in the gaps
     * (53..55 are the versions, 58..61 the player numbers). No base plus offset
     * reaches those three, which is exactly why base_index could not name this
     * screen and why the mod said "2 of 3" on it.
     *
     * ⛔ A row carrying a map must NEVER also fall back to base+index: the
     * whole point is that base+index lands on a VALUE label here ("English
     * Version" where "Game Version" belongs). `labels != NULL` therefore
     * REPLACES the base, and base_index stays -1 on such a row. */
    const int *labels;     /* GMD index per item, or NULL */
    int        nlabels;    /* entries in `labels`; must equal `count` */
    /* ⭐⭐ VALUE labels, for MC_SETTINGS_VALUE: one variant per row, chosen by
     * the row's LIVE count. See mc_value_set. NULL when the values cannot be
     * named (Select Game's row 0). ⚠ Not because the titles do not exist --
     * they are in the exe, see rom_table.h -- but because the mapping from
     * cursor index to ROM index is not yet confirmed against a real screen. */
    const struct mc_value_set *values;
    int        nvalues;    /* entries in `values` */
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
    MC_UNNAMED_TEXT_UNREADABLE,/* screen known, count matched, read failed */
    /* ⭐ Settings-screen specific. The row is derived by arithmetic from the
     * cursor and screen pointers; when that arithmetic does not land cleanly
     * -- a misaligned difference, or a row outside 0..count-1 -- the answer is
     * REFUSED rather than rounded into a plausible-looking row. A wrong row
     * would name the wrong setting, which is the failure this file exists to
     * prevent. */
    MC_UNNAMED_ROW_UNDERIVABLE,
    /* ⭐ The screen's SHAPE is known but its LABELS are not. Distinct from
     * SCREEN_UNKNOWN: we do know what this screen is and can say "row 2 of 3"
     * meaningfully; we simply have no verified label for the row. Kept as its
     * own reason so a run's log shows which settings screens still need a
     * discovery pass. */
    MC_UNNAMED_NO_LABELS
} mc_named_why;

/* ⭐⭐ One row's value labels, keyed by how many values are LIVE.
 *
 * The Select Game screen's value lists are shorter than the label sets that
 * exist for them, because some labels are online-only or game-dependent:
 *   - Game Version has THREE labels (Japanese / English / Either) but "Either"
 *     is a lobby-search filter; offline the list is J/E, and the live count
 *     was measured at 2.
 *   - Player has FOUR (P1..P4) but only the 4-player games (Power Stone 2,
 *     Project Justice) offer P3/P4; the live count was measured at 2.
 *
 * ⛔ So the labels CANNOT be indexed directly -- the live list is a subset, and
 * which subset depends on the game and on online/offline. Keying each variant
 * on its `count` makes the match checkable: a count with no matching variant
 * names nothing rather than indexing a list that does not apply.
 *
 * ⚠ `count` is the LIVE number of values, and `first` the GMD index the list
 * starts at. Variants are only ever added from a MEASURED count. */
typedef struct mc_value_set {
    int row;      /* which row of the screen these values belong to */
    int count;    /* the live value count this variant applies to */
    int first;    /* GMD index of value 0 */
} mc_value_set;

/* Which axis a movement was on -- so the readout can say what the user
 * actually did. ⭐ On a settings screen "3 of 8" is ambiguous between "the
 * third setting" and "the third value of this setting"; these disambiguate it
 * without needing any label. */
typedef enum {
    MC_AXIS_ITEM = 0,  /* an ordinary menu item */
    MC_AXIS_ROW,       /* a settings ROW (which setting is focused) */
    MC_AXIS_VALUE      /* a settings VALUE (what that setting is set to) */
} mc_axis;

typedef struct {
    BOOL  named;               /* did we resolve an item NAME? */
    mc_named_why why;          /* if not, WHICH guard declined */
    mc_axis axis;              /* item / settings row / settings value */
    int   row;                 /* MC_AXIS_VALUE: which row, 0-based */
    BOOL  have_row;            /* was `row` successfully derived? */
    char  item[128];           /* the item text, if named */
    char  row_name[64];        /* MC_AXIS_VALUE: the name of the row this value
                                * belongs to ("Game Version"), if known */
    char  screen_name[64];     /* the screen, if known */
    int   position;            /* 1-based, for "item 3 of 6" */
    int   total;
} mc_speech;

/* Build the announcement for a movement. Always fills `position`/`total`;
 * sets `named` only when the screen is known AND the text was read. */
void menu_speak_build(const mc_move *m, mc_speech *out);

/* Build it and speak it. */
void menu_speak(const mc_move *m);

/* Report what the ROM table would name the game the cursor is RESTING on.
 * Logged, never spoken -- the index mapping is not yet confirmed. */
void menu_speak_probe_rest(const mc_move *m);

/* Log a movement from an UNKNOWN screen in the exact form needed to add it to
 * the table -- caller address, count, axis. ⭐ This is the discovery mode: the
 * next run's log becomes the table, instead of another guessing round. */
void menu_speak_log_unknown(const mc_move *m);

/* ⭐⭐ Derive which setting ROW is focused, from the hook's own arguments.
 *
 * The value cursor of a settings screen lives at `screen + values_off + 4*row`,
 * so the row is recoverable by arithmetic -- no state, no ordering assumption
 * between the vertical and horizontal hooks, and no dereference of either
 * pointer (both are compared as VALUES only, never read).
 *
 * Returns TRUE and writes *row on success. Returns FALSE when the difference is
 * negative, not a multiple of 4, or lands outside 0..max_row-1 -- which is the
 * point: a settings screen we have modelled WRONGLY fails loudly here instead
 * of naming an arbitrary row.
 *
 * ⚠ `max_row` bounds the answer. Pass the row count when it is known; pass 0
 * when it is not, which falls back to MC_MAX_PLAUSIBLE_ROWS -- "unknown count"
 * must not mean "accept any row". */
/* ⭐ The ceiling used when a screen's row count is not known. No settings
 * screen in this shell comes close: the one measured has 3 rows, and the
 * largest cursor list seen anywhere is the 40-slot character select. 64 is
 * loose enough never to reject a real row and tight enough that a wrong model
 * -- which lands in the millions via wraparound -- is always refused. */
#define MC_MAX_PLAUSIBLE_ROWS 64
BOOL menu_speak_derive_row(const void *screen, const void *cursor,
                           int values_off, int max_row, int *row);

/* The value-label variant for `row` matching the LIVE value count, or NULL. */
const mc_value_set *menu_speak_values_for(const mc_screen *s, int row,
                                          int live_count);

/* The MC_SETTINGS_ROW entry of the same screen as a value entry, or NULL --
 * so a value can name the row it belongs to. */
const mc_screen *menu_speak_sibling_row(const mc_screen *v);

/* Test seam: how many screens the table holds, and lookup by caller. */
int  menu_speak_screen_count(void);
const mc_screen *menu_speak_lookup(ULONGLONG caller);

/* ⭐⭐ Look a screen up by caller AND live item count.
 *
 * ONE CALLER CAN SERVE SEVERAL SCREENS -- measured, not anticipated.
 * 0x14009ba21 ships as "Select Mode" (3 items) and is ALSO the Online Play
 * menu (6 items); across two runs it appeared 15 times, always at 6. The
 * generic cursor helper is shared between menus, so the call site is not a
 * unique screen identity.
 *
 * A row with count > 0 matches only when the live count agrees. A row with
 * count <= 0 (a deliberately unnamed map-only row) still matches on caller
 * alone, so existing behaviour is unchanged.
 *
 * ⛔ The count DISCRIMINATES between verified rows; it never admits an
 * unverified one. Every row still needs MC_VERIFIED_LIVE_TEXT. */
const mc_screen *menu_speak_lookup_n(ULONGLONG caller, int item_count);

/* Enable the DISCOVERY candidate sweep. OFF by default.
 *
 * ⛔ It is a development aid, not a user feature, and it stalled a live session
 * twice (37 s, then 11.9 s) while having named exactly ZERO shipped screens --
 * every g_screens row cites live text, the disassembly, or the caption
 * channel. Unknown screens are still LOGGED when it is off; only the expensive
 * candidate hunt is gated.
 *
 * ⭐ For naming screens, prefer the caption channel: tools/name_screens.py
 * reproduces three independently verified bases from an ordinary run's log. */
void menu_speak_set_discovery(BOOL on);

/* Is the sweep currently enabled? Exists so the suite can assert the DEFAULT
 * directly.
 *
 * ⚠ Asserting it by TIMING menu_speak_log_unknown() looked reasonable and was
 * vacuous: the offline harness has no live GMD, so the sweep exits early on
 * `gmd_text_count() > 0` whatever the gate says, and the case passed with the
 * gate deleted. Test the state, not a side effect that another guard also
 * produces. (memory: harness-supplies-the-mechanism) */
BOOL menu_speak_discovery_enabled(void);

/* Test seam: does a captured string look like a SETTING VALUE?
 *
 * ⚠ On a value axis the mod speaks the captured text WITHOUT corpus
 * verification -- "Off" occurs 22 times in the GMD and "1024x768" not at all,
 * so identification is impossible and unnecessary: it is a QUOTE of what the
 * game displayed, not a CLAIM about which row you are on. This predicate is
 * the only thing keeping prose or a button strip out of that path. */
BOOL menu_speak_test_value_shaped(const char *s);

/* Test seam: would this caption be suppressed as the SCREEN's rather than the
 * row's? A caption that does not change as the cursor moves describes the
 * screen, and repeating it after every movement is noise -- Fighter Awards sets
 * one sentence for all 40 grid positions. Keeps static state, so drive it as a
 * SEQUENCE. */
BOOL menu_speak_test_caption_repeat(const char *cap, const char *screen_name);

/* Test-only: row i of the shipped table, or NULL. Lets the suite check the
 * hand-edited rows for typos and duplicate callers. */
const mc_screen *menu_speak_row(int i);

#endif /* CFC2_MENU_SPEAK_H */
