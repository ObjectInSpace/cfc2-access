/*
 * The in-game MOVE LIST: name the focused move.
 *
 * ⭐⭐⭐ SOLVED FROM THE DISASSEMBLY, not from what is on screen. The vertical
 * redraw at 0x1400803d0 does, per drawn row:
 *
 *     move_id = [screen + row*4 + 0x354]
 *     record  = game_table[ [screen+0x330] ] + 12 * move_id
 *     NAME    = gmd_text_by_index( record[+4] )
 *
 * ⛔⛔ WHY THE OBVIOUS ROUTE IS CLOSED. The draw loop clamps to TEN rows
 * (`mov ecx,0xa` / `cmp r8d,ecx` / `cmovge` at 0x14008040f-23) while the real
 * item count lives at +0x3d8. So a coordinate join that orders the visible
 * name objects and indexes them by the cursor's total CANNOT work once a list
 * exceeds ten: measured, names != total in 0 of 17 tight pairings. Two other
 * routes are closed with evidence too -- GMD order is not screen order (13 of
 * 19 captured windows were neither a prefix nor a contiguous slice), and the
 * per-category totals do not partition a character's moves (Akuma's observed
 * totals sum to 26 against 19 corpus moves). See docs/move-list-solved.md.
 *
 * ⭐⭐ THIS ROUTE IS LANGUAGE-SAFE AND GAME-AGNOSTIC BY CONSTRUCTION. The
 * record yields a GMD INDEX, and gmd_text_by_index() reads whichever language
 * table is live -- so no per-game and no per-region name tables exist to drift,
 * the JP/US naming question never arises, and one path covers all 1658 move
 * names across 183 characters in 9 game tags.
 *
 * ⚠ EVERY read here is of game memory at an address derived from other game
 * memory, so every step is range-checked and guarded. A move_id past the end
 * of its table would resolve to a wrong GMD index and speak a confidently
 * wrong move name, which is the failure this project exists to prevent.
 */
#ifndef CFC2_MOVE_LIST_H
#define CFC2_MOVE_LIST_H

#include <windows.h>

/* The per-game move-table pointer array. EXACTLY EIGHT valid pointers, then
 * the array ends -- the collection's eight games, in order:
 *   0 CVS1  1 CVS2  2 CFJ  3 Z3U  4 PWS1  5 PWS2  6 MJUS  7 STG2
 * Identified by the CMD_NAME_<TAG> family of the labels their records point
 * at; tables 2-7 are unambiguous. */
#define ML_GAME_TABLES   0x141205d30ULL
#define ML_GAME_COUNT    8

/* Screen-object fields, from the redraw's own instructions. */
#define ML_OFF_GAME      0x330    /* which game (0..7) */
#define ML_OFF_IDS       0x354    /* array of per-row move IDs */
#define ML_OFF_CURSOR    0x3d4    /* the SELECTED row */
#define ML_OFF_TOTAL     0x3d8    /* the REAL item count */

/* 12-byte records; +4 is the GMD index. +0 and +8 are not identified. */
#define ML_RECORD_SIZE   12
#define ML_REC_OFF_NAME  4

/* Why a lookup declined. ⭐ A REASON, not a bare FALSE: "the screen is not a
 * move list" and "the tables are not where we think" are different facts, and
 * a caller asserting only failure cannot tell them apart -- the same reasoning
 * behind kbd_install_why() and mc_named_why. */
typedef enum {
    ML_OK = 0,
    ML_NO_GMD,            /* the live message table is not resolved yet */
    ML_BAD_SCREEN,        /* screen pointer unreadable */
    ML_BAD_GAME,          /* the game index is not 0..7 */
    ML_BAD_TABLE,         /* the game's table pointer failed its checks */
    ML_BAD_ROW,           /* the row is outside the screen's own item count */
    ML_BAD_ID,            /* the move id failed its range check */
    ML_BAD_INDEX,         /* record's GMD index is out of the table's range */
    ML_NO_TEXT            /* the index resolved to nothing */
} ml_why;

/* Name the move at `row` (0-based) of the move list on `screen`.
 *
 * ⚠ `screen` is the cursor helper's own screen pointer. Unlike everywhere else
 * in this project it IS dereferenced here -- that is the whole mechanism -- so
 * it is range-checked with mem_readable_run() before every read and the copies
 * are fault-guarded.
 *
 * Returns TRUE and fills `out` only on ML_OK. */
BOOL ml_move_name(const void *screen, int row, char *out, int outlen);
ml_why ml_move_name_why(void);

/* Is this screen shaped like a move list at all? ⭐ A POSITIVE identity check,
 * not a readability test: the game index must be 0..7, its table pointer must
 * land in the image, and the item count must be sane. Readability alone proves
 * nothing -- the whole image is readable. */
BOOL ml_screen_ready(const void *screen);

/* Test seams: point the module at a synthetic tables array and image bounds so
 * the record walk and every range check can be exercised without the game.
 * ⚠ Passing tables = NULL restores the shipping constant. */
void ml_test_set_tables(const void *tables, const void *lo, const void *hi);

#endif /* CFC2_MOVE_LIST_H */
