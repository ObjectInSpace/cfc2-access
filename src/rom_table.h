/*
 * The shell's ROM information table -- the game TITLES as real text.
 *
 * ⭐⭐ WHY THIS EXISTS, AND WHAT IT CORRECTS
 * I concluded twice that the eight game titles on the Select Game screen were
 * logo IMAGES, because they have no clean run in the GMD -- the only place a
 * title appears in msg.arc is inside ACHIEVEMENT_*_DESC sentences. That was
 * true about the GMD and WRONG about the game: the titles live in the exe's
 * own data as an array of ROM records, with the code, the full title, and the
 * region/date string.
 *
 * ⚠ The lesson: "not in the text file" is not "not in the program". I stopped
 * at the first place I knew how to look.
 *
 * THE LAYOUT, read out of the unpacked image
 * ------------------------------------------
 * An array of 40-byte records at ROM_TABLE, and an INDEX table of pointers to
 * them at ROM_INDEX, null-terminated:
 *
 *   record +0x00  const char *code     "cvs1", "cvs1u", ...
 *          +0x08  const char *title    "Capcom vs. SNK: Millennium Fight..."
 *          +0x10  const char *region   "(Japan 010502)"
 *          +0x18  const char *         (empty string)
 *          +0x20  int                  a field the shell's accessor returns
 *
 * 18 entries, in JAPANESE/US PAIRS -- 8 games x 2 regions, plus the two
 * Street Fighter Zero 3 Upper "EX" variants at 16 and 17.
 *
 * ⭐ The pairing is what explains the Select Game screen: its Game row has 8
 * values (the games) and its Game Version row has 2 (this game's JP and US
 * ROM). So the natural mapping is rom_index = 2 * game + version.
 *
 * ⛔⛔ THAT MAPPING IS A HYPOTHESIS, NOT A MEASUREMENT, and this header is the
 * place it is written down as such. It is consistent with the table's pair
 * ordering AND with the game_00..game_70 archive numbering, but consistency is
 * not confirmation -- this project has been burned four times by a model that
 * fit the evidence it was built from. Nothing is SPOKEN from it until a run
 * shows the logged title matching the game the user is actually on.
 */
#ifndef CFC2_ROM_TABLE_H
#define CFC2_ROM_TABLE_H

#include <windows.h>

/* Live addresses. ASLR is off and the exe has no relocations, so these are
 * valid in every run -- the same property the cursor hooks rely on.
 * ⚠ Both are VERIFIED at runtime before use (rom_table_ready), because an
 * address that is merely remembered is an assumption. */
#define ROM_INDEX_ADDR  0x141208b00ULL   /* null-terminated pointer array */
#define ROM_TABLE_ADDR  0x141208830ULL   /* the first record */
#define ROM_RECORD_SIZE 40

/* How many ROM records the live table holds, or 0 if it could not be read.
 * ⭐ A return value, not a logged warning: "could not read" must be
 * distinguishable from "read and found nothing". */
int rom_table_count(void);

/* Copy ROM record `i`'s title (and optionally its code and region) out of the
 * live image. Returns FALSE if the index is out of range or any read failed.
 * All reads are guarded; nothing is cached across calls. */
BOOL rom_table_title(int i, char *out, unsigned cap);
BOOL rom_table_code(int i, char *out, unsigned cap);
BOOL rom_table_region(int i, char *out, unsigned cap);

/* ⭐ The HYPOTHESIS, isolated in one function so it has exactly one place to
 * be corrected. Maps a Select Game cursor position to a ROM index.
 * Returns -1 when the result would be out of range. */
int rom_table_index_for(int game, int version);

/* ⭐⭐ Is `s` one of the titles the exe's ROM table holds? Compared
 * case-INSENSITIVELY: the shell displays both "Capcom Vs. SNK" and "Capcom vs.
 * SNK" for the same game (both appear in the 2026-09-10 17:32 log), so an exact
 * compare would name a game on one screen and refuse it on another.
 *
 * Used to corroborate a live caption on a game-title strip: the caption says
 * what the game DREW, this says the exe agrees it is a real game title. Two
 * independent sources, which is the standard every shipped row here meets.
 *
 * ⚠ Returns FALSE unless rom_table_ready() -- without the identity check this
 * would compare against whatever sits at those addresses. */
BOOL rom_table_has_title(const char *s);

/* Is the table where we think it is? Checks that record 0's code is "cvs1"
 * and that the index table's first pointer targets it -- a positive identity
 * check, not merely a readable address. */
BOOL rom_table_ready(void);

/* Test-only: point the reader at a synthetic table. The shipping code never
 * calls this; passing 0 restores the real addresses. */
void rom_table_test_redirect(ULONGLONG index_addr, ULONGLONG table_addr);

#endif /* CFC2_ROM_TABLE_H */
