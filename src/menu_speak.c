/*
 * Turning a cursor movement into speech -- see menu_speak.h for the rules.
 */

#include "menu_speak.h"
#include "gmd_find.h"
#include "rom_table.h"
#include "roster_table.h" /* the 8 per-game rosters, generated */
#include "soundtrack_table.h" /* Museum > Music's 9 soundtracks, generated */
#include "memsafe.h"
#include "speech.h"
#include "log.h"
#include "gui_text.h"     /* the live caption channel */
#include "text_channel.h" /* shared speech + read-current-text */
#include "mod_text.h"     /* the mod's own words, in the game's language */
#include "gui_markup.h"   /* markup -> speech */

/* Discovery-sweep string classification -- see the sweep in
 * menu_speak_log_unknown(). Each string is judged ONCE and the window test
 * becomes array arithmetic.
 *
 * ⚠ The ceiling is a fixed array, not an allocation, because this runs on the
 * poll thread and a failed HeapAlloc there is a worse outcome than a bounded
 * table. The shipped GMD holds 4165 strings; 8192 leaves headroom without
 * pretending the number is unbounded, and the classification loop clamps to it
 * rather than writing past the end. */
#define GMD_MAX_STRINGS 8192
#define CLS_ITEM  0x01   /* short, no markup, no terminal punctuation */
#define CLS_BLURB 0x02   /* ends in . ? ! -- a HELP-style sentence     */
#include "prompt_id.h"    /* which label produced a captured string */
#include "move_list.h"    /* the in-game move list's own indirection */
#include "help_line.h"    /* the help line, as the message the game sets */

#include <stdio.h>
#include <stdlib.h>       /* malloc/free: menu_speak_line sizes to its line */
#include <string.h>

/* "3 of 6" in the game's language (mod_text.h), as UTF-8. */
static void pos_text(char *out, size_t cap, int pos, int total)
{
    char a[16], b[16];
    _snprintf(a, sizeof a, "%d", pos);   a[sizeof a - 1] = 0;
    _snprintf(b, sizeof b, "%d", total); b[sizeof b - 1] = 0;
    mt_fmt8(out, cap, MT_POS, a, b, NULL);
}

































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
    /* ⭐⭐⭐ Since 2026-11-24 READ FROM THE GAME'S DATA: list screens keep
     * the row at +0x330 and the count at +0x334, and every one of them
     * (refreshes 0x140098200 / 0x14009c090 / 0x14009a1a0, Options
     * 0x1400a2440) draws row r through the LIST part table 0x141207188;
     * the label is child 8 (main menu layout dumps: +0x050 = 8; Options
     * writes its labels into child 8 as messages). */
    { .caller = 0x140097a59ULL, .base_index = -1, .count = 0,
      .name = "Main menu", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_ROWS, .row_off = 0x330, .rows_off = 0x334,
      .part_table = 0x141207188ULL, .label_child = 8 },

    /* ⭐ Options. CONFIRMED 2026-09-08 by live text: standing on position 5 of
     * 5, base 170 read "Credits" (French "Credits"), which is MENU_OP_CREDITS
     * -- the last of Systeme / Audio / Reseau / Options PC / Credits.
     *
     * ⚠ The rival candidate was base 37, whose item 5 is "Options" -- and the
     * user had just SELECTED Options to get here, so the word alone would have
     * been convincing. The item COUNT settles it: this screen has 5 items, the
     * main menu has 6. That is why the count is stored and checked, not merely
     * recorded. */
    /* ⭐⭐⭐ Since 2026-11-24 READ FROM THE GAME'S DATA: list screens keep
     * the row at +0x330 and the count at +0x334, and every one of them
     * (refreshes 0x140098200 / 0x14009c090 / 0x14009a1a0, Options
     * 0x1400a2440) draws row r through the LIST part table 0x141207188;
     * the label is child 8 (main menu layout dumps: +0x050 = 8; Options
     * writes its labels into child 8 as messages). */
    { .caller = 0x1400a20fcULL, .base_index = -1, .count = 0,
      .name = "Options", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_ROWS, .row_off = 0x330, .rows_off = 0x334,
      .part_table = 0x141207188ULL, .label_child = 8 },

    /* Select Mode. ⚠ nlabels 3 vs a live count of 6 means menu_speak_build
     * REFUSES the map and speaks position only -- correct until items 3..5 are
     * established. See the comment on g_mode_select_rows. */
    /* ⭐⭐⭐ Since 2026-11-24 READ FROM THE GAME'S DATA: list screens keep
     * the row at +0x330 and the count at +0x334, and every one of them
     * (refreshes 0x140098200 / 0x14009c090 / 0x14009a1a0, Options
     * 0x1400a2440) draws row r through the LIST part table 0x141207188;
     * the label is child 8 (main menu layout dumps: +0x050 = 8; Options
     * writes its labels into child 8 as messages). */
    { .caller = 0x14009ba21ULL, .base_index = -1, .count = 3,
      .name = "Select Mode", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_ROWS, .row_off = 0x330, .rows_off = 0x334,
      .part_table = 0x141207188ULL, .label_child = 8 },

    /* ⭐⭐ THE MODE STRIP after choosing a game -- the screen the user asked
     * about ("the setting when you select a game from online/offline play").
     *
     * Arcade / Versus / Training, 3 items, horizontal. Positions 1 and 3 were
     * confirmed by the game's OWN live text in the 2026-09-10 00:53 run; see
     * the note on g_mode_select_rows, including the weaker basis for position
     * 2 and the wrong guess this replaces.
     *
     * ⚠ MC_LIST, not MC_SETTINGS_ROW: this strip has no value selector. Its
     * three items are destinations, and confirming one enters a mode rather
     * than changing a setting -- the dispatch at 0x1400b27b2 sends 1, 2 and 3
     * to three different places. There is no sibling half to link to.
     *
     * ⚠ Same labels as the "Select Mode" row above, deliberately. Two callers
     * present the same three modes; menu_speak_lookup_n keys on caller+count,
     * so both rows coexist and neither shadows the other. */
    /* ⭐⭐ Since 2026-11-24 READ FROM THE GAME'S DATA (it was thought to be
     * layout-only text). The strip's refresh (0x1400b2970..) names the
     * selected item [+0x338] by mode id [+0x340 + 4*item] -> message
     * 0x1412084d8[id]: Arcade Mode / Versus Mode / Training Mode / Training
     * Mode (Boss) -- the fourth was missing from the old three-name map. */
    { .caller = 0x1400b2729ULL, .base_index = -1, .count = 0,
      .name = "Select Mode", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_MSG_LIST, .row_off = 0x338, .rows_off = 0x33c,
      .id_off = 0x340, .msg_table = 0x1412084d8ULL, .id_max = 4 },

    /* ⭐⭐ Online Play -- SAME CALLER as the row above, six items instead of
     * three. See g_online_play_rows for the per-row evidence, and
     * menu_speak_lookup_n() for why the count is now part of the key.
     *
     * ⚠⚠ THIS IS THE CALLER A GUESSED ROW WAS ONCE SHIPPED FOR, and the reason
     * that guess was wrong is visible right here: the caller genuinely serves
     * more than one screen, so "the only 6-item vertical screen we have seen"
     * was never going to be a safe inference. What makes THIS row different is
     * not more confidence -- it is that every one of its six labels came from a
     * caption the game itself set on that row.
     *
     * MC_LIST with a label MAP: labels != NULL replaces base_index, and
     * base_index stays -1 so no base+offset fallback can reach a HELP_ string
     * at positions 5 and 6. */
    /* ⭐⭐⭐ Since 2026-11-24 READ FROM THE GAME'S DATA: list screens keep
     * the row at +0x330 and the count at +0x334, and every one of them
     * (refreshes 0x140098200 / 0x14009c090 / 0x14009a1a0, Options
     * 0x1400a2440) draws row r through the LIST part table 0x141207188;
     * the label is child 8 (main menu layout dumps: +0x050 = 8; Options
     * writes its labels into child 8 as messages). */
    { .caller = 0x14009ba21ULL, .base_index = -1, .count = 6,
      .name = "Online Play", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_ROWS, .row_off = 0x330, .rows_off = 0x334,
      .part_table = 0x141207188ULL, .label_child = 8 },
    /* ⭐⭐⭐ Since 2026-11-24 READ FROM THE GAME'S DATA: list screens keep
     * the row at +0x330 and the count at +0x334, and every one of them
     * (refreshes 0x140098200 / 0x14009c090 / 0x14009a1a0, Options
     * 0x1400a2440) draws row r through the LIST part table 0x141207188;
     * the label is child 8 (main menu layout dumps: +0x050 = 8; Options
     * writes its labels into child 8 as messages). */
    { .caller = 0x14009ba21ULL, .base_index = -1, .count = 0,
      .name = "Select Mode", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_ROWS, .row_off = 0x330, .rows_off = 0x334,
      .part_table = 0x141207188ULL, .label_child = 8 },

    /* ⭐⭐ MUSEUM > GALLERY's game chooser, 9 items, horizontal.
     *
     * ESTABLISHED 2026-09-10 (17:32 run) -- the FIRST run ever to enter it.
     * It sets the game's full arcade title as a per-movement caption:
     *
     *   pos 1  "Capcom Fighting Collection 2"                   (x1)
     *   pos 2  "Capcom Vs. SNK: Millennium Fight 2000 Pro"      (x2)
     *   pos 3  "Capcom Vs. SNK 2: Mark of the Millennium 2001"  (x2)
     *   pos 4  "Capcom Fighting Evolution"                      (x1)
     *
     * ⛔⛔ THIS RETIRES A CLAIM HELD FOR WEEKS: "Gallery's items are unnumbered
     * logos and its confirm indexes resource-id tables, so it needs a selection
     * signal." The second half is true and the CONCLUSION was false -- the
     * screen sets text freely. Nothing had ever entered it; the claim was
     * extrapolated from the confirm handler.
     *
     * ⚠ count = 0: position 1 is the COLLECTION itself, not a ROM, so the strip
     * is 1 + 8 games. A count is not what keeps this honest -- the ROM-table
     * corroboration is.
     *
     * ⚠ The sibling strip under MUSIC (0x14009af21) is NOT this screen and is
     * deliberately absent: in the same run it produced NO captions at all. Two
     * 9-item horizontal choosers, different behaviour, separate evidence. */
    /* ⭐ Since 2026-11-24: the refresh 0x14009a7e0 writes the current title
     * into part 0xd child 7. */
    { .caller = 0x14009a482ULL, .base_index = -1, .count = 0,
      .name = "Gallery", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_TEXT, .part_id = 0xd, .label_child = 7 },

    /* ⭐⭐⭐ FIGHTER AWARDS' 40-ITEM GRID (0x140079739, horizontal).
     *
     * See g_fighter_awards_rows for the evidence: position N is achievement N,
     * checked against 16 (position, title) pairs the game drew (15 agree, the
     * one exception being a stale title from the known race).
     *
     * ⭐ Named from the CORPUS, so it does not depend on the game having drawn
     * a title -- which, measured, it does for only 17 of 78 movements while the
     * user sweeps. This works at any speed, which is the point: "there are
     * hundreds of options in there."
     *
     * ⚠ count = 40 is a REAL invariant and it is the guard: the same caller
     * also drives a 35-item axis on the Player Progress page (Y toggles it),
     * and this map must not reach that screen. menu_speak_lookup_n() requires
     * caller AND count, so the 35-item page falls through to position only --
     * correct, because nothing here knows what those 35 are. */
    /* ⭐ Since 2026-11-24 READ FROM THE GAME'S DATA (MC_AWARD). */
    { .caller = 0x140079739ULL, .base_index = -1, .count = 40,
      .name = "Fighter Awards", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_AWARD },
    /* ⭐ The 7-row list was left unnamed because it SCROLLS over the 40; it
     * moves the same [+0x354], so it is named from the same data. */
    { .caller = 0x140079833ULL, .base_index = -1, .count = 0,
      .name = "Fighter Awards", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_AWARD },

    /* ⭐⭐⭐ THE PLAY STATS ROSTERS -- the same caller's other page (Y toggles
     * it). Each tab is one game's character roster, and the game is read from
     * the screen's own tab index at +0x338. See MC_GAME_ROSTER.
     *
     * ⚠⚠ count = 0 ON PURPOSE, and it is not a relaxation. The eight tabs have
     * eight DIFFERENT sizes (35/48/23/39/10/14/40/24), so no single count could
     * describe this entry, and a fixed one would disable seven tabs. The count
     * check simply MOVES: menu_speak_build cross-checks the live count against
     * the count of the roster the tab index names, which is strictly stronger
     * than a constant -- it verifies the identity, not just the shape.
     *
     * ⛔⛔ THE COLLISION THIS ENTRY MUST SURVIVE. A plain caller+count lookup
     * returns an exact count match first, so a 40-item list would always reach
     * the achievements row above -- right on the achievements page, and WRONG
     * on Project Justice's 40-character tab, where it would speak an
     * achievement title where a character name belongs.
     *
     * ⭐ THE FIX IS IN THE LOOKUP, NOT IN THIS TABLE'S ORDER.
     * menu_speak_lookup_for() asks each entry whether it CLAIMS the live
     * screen, and this entry claims it only when the tab index at +0x338 reads
     * 0..7 AND names a roster whose size equals the live count. On the
     * achievements page that field must independently read 6 for the claim to
     * succeed. Evidence decides, not row order. */
    { .caller = 0x140079739ULL, .base_index = -1, .count = 0,
      .name = "Play Stats", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_GAME_ROSTER, .roster_off = 0x338 },

    /* ⭐⭐ THE RANKED LEADERBOARD's game strip (Online Play > Ranked
     * Leaderboard), 8 items, horizontal. Caller 0x1400ac059.
     *
     * ESTABLISHED 2026-09-10 (19:45 run). It captions EXACTLY like Gallery: one
     * full arcade title per movement, on the movement it belongs to. Seven of
     * eight positions captured, each a unique ROM-table title in the
     * collection's canonical order:
     *
     *   1 CVS: Millennium Fight 2000 Pro   3 Capcom Fighting Evolution
     *   4 Street Fighter Alpha 3 Upper     5 Power Stone
     *   6 Power Stone 2                    7 Project Justice
     *   8 Plasma Sword: Nightmare of Bilstein
     *
     * ⭐ MC_GAME_STRIP needs no per-position map -- it names from the LIVE
     * caption and corroborates against the ROM table -- so position 2 being
     * uncaptured costs nothing: it will name itself when the user stands on it.
     * That is the difference between this shape and a label MAP.
     *
     * ⚠ count = 8 (not 0 like Gallery): this strip is the eight GAMES with no
     * collection entry, which is why position 1 is a ROM here and the
     * collection title at Gallery's position 1. A live count of 8 is therefore
     * a real invariant worth checking.
     *
     * ⛔ NOT to be confused with Casual Match's 8-item strip (0x140098986),
     * which looks identical and captions NOT AT ALL -- see the plan doc. Two
     * 8-item horizontal game strips, different channels, separate evidence. */
    /* ⭐ Since 2026-11-24: the refresh 0x1400ac2a0 writes the current game's
     * title into part 0x1e child 15 -- that is what is spoken. */
    { .caller = 0x1400ac059ULL, .base_index = -1, .count = 0,
      .name = "Ranked Leaderboard", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_TEXT, .part_id = 0x1e, .label_child = 15 },

    /* ⭐⭐⭐ IN-GAME DISPLAY & SOUND SETTINGS, 7 rows. See g_ingame_av_rows.
     * ⭐ Rows 4-5 (Music Volume, Sound Effects Volume) are LEVELS -- their
     * cursor position IS the setting, same as the Options volume sliders. */
    /* ⛔⛔ MC_SETTINGS_ROW, not MC_LIST -- and it was MC_LIST, which is why the
     * user heard "row 5, 1 of 11" instead of "Music Volume, 1 of 11".
     *
     * menu_speak_sibling_row_for() only considers MC_SETTINGS_ROW entries, so
     * declaring this a plain list made it invisible to its OWN value axis: every
     * row without a shipped value set fell back to the row NUMBER. Display Size
     * sounded right only because it has one, which is exactly how the bug
     * survived -- one working row is enough to look correct in passing.
     *
     * ⚠ The two halves are linked by this identical `name`; the kind is what
     * makes the link reachable at all. */
    /* ⚠ levels 0x30 = rows 4-5 (Music Volume, Sound Effects Volume). It lives on
     * the ROW entry, not the value entry, because the 6-row variant below puts
     * those same two rows at 3-4 -- see the sibling-mask note in
     * menu_speak_build(). */
    { .caller = 0x14009c755ULL, .base_index = -1, .count = 7,
      .name = "Display & Sound Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_ROW, .levels = 0x30u },

    /* ⭐⭐ THE SIX-ROW VARIANT of the same screen -- games with no
     * "Internal Resolution" option. Same caller, discriminated by COUNT, which
     * is what menu_speak_lookup_n() is for and what makes this safe where the
     * training tabs needed a different approach entirely (there, two tabs shared
     * a count; here the counts differ, so the existing key suffices).
     *
     * ⚠ Same `name` as the 7-row entry ON PURPOSE: menu_speak_sibling_row_for()
     * links the value axis by name, and the value axis's own count-based
     * tie-break then picks whichever row entry matches the live row count. */
    /* ⚠ levels 0x18 = rows 3-4 here, the SAME two volume rows one slot earlier
     * because Internal Resolution is absent. A shared 0x30 would have marked
     * Sound Effects Volume and Default instead -- a level stated where a name
     * belongs, on the row that resets the screen. */
    { .caller = 0x14009c755ULL, .base_index = -1, .count = 6,
      .name = "Display & Sound Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_ROW, .levels = 0x18u },

    /* ⭐⭐ The in-game AV screen's VALUE axis. Same screen object as the row
     * site above -- five call sites share 0x2C498050, which is why the axes
     * must be told apart by cursor OFFSET and not by screen.
     * ⭐ Rows 4-5 (Music Volume, Sound Effects Volume) are LEVELS: 11 values
     * each, and the position IS the setting. */
    { .caller = 0x14009c7a3ULL, .base_index = -1, .count = 0,
      .name = "Display & Sound Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_VALUE, .values_off = 0x398, .levels = 0x30u },

    /* ⭐⭐⭐ PER-GAME KEYBOARD SETTINGS -- all three call sites of one screen
     * (pause menu > Controller Settings, or Y on Select Game). Every movement,
     * up/down, left/right column switch or Key Bindings type, reads the
     * screen's own row cursor and quotes that row's label and key. See
     * key_grid_build() for the disassembly, and for why the old row map and
     * the "Confirm" dialog entry were removed.
     *   0x14009ec58  rows, 0..15 (count 16)
     *   0x14009ed68  left/right column switch, 0..1 (stack-local cursor)
     *   0x14009ed20  Key Bindings type, 0..3 (screen+0x380) */
    { .caller = 0x14009ec58ULL, .base_index = -1, .count = 16,
      .name = "Keyboard Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_KEY_GRID },
    { .caller = 0x14009ed68ULL, .base_index = -1, .count = 2,
      .name = "Keyboard Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_KEY_GRID },
    { .caller = 0x14009ed20ULL, .base_index = -1, .count = 4,
      .name = "Keyboard Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_KEY_GRID },

    /* ⭐⭐ THE GAMEPAD CONTROLS SCREEN (per game, with a controller: Y on
     * Select Game or the pause menu). First reached 2026-11-24 14:40 from
     * Plasma Sword: 8 rows on 0x1400a031d, a 4-item axis on 0x1400a0513 and
     * a 159-item action list on 0x1400a056f, all on one screen object; the
     * game drew "Vertical Slash" and "Default" while it was walked.
     * From the disassembly: the cursor helper moves a window SLOT 0..7 at
     * screen+0x33c over a scrolling list (top +0x338, length +0x340);
     * value cursors from +0x344 and a stack local; 0x1400a05b2 is a third
     * axis. Read through its refresh routine 0x1400a0e10 (MC_PAD_LIST),
     * which logs the focused part's children once so the next run
     * establishes where an action row keeps its button. */
    { .caller = 0x1400a031dULL, .base_index = -1, .count = 8,
      .name = "Controller Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PAD_LIST },
    { .caller = 0x1400a0513ULL, .base_index = -1, .count = 0,
      .name = "Controller Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PAD_LIST },
    { .caller = 0x1400a056fULL, .base_index = -1, .count = 0,
      .name = "Controller Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PAD_LIST },
    { .caller = 0x1400a05b2ULL, .base_index = -1, .count = 0,
      .name = "Controller Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PAD_LIST },

    /* ⭐⭐⭐ THE IN-GAME PAUSE MENU -- the first screen behind the emulated ROM
     * that this mod names; the shell's cursor hooks fire there normally, so it
     * needed no new mechanism at all.
     *
     * ⛔⛔ THE TWO LABEL MAPS ARE GONE, AND THIS IS WHY.
     *
     * USER: "In training mode it is saying quicksave/load rather than training
     * options/return to character select." A confidently WRONG name -- the one
     * failure worse than silence, because a blind player cannot tell it is
     * wrong and may Load a Quick Save while trying to open the training menu.
     *
     * The cause: TRAINING MODE ALSO DRAWS EIGHT ROWS. The maps assumed 8 = the
     * arcade pause menu (…Load Quick Save, Quick Save, Quit) and 7 = training,
     * but the snapshot sweep caught FIVE distinct variants on this caller, two
     * of them 8 rows:
     *     8: Resume, Marquee Card, Move List, Controller Settings,
     *        Display & Sound Settings, Training Menu, Change Character, Quit
     *     8: Resume, Marquee Card, Move List, Controller Settings,
     *        Display & Sound Settings, Load Quick Save, Quick Save, Quit
     * The count cannot separate them, so the arcade map won and spoke its rows
     * 6-7 over training's. ⚠ The 7-row entry was added for an earlier report of
     * this same screen being silent -- the count key looked sufficient because
     * only two variants had been seen. A third made it wrong.
     *
     * ⭐ ONE MC_ROW_COLUMN ENTRY REPLACES BOTH. The rows are at x=362 in all 22
     * snapshots, counts 7 and 8, and quoting what the game drew needs no
     * variant, no count and no map. The same reasoning that made Game Settings
     * work across eight games: the row sets differ, the geometry does not.
     *
     * ⛔ Five snapshots held a BLANK " " in that column, and this comment
     * once said a blank "still counts, so the count guard passes". Wrong: the
     * blank is the unused 8th POOLED slot (always y=785, where the 8-row
     * variant draws Quit), so every 7-row menu read 8 labels against 7 rows
     * and was refused -- the CVS2 intro's pause menu spoke bare positions.
     * widgets_of() now drops whitespace-only labels; case 55 pins it.
     *
     * ⚠ g_pause_menu_rows / g_training_pause_rows are KEPT as the offline
     * expectation that tools/test_screen_maps.py validates against the corpus;
     * nothing reads them at runtime. */
    /* ⭐⭐⭐ READ FROM THE GAME'S DATA (2026-11-24). Refresh 0x1400a86e0:
     * rows 0..[+0x33c]-1, selected [+0x338], part = 0x141207ce8[row]; the
     * label is child 8 (layout dumps: Resume / Move List / Quit all +0x050 =
     * 8). Replaces the x=362 column, which needed the pooled-blank rule and a
     * count check to be safe; the game's own row is never ambiguous. */
    { .caller = 0x1400a749dULL, .base_index = -1, .count = 0,
      .name = "Pause Menu", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_ROWS, .row_off = 0x338, .rows_off = 0x33c,
      .part_table = 0x141207ce8ULL, .label_child = 8 },

    /* ⭐⭐⭐ THE STANDBY SETTINGS (0x1400991c1, 3 rows, vertical) -- named by
     * the SNAPSHOT, which read its column directly:
     *
     *     snap: === SCREEN 2C4BA0E0: 3 row(s) at x=494, cursor says 3 ===
     *       columns: 494(3) 960(1)
     *        1. Basic Standby
     *        2. In-game Standby
     *        3. Museum Standby
     *
     * ⭐ AN EXACT MATCH, which is what makes this shippable: the column holds
     * THREE objects and the cursor reports THREE, with no rival column on the
     * screen (960 is the heading). Every other 3-row capture in that run was a
     * different screen with a competing column, and this one is unambiguous.
     * 10 movements on this caller, all at count 3.
     *
     * ⛔⛔ NOT the 3-row pair on the Casual Match screens
     * (0x14009885e/0x1400988bf, screens 2C4077F0 / 2C4902F0). I spent a round
     * treating those as one problem with this; they are different callers on
     * different screens and THOSE ARE STILL UNNAMED. Do not let this row's
     * success suggest otherwise -- see the memory note.
     *
     * ⚠ count = 0 with MC_ROW_COLUMN: the kind quotes the row's own label from
     * its column, so no per-count map exists to go stale. The column's
     * population is the membership test, inside gt_column_item(). */
    /* ⭐⭐ Since 2026-11-24: READ FROM THE GAME'S DATA. Refresh 0x1400997c0:
     * rows 0..[+0x338]-1, selected [+0x334], each row through the LIST-row
     * table 0x1411fb510 (0x14007ed70, the list counterpart of 0x14007ea80).
     * label_child 7: MEASURED 2026-11-24 17:06 on Lobby, which uses the same
     * list-row table -- its part dump showed child 7 = "Join Lobby" and no
     * child 8 (the main/pause list rows' 8 does not apply to this table). */
    { .caller = 0x1400991c1ULL, .base_index = -1, .count = 0,
      .name = "Standby Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_ROWS, .row_off = 0x334, .rows_off = 0x338,
      .part_table = 0x1411fb510ULL, .label_child = 7 },

    /* ⭐⭐⭐ TRAINING OPTIONS -- the ROW axis. See g_training_option_rows for
     * the coordinate evidence and the control that validates it.
     *
     * Reached from the training pause menu's "Training Menu" row. ⚠ There is
     * no separate game-settings menu; the training settings live here.
     *
     * ⛔ MC_SETTINGS_ROW, not MC_LIST -- the defect that shipped twice before
     * (in-game AV, Keyboard Settings). menu_speak_sibling_row_for() only
     * considers MC_SETTINGS_ROW entries, so MC_LIST here would make this screen
     * invisible to its own value axis and every row without a shipped value set
     * would speak the row NUMBER. */
    /* ⭐⭐⭐ TRAINING OPTIONS, every tab -- READ FROM THE GAME'S DATA
     * (2026-11-24). Refresh 0x1400a5790: tab = [+0x338], selected row =
     * [+0x350 + 4*tab], row count = [+0x360 + 4*tab]; rows go through the
     * shared row -> part table 0x1411fb4f0 (0x14007ea80 / 0x14007e130);
     * label child 7, value child 8 (dumps: "Action" 7, "Normal"/"Off" 8).
     * The tab title is message 0x141207a68[[+0x340 + 4*tab]], the index the
     * tab's layout builder 0x14007bc90 is given -- so it cannot lag the rows
     * the way the heading OBJECT did. Replaces the 6-row label map, the x=320
     * column and the count-keyed value sets. */
    { .caller = 0x1400a41efULL, .base_index = -1, .count = 0,
      .name = "Training Options", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_ROWS, .row_off = 0x350, .rows_off = 0x360,
      .tab_off = 0x338, .part_table = 0x1411fb4f0ULL,
      .label_child = 7, .value_child = 8,
      .title_table = 0x141207a68ULL, .title_idx_off = 0x340 },

    /* ⭐⭐⭐ TRAINING OPTIONS' OTHER TABS -- read from the LIVE ROW LABELS, with
     * no map and no heading. Reached with LB/RB from the tab above.
     *
     * ⛔⛔ THERE ARE FOUR TABS, NOT THREE, and two of them have the IDENTICAL
     * shape: 5 rows whose value lists are (3,3,3,2). The full walk at 01:25:56
     * -> 01:26:30 goes block 0 (6 rows) -> 1 -> 2 -> 3 on ONE screen object, so
     * neither the row count nor the value counts can tell tabs 1 and 2 apart.
     *
     * ⛔⛔ AND THE HEADING CANNOT EITHER -- it is STALE exactly when it matters.
     * Measured per dump block, heading against the first row:
     *
     *     'Dummy Behavior'   + 'Action'        OK
     *     'Dummy Settings'   + 'Health Bar'    OK
     *     'Dummy Settings'   + 'Health Bar'    OK
     *     'Player Settings'  + 'Health Bar'    <-- MISMATCH
     *     'Training Options' + 'Attack Data'   <-- MISMATCH
     *     'Dummy Behavior'   + 'Action'        OK
     *
     * 2 of 6. The heading object is updated on a different schedule from the row
     * objects and leads them by one tab, and it sometimes reverts to the generic
     * "Training Options". A heading_index key was BUILT and SHIPPED against this
     * screen before the walk was read properly; it would have paired Player
     * Settings' title to Dummy Settings' labels. Removed -- see the note in
     * menu_speak.h.
     *
     * ⭐ So do not identify the tab at all. The ROW LABELS ARE the identity, the
     * game draws them, and MC_COORD_LIVE speaks the Nth by its own coordinate --
     * correct on every tab including ones never seen, in all 14 languages, with
     * no map to get out of step. The labels the corpus confirmed
     * (g_dummy_settings_rows / g_player_settings_rows) are kept only as the
     * offline EXPECTATION that test_screen_maps.py checks; nothing reads them at
     * runtime.
     *
     * ⛔ NOT MC_COORD_LIVE: its filter is TEXT-ONLY, and on this screen the
     * value column (X=835: "Normal", "Off", "Auto-Recovery") and the heading
     * (X=960) are all short, markup-free and non-sentence, so they pass it too.
     * Measured on the real dumps: 10 candidates where 5 are needed, and
     * gt_item_at_position() then refuses the whole screen. Correct, but silent.
     *
     * ⭐ MC_ROW_COLUMN instead: scope by the COLUMN (X=320) and order by Y, the
     * same gt_column_item() pair the move list uses. The column is what
     * separates a row label from its value, and no text rule can.
     *
     * ⭐⭐⭐ count = 0 -- EVERY TAB OF EVERY GAME.
     *
     * ⛔ USER: "also some training modes" were still silent. Measured across
     * the collection: this caller reports counts 2, 3, 4, 5, 6 AND 7, because
     * each game's training tabs hold different settings. Entries for 5 and 6
     * named two of those and left the rest speaking bare positions.
     *
     * ⭐ Thirteen distinct row sets were captured and every one draws its
     * labels at x=320. MC_ROW_COLUMN quotes what is drawn, so it needs no
     * per-tab and no per-game knowledge.
     *
     * ⚠ SAFE because this path re-checks the LIVE COLUMN against the cursor's
     * count on every movement and refuses on a mismatch -- the column is the
     * safety mechanism, not the table's count.
     *
     * ⚠ The 6-row entry ABOVE keeps its exact count deliberately: it is
     * MC_SETTINGS_ROW with a LABEL MAP (g_training_option_rows), where the
     * count is the only thing standing between that map and another tab's
     * rows. lookup_n prefers an exact count match, so that entry still wins
     * for 6 and this one catches everything else. */


    /* ⭐⭐ GAME SETTINGS / EX SETTINGS (Select Game > Game Settings), 7 rows,
     * vertical, caller 0x14009d8df. Diagnosed 2026-09-15 from the 13:05 run;
     * before this the screen spoke bare "N of 7" with no row names at all.
     *
     * ⭐⭐ IT IS TWO TABS ON ONE CALLER, cycled by RB (key E / 0x45):
     *   tab A, heading "Game Settings": Game Difficulty, Attack Power, Timer
     *     Speed, Speed Select, Battle Type, Max Rounds (Single), Default
     *   tab B, heading "EX Settings": Coin Mode, Boot Display, Secret
     *     Characters, Same Character Select, Version, Sound Style, Default
     * B is the emulated game's own arcade DIP settings; A is the match settings.
     * Three E presses in the run (13:07:53, :56, :59) and the row set flips
     * within ~180 ms of each; no dump ever changed tab without one.
     *
     * ⛔⛔ A LABEL MAP WAS AVAILABLE AND IS REFUSED. All seven of tab A's labels
     * pair exactly and uniquely against the corpus (OPT_DIFFICULTY 643,
     * OPT_DAMAGE_LEVEL 644, OPT_TIMER_SPEED 649, OPT_GAME_SPEED 661,
     * OPT_BATTLE_TYPE 1108, OPT_CVS2_MR 1115, MENU_OP_DEFAULT 168 --
     * non-contiguous, so a MAP, never a base). But the two tabs are identical
     * in every property a map could key on: same caller, same count of 7, same
     * column, same 80px pitch. A (caller, count) map would have spoken tab A's
     * seven names on tab B -- a confidently wrong SETTING name, which is the
     * failure this file exists to refuse. MC_ROW_COLUMN quotes what the game
     * drew, so it is right on BOTH tabs without telling them apart.
     *
     * Geometry measured over 16 field dumps: labels X=320, Y 286..766 on an
     * exact 80px pitch, 7 objects, no chrome in the column. Values sit at
     * X=835 sharing each label's Y; the heading is at X=960.
     * ⚠ The layout dump's floats are BIG-endian -- decoding them little-endian
     * yields all-zero coordinates, a wrong answer that reads like a real one.
     *
     * ⛔ THE TAB NAME IS NOT ANNOUNCED, and the shipped mechanism cannot do it.
     * MC_ROW_COLUMN's tab announcement keys on the row cursor's OFFSET changing
     * (Training Options' four tabs own +0x350/+0x354/+0x358/+0x35c). Measured
     * here: BOTH tabs drive the same field, +0x35C, across all 34 movements. So
     * the offset can never signal the switch on this screen. The heading at
     * X=960 did track its own rows in 16 of 16 dumps -- but that is exactly the
     * heading_index key that was built, shipped and REMOVED for Training
     * Options, and 3 tab switches in one session is not the walk that exposed
     * it there. Rows first; the tab name needs its own evidence.
     *
     * ⭐⭐ pair_x = 835: the VALUE column, measured across 16 field dumps at
     * exactly that X with each value sharing its label's Y (labels 320, values
     * 835, heading 960, rows 80px apart). So a row now reads "Game Difficulty,
     * Free Play". The values are QUOTED, never mapped -- "Normal" has thirteen
     * corpus stems and TWO of them are rows of this screen, and "Free Play"'s
     * only stem belongs to a different row than the one the game draws it on.
     *
     * ⭐⭐⭐ count = 0 -- ONE ENTRY FOR ALL EIGHT GAMES.
     *
     * ⛔ USER: "There are a few missing still, mostly for game/ex settings."
     * Measured across a full sweep of the collection: this caller reports
     * counts 3, 4, 5, 6, 7 AND 8, because every game draws a different set of
     * settings. A `count = 7` entry named CVS2 and refused all the rest --
     * correctly, but it left most of the collection silent.
     *
     * ⭐ The row sets differ; THE GEOMETRY NEVER DOES. Thirteen distinct row
     * sets were captured and every one draws labels at x=320 with values at
     * x=835. MC_ROW_COLUMN quotes whatever is drawn there, so it does not care
     * WHICH settings a game has -- which is exactly why one entry can cover
     * eight games where a label map would need eight.
     *
     * ⚠ count = 0 IS SAFE HERE, AND NOT BECAUSE THE CHECK WAS DROPPED. This
     * path validates against the LIVE COLUMN on every movement: if the column
     * does not hold exactly as many labels as the cursor reports, it refuses
     * and says so. The table count is not the safety mechanism, the column is.
     * ⛔ That reasoning does NOT transfer to a label map, where count is the
     * only thing standing between a screen and another screen's names. */
    /* ⭐⭐⭐ GAME SETTINGS / EX SETTINGS -- READ FROM THE GAME'S DATA
     * (2026-11-24). Refresh 0x14009ddb0: rows 0..[+0x360]-1, selected
     * [+0x35c] (both pages), row r through the shared table 0x1411fb4f0;
     * label child 7, value child 8. Replaces the x=320/835 columns. */
    { .caller = 0x14009d8dfULL, .base_index = -1, .count = 0,
      .name = "Game Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_ROWS, .row_off = 0x35c, .rows_off = 0x360,
      .part_table = 0x1411fb4f0ULL, .label_child = 7, .value_child = 8 },

    /* ⭐⭐ GAME SETTINGS -- the VALUE axis (caller 0x14009d929, horizontal).
     *
     * ⛔ USER: "They don't read when I change them with left or right, only
     * when I leave and come back." This entry is that fix: without it the
     * value axis had NO table row and spoke bare position.
     *
     * ⚠ count = 0 DELIBERATELY. Every row has a different number of values --
     * measured 2, 3, 4, 5 and 9 on this screen in one run -- so a fixed count
     * would disable the entry on every row but one. The same reasoning as the
     * music strip's count = 0.
     *
     * ⚠ row_off = 0x35C is the ROW cursor, not this axis's own. Measured on all
     * 34 vertical movements of the 13:05 run, identical on both tabs. This
     * axis's own cursor indexes a value array shared by the tabs (tab A slots
     * 0-5 at +0x384.., tab B slots 6-11), which is why the usual
     * (cursor - screen - values_off)/4 derivation cannot give a row here.
     *
     * ⭐⭐ levels bit 0 = Game Difficulty, whose value is NOT TEXT.
     *
     * ⛔ USER-VERIFIED: with every other row speaking its value, that row said
     * "Game Difficulty, 9 of 9" -- no value at all. Measured in the layout
     * dump: every other row carries an object at x=835 and this one has
     * NOTHING at any X on its line. It is a 9-position numeric scale drawn as
     * graphics, which is also why OPT_DIFFICULTY is the only row of this screen
     * with no value run in the corpus. So the POSITION IS THE SETTING and it
     * speaks "Game Difficulty, 4, of 9".
     *
     * ⚠ A per-row MASK, never inferred from "the value read came back empty":
     * a value that failed to READ is not a level, and treating it as one would
     * state a setting where a read error belongs. Only bit 0 is set because
     * only that row was measured to have no text. */
    /* Game Settings' value axis. Game Difficulty's value is drawn as
     * graphics (no child-8 text), so its position is the setting: it is
     * recognised by its label being message 643 (OPT_DIFFICULTY). */
    { .caller = 0x14009d929ULL, .base_index = -1, .count = 0,
      .name = "Game Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_ROWS, .row_off = 0x35c, .rows_off = 0x360,
      .part_table = 0x1411fb4f0ULL, .label_child = 7, .value_child = 8,
      .level_label = 643 },

    /* ⭐⭐ TRAINING OPTIONS -- the VALUE axis. Same screen object as the row
     * site above; values_off 0x3F0 re-derived on a fresh screen object across
     * five rows in the CVS1 run, so the two axes are told apart by cursor
     * OFFSET as every other settings screen here is.
     *
     * ⚠ No `levels` bits: none of these rows is a numeric range. Every value
     * list is a set of NAMES, and a wrong levels bit would say "Block, 3, of 4"
     * where a name belongs. */
    /* ⚠ `levels` bit 27 = Network Delay, whose 11 values are a GENERATED range
     * ("Set the level of network delay to simulate."), not a label run. Without
     * it the row would read "4 of 11" as a list position; with it the index IS
     * the setting. Every other slot here is a set of NAMES -- a wrong levels bit
     * states a level where a name belongs, which is why this is a per-row mask
     * and never inferred from the value count. */
    /* Training Options' value axis: the same rows, read the same way. */
    { .caller = 0x1400a4241ULL, .base_index = -1, .count = 0,
      .name = "Training Options", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_ROWS, .row_off = 0x350, .rows_off = 0x360,
      .tab_off = 0x338, .part_table = 0x1411fb4f0ULL,
      .label_child = 7, .value_child = 8 },

    /* ⭐⭐ THE LOBBY-ID KEYPAD's character picker (Search Lobby ID > Enter
     * Lobby ID), 36 items, vertical. Caller 0x14008a8ef.
     *
     * Every position sets the character under the cursor as live text --
     * measured across 83 movements in three runs, positions 1-35 are exactly
     * "0".."9" then "A".."Y", one per position, no gaps and no conflicts.
     *
     * ⭐ AND POSITION 36 = 'Z' IS NOW MEASURED, not extrapolated: in the
     * 2026-09-11 03:22 run the picker sat at 36 while the lobby-ID slot mirroring
     * it read "Z". The alphabet "0".."9","A".."Z" is 36 characters, which is
     * exactly the item count -- two independent facts agreeing.
     *
     * ⚠ count = 36 is a real invariant: 10 digits + 26 letters. A different
     * live count means this is not that picker.
     *
     * ⛔ NOT a 36-entry label table, deliberately -- see MC_CHAR_PICKER. */
    { .caller = 0x14008a8efULL, .base_index = -1, .count = 36,
      .name = "Enter Lobby ID", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_CHAR_PICKER },

    /* ⭐⭐⭐ THE DATA-COLLECTION CONSENT DIALOG (history; superseded below). Entry point
     * (always reached from "Data Collection, Agree") plus the SYS_WINDOW label
     * run, which is two independent sources where the count alone was one. */
    /* ⭐⭐⭐ THE DATA-COLLECTION CONSENT DIALOG -- NAMED FROM THE GAME'S DATA
     * (2026-11-24). Its setup (0x14008814a..) writes button i as message
     * 0x141206b98[i] into part 0x141206b78[i], and the cursor +0x340 (of
     * +0x344) highlights part 0x141206b78[cursor]. In cursor order the
     * buttons are Privacy Policy, Agree, Decline (messages 31, 29, 30).
     * ⛔ The order shipped and reverted earlier was "Agree / Decline /
     * Privacy Policy": the right words in the WRONG order. */
    { .caller = 0x140087cdaULL, .base_index = -1, .count = 0,
      .name = "Data Collection", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_MSG_LIST, .row_off = 0x340, .rows_off = 0x344,
      .msg_table = 0x141206b98ULL, .id_max = 3 },

    /* ⭐⭐⭐ THE LOBBY-ID FIELD ITSELF (0x14008a91b), 6 character slots,
     * horizontal. The screen beside the picker above: the picker chooses a
     * character, this says which SLOT it goes in.
     *
     * MEASURED in run-2026-09-11-0322. All six slot objects live at once, one
     * Y (556.0) with X ascending 800, 864, 928, 992, 1056, 1120 -- a ROW, 64.0
     * px apart. So this is the screen that proved the coordinate sort must
     * derive its axis rather than assume Y.
     *
     * ⛔⛔ AND THE SCREEN THAT PROVES THE COORDINATE IS THE ONLY USABLE KEY.
     * Those same six objects carry +0x050 = 0a, 0c, 0b, 0d, 0e, 0f -- slots 2
     * and 3 INVERTED -- and the instance set's insertion order put the edited
     * slot FIRST in one run and LAST in another. Ordering by either would
     * silently swap two characters of a code the user is spelling out, which
     * on this screen is the whole point of the screen.
     *
     * ⚠ coord_maxlen = 1: every slot is exactly one character. That is the
     * narrowest possible membership rule and it is exactly right here -- the
     * screen's other text (prompts, the button strip) cannot be one character.
     *
     * ⚠ count = 6 is a real invariant, and gt_item_at_position() REFUSES when
     * the live candidate count disagrees with it. A lobby ID is six characters. */
    /* ⭐ Since 2026-11-24: read from the game's data (lobby_char), not by
     * ordering the slot objects' coordinates. */
    { .caller = 0x14008a91bULL, .base_index = -1, .count = 0,
      .name = "Lobby ID", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_LOBBY_SLOT },

    /* ⭐⭐⭐ THE IN-GAME MOVE LIST (0x140080250, vertical) -- the largest single
     * gap in the mod: 529 of 924 unnamed movements across every run, and
     * user-reported as not reading.
     *
     * Named from the game's OWN indirection (src/move_list.c), which is why
     * this needs no base, no map and no count. See MC_MOVE_LIST in the header
     * for the mechanism and for the three routes closed with evidence.
     *
     * ⚠⚠ count = 0 ON PURPOSE -- and here that is load-bearing rather than a
     * placeholder. The list length is the CHARACTER'S move count: measured
     * totals of 2,3,4,5,6,7,8,9,12,14 within a single session as the roster
     * cursor moved. A count would disable the row on every character but one.
     * menu_speak_lookup_n() treats 0 as "match the caller, any count".
     *
     * ⚠ The sibling axis 0x14008028e is the character ROSTER, not a second
     * move list, and it is deliberately NOT given a row here -- its items are
     * the cast, which the live heading already names ("Ken Moves"). Naming it
     * from the roster corpus would reintroduce the JP/US question this route
     * avoids. */
    { .caller = 0x140080250ULL, .base_index = -1, .count = 0,
      .name = "Move List", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_MOVE_LIST },

    /* ⭐⭐ THE MOVE LIST'S CHARACTER ROSTER (0x14008028e, horizontal) -- the
     * sibling axis of the row above. Speaks the screen's own heading, so the
     * player knows WHOSE moves the list is showing.
     *
     * ⚠⚠ count = 0: the roster size is per GAME, not fixed -- 48 in CVS2, 35
     * elsewhere in the same session. A count would disable the row on every
     * game but one, exactly as it would on the move list itself.
     *
     * ⛔ Leaving this unnamed was a real defect, not a scoping decision: it
     * made the whole screen unverifiable for the user it is built for. */
    { .caller = 0x14008028eULL, .base_index = -1, .count = 0,
      .name = "Move List", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_MOVE_ROSTER },

    /* ⚠ TEST-ONLY. Not a real call site -- no game movement can reach
     * 0xDEADCA11 -- so this cannot affect speech. It keeps the
     * MC_LABEL_UNKNOWN path covered; see MC_TEST_CALLER in the header. */
    /* (Sentinel Test entry retired 2026-11-24 with the label maps.) */

    /* ⭐⭐ CREATE LOBBY's settings, 10 rows, 5 named. See g_create_lobby_rows.
     *
     * ⭐⭐ values_off = 0x33c, DERIVED AND VERIFIED against all ten rows in the
     * 21:30 run. That run walked every row and nudged each one's value, so the
     * log pairs each focused row with its own value cursor:
     *
     *   row 1 -> +0x32c (9 vals) · row 2 -> +0x330 (2) · ... · row 10 -> +0x350
     *
     * Ten consecutive 4-byte slots, and (cursor - screen - 0x33c)/4 reproduces
     * every one. ⚠ I first solved it as 0x328 from the lowest cursor alone and
     * it was off by five rows on every check -- the offset must be fitted to
     * the row/cursor PAIRS, never to the minimum address.
     *
     * ⭐ This is what lets the VALUE axis speak the row's name: moving a value
     * now says "Victory Condition, 3 of 10" instead of "3 of 10". */
    /* ⭐ SCROLLS (refresh 0x140083040): 8 slots, slot s shows item [+0x330]
     * + s, so the part is table[item - [+0x330]] (scroll_off). Without it
     * rows 9-10 looked up parts past the shared table's eight entries --
     * caught by the label check in the 17:07 run, nothing wrong spoken. */
    { .caller = 0x140082c3cULL, .base_index = -1, .count = 10,
      .name = "Lobby Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_ROW, .scroll_off = 0x330 },
    { .caller = 0x140082d16ULL, .base_index = -1, .count = 0,
      .name = "Lobby Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_VALUE, .values_off = 0x33c,
      .scroll_off = 0x330 },

    /* ⭐⭐ CASUAL MATCH's 4 rows + their value axis. See g_casual_match_rows:
     * three independent sources agree, including the value-slot arithmetic that
     * was missing until the 21:00 run.
     *
     * ⚠ values_off = 0x344 -- read from THIS screen's own offsets (screen
     * 0x2BDBC7C0, row cursor +0x334, value cursors +0x344 and +0x348). The
     * 8-value list is row 0 and the 3-value list is row 1.
     *
     * ⚠ The VALUE site (0x140098986) is the same call site as the 8-item game
     * strip, and it is entered here as MC_SETTINGS_VALUE with count 0 because
     * its live count VARIES by row (8 for Game, 3 for Game Version). The strip
     * itself is deliberately NOT MC_GAME_STRIP -- see the plan doc: it captions
     * not at all, and its titles arrive only on the transcript channel. */
    /* ⭐ Since 2026-11-24 also checked against and valued from the game's own
     * rows: refresh 0x140098c50 draws row r through part 0x141207230[r]
     * (label child 7, value child 8 = message 0x141207280[r] + [+0x344+4r]). */
    { .caller = 0x14009890dULL, .base_index = -1, .count = 4,
      .name = "Casual Match", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_ROW, .part_table = 0x141207230ULL },

    /* ⭐⭐⭐ RANKED MATCH -- THE SAME CALLER, THREE ROWS. See
     * g_ranked_match_rows: 17 visits across every log, entry point decides the
     * count with zero exceptions (Casual -> 4, Ranked -> 3).
     *
     * ⚠ This is why menu_speak_lookup_n() discriminates on caller AND count --
     * the same mechanism that split 0x14009ba21 into Select Mode and Online
     * Play. A caller is not a screen identity. Without this row the 4-row map
     * was applied to a 3-row screen, which is the user-visible bug: the row
     * count appeared to change while arrowing.
     *
     * ⭐⭐ NAMED HONESTLY -- "Ranked Match", not "Casual Match".
     *
     * An earlier version of this row carried the name "Casual Match" so the
     * value axis could find its map through the sibling NAME link, and that was
     * wrong twice over. The user named both faults: "the values won't be
     * correct if the rows are wrong, and it's confusing to announce the wrong
     * thing."
     *
     * ⛔⛔ The second fault was the serious one. With two same-named row
     * entries, menu_speak_sibling_row() returned whichever came FIRST -- the
     * 4-row map -- on BOTH screens, so on Ranked it named row 3 "One-button
     * Special Moves" when the row is Cross-region Matchmaking. The mod would
     * have announced the wrong SETTING while the user changed it.
     *
     * ⭐ The link is now keyed on the live ROW COUNT (see
     * menu_speak_sibling_row_for), which is a fact the game supplies as an
     * argument -- so the names no longer have to lie to keep the halves
     * joined. */
    { .caller = 0x14009890dULL, .base_index = -1, .count = 3,
      .name = "Ranked Match", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_ROW, .part_table = 0x141207230ULL },

    /* ⭐⭐ THE "UNNAMED 3-ROW PAIR" -- up/down INSIDE the game grid, not lists
     * (0x140098410: stack-local 0..2 cursors that move [+0x344] by a grid row
     * or leave the grid). */
    { .caller = 0x14009885eULL, .base_index = -1, .count = 0,
      .name = "Casual Match", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_GAME_GRID },
    { .caller = 0x1400988bfULL, .base_index = -1, .count = 0,
      .name = "Casual Match", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_GAME_GRID },

    /* ⚠ sibling_caller = 0x14009890d: this VALUE axis is shared by Casual Match
     * and Ranked Match, so its single `name` cannot identify which row map
     * applies. The sibling search uses (this caller, live row count) instead --
     * see menu_speak_sibling_row_for(). */
    { .caller = 0x140098986ULL, .base_index = -1, .count = 0,
      .name = "Casual Match", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_VALUE, .values_off = 0x344,
      .sibling_caller = 0x14009890dULL, .part_table = 0x141207230ULL },

    /* ⭐⭐ JOIN LOBBY > search filters. Two call sites, one screen object:
     * the vertical ROW selector and the horizontal VALUE selector.
     *
     * ⚠ values_off = 0x33c and the row cursor is +0x334 -- BOTH read from this
     * screen's own log offsets (screen 0x2C3FEEF0, row cursor 0x2C3FF224, value
     * cursors 0x2C3FF22C..0x2C3FF240 at 4-byte stride). Every settings screen
     * here has different offsets; none may be copied from another.
     *
     * ⛔ The VALUE row carries no `values` variants -- see the note above
     * g_lobby_filter_rows. It still names the ROW via the sibling link, which
     * is the useful half ("Cross-region Matchmaking, 2 of 2"). */
    { .caller = 0x14008e799ULL, .base_index = -1, .count = 6,
      .name = "Lobby Filters", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_ROW },
    { .caller = 0x14008e813ULL, .base_index = -1, .count = 0,
      .name = "Lobby Filters", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_VALUE, .values_off = 0x33c },

    /* ⭐⭐ The LOBBY menu (Online Play > Custom Match). See g_lobby_rows: all
     * three positions named by their own captions across 7 sightings. */
    /* ⭐ Since 2026-11-24 READ FROM THE GAME'S DATA: row +0x330, count +0x334,
     * the LIST-row table 0x1411fb510 (0x14007ed70), label child 7 (measured:
     * the 17:06 part dump read child 7 = "Join Lobby"). */
    { .caller = 0x140083e09ULL, .base_index = -1, .count = 0,
      .name = "Lobby", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_ROWS, .row_off = 0x330, .rows_off = 0x334,
      .part_table = 0x1411fb510ULL, .label_child = 7 },

    /* ⭐⭐ THE MUSIC PLAYER'S TRACK LIST (Museum > Music, after choosing a game).
     *
     * Caller 0x1400b2ebb, vertical. Its items are named from the game's OWN live
     * text via the instance set -- "01. Title: Take Your Best Shot" -- because
     * they can be named no other way: track titles are not GMD labels, so there
     * is no base and no map to build, and the caption channel is blind here (the
     * list's text is set ONCE on entry, 5 sets per object all at one move seq,
     * and moving the cursor sets nothing at all).
     *
     * ⚠⚠ count = 0 ON PURPOSE -- the count legitimately VARIES. This screen is
     * reached through a 9-item horizontal game strip (caller 0x14009af21: the 8
     * games plus the collection's own music), and each game's soundtrack is a
     * different length. The run that measured this had 5 tracks because it was
     * standing on one particular game. A fixed count here would disable the row
     * on every other game -- and the staleness check has nothing to protect,
     * since the name comes from the live text rather than from an index into a
     * label run.
     *
     * ⭐ The safety does not come from the count; it comes from the numbering
     * having to AGREE with the cursor. See gt_numbered_item(). */
    /* ⭐ Since 2026-11-24 READ FROM THE GAME'S DATA: refresh 0x1400b32f0
     * highlights window slot [+0x340] (top track [+0x33c]); slot s is part
     * 0x141208500[s] and the track title is written into its child 8. No
     * count field: the slot is not the track number, so the cursor's own
     * position is kept. Replaces matching the printed "02." number. */
    { .caller = 0x1400b2ebbULL, .base_index = -1, .count = 0,
      .name = "Music", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_ROWS, .row_off = 0x340,
      .part_table = 0x141208500ULL, .label_child = 8 },

    /* ⭐⭐ THE SOUNDTRACK CHOOSER ITSELF (0x14009af21, 9 items, horizontal) --
     * the strip that reaches the track list above. It spoke bare positions
     * ("2 of 9") in every run because it captions NOTHING; see
     * MC_SOUNDTRACK_STRIP for the evidence closing every other route.
     *
     * ⭐ Position 1 is USER-CONFIRMED: opening it gave a five-track list of
     * SOUND_DINGO_*, and DINGO_2400430 is CFC2's own engine codename, so the
     * first entry is the collection's own music and the eight games follow in
     * ROM-table order. The families appear in the corpus in exactly that
     * order, which tools/gen_soundtrack_table.py verifies rather than assumes.
     *
     * ⚠ count = 9 is a REAL invariant here, unlike the track list's 0: the
     * strip is always the collection plus eight games. It is the membership
     * test that keeps this map off any other nine-item screen.
     *
     * ⛔ NOT MC_GAME_STRIP, which names from a caption -- there is none. And
     * NOT to be confused with Gallery's 9-item sibling (0x14009a482), which
     * DOES caption and is handled as a game strip. Two nine-item horizontal
     * choosers, different channels, separate evidence. */
    /* ⭐ Since 2026-11-24: the refresh 0x14009b150 writes the current
     * soundtrack's title into part 0xd child 7. */
    { .caller = 0x14009af21ULL, .base_index = -1, .count = 0,
      .name = "Music", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_TEXT, .part_id = 0xd, .label_child = 7 },

    /* ⭐ PC Settings -- 4 rows, every one named by its own live caption.
     * See g_pc_settings_rows for why no base works here.
     *
     * ⚠ DERIVED FROM A RUN, NOT YET HEARD BACK. Both this row and Sound
     * Settings below were built from the captions in the 2026-09-10 log and
     * shipped without a confirming run -- the user chose to verify later. The
     * per-position evidence is as strong as any row here, but "spoke the right
     * thing on screen" is a separate claim from "the map matches the corpus",
     * and only the second has been checked. If a row here ever reads wrong,
     * suspect the POSITION ORDER first: the labels are certain, their order is
     * what a run would confirm. */
    /* ⭐⭐ MC_SETTINGS_ROW, not MC_LIST. This is the ROW half of a settings
     * screen -- 0x1400a1757 below is its VALUE half, and the two are linked by
     * this identical `name` string (menu_speak_sibling_row). Being the row
     * half is what lets the value site say "Display, Windowed, 1 of 3" instead
     * of a bare position. */
    { .caller = 0x1400a1715ULL, .base_index = -1, .count = 4,
      .name = "PC Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_ROW },

    /* ⭐ Sound Settings -- 6 rows, every one named by its own live caption,
     * and the row ORDER corroborated by the value-slot arithmetic: the row
     * selector's index and (cursor - screen - 0x358)/4 agreed on every move.
     *
     * ⚠ Same ⚠ as PC Settings above: derived from a run, not yet heard back. */
    { .caller = 0x1400a2729ULL, .base_index = -1, .count = 6,
      .name = "Sound Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_ROW },

    /* ⭐ Sound Settings -- the VALUE half.
     *
     * ⚠ values_off is 0x358, NOT the 0x338 that PC Settings and Select Game
     * use. Read from this screen's own offsets in the log (row 0 -> +0x358,
     * row 4 -> +0x368), which is exactly why the header insists this be read
     * per screen and never copied. */
    /* ⭐ Rows 0-3 are the four VOLUME sliders -- 10 values each, measured by
     * cursor address across every run. Row 4 (Challenger Jingle) is
     * Original/Remix and is deliberately NOT a level. */
    { .caller = 0x1400a2771ULL, .base_index = -1, .count = 0,
      .name = "Sound Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_VALUE, .values_off = 0x358, .levels = 0x0Fu },

    /* ⭐⭐ NETWORK SETTINGS -- the ROW half. See g_network_settings_rows. */
    { .caller = 0x14009f48eULL, .base_index = -1, .count = 5,
      .name = "Network Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_ROW },

    /* ⭐⭐ NETWORK SETTINGS -- the VALUE half.
     *
     * ⚠ values_off 0x35c, and note this screen's ROW cursor is at +0x334
     * rather than the +0x330 the others use -- two more reasons never to
     * assume one settings screen's offsets apply to another. */
    /* ⭐ Rows 1-2 are Voice Chat Volume and Input Delay, 11 values each.
     * Row 0 (Microphone) is On/Off and row 3 (Connection Strength) is a
     * named filter list -- neither is a level. */
    { .caller = 0x14009f4c1ULL, .base_index = -1, .count = 0,
      .name = "Network Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_VALUE, .values_off = 0x35c, .levels = 0x06u },

    /* ⛔⛔ THE CONSENT DIALOG (0x140087cda, 3 items) IS NOT NAMED -- REVERTED.
     *
     * I shipped SYS_WINDOW_YES2/NO2/PP ("Agree / Decline / Privacy Policy")
     * here after the user approved doing so, then the user said they were "not
     * confident about the data collection options you chose". They were right
     * to doubt it and I was wrong to ship it.
     *
     * ⚠ The screen sets NO item text -- its buttons come from layout data --
     * so nothing has ever shown these three words on it, and the ORDER was pure
     * assumption. Approval to ship a guess does not make the guess true; it
     * only means the user accepted my risk assessment, and my assessment was
     * that this is the exact "fits the count and the category" reasoning that
     * has already mislabelled two screens in this project.
     *
     * ⭐ A rare one-time dialog reading "1 of 3" is a small loss. Three
     * confident wrong words on a PRIVACY choice is not. Reverted to position
     * until the GUI tree can read the buttons. */

    /* ⭐⭐ OPTIONS > SYSTEM -- the ROW half. See g_system_settings_rows.
     *
     * ⛔ I called this screen "Controls" in earlier notes. It is Options >
     * SYSTEM, and the log settles it in two lines: the mod spoke
     * "System, 1 of 5" on the Options list, the user confirmed, and this
     * caller appeared. ⭐ The screen a caller belongs to is proved by what the
     * user SELECTED to reach it, and that is in every log for free -- I named
     * it from its CONTENTS (rows that look like button bindings) instead, and
     * was wrong. */
    { .caller = 0x1400a33b8ULL, .base_index = -1, .count = 6,
      .name = "System Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_ROW },

    /* ⭐⭐ OPTIONS > SYSTEM -- the VALUE half.
     *
     * ⚠ FOUR callers drive this screen's values, one per row group
     * (0x1400a3431 row 0, 0x1400a3538 rows 1 and 3, 0x1400a3578 row 2,
     * 0x1400a34a8 row 4). They share values_off 0x338, so each gets its own
     * row here and menu_speak_derive_row sorts out which row it is. All four
     * carry the same `name` so the sibling link finds the row map above. */
    { .caller = 0x1400a3431ULL, .base_index = -1, .count = 0,
      .name = "System Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_VALUE, .values_off = 0x338 },
    { .caller = 0x1400a3538ULL, .base_index = -1, .count = 0,
      .name = "System Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_VALUE, .values_off = 0x338 },
    { .caller = 0x1400a3578ULL, .base_index = -1, .count = 0,
      .name = "System Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_VALUE, .values_off = 0x338 },
    { .caller = 0x1400a34a8ULL, .base_index = -1, .count = 0,
      .name = "System Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_VALUE, .values_off = 0x338 },

    /* ⭐ PC Settings -- a row/value screen like Select Game.
     *
     * ESTABLISHED FROM THE SEQUENCE plus the cursor offsets, both in the
     * 13:11 run. The user was on "PC Settings, 4 of 5" in the Options list,
     * confirmed it, and 0x1400a1757 appeared with THREE items at cursor
     * screen+0x33c -- the second slot of a value-cursor array, i.e. row 1.
     * 0x1400a1715 is the matching row selector (flat, cursor +0x330).
     *
     * ⭐ Row 1's three values are MENU_OP_PC_WINDOW / _BORDERLESS /
     * _FULL_SCREEN at 220..222 -- the Display setting, whose name sits at
     * 219. Three values matches the live count exactly.
     *
     * ⛔ NO row labels shipped. The rows are Resolution (218) and Display
     * (219), but the run never moved on the ROW selector, so which row is
     * which position is not established -- and this file's rule is that a row
     * exists only when live text showed it. The value row is named because
     * its count and offset together pin it; the row list is not.
     *
     * ⚠ values_off is 0x338, the same array base as Select Game's. Read from
     * the offsets in the log (row 1 -> +0x33c => base +0x338), not assumed
     * from the other screen. */
    { .caller = 0x1400a1757ULL, .base_index = -1, .count = 0,
      .name = "PC Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_VALUE, .values_off = 0x338 },

    /* ⭐ Museum. ESTABLISHED FROM THE NAVIGATION SEQUENCE, not from a lone
     * candidate list. The 12:45 run shows the user on "Museum, 3 of 6", then
     * Enter, then this caller appearing with 2 items -- so this is the screen
     * Museum leads to.
     *
     * GMD 992 GALLERY / 993 SOUND, with HELP twins at 995/996 pairing by NAME
     * (HELP_GALLERY <-> GALLERY, HELP_SOUND <-> SOUND). Two items, matching the
     * live count. Verified across English, French and Japanese: the labels are
     * GALLERY/SOUND in all three, so this is not an artifact of one language.
     *
     * ⚠ TITLE_SELECT ("Game Select") sits at 994, immediately after -- close
     * enough to look like a third item, and it is NOT one here: the screen
     * reported TWO items and only 992/993 have paired HELP entries.
     *
     * ⛔ The rival candidate my filter offered was base 993 ("Music", "Game
     * Select"), which passes the structural test purely by luck -- its
     * "parallel HELP run" is HELP_GALLERY and HELP_SOUND, which belong to the
     * labels BEFORE it. The label NAMES are what settled it; the structure
     * alone would have shipped the off-by-one. */
    /* ⭐⭐⭐ Since 2026-11-24 READ FROM THE GAME'S DATA: list screens keep
     * the row at +0x330 and the count at +0x334, and every one of them
     * (refreshes 0x140098200 / 0x14009c090 / 0x14009a1a0, Options
     * 0x1400a2440) draws row r through the LIST part table 0x141207188;
     * the label is child 8 (main menu layout dumps: +0x050 = 8; Options
     * writes its labels into child 8 as messages). */
    { .caller = 0x140099b23ULL, .base_index = -1, .count = 0,
      .name = "Museum", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_PART_ROWS, .row_off = 0x330, .rows_off = 0x334,
      .part_table = 0x141207188ULL, .label_child = 8 },

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
    /* ⭐ Since 2026-11-24 also valued from the game's own rows: refresh
     * 0x1400b1f60 draws row r through part 0x141208408[r] (label child 7 =
     * message 0x141208498[r], value child 8 = the string it writes). */
    { .caller = 0x1400b1b69ULL, .base_index = -1, .count = 3,
      .name = "Select Game", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_ROW, .part_table = 0x141208408ULL },

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
    { .caller = 0x1400b1bd9ULL, .base_index = -1, .count = 0,
      .name = "Select Game", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_SETTINGS_VALUE, .values_off = 0x338,
      .part_table = 0x141208408ULL },
};

#define NSCREENS (int)(sizeof(g_screens) / sizeof(g_screens[0]))

int menu_speak_screen_count(void) { return NSCREENS; }

const mc_screen *menu_speak_screen_at(int i)
{
    if (i < 0 || i >= NSCREENS)
        return NULL;
    return &g_screens[i];
}

const mc_screen *menu_speak_row(int i)
{
    if (i < 0 || i >= NSCREENS)
        return NULL;
    return &g_screens[i];
}

/* ⭐⭐ THE LAST ROW-AXIS COUNT, per screen object.
 *
 * ⛔⛔ WHY THIS EXISTS. One caller (0x14009890d) serves BOTH Casual Match
 * (4 rows) and Ranked Match (3), and the two share a VALUE axis. The value site
 * finds its row map through the screen NAME, so with two same-named row entries
 * the search returned whichever came first -- the 4-row map -- on BOTH screens.
 * On Ranked that names row 3 "One-button Special Moves" when it is actually
 * Cross-region Matchmaking: the mod announces the WRONG SETTING while the user
 * changes it. User-reported: "the values won't be correct if the rows are
 * wrong, and it's confusing to announce the wrong thing."
 *
 * ⭐ The row count is not guessed or read out of memory -- it arrives as an
 * ARGUMENT to the row axis (`max_index`, measured at screen+0x334 across every
 * log), so this only remembers what the game already told us.
 *
 * ⚠ KEYED ON THE SCREEN OBJECT, so a stale count cannot leak across screens: a
 * different screen pointer yields no answer rather than a wrong one. The
 * pointer is only COMPARED, never dereferenced.
 *
 * ⚠ Cross-movement state is a hazard in this file, so it is deliberately
 * minimal: one pointer and one int, written only by the row axis, and read only
 * to disambiguate same-named siblings. When it does not apply, the old
 * name-only behaviour stands. */
static const void *g_last_row_screen;
static int         g_last_row_total;

void menu_speak_note_row_total(const void *screen, int total)
{
    if (!screen || total <= 0)
        return;
    g_last_row_screen = screen;
    g_last_row_total  = total;
}

void menu_speak_reset_row_total(void)
{
    g_last_row_screen = NULL;
    g_last_row_total  = 0;
}

/* Find the MC_SETTINGS_ROW entry belonging to the same screen as `v`.
 *
 * ⭐ Matched on the screen NAME, so the two halves of one screen (the row
 * selector and the value selector) stay linked through one fact rather than a
 * second hand-copied address that could drift.
 *
 * ⭐⭐ AND, when several row entries share that name, on the live ROW COUNT --
 * which is what tells Casual Match (4) from Ranked Match (3). See
 * menu_speak_note_row_total(). `screen` is the value movement's own screen
 * pointer; pass NULL when it is not known and the name-only match stands. */
const mc_screen *menu_speak_sibling_row_for(const mc_screen *v,
                                            const void *screen)
{
    const mc_screen *first = NULL;
    int matches = 0;

    if (!v || !v->name)
        return NULL;
    for (int i = 0; i < NSCREENS; i++) {
        if (g_screens[i].kind != MC_SETTINGS_ROW || !g_screens[i].name ||
            strcmp(g_screens[i].name, v->name) != 0)
            continue;
        matches++;
        if (!first)
            first = &g_screens[i];
    }
    /* ⭐⭐ THE ROW COUNT WINS WHENEVER IT IS KNOWN FOR THIS SCREEN OBJECT, even
     * when the name matched exactly once.
     *
     * ⚠⚠ THIS IS THE CASE THAT MATTERS, and it is not the obvious one. Casual
     * Match and Ranked Match share a VALUE axis, and that axis can carry only
     * ONE name. So on Ranked the value entry's name matches the CASUAL row
     * entry -- a single, unambiguous-looking match that is nonetheless the
     * WRONG MAP. Gating this on `matches > 1` would have missed exactly the bug
     * it exists to fix: the ambiguity is between the value axis and the row
     * entries, not among the row entries themselves.
     *
     * ⭐ The candidates are the row entries sharing the VALUE entry's CALLER-set
     * -- i.e. every row entry this value axis could plausibly belong to. Rather
     * than model that, take the simplest sound rule: among row entries whose
     * name matches, or which were seen on this same screen object, prefer the
     * one whose count equals the live row count. */
    if (screen && screen == g_last_row_screen && g_last_row_total > 0) {
        /* First choice: a same-named entry with the right count. */
        for (int i = 0; i < NSCREENS; i++) {
            if (g_screens[i].kind == MC_SETTINGS_ROW && g_screens[i].name &&
                strcmp(g_screens[i].name, v->name) == 0 &&
                g_screens[i].count == g_last_row_total)
                return &g_screens[i];
        }
        /* ⭐ Second choice: the name does NOT match any entry with this count,
         * which is the Ranked case -- the value axis says "Casual Match" while
         * the screen has 3 rows. Fall back to the row entry sharing the value
         * axis's CALLER pairing via `sibling_caller`, matched on count. */
        if (v->sibling_caller) {
            for (int i = 0; i < NSCREENS; i++) {
                if (g_screens[i].kind == MC_SETTINGS_ROW &&
                    g_screens[i].caller == v->sibling_caller &&
                    g_screens[i].count == g_last_row_total)
                    return &g_screens[i];
            }
        }
        /* ⛔ A count is known and NOTHING matches it. The maps on file do not
         * describe the screen in front of the user, so naming a row from any of
         * them would be a guess. A bare position is always true. */
        if (first && first->count != g_last_row_total)
            return NULL;
    }

    return first;
}

const mc_screen *menu_speak_sibling_row(const mc_screen *v)
{
    return menu_speak_sibling_row_for(v, NULL);
}


const mc_screen *menu_speak_lookup(ULONGLONG caller)
{
    for (int i = 0; i < NSCREENS; i++) {
        if (g_screens[i].caller == caller)
            return &g_screens[i];
    }
    return NULL;
}

/* ⭐⭐ ONE CALLER CAN SERVE SEVERAL SCREENS, so the caller alone is no longer a
 * sufficient key -- MEASURED, not anticipated.
 *
 * 0x14009ba21 shipped as "Select Mode" (3 items: Arcade / Versus / Training).
 * Across the runs of 2026-09-09 22:50 and 23:13 it appeared FIFTEEN times and
 * was a SIX-item screen every single time -- the Online Play menu, identified
 * from the game's own captions. The generic cursor helper is shared, so the
 * same call site drives more than one menu.
 *
 * ⚠ The count guard already kept this SAFE: a row whose count disagrees is
 * refused and the mod speaks position only. What it could not do is let the
 * OTHER screen be named, because this lookup returned the first caller match
 * and menu_speak_build then rejected it.
 *
 * ⭐ So match on caller AND live item count. A row with count <= 0 (a map-only
 * row that deliberately names nothing yet) still matches on caller alone, so
 * nothing that worked before changes.
 *
 * ⛔ This does NOT relax any verification rule. Both rows for a shared caller
 * must still be MC_VERIFIED_LIVE_TEXT; the count is a DISCRIMINATOR between
 * verified rows, never a reason to admit an unverified one. */
const mc_screen *menu_speak_lookup_n(ULONGLONG caller, int item_count)
{
    const mc_screen *any_count = NULL;     /* an explicit count == 0 entry */
    const mc_screen *caller_only = NULL;   /* last resort: first match */

    for (int i = 0; i < NSCREENS; i++) {
        if (g_screens[i].caller != caller)
            continue;
        /* ⛔⛔ A CLAIM-ONLY entry is NOT a catch-all. MC_GAME_ROSTER carries
         * count == 0 because its eight tabs have eight different sizes -- but
         * its identity comes from a field on the screen object, so without a
         * screen it can establish nothing and must not stand in for one.
         *
         * Letting it act as the caller's catch-all made it swallow EVERY
         * unmatched count on 0x140079739: the 35-item page stopped reporting
         * COUNT_MISMATCH and reported "text unreadable" instead, which is the
         * shadowing this function's comments already warn about. Caught by case
         * 33n, which asserts the user-visible refusal rather than table shape. */
        if (g_screens[i].kind == MC_GAME_ROSTER)
            continue;
        if (g_screens[i].count > 0 && g_screens[i].count == item_count)
            return &g_screens[i];          /* exact: caller AND count agree */
        /* ⭐⭐ A count == 0 entry is a deliberate CATCH-ALL and must beat the
         * table's ordering.
         *
         * ⛔ It used to fall back to the FIRST caller match, which shadowed
         * this: Training Options ships a 6-row LABEL MAP and a count == 0
         * MC_ROW_COLUMN entry on one caller, and a 3-row tab would have been
         * handed the 6-row map -- one screen indexing another's labels, which
         * is the confident-wrong-name failure case 9 exists to prevent.
         *
         * ⚠ Only ONE catch-all per caller is legal; case 9 enforces that, so
         * this choice is unambiguous rather than order-dependent. */
        if (g_screens[i].count == 0 && any_count == NULL)
            any_count = &g_screens[i];
        else if (caller_only == NULL)
            caller_only = &g_screens[i];
    }
    return any_count ? any_count : caller_only;
}

/* ⭐⭐⭐ Does this entry CLAIM the live screen, on evidence only the screen
 * object carries?
 *
 * ⛔⛔ THE PROBLEM IT SOLVES. Fighter Awards' achievements grid and Project
 * Justice's Play Stats roster share a caller AND an item count of 40. A
 * caller+count lookup necessarily picks one, and picking the achievements row
 * makes MJUS's tab speak achievement titles where character names belong --
 * confidently, and undetectably to a listener.
 *
 * ⭐ A count cannot separate them, so the claim is decided by a FIELD the
 * screen itself carries: the 0..7 tab index at +roster_off. The roster entry
 * claims the screen only when that index is in range AND names a roster whose
 * size equals the live count. On the achievements page the same field must
 * independently read 6 for the claim to succeed, so the two pages are
 * separated by evidence rather than by table order.
 *
 * ⚠ Returns FALSE for anything it cannot establish -- an unreadable pointer, a
 * field out of range, a size disagreement. "Could not tell" must never read as
 * "yes": the fallback is the count match, which is what shipped before.
 *
 * ⚠ Guarded read. `screen` comes from a live game thread; this module reads it
 * and never trusts it. */
/* ⭐⭐ WHY a claim was refused -- reported, never inferred.
 *
 * ⛔ USER-VISIBLE BUG THIS EXISTS FOR: in the 02:38 run, 5 of 6 roster
 * movements named correctly and ONE fell through to the achievements entry,
 * speaking a bare "3 of 48". The log could not say whether the field was
 * unreadable, out of range, or named a roster of the wrong size -- three
 * different bugs with three different fixes, and one indistinguishable log
 * line. A refusal that cannot be diagnosed costs a whole run to re-measure. */
typedef enum {
    MC_CLAIM_OK = 0,
    MC_CLAIM_NOT_APPLICABLE,   /* not a roster entry, or no screen/offset */
    MC_CLAIM_UNREADABLE,       /* the field could not be read at all */
    MC_CLAIM_OUT_OF_RANGE,     /* read, but not a 0..7 game index */
    MC_CLAIM_SIZE_DISAGREES    /* in range, but that roster is a different size */
} mc_claim_why;

static mc_claim_why mc_entry_claims_why(const mc_screen *s, const void *screen,
                                        int item_count, int *out_tab)
{
    int tab = -1;

    if (out_tab) *out_tab = -1;
    if (!s || s->kind != MC_GAME_ROSTER || !screen || s->roster_off <= 0)
        return MC_CLAIM_NOT_APPLICABLE;
    {
        const void *tc = (const char *)screen + s->roster_off;
        if (!mem_readable(tc, sizeof(tab)) ||
            !mem_copy(&tab, tc, sizeof(tab)))
            return MC_CLAIM_UNREADABLE;
    }
    if (out_tab) *out_tab = tab;
    if (tab < 0 || tab >= 8)
        return MC_CLAIM_OUT_OF_RANGE;
    if (g_rosters[tab].count != item_count)
        return MC_CLAIM_SIZE_DISAGREES;
    return MC_CLAIM_OK;
}

static BOOL mc_entry_claims(const mc_screen *s, const void *screen,
                            int item_count)
{
    int tab = -1;

    if (!s || s->kind != MC_GAME_ROSTER || !screen || s->roster_off <= 0)
        return FALSE;
    {
        const void *tc = (const char *)screen + s->roster_off;
        if (!mem_readable(tc, sizeof(tab)) ||
            !mem_copy(&tab, tc, sizeof(tab)))
            return FALSE;
    }
    if (tab < 0 || tab >= 8)
        return FALSE;
    return g_rosters[tab].count == item_count;
}

/* The screen-aware lookup. Identical to menu_speak_lookup_n() except that an
 * entry which CLAIMS the live screen wins over a plain count match.
 *
 * ⭐ Claims are checked before count matches, but a claim is itself gated on
 * the live count agreeing with the identity the screen reports -- so this
 * strengthens the count test rather than bypassing it.
 *
 * ⚠ With `screen` NULL this is exactly menu_speak_lookup_n(), which is what
 * every offline test that does not model a screen object still exercises. */
const mc_screen *menu_speak_lookup_for(ULONGLONG caller, int item_count,
                                       const void *screen)
{
    if (screen) {
        for (int i = 0; i < NSCREENS; i++) {
            if (g_screens[i].caller != caller)
                continue;
            if (mc_entry_claims(&g_screens[i], screen, item_count))
                return &g_screens[i];

            /* ⭐⭐ A ROSTER ENTRY THAT DECLINED SAYS WHY, ONCE PER REASON.
             *
             * ⛔ Measured in the 02:38 run: 5 of 6 roster movements named and
             * ONE spoke a bare "3 of 48". Three different causes produce that
             * same outcome and the log could not tell them apart, so the next
             * step would have been a guess. It reports the tab VALUE too --
             * an out-of-range read and a mid-update read look identical
             * without it.
             *
             * ⚠ Per-reason, not per-movement: a user sweeping a 48-item
             * roster must not generate 48 log lines. */
            if (g_screens[i].kind == MC_GAME_ROSTER) {
                static BOOL said[5];
                int tab = -1;
                mc_claim_why w = mc_entry_claims_why(&g_screens[i], screen,
                                                     item_count, &tab);
                if (w != MC_CLAIM_OK && w != MC_CLAIM_NOT_APPLICABLE &&
                    (unsigned)w < 5 && !said[w]) {
                    said[w] = TRUE;
                    log_line("mcspeak: the roster entry declined: %s "
                             "(tab field at +0x%x read %d, live count %d). "
                             "Speaking position only.",
                             w == MC_CLAIM_UNREADABLE   ? "the tab field could "
                                 "not be READ -- nothing is concluded" :
                             w == MC_CLAIM_OUT_OF_RANGE ? "the tab index is "
                                 "OUTSIDE 0..7" :
                                 "that tab's roster is a DIFFERENT SIZE",
                             (unsigned)g_screens[i].roster_off, tab,
                             item_count);
                }
            }
        }
    }
    return menu_speak_lookup_n(caller, item_count);
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

/* gt_screen_texts() callback for the move list: learn this game's icon names
 * from any string on the screen that is a legend. */
static BOOL learn_legend_cb(const char *text, void *ctx)
{
    *(int *)ctx += gm_icon_learn_legend(text);
    return TRUE;
}

/* ⭐⭐⭐ THE PER-GAME KEYBOARD CONTROLS GRID -- one row, named and valued.
 *
 * ⛔⛔ WHAT WAS WRONG, user-reported 2026-11-24: "the controls menus are not
 * reading their values." Three faults, all on this one screen:
 *   1. The row map was the live objects' CREATION order (Key Bindings, Default,
 *      Menu, ... Heavy Kick, "Type A"), not the cursor's order. The 03:30 run's
 *      own captions contradicted it: position 2 carried "Select an action to
 *      assign." and the move that landed on Default carried "Reset to default
 *      settings." -- while the map called them Default and Down. And "Type A"
 *      is a VALUE, not a row.
 *   2. The helper's index is NOT the row. The screen's code (0x14009ec58 on)
 *      rewrites the cursor at +0x334 after the helper returns:
 *          old 7,  new 8  -> 15   (down off the left column lands on Default)
 *          old 15, new 14 -> 7    (up from Default returns to the left column)
 *          old 8,  new 7  -> 0    (up off the right column goes to Key Bindings)
 *          old == [+0x37c] (last button), moving past it -> 15
 *      Every one of these is visible in run 15:55's raw `mc:` lines: the move
 *      after "15 -> 14" starts from 7, the move after "7 -> 8" from 15.
 *   3. 0x14009ed68 is the LEFT/RIGHT COLUMN SWITCH, not a Yes/No dialog. It
 *      passes a stack-local 0/1 side (setge on row >= 8) and then writes
 *      row +/- 7, clamped to [+0x37c]. It was named "Confirm" and read the
 *      x=960 column, which holds one label off-dialog, so every left/right said
 *      "2 of 2".
 *
 * ⭐ So every call site on this screen reads the SAME thing: the row cursor at
 * +0x334, read on the worker AFTER the game's rewrite (the hook only records;
 * menu_speak runs on the next poll). Index layout from the code above:
 *     0 Key Bindings | 1..7 left column | 8..14 right column | 15 Default
 * with right-column row r on the same line as left-column row r-7.
 *
 * ⭐⭐ NAMES AND VALUES COME FROM THE GAME'S OWN OBJECTS, never positions.
 * The refresh routine (0x14009f100, and the setup loop at 0x14009e861) gives
 * the whole mapping:
 *   part ID of row r    = int table at 0x141207620[r]
 *   row r's value       = written into that part's child 8 by 0x14007d290
 *                         (row 0: message 234 + type; key rows: the bound key
 *                          via 0x1412076f0; row 15 Default: none)
 *   row r's label       = the part's child 7, set by the layout
 *   key rows drawn      = 1..[+0x37c] (the game skips the rest)
 * gt_part_text() walks the game's GUI tree to that exact object and returns
 * the text the game set on it. A game with three buttons reads its own three,
 * and any language reads its own words.
 *
 * ⚠ Refuses (position only) when the row reads outside 0..15, names a key
 * row the game does not draw, or the object has no text: a part that cannot
 * be found is never replaced by a nearby one. */
/* The row -> part table shared by every row-indexed settings helper
 * (0x14007e130, 0x14007ede0, 0x14007ea80). */
#define SETTINGS_PART_TABLE 0x1411fb4f0ULL
#define KG_ROW_OFF      0x334
#define KG_LAST_KEY_OFF 0x37c
#define KG_PART_TABLE   0x141207620ULL

/* One int from the exe's own data (ASLR off, no relocations). Guarded. */
static BOOL exe_int_real(ULONGLONG addr, int *out)
{
    const void *p = (const void *)(ULONG_PTR)addr;
    *out = 0;
    return mem_readable(p, sizeof(int)) && mem_copy(out, p, sizeof(int));
}
static BOOL (*g_exe_int)(ULONGLONG, int *) = exe_int_real;
static BOOL exe_int(ULONGLONG addr, int *out) { return g_exe_int(addr, out); }
void menu_speak_test_set_exe_reader(BOOL (*fn)(ULONGLONG, int *))
{
    g_exe_int = fn ? fn : exe_int_real;
}

static BOOL screen_int(const void *screen, int off, int *out)
{
    const void *p = (const char *)screen + off;
    *out = 0;
    return screen && mem_readable(p, sizeof(int)) && mem_copy(out, p, sizeof(int));
}

/* Label (child 7) and value (child 8) of one part, spoken. */
static BOOL part_line(const void *screen, int part, BOOL want_value,
                      const char *who, mc_speech *out)
{
    char label[GT_CAPTION_MAX], value[GT_CAPTION_MAX];
    char sl[GM_OUT_MAX], sv[GM_OUT_MAX];

    if (!gt_part_text(screen, part, 7, label, (int)sizeof(label))) {
        log_line("mcspeak: %s -- part %d has no label text in the GUI tree; "
                 "speaking position only.", who, part);
        gt_part_dump_once(screen, part, who);
        out->why = MC_UNNAMED_TEXT_UNREADABLE;
        return FALSE;
    }
    gm_speak(label, sl, sizeof(sl));
    if (!sl[0] || gm_has_residue(sl)) {
        out->why = MC_UNNAMED_TEXT_UNREADABLE;
        return FALSE;
    }
    sv[0] = '\0';
    if (want_value && gt_part_text(screen, part, 8, value, (int)sizeof(value))) {
        gm_speak(value, sv, sizeof(sv));
        if (gm_has_residue(sv))
            sv[0] = '\0';
    }
    if (sv[0])
        _snprintf(out->item, sizeof(out->item), "%s, %s", sl, sv);
    else
        _snprintf(out->item, sizeof(out->item), "%s", sl);
    out->item[sizeof(out->item) - 1] = '\0';
    out->named = TRUE;
    out->why   = MC_NAMED_OK;
    return TRUE;
}

static void key_grid_build(const mc_move *m, mc_speech *out)
{
    int row = -1, last_key = -1, part = 0;

    if (!m->screen || !screen_int(m->screen, KG_ROW_OFF, &row) ||
        row < 0 || row > 15) {
        log_line("mcspeak: key grid -- row cursor at +0x%x unreadable or out "
                 "of range (%d); speaking position only.", KG_ROW_OFF, row);
        out->why = MC_UNNAMED_ROW_UNDERIVABLE;
        return;
    }
    if (row >= 1 && row <= 14 &&
        (!screen_int(m->screen, KG_LAST_KEY_OFF, &last_key) || row > last_key)) {
        log_line("mcspeak: key grid -- row %d is past the last drawn key row "
                 "(%d); speaking position only.", row, last_key);
        out->why = MC_UNNAMED_ROW_UNDERIVABLE;
        return;
    }
    if (!exe_int(KG_PART_TABLE + 4ull * (ULONGLONG)row, &part)) {
        out->why = MC_UNNAMED_ROW_UNDERIVABLE;
        return;
    }
    if (part_line(m->screen, part, row != 15, "key grid", out))
        out->bare = TRUE;
}

/* ⭐⭐ THE GAMEPAD CONTROLS SCREEN, from its refresh routine 0x1400a0e10.
 * Eight on-screen SLOTS over a scrolling list:
 *   selected item   = [+0x338] + [+0x33c]   (r13d)
 *   slot s's item   = 0 for s == 0, [+0x340] - 1 for s == 7 (Default),
 *                     [+0x338] + s otherwise
 *   slot s's part   = int table at 0x141207848[s]
 *   label           = child 7 (message 0x141207868[item], by 0x14007d290)
 *   slot 0's value  = child 8 (message 234 + type)
 * The focused slot is the one whose item equals the selected item -- the
 * comparison the routine itself makes to choose each slot's highlight. */
#define PAD_SCROLL_OFF  0x338
#define PAD_CURSOR_OFF  0x33c
#define PAD_COUNT_OFF   0x340
#define PAD_PART_TABLE  0x141207848ULL

static void pad_list_build(const mc_move *m, mc_speech *out)
{
    int scroll, cursor, count, sel, slot = -1, part = 0;

    if (!m->screen || !screen_int(m->screen, PAD_SCROLL_OFF, &scroll) ||
        !screen_int(m->screen, PAD_CURSOR_OFF, &cursor) ||
        !screen_int(m->screen, PAD_COUNT_OFF, &count) || count < 2) {
        out->why = MC_UNNAMED_ROW_UNDERIVABLE;
        return;
    }
    sel = scroll + cursor;
    for (int s = 0; s < 8; s++) {
        int item = s == 0 ? 0 : s == 7 ? count - 1 : scroll + s;
        if (item == sel) { slot = s; break; }
    }
    if (slot < 0 || !exe_int(PAD_PART_TABLE + 4ull * (ULONGLONG)slot, &part)) {
        log_line("mcspeak: controller settings -- no slot shows item %d "
                 "(scroll %d, cursor %d, count %d); speaking position only.",
                 sel, scroll, cursor, count);
        out->why = MC_UNNAMED_ROW_UNDERIVABLE;
        return;
    }
    /* ⭐ Established 2026-11-24 15:50 from the part dump: on this screen
     * child 7 is the controller BUTTON (<ICON BTN_X>) and child 8 the ACTION
     * assigned to it ("Light Punch"), on every slot -- the reverse of the
     * keyboard screen, and exactly what part_line() reads. */
    if (part_line(m->screen, part, TRUE, "controller settings", out)) {
        if (m->vertical && out->total == 8) {
            out->position = sel + 1;
            out->total = count;
        } else {
            out->bare = TRUE;
        }
    }
}

/* ⭐⭐ IS THIS SETTINGS ROW SHOWING A VALUE AT ALL?
 *
 * ⛔ User run 2026-11-24 16:18: the EX page's Default row spoke "Default,
 * 3 (First to 2)" -- page 1's Max Rounds value. The value object keeps the
 * last text set on it; for an action row the game HIDES it by playing the
 * action-row animation (0x14007eaf0, states 0x1411fb640) instead of the
 * value-row one (0x14007ea80, states 0x1411fb620). Game Settings' refresh
 * picks between them per setting ([+0x514 + id*4]).
 *
 * So: the row's part is playing one of the action-row states -> no value.
 * Both sides come from the game (the part's current animation, the exe's
 * state table). An animation that cannot be read is NOT treated as hidden:
 * that is "unknown", and the value is then still subject to every other
 * check. */
#define SETTINGS_ACTION_STATES 0x1411fb640ULL

static BOOL settings_value_hidden(const void *screen, int part)
{
    int anim = 0;
    if (!gt_part_anim(screen, part, &anim))
        return FALSE;
    for (int i = 0; i < 3; i++) {
        int st = 0;
        if (exe_int(SETTINGS_ACTION_STATES + 4ull * (ULONGLONG)i, &st) &&
            st == anim)
            return TRUE;
    }
    return FALSE;
}

/* ⭐⭐⭐ MC_PART_ROWS: a row list read entirely from the screen's own data.
 *
 * Every field comes from the screen's refresh routine (see the table entry):
 * the row cursor and row count are screen fields, the part ID is an exe
 * table indexed by row, and the label/value are the part children whose
 * +0x050 index the layout dumps record. Nothing is located by position.
 *
 * Row axis (the entry's own vertical caller): "<label>[, <value>], N of M",
 * with N/M the game's row and row count. Value axis: "<label>, <value>,
 * <position in the value list>" -- the list position is the helper's own
 * range, so it is the game's statement too.
 *
 * ⚠ A level row (the value is drawn as graphics, e.g. Game Difficulty) is
 * recognised only by its LABEL being the entry's `level_label` message, and
 * only on the value axis, where the position is the setting.
 *
 * ⭐ The tab title (Training Options) is the tab's own MESSAGE, read when the
 * tab changes -- atomic with the tab index, unlike the heading OBJECT, which
 * the game refreshes on another schedule (it led the rows by one tab). */
static void part_rows_build(const mc_screen *s, const mc_move *m, mc_speech *out)
{
    int tab = 0, row = -1, rows = -1, part = 0;
    char label[GT_CAPTION_MAX], value[GT_CAPTION_MAX];
    char sl[GM_OUT_MAX], sv[GM_OUT_MAX];
    BOOL row_axis = (m->vertical &&
                     m->caller == (void *)(ULONG_PTR)s->caller);
    const char *who = s->name ? s->name : "(unnamed)";

    if (!m->screen || !s->part_table || s->row_off <= 0 || s->label_child <= 0) {
        out->why = MC_UNNAMED_TEXT_UNREADABLE;
        return;
    }
    if (s->tab_off > 0 && (!screen_int(m->screen, s->tab_off, &tab) ||
                           tab < 0 || tab > 15)) {
        out->why = MC_UNNAMED_ROW_UNDERIVABLE;
        return;
    }
    if (!screen_int(m->screen, s->row_off + 4 * tab, &row) ||
        (s->rows_off > 0 && !screen_int(m->screen, s->rows_off + 4 * tab, &rows)) ||
        row < 0 || row >= 32 || (s->rows_off > 0 && (rows <= 0 || row >= rows))) {
        log_line("mcspeak: %s -- row %d of %d (tab %d) is not a drawn row; "
                 "speaking position only.", who, row, rows, tab);
        out->why = MC_UNNAMED_ROW_UNDERIVABLE;
        return;
    }
    if (!exe_int(s->part_table + 4ull * (ULONGLONG)row, &part)) {
        out->why = MC_UNNAMED_ROW_UNDERIVABLE;
        return;
    }
    if (!gt_part_text(m->screen, part, s->label_child, label, (int)sizeof(label))) {
        log_line("mcspeak: %s -- row %d (part %d) has no label in child %d; "
                 "speaking position only.", who, row, part, s->label_child);
        gt_part_dump_once(m->screen, part, who);
        out->why = MC_UNNAMED_TEXT_UNREADABLE;
        return;
    }
    gm_speak(label, sl, sizeof(sl));
    if (!sl[0] || gm_has_residue(sl)) {
        out->why = MC_UNNAMED_TEXT_UNREADABLE;
        return;
    }
    sv[0] = 0;
    if (s->value_child > 0 &&
        !(s->part_table == SETTINGS_PART_TABLE &&
          settings_value_hidden(m->screen, part)) &&
        gt_part_text(m->screen, part, s->value_child, value, (int)sizeof(value))) {
        gm_speak(value, sv, sizeof(sv));
        if (gm_has_residue(sv))
            sv[0] = 0;
    }

    if (s->title_table && s->title_idx_off > 0) {
        static const void *last_screen;
        static int last_tab = -1;
        if (m->screen != last_screen || tab != last_tab) {
            char title[GT_CAPTION_MAX], ts[GM_OUT_MAX];
            int tid = -1, msg = 0;
            if (screen_int(m->screen, s->title_idx_off + 4 * tab, &tid) &&
                tid >= 0 && tid < 64 &&
                exe_int(s->title_table + 4ull * (ULONGLONG)tid, &msg) &&
                gmd_text_by_index(msg, title, sizeof(title)) && title[0]) {
                gm_speak(title, ts, sizeof(ts));
                if (ts[0] && !gm_has_residue(ts) && strcmp(ts, sl) != 0) {
                    _snprintf(out->title, sizeof(out->title), "%s", ts);
                    out->title[sizeof(out->title) - 1] = 0;
                }
            }
            last_screen = m->screen;
            last_tab = tab;
        }
    }

    if (row_axis) {
        out->axis = MC_AXIS_ROW;
        menu_speak_note_row_total(m->screen, rows > 0 ? rows : out->total);
        if (sv[0])
            _snprintf(out->item, sizeof(out->item), "%s, %s", sl, sv);
        else
            _snprintf(out->item, sizeof(out->item), "%s", sl);
        out->item[sizeof(out->item) - 1] = 0;
        /* ⚠ The row is the game's only when the entry names a count field. A
         * scrolling list (the Music tracks) gives a window SLOT here, so its
         * position stays the cursor's own. */
        if (s->rows_off > 0) {
            out->position = row + 1;
            if (rows > 0)
                out->total = rows;
        }
        out->named = TRUE;
        out->why = MC_NAMED_OK;
        return;
    }

    /* Value axis. */
    out->axis = MC_AXIS_VALUE;
    out->have_row = TRUE;
    out->row = row;
    _snprintf(out->row_name, sizeof(out->row_name), "%s", sl);
    out->row_name[sizeof(out->row_name) - 1] = 0;
    if (sv[0]) {
        _snprintf(out->item, sizeof(out->item), "%s", sv);
        out->item[sizeof(out->item) - 1] = 0;
        out->named = TRUE;
        out->why = MC_NAMED_OK;
        return;
    }
    if (s->level_label > 0) {
        char want[GT_CAPTION_MAX];
        if (gmd_text_by_index(s->level_label, want, sizeof(want)) &&
            strcmp(want, label) == 0) {
            out->is_level = TRUE;
            out->why = MC_NAMED_OK;
            return;
        }
    }
    /* No value text: the row is named and the value's list position is
     * spoken -- both true, neither claimed as more. */
    out->why = MC_UNNAMED_NO_LABELS;
}

/* ⭐⭐⭐ THE LOBBY-ID CHARACTER, from the game's own data.
 *
 * The refresh routine 0x14008ad70 writes slot i as
 *     charset[ screen[0x334 + 4*i] ]   into part 2, child 0x141206cb0[i]
 * with `charset` the string pointed to by 0x141206c90 -- in the dump
 * "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ" -- and the slot count at +0x354.
 * The child table reads 0a 0c 0b 0d 0e 0f: exactly the "slots 2 and 3
 * INVERTED" order measured on screen in September, so the code and the live
 * objects agree. The 36-item picker (0x14008a8ef) moves screen[0x334 +
 * 4*slot] itself, and the slot strip (0x14008a91b) moves the slot at +0x350,
 * so both read the same character: the current slot's.
 * ⚠ The charset must read as exactly 36 printable characters, or nothing is
 * said -- a wrong pointer must never yield a plausible character. */
#define LOBBY_CHARSET_PTR 0x141206c90ULL
#define LOBBY_SLOT_OFF    0x350
#define LOBBY_NSLOT_OFF   0x354
#define LOBBY_CHARS_OFF   0x334

static BOOL lobby_char(const void *screen, char *out)
{
    int slot = -1, nslot = 0, idx = -1, lo = 0, hi = 0;
    char cs[37];
    const char *ptr;

    out[0] = 0;
    if (!screen || !screen_int(screen, LOBBY_SLOT_OFF, &slot) ||
        !screen_int(screen, LOBBY_NSLOT_OFF, &nslot) ||
        nslot < 1 || nslot > 16 || slot < 0 || slot >= nslot ||
        !screen_int(screen, LOBBY_CHARS_OFF + 4 * slot, &idx) ||
        idx < 0 || idx >= 36)
        return FALSE;
    if (!exe_int(LOBBY_CHARSET_PTR, &lo) || !exe_int(LOBBY_CHARSET_PTR + 4, &hi))
        return FALSE;
    ptr = (const char *)(ULONG_PTR)(((ULONGLONG)(unsigned)hi << 32) |
                                    (unsigned)lo);
    if (!ptr || !mem_readable(ptr, 37) || !mem_copy(cs, ptr, 37) || cs[36] != 0)
        return FALSE;
    for (int i = 0; i < 36; i++)
        if (cs[i] < 0x21 || cs[i] > 0x7e)
            return FALSE;
    out[0] = cs[idx];
    out[1] = 0;
    return TRUE;
}

/* The on-screen slot of `item` on a screen that may scroll (scroll_off). */
static int settings_slot(const mc_screen *e, const void *screen, int item)
{
    int top = 0;
    if (!e || e->scroll_off <= 0)
        return item;
    if (!screen_int(screen, e->scroll_off, &top) || top < 0 || top > item)
        return -1;
    return item - top;
}


/* ⭐⭐ MC_PART_TEXT: the current item's title, from the one part the strip's
 * refresh writes it into. */
static void part_text_build(const mc_screen *s, const mc_move *m, mc_speech *out)
{
    char t[GT_CAPTION_MAX], sp[GM_OUT_MAX];
    if (!m->screen || s->part_id <= 0 || s->label_child <= 0 ||
        !gt_part_text(m->screen, s->part_id, s->label_child, t, (int)sizeof(t))) {
        log_line("mcspeak: %s -- part %d child %d holds no text; speaking "
                 "position only.", s->name ? s->name : "(unnamed)", s->part_id,
                 s->label_child);
        if (m->screen && s->part_id > 0)
            gt_part_dump_once(m->screen, s->part_id, s->name);
        out->why = MC_UNNAMED_TEXT_UNREADABLE;
        return;
    }
    gm_speak(t, sp, sizeof(sp));
    if (!sp[0] || gm_has_residue(sp)) {
        out->why = MC_UNNAMED_TEXT_UNREADABLE;
        return;
    }
    _snprintf(out->item, sizeof(out->item), "%s", sp);
    out->item[sizeof(out->item) - 1] = 0;
    out->named = TRUE;
    out->why = MC_NAMED_OK;
}

/* ⭐⭐ CASUAL MATCH'S GAME CHECKLIST: is the game ticked?
 * The refresh 0x140098c50 draws game i ticked when bit i of [+0x340] is set
 * (animations 0xf4258 / 0xf4259 vs 5 / 1). The status word is the mod's own
 * (MT_SELECTED / MT_NOT_SELECTED), in the game's language.
 *
 * ⭐ Toggling a game moves no cursor, so a WATCH follows the last checklist
 * screen moved on: when its tick mask changes, the highlighted game and its new
 * status are spoken. ⚠ The screen object's first qword (its vtable) is
 * re-checked on every read; a freed or reused screen stops the watch instead
 * of being read. */
#define TICK_MASK_OFF  0x340
#define TICK_HILITE_OFF 0x344

static const void *g_tick_screen;
static ULONGLONG   g_tick_vt;
static int         g_tick_mask = -1;

static BOOL tick_status(const void *screen, int game, char *out, size_t cap)
{
    int mask = 0;
    out[0] = 0;
    if (game < 0 || game > 30 || !screen_int(screen, TICK_MASK_OFF, &mask))
        return FALSE;
    mt_fmt8(out, cap, (mask >> game) & 1 ? MT_SELECTED : MT_NOT_SELECTED,
            NULL, NULL, NULL);
    return out[0] != 0;
}

static void tick_watch(const void *screen)
{
    ULONGLONG vt = 0;
    int mask = 0;
    if (!screen || !mem_readable(screen, sizeof(vt)) ||
        !mem_copy(&vt, screen, sizeof(vt)) ||
        !screen_int(screen, TICK_MASK_OFF, &mask)) {
        g_tick_screen = NULL;
        return;
    }
    g_tick_screen = screen;
    g_tick_vt = vt;
    g_tick_mask = mask;
}

static BOOL game_title(int game, int version, char *out, size_t cap)
{
    int ri = rom_table_index_for(game, version);
    if (ri < 0)
        ri = rom_table_index_for(game, version ? 0 : 1);
    return ri >= 0 && rom_table_title(ri, out, cap) && out[0];
}

void menu_speak_poll_ticks(void)
{
    ULONGLONG vt = 0;
    int mask = 0, game = -1, version = 1, v = -1;
    char title[128], st[64], line[256];

    if (!g_tick_screen)
        return;
    if (!mem_readable(g_tick_screen, sizeof(vt)) ||
        !mem_copy(&vt, g_tick_screen, sizeof(vt)) || vt != g_tick_vt ||
        !screen_int(g_tick_screen, TICK_MASK_OFF, &mask)) {
        g_tick_screen = NULL;              /* the screen is gone */
        return;
    }
    if (mask == g_tick_mask)
        return;
    g_tick_mask = mask;
    if (!screen_int(g_tick_screen, TICK_HILITE_OFF, &game) || game < 0 ||
        game > 7 || !rom_table_ready())
        return;
    if (screen_int(g_tick_screen, 0x348, &v) && (v == 0 || v == 1))
        version = v;
    if (!tick_status(g_tick_screen, game, st, sizeof st))
        return;
    if (game_title(game, version, title, sizeof title))
        _snprintf(line, sizeof line, "%s, %s", title, st);
    else
        _snprintf(line, sizeof line, "%s", st);
    line[sizeof line - 1] = 0;
    menu_speak_line(line);
}

/* ⭐⭐ MC_GAME_GRID: Casual Match's game grid, up/down. The refresh
 * 0x140098c50 highlights game [+0x344] of [+0x33c] (parts 0x141207240[i]);
 * the grid's own up/down calls move [+0x344] by a grid row. Named from the
 * exe's ROM title table in the collection's order -- the same mapping the
 * Game row uses, walked across all eight in September -- on the side the
 * Game Version row ([+0x348]) selects. */
static void game_grid_build(const mc_move *m, mc_speech *out)
{
    int game = -1, n = 0, version = 1, ri;
    char t[128];
    if (!m->screen || !screen_int(m->screen, 0x33c, &n) || n < 1 || n > 8 ||
        !screen_int(m->screen, 0x344, &game) || game < 0 || game >= n ||
        !rom_table_ready()) {
        out->why = MC_UNNAMED_ROW_UNDERIVABLE;
        return;
    }
    {
        int v = -1;
        if (screen_int(m->screen, 0x348, &v) && (v == 0 || v == 1))
            version = v;
    }
    ri = rom_table_index_for(game, version);
    if (ri < 0)
        ri = rom_table_index_for(game, version ? 0 : 1);
    if (ri < 0 || !rom_table_title(ri, t, sizeof(t)) || !t[0]) {
        out->why = MC_UNNAMED_TEXT_UNREADABLE;
        return;
    }
    {
        char st[64];
        if (tick_status(m->screen, game, st, sizeof st))
            _snprintf(out->item, sizeof(out->item), "%s, %s", t, st);
        else
            _snprintf(out->item, sizeof(out->item), "%s", t);
    }
    out->item[sizeof(out->item) - 1] = 0;
    tick_watch(m->screen);
    out->position = game + 1;
    out->total = n;
    out->named = TRUE;
    out->why = MC_NAMED_OK;
}

/* ⭐⭐⭐ MC_AWARD: Fighter Awards' achievement, from the game's own data.
 * The refresh 0x140079cb0 writes the title of achievement [+0x354] as message
 * 0xbb2 + 2*[+0x354] (description 0xbb3 + 2*[+0x354]) -- exactly the
 * "base 2994, stride 2" this file had derived from 15 of 16 sightings, now read
 * from the code. Both the 40-item grid and the 7-row scrolling list move
 * [+0x354], so both are named the same way. */
#define AWARD_SEL_OFF   0x354
#define AWARD_TITLE_MSG 0xbb2

static void award_build(const mc_move *m, mc_speech *out)
{
    int sel = -1;
    char t[GT_CAPTION_MAX];
    if (!m->screen || !screen_int(m->screen, AWARD_SEL_OFF, &sel) ||
        sel < 0 || sel >= 64) {
        out->why = MC_UNNAMED_ROW_UNDERIVABLE;
        return;
    }
    if (!gmd_text_by_index(AWARD_TITLE_MSG + 2 * sel, t, sizeof(t)) || !t[0]) {
        out->why = MC_UNNAMED_TEXT_UNREADABLE;
        return;
    }
    _snprintf(out->item, sizeof(out->item), "%s", t);
    out->item[sizeof(out->item) - 1] = 0;
    out->named = TRUE;
    out->why = MC_NAMED_OK;
}

/* ⭐⭐ THE SCREEN'S HEADING, from the game's own data. The generic heading
 * setter 0x14007ee00 writes it into part [+0x2e4], child
 * 0x1411fb368[[+0x2e0]], then under that object's +0xf0 the child
 * 0x1411fb3a8[[+0x2e0]] (always 9 -- the index every heading had in the
 * layout dumps: "Ken Moves", "Dummy Behavior", "P1 Pause Menu"). */
static BOOL heading_text(const void *screen, char *out, int outlen)
{
    int n = -1, part = 0, a = 0, b = 0;
    out[0] = 0;
    if (!screen || !screen_int(screen, 0x2e0, &n) || n < 0 || n > 15 ||
        !screen_int(screen, 0x2e4, &part) ||
        !exe_int(0x1411fb368ULL + 4ull * (ULONGLONG)n, &a) ||
        !exe_int(0x1411fb3a8ULL + 4ull * (ULONGLONG)n, &b))
        return FALSE;
    return gt_part_sub_text(screen, part, a, b, out, outlen);
}

/* The move list's rows (refresh 0x1400803d0): part 0x141204908[i] for the
 * ten on-screen rows; the move NAME is child 8 and its condition/input
 * child 7 (layout dumps: x=84 names +0x050 = 8 on 647 objects, x=633
 * inputs = 7; the third column, x=1565, is child 30). */
#define MOVE_ROW_PARTS 0x141204908ULL
#define MOVE_ROWS      10

/* ⭐⭐ MC_MSG_LIST: the item's name is a message chosen by the game's own
 * table (see the kind). Read at speak time, after the game's own move. */
static void msg_list_build(const mc_screen *s, const mc_move *m, mc_speech *out)
{
    int item = -1, n = 0, id = -1, msg = -1;
    char t[GT_CAPTION_MAX], sp[GM_OUT_MAX];

    if (!m->screen || !s->msg_table || s->row_off <= 0 ||
        !screen_int(m->screen, s->row_off, &item) ||
        (s->rows_off > 0 && (!screen_int(m->screen, s->rows_off, &n) ||
                             item < 0 || item >= n))) {
        out->why = MC_UNNAMED_ROW_UNDERIVABLE;
        return;
    }
    id = item;
    if (s->id_off > 0 && !screen_int(m->screen, s->id_off + 4 * item, &id))
        id = -1;
    if (id < 0 || id >= s->id_max ||
        !exe_int(s->msg_table + 4ull * (ULONGLONG)id, &msg) || msg < 0 ||
        !gmd_text_by_index(msg, t, sizeof(t)) || !t[0]) {
        log_line("mcspeak: %s -- item %d (id %d) has no message; speaking "
                 "position only.", s->name ? s->name : "(unnamed)", item, id);
        out->why = MC_UNNAMED_TEXT_UNREADABLE;
        return;
    }
    gm_speak(t, sp, sizeof(sp));
    if (!sp[0] || gm_has_residue(sp)) {
        out->why = MC_UNNAMED_TEXT_UNREADABLE;
        return;
    }
    _snprintf(out->item, sizeof(out->item), "%s", sp);
    out->item[sizeof(out->item) - 1] = 0;
    if (s->rows_off > 0 && n > 0) {
        out->position = item + 1;
        out->total = n;
    }
    out->named = TRUE;
    out->why = MC_NAMED_OK;
}

/* ⭐⭐ The part that draws settings row `item` (see the settings branch in
 * menu_speak_build): the entry's own part table or the shared one, at the
 * on-screen slot. */
static BOOL settings_row_part(const mc_screen *e, const void *screen, int item,
                              int *part)
{
    ULONGLONG table = (e && e->part_table) ? e->part_table : SETTINGS_PART_TABLE;
    int slot = settings_slot(e, screen, item);
    *part = 0;
    return screen && item >= 0 && item < 32 && slot >= 0 && slot < 16 &&
           exe_int(table + 4ull * (ULONGLONG)slot, part) && *part > 0;
}

/* The row's label (child 7), raw and spoken. Logs the part's children once
 * when it cannot be read, so a mismatch is visible in one visit. */
static BOOL settings_label(const void *screen, int part, const char *who,
                           char *raw, size_t rawcap, char *spoken, size_t spcap)
{
    if (!gt_part_text(screen, part, 7, raw, (int)rawcap)) {
        static const void *warned_screen;
        static int warned_part = -1;
        if (warned_screen != screen || warned_part != part) {
            warned_screen = screen;
            warned_part = part;
            log_line("mcspeak: %s -- part %d has no label in child 7; speaking "
                     "position only.", who ? who : "(unnamed)", part);
            gt_part_dump_once(screen, part, who);
        }
        return FALSE;
    }
    gm_speak(raw, spoken, spcap);
    return spoken[0] && !gm_has_residue(spoken);
}

/* The row's value (child 8), spoken -- unless the game is hiding it. */
static BOOL settings_value(const mc_screen *e, const void *screen, int part,
                           char *spoken, size_t cap)
{
    char raw[GT_CAPTION_MAX];
    ULONGLONG table = (e && e->part_table) ? e->part_table : SETTINGS_PART_TABLE;
    spoken[0] = 0;
    if (table == SETTINGS_PART_TABLE && settings_value_hidden(screen, part))
        return FALSE;
    if (!gt_part_text(screen, part, 8, raw, (int)sizeof raw))
        return FALSE;
    gm_speak(raw, spoken, cap);
    if (!spoken[0] || gm_has_residue(spoken)) {
        spoken[0] = 0;
        return FALSE;
    }
    return TRUE;
}

void menu_speak_build(const mc_move *m, mc_speech *out)
{
    memset(out, 0, sizeof(*out));

    /* Position is ALWAYS available and always true: it comes from arguments,
     * not from any table. Compute it first so every path has it. */
    out->position = m->new_index - m->min_index + 1;
    out->total    = m->max_index - m->min_index + 1;

    /* ⭐ Count-aware: one caller can serve several screens (0x14009ba21 is both
     * "Select Mode" at 3 items and the Online Play menu at 6). See
     * menu_speak_lookup_n().
     *
     * ⚠ A heading-aware variant of this lookup was added and REMOVED -- see
     * mc_screen.coord_x. Training Options' tabs cannot be told apart by their
     * heading, because the heading object is refreshed on a different schedule
     * from the rows and leads them by one tab. MC_ROW_COLUMN names those rows
     * without identifying the tab at all. */
    /* ⭐⭐ Screen-aware: two entries on this caller can share an item count
     * (the 40 achievements vs Project Justice's 40 characters), and only a
     * field on the screen object separates them. See menu_speak_lookup_for(). */
    const mc_screen *s = menu_speak_lookup_for((ULONGLONG)(ULONG_PTR)m->caller,
                                               out->total, m->screen);
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
    /* MC_GAME_STRIP: retired 2026-11-24 (no screen reads by position, by
     * coordinate order or by matching text any more). */

    /* ⭐⭐⭐ A CHARACTER PICKER NAMES ITSELF FROM ITS POSITION.
     *
     * ⛔⛔ THE BUG THIS FIXES, user-reported: "when I press up and down to
     * choose a value I don't hear anything". Measured in run-...-1222: of 101
     * up/down movements only TWO spoke a character; 99 gave a bare "19 of 36",
     * which is useless when you are spelling out a code.
     *
     * THE CAUSE: this block used to set the flag and RETURN UNNAMED, leaving
     * the naming to the caption path in menu_speak() -- and THIS SCREEN SETS NO
     * TEXT AT ALL. Its 36 characters have no text objects (the layout probe
     * finds only the six lobby SLOTS at y=556 and nothing else), so they are
     * drawn as sprites. The path was gated on a signal that does not exist for
     * it, which is why it fired on 2 movements out of 101 -- those two picked
     * up a caption that belonged to the FIELD, not the picker.
     *
     * ⭐ THE POSITION IS SUFFICIENT, AND THAT IS MEASURED. Across every run, 23
     * movements spoke a character and 21 agreed exactly with the alphabet; the
     * only 2 disagreements are the same stale '0' at position 36 from a build
     * predating the staleness guard. The 03:22 run added 16 of 16 exact over 32
     * upward movements, and the layout probe independently caught the edited
     * slot mirroring the picker (F@16, M@23, Z@36, all exact).
     *
     * ⚠ THE CORROBORATION MOVED, IT DID NOT VANISH. The alphabet is pinned by
     * two independent facts: the live item count is exactly 36, and
     * "0".."9","A".."Z" is exactly 36 characters. char_picker is still set, so
     * char_picker_matches() still guards the caption path for the rare movement
     * that does carry one.
     *
     * ⚠ The count is the membership test: a screen with a different live count
     * is not this picker and gets nothing. */
    /* ⭐⭐⭐ Since 2026-11-24 the character is the game's own: the current
     * slot's index into the game's charset (lobby_char), not the position
     * mapped through an alphabet written down from observation. */
    if (s->kind == MC_CHAR_PICKER || s->kind == MC_LOBBY_SLOT) {
        out->char_picker = (s->kind == MC_CHAR_PICKER);
        if (lobby_char(m->screen, out->item)) {
            out->named = TRUE;
            out->why = MC_NAMED_OK;
            return;
        }
        out->why = MC_UNNAMED_TEXT_UNREADABLE;
        return;
    }

    /* ⭐⭐⭐ A PER-GAME CHARACTER ROSTER (Fighter Awards > Play Stats).
     *
     * The tab -- which game's roster is on screen -- is a STORED FIELD on the
     * screen object, so this works on every movement, including the many where
     * the game draws no text at all. See MC_GAME_ROSTER for why the item count
     * cannot be the key (Project Justice has 40 characters; the achievements
     * page on this same caller has 40 achievements).
     *
     * ⚠ Read guarded. `screen` is a pointer from a live game thread: this
     * module reads it and never trusts it. mem_readable first, then the copy --
     * the fault guard is a net for the race, never the check. */
    if (s->kind == MC_GAME_ROSTER) {
        const mc_roster *r;
        int tab = -1, idx;

        if (!m->screen || s->roster_off <= 0) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        {
            const void *tc = (const char *)m->screen + s->roster_off;
            if (!mem_readable(tc, sizeof(tab)) ||
                !mem_copy(&tab, tc, sizeof(tab))) {
                out->why = MC_UNNAMED_ROW_UNDERIVABLE;
                return;
            }
        }
        /* ⭐ The game's own lookup is bounds-checked with `cmp edx,8`; so is
         * this. An out-of-range index means the field is not what we think on
         * this screen, which is a reason to say nothing -- never to clamp into
         * some roster and name a character from it. */
        if (tab < 0 || tab >= 8) {
            static BOOL warned;
            if (!warned) {
                warned = TRUE;
                log_line("mcspeak: %s -- the tab index at +0x%x reads %d, "
                         "outside 0..7. Refusing to name a roster.",
                         s->name ? s->name : "(unnamed)",
                         (unsigned)s->roster_off, tab);
            }
            out->why = MC_UNNAMED_ROW_UNDERIVABLE;
            return;
        }
        r = &g_rosters[tab];

        /* ⭐⭐ THE CROSS-CHECK, and the whole safety argument. The roster this
         * index names must have exactly the number of items the cursor helper
         * just reported. Two independent sources -- the exe's own count table
         * (baked into roster_table.h and verified against the corpus by the
         * generator) and the live count -- must agree.
         *
         * ⛔ This is what makes a misread index HARMLESS instead of wrong: a
         * stale or wrong tab almost always names a roster of a different size,
         * and is refused here. It is also what keeps the ACHIEVEMENTS page out:
         * it reaches this entry only with 40 items, and then only if the tab
         * field independently reads 6 (Project Justice). */
        if (out->total != r->count) {
            static BOOL warned;
            if (!warned) {
                warned = TRUE;
                log_line("mcspeak: %s -- tab %d (%s) has %d character(s) but "
                         "this list has %d. Refusing rather than naming from "
                         "the wrong roster.",
                         s->name ? s->name : "(unnamed)", tab, r->tag,
                         r->count, out->total);
            }
            out->why = MC_UNNAMED_COUNT_MISMATCH;
            return;
        }

        idx = out->position - 1;
        if (idx < 0 || idx >= r->count) {
            out->why = MC_UNNAMED_ROW_UNDERIVABLE;
            return;
        }
        /* Character N is base + 2*N: the NAME labels alternate with _DESC.
         * Uniformity of that run is checked by the generator, not here. */
        {
            char text[GT_CAPTION_MAX];
            if (!gmd_text_by_index(r->base + 2 * idx, text, sizeof(text)) ||
                !text[0]) {
                out->why = MC_UNNAMED_TEXT_UNREADABLE;
                return;
            }
            _snprintf(out->item, sizeof(out->item), "%s", text);
            out->item[sizeof(out->item) - 1] = '\0';
        }
        /* ⭐ Name the GAME as the screen, not the generic page title. On a
         * page of eight identical-looking tabs, "Capcom vs. SNK 2" is the one
         * thing a listener cannot otherwise recover -- the game is NOT drawn
         * anywhere on this screen (measured: no captured string names it). */
        _snprintf(out->screen_name, sizeof(out->screen_name), "%s", r->name);
        out->screen_name[sizeof(out->screen_name) - 1] = '\0';
        out->named = TRUE;
        return;
    }

    /* MC_SOUNDTRACK_STRIP: retired 2026-11-24 (no screen reads by position, by
     * coordinate order or by matching text any more). */

    /* MC_NUMBERED_LIVE: retired 2026-11-24 (no screen reads by position, by
     * coordinate order or by matching text any more). */

    /* ⭐⭐⭐ COORDINATE-ORDERED LIVE TEXT. The screen's items are on screen and
     * the caption channel is blind, so the objects are ordered by their own
     * X/Y and position N is the Nth in layout order. See MC_COORD_LIVE.
     *
     * ⚠ The candidate filter is the membership test; the coordinates only
     * ORDER what it admits. gt_item_at_position() then refuses outright if the
     * number of candidates disagrees with this screen's count -- which is what
     * stops a neighbouring screen's objects being ordered as if they were this
     * one's. Two independent gates, same as every other channel here. */
    /* ⭐⭐ A ROW LABEL FROM ITS OWN COLUMN -- see MC_ROW_COLUMN. The column is
     * the membership test and Y is the order, so a row label is never confused
     * with its value (X=835 here) or with the screen heading (X=960). No map, no
     * screen identity, no index. */
    if (s->kind == MC_KEY_GRID) {
        key_grid_build(m, out);
        return;
    }

    if (s->kind == MC_PART_ROWS) {
        part_rows_build(s, m, out);
        return;
    }
    if (s->kind == MC_PART_TEXT) {
        part_text_build(s, m, out);
        return;
    }
    if (s->kind == MC_GAME_GRID) {
        game_grid_build(m, out);
        return;
    }
    if (s->kind == MC_AWARD) {
        award_build(m, out);
        return;
    }
    if (s->kind == MC_MSG_LIST) {
        msg_list_build(s, m, out);
        return;
    }

    /* ⭐⭐ THE GAMEPAD CONTROLS SCREEN, from its own refresh routine. */
    if (s->kind == MC_PAD_LIST) {
        pad_list_build(m, out);
        return;
    }

    /* MC_ROW_COLUMN: retired 2026-11-24 (no screen reads by position, by
     * coordinate order or by matching text any more). */

    /* MC_COORD_LIVE: retired 2026-11-24 (no screen reads by position, by
     * coordinate order or by matching text any more). */

    /* ⭐⭐⭐ THE IN-GAME MOVE LIST. See MC_MOVE_LIST in the header for why every
     * screen-derived route is closed and this one is not.
     *
     * ⚠ The ROW is the cursor index relative to min, which is what `position`
     * already is, minus one -- the array at +0x354 is 0-based while position is
     * 1-based. Using position directly would name the NEXT move on every row,
     * which is the off-by-one that would be least visible in testing and worst
     * in use.
     *
     * ⚠ The name comes from the GMD, so it carries no engine markup -- but it
     * goes through gm_speak() anyway, because that is where residue is refused
     * and one channel exempting itself is how tag soup reaches the user. */
    /* ⭐⭐⭐ THE MOVE LIST IS PANELS, NOT ROWS -- and the first version got this
     * wrong in a way that MISLED rather than merely omitted.
     *
     * ⛔⛔ USER-REPORTED: "there might be three or four entries in the list but
     * there are more than that number of moves... some panels only have one
     * entry and up and down does nothing so they need to be read."
     *
     * MEASURED, and the user's model is right: the cursor reports "of 3" while
     * TEN move names are live on screen. So the vertical axis moves between
     * PANELS/categories, not between rows, and "Somersault Throw, 2 of 3"
     * implied the character had three moves and you were on the second. Both
     * halves of that were false.
     *
     * ⚠ And a one-entry panel emits NO cursor movement at all, so it was
     * silent -- the worst case, because nothing signals that anything is there.
     *
     * ⭐ So a panel change reads the WHOLE panel: every visible move with its
     * input. That is what a sighted player takes in at a glance. Speech
     * interrupts on the next movement (speech_say's interrupt=TRUE), so a fast
     * walk cannot build a backlog. */
    if (s->kind == MC_MOVE_LIST || s->kind == MC_MOVE_ROSTER) {
        char head[GT_CAPTION_MAX], spoken[GM_OUT_MAX];
        /* ⭐⭐ DERIVED FROM WHAT CAN ACTUALLY ARRIVE, not a number someone liked.
         *
         * ⛔ This was 1024 and it was REACHABLE in ordinary play: the longest
         * corpus move name is 43 chars and the longest live condition+input
         * translates to about 60, so a worst-case row is ~110 and a full panel
         * ~1150. Worse, the loop below used to STOP at `sizeof(line) - 64`, so
         * a long panel silently lost its last rows -- and a panel that reads 7
         * of its 10 moves sounds perfectly fine to the ear.
         *
         * The bound is computable, so compute it. gt_column_item() caps each
         * row at sizeof(row) = GT_CAPTION_MAX*2, and the instance table holds
         * at most 64 objects, so 64 rows is the ceiling on what can ever be
         * found -- generous, since the SCREEN draws at most 10 (the hard cap at
         * 0x14008040f). Plus the heading and a ". " per row.
         *
         * ⚠ 64 is written here rather than including gui_text.c's private
         * GT_INST_MAX: exporting an internal just to size a buffer couples them
         * the wrong way round, and this only needs to be an upper bound, not
         * that exact value. Nothing the screen can produce overflows it, so the
         * loop needs no early exit and nothing is ever trimmed.
         *
         * ⚠ static, not a stack array: at ~33 KB this is too much to put on the
         * worker thread's frame in an already-deep call path. menu_speak() runs
         * only on that one thread -- the same single-threaded assumption the
         * `static char last[]` dedupe buffers in this file already rely on. */
        static char line[64 * (GT_CAPTION_MAX * 2 + 2) + GT_CAPTION_MAX + 1];
        int used = 0, n, spoken_rows = 0, named_rows = 0;

        if (!m->screen) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        line[0] = '\0';
        /* The screen's own heading names the character -- quoted from the game,
         * so JP/US roster naming never arises. */
        /* The heading names the character, read from the game's heading
         * part (heading_text), never from a screen position. */
        if (heading_text(m->screen, head, (int)sizeof(head)) && head[0]) {
            gm_speak(head, spoken, sizeof(spoken));
            if (spoken[0] && !gm_has_residue(spoken))
                used += _snprintf(line + used, sizeof(line) - used - 1,
                                  "%s", spoken);
        }
        /* ⭐⭐ LEARN THIS GAME'S ICON NAMES FROM ITS OWN LEGEND.
         *
         * ⛔⛔ C_A is "Any Attack" in Cybots and "Vertical Slash" in Star
         * Gladiator; C_EX has NINE readings. The static table carries the
         * generic one, so this screen spoke Cybots' names on other games -- a
         * confident wrong reading, measured in the user's sweep (C_A live 8
         * times, C_B 3, C_EX 4).
         *
         * ⭐ The game draws its legend on this very screen and it is
         * co-resident with the moves: one instance-report block held the STG2
         * legend AND all nine ambiguous uses. So ask the screen.
         *
         * ⚠ CLEARED FIRST, every visit. A leftover override from the previous
         * game would be the same bug one game late, and the roster axis can
         * change the game without leaving the screen.
         *
         * ⚠ The legend is not at a fixed column -- it is prose among the move
         * rows -- so every live string on this screen is offered, and
         * gm_icon_learn_legend() takes only those defining TWO OR MORE tokens.
         * A one-token string is an ordinary move row ("Hold <ICON C_S2> and")
         * and must never redefine a token. */
        gm_icon_overrides_clear();
        {
            /* ⛔⛔ THIS NEVER LEARNED ANYTHING IN SHIPPING. It walked the focus
             * record through gt_prose_at(), which returns only strings WITHOUT
             * markup -- and a legend is nothing but "<ICON C_A>Name" markup.
             * It also read a HISTORY, not this screen. Now: the live objects
             * whose parent is this screen, markup intact. */
            int learned = 0;
            gt_screen_texts(m->screen, learn_legend_cb, &learned);
            if (learned)
                log_line("mcspeak: move list -- learned %d icon name(s) from "
                         "this game's own legend.", learned);
        }

        n = MOVE_ROWS;
        /* ⛔⛔ THIS LOOP USED TO STOP EARLY: `used < sizeof(line) - 64` quietly
         * abandoned the remaining rows once the line filled. A panel that read
         * 7 of its 10 moves sounds completely fine to the ear -- there is no
         * gap to hear -- so rows vanished with nothing to reveal it. The buffer
         * is now sized past the worst case instead, so every row the screen
         * draws is read. If it ever DOES fill, the count logged below disagrees
         * with the rows spoken and says so, rather than trimming in silence. */
        for (int i = 1; i <= n; i++) {
            char row[GT_CAPTION_MAX * 2], row_spoken[GM_OUT_MAX];
            /* ⭐⭐ THE PAIR COLUMN IS x=633: the FULL input with its condition
             * (" During Hyakkishu<ICON C_623>+<ICON C_P>"). x=1565 shipped
             * before this and carried only a direction plus a move-type glyph,
             * so a row read "Shoryuken, back, special move" -- true, and not
             * the input the player needs.
             *
             * ⭐ THE SPARSENESS IS NOW UNDERSTOOD, and it was never a reason to
             * avoid this column. Measured across four runs (627 name objects):
             *   - x=1565 occupies ALL TEN rows in every run;
             *   - x=633 occupies a VARYING SUBSET (29 objects);
             *   - every x=633 Y is a subset of the name Ys -- alignment is
             *     perfect in all three runs, so the pairing always worked.
             * The subset is per-ROW, not per-screen: only moves that HAVE a
             * condition get text, and the rest keep a BLANKED pooled object.
             * That is the 153-plain / 23-condition split, seen from the object
             * side.
             *
             * ⚠ An earlier note said "zero objects in the 00:06 run". That run
             * genuinely has none -- but the count that produced it also read
             * zero for every other run, because 633.0f was encoded as
             * 0x441e8000 (= 634.0) instead of 0x441e4000. Verify a float
             * constant before believing a negative built on it.
             *
             * ⛔⛔ The silence this column once caused is NOT the sparseness and
             * NOT the pairing -- gt_column_item() falls through to name-only
             * when a row has no mate (case 51). The real defect was the BLANKED
             * mate: a pooled " " is Y-aligned and passes the text[0] filter, so
             * a row read " Jinchu Watari,  ". Fixed in gt_column_item(), pinned
             * by cases 53 and 54, sabotage-verified.
             *
             * ⚠ The silent build was also from BEFORE MC_MOVE_ROSTER and before
             * this path logged anything, so it differed by more than the
             * constant. If the screen goes quiet again the refusal below now
             * NAMES the cause in one run rather than by theory. */
            {
                /* Row i's own part: name (child 8), then its input (child 7)
                 * when the row has one. A blank or missing name is an unused
                 * row, not a move. */
                int part = 0;
                char nm[GT_CAPTION_MAX], in[GT_CAPTION_MAX];
                if (!exe_int(MOVE_ROW_PARTS + 4ull * (ULONGLONG)(i - 1), &part) ||
                    !gt_part_text(m->screen, part, 8, nm, (int)sizeof(nm)))
                    continue;
                named_rows++;
                if (gt_part_text(m->screen, part, 7, in, (int)sizeof(in)))
                    _snprintf(row, sizeof(row), "%s, %s", nm, in);
                else
                    _snprintf(row, sizeof(row), "%s", nm);
                row[sizeof(row) - 1] = 0;
            }
            gm_speak(row, row_spoken, sizeof(row_spoken));
            if (!row_spoken[0] || gm_has_residue(row_spoken))
                continue;
            used += _snprintf(line + used, sizeof(line) - used - 1,
                              "%s%s", used ? ". " : "", row_spoken);
            spoken_rows++;
        }
        line[sizeof(line) - 1] = '\0';
        if (!line[0]) {
            /* ⛔⛔ THIS REFUSAL USED TO BE COMPLETELY SILENT, and that cost a
             * whole debugging session. The user reported the move list had gone
             * quiet; because nothing was logged here, a failed readout was
             * INDISTINGUISHABLE from never visiting the screen -- and I
             * produced FOUR wrong diagnoses in a row (table eviction, parent
             * pointer mismatch, the x=633 column, a missing g_screens row),
             * every one of them refuted by the log afterwards, before
             * discovering the path simply could not report itself.
             *
             * ⭐ The counts are what identify the cause in ONE run: names==0
             * means the column query found nothing (wrong screen pointer, or an
             * evicted table); names>0 with an empty line means every row was
             * refused by the markup translator. Those need different fixes. */
            /* ⚠ Report the PAIR column's census too. With names>0 the cause is
             * downstream of the name query, and knowing whether the pair column
             * was populated separates "the mate broke the row" from "the
             * translator refused the name" -- the two that cost rounds before. */
            log_line("mcspeak: move list read NOTHING -- %d of %d row part(s) "
                     "had a name (child 8), screen %p, cursor %d of %d. "
                     "(0 -> no row part held text; >0 -> every row was refused "
                     "after translation.)",
                     named_rows, n, m->screen, out->position, out->total);
            if (!named_rows) {
                int p0 = 0;
                if (exe_int(MOVE_ROW_PARTS, &p0))
                    gt_part_dump_once(m->screen, p0, "move list row 1");
            }
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        /* ⚠ Spoken DIRECTLY, not through out->item: a panel of ten moves with
         * their inputs far exceeds item[128], and truncating it would cut a
         * move's input mid-notation -- worse than not reading it. */
        /* ⚠ Log the COUNT alongside, because menu_speak_line() logs only the
         * text. A panel that read 3 of its 10 rows sounds perfectly fine to
         * the ear -- there is no gap to hear -- so without the count a partial
         * readout is invisible. Same reason the probe prints what it examined
         * rather than only what it found. */
        log_line("mcspeak: move list panel -- %d of %d named row(s) SPOKEN "
                 "(of %d row parts), %d of %d bytes, cursor %d of %d.%s",
                 spoken_rows, named_rows, n, used, (int)sizeof(line),
                 out->position, out->total,
                 spoken_rows < named_rows ? "  ⚠ ROWS WERE DROPPED." : "");
        menu_speak_line(line);
        out->named = TRUE;
        out->spoken_already = TRUE;
        return;
    }

    /* ⭐⭐⭐ SETTINGS SCREENS, READ FROM THE GAME'S OWN ROWS (2026-11-24).
     * Row r is drawn through a part table -- the shared 0x1411fb4f0 (System,
     * Sound, Network, PC, Display & Sound, Lobby Settings/Filters) or the
     * screen's own (part_table: Casual/Ranked Match, Select Game) -- at slot
     * r - [scroll_off] when the screen scrolls. The label is that part's child
     * 7 and the value its child 8, both the text the game wrote. The
     * hand-made label maps and count-keyed value lists are gone. Level rows
     * (`levels`: the position is the setting) and values the game is hiding
     * (settings_value_hidden) never speak child 8. */
    if (s->kind == MC_SETTINGS_ROW) {
        int idx = m->new_index - m->min_index, part = 0;
        char lab[GT_CAPTION_MAX], sl[GM_OUT_MAX];
        out->axis = MC_AXIS_ROW;
        menu_speak_note_row_total(m->screen, out->total);
        if (!settings_row_part(s, m->screen, idx, &part) ||
            !settings_label(m->screen, part, s->name, lab, sizeof lab, sl,
                            sizeof sl)) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        _snprintf(out->item, sizeof(out->item), "%s", sl);
        out->item[sizeof(out->item) - 1] = '\0';
        out->named = TRUE;
        out->why = MC_NAMED_OK;
        if (!(idx < 32 && (s->levels & (1u << idx)))) {
            char sv[GM_OUT_MAX];
            if (settings_value(s, m->screen, part, sv, sizeof sv)) {
                char both[sizeof(out->item)];
                _snprintf(both, sizeof(both), "%s, %s", sl, sv);
                both[sizeof(both) - 1] = '\0';
                memcpy(out->item, both, sizeof(out->item));
            }
        }
        return;
    }
    if (s->kind == MC_SETTINGS_VALUE) {
        int row = 0, part = 0;
        out->axis = MC_AXIS_VALUE;
        if (!menu_speak_derive_row(m->screen, m->cursor, s->values_off, 0, &row)) {
            out->why = MC_UNNAMED_ROW_UNDERIVABLE;
            return;
        }
        out->row = row;
        out->have_row = TRUE;
        out->why = MC_UNNAMED_NO_LABELS;
        /* The row entry (same name, live row count) carries the level mask:
         * row indices shift between variants (Display & Sound 6 vs 7). */
        const mc_screen *sib = menu_speak_sibling_row_for(s, m->screen);
        const mc_screen *rows_e = (s->part_table || s->scroll_off || !sib) ? s : sib;
        if (row >= 0 && row < 32) {
            unsigned mask = (sib && sib->levels) ? sib->levels : s->levels;
            if (mask & (1u << row))
                out->is_level = TRUE;
        }
        if (settings_row_part(rows_e, m->screen, row, &part)) {
            char lab[GT_CAPTION_MAX], sl[GM_OUT_MAX];
            if (settings_label(m->screen, part, s->name, lab, sizeof lab, sl,
                               sizeof sl)) {
                _snprintf(out->row_name, sizeof(out->row_name), "%s", sl);
                out->row_name[sizeof(out->row_name) - 1] = '\0';
            }
        }
        /* ⭐⭐ ROW 0 OF A GAME LIST: the exe's own title table, in the
         * collection's order (walked across all eight in September), on the
         * side the Game Version row selects. The lobby lists start with "Any"
         * -- GAME_NAME_ANY, message 14, in the game's language. Casual Match's
         * list is a checklist, so the tick is said too. */
        int nine = (s->caller == 0x140082d16ULL || s->caller == 0x14008e813ULL);
        if (row == 0 && rom_table_ready() &&
            (s->caller == 0x1400b1bd9ULL || s->caller == 0x140098986ULL || nine)) {
            int game = m->new_index - m->min_index;
            if (nine) {
                if (game == 0) {
                    char any[64];
                    if (gmd_text_by_index(14, any, sizeof any) && any[0]) {
                        _snprintf(out->item, sizeof(out->item), "%s", any);
                        out->item[sizeof(out->item) - 1] = '\0';
                        out->named = TRUE;
                        out->why = MC_NAMED_OK;
                    }
                    return;
                }
                game -= 1;
            }
            int version = 1;                 /* US unless the row says JP */
            {
                int sel = 0;
                if (s->values_off > 0 &&
                    screen_int(m->screen, s->values_off + 4, &sel) &&
                    (sel == 0 || sel == 1))
                    version = sel;
            }
            char gt[128];
            if (game_title(game, version, gt, sizeof gt)) {
                char st[64];
                if (s->caller == 0x140098986ULL &&
                    tick_status(m->screen, game, st, sizeof st)) {
                    _snprintf(out->item, sizeof(out->item), "%s, %s", gt, st);
                    tick_watch(m->screen);
                } else {
                    _snprintf(out->item, sizeof(out->item), "%s", gt);
                }
                out->item[sizeof(out->item) - 1] = '\0';
                out->named = TRUE;
                out->why = MC_NAMED_OK;
            }
            return;
        }
        if (!out->is_level && part) {
            char sv[GM_OUT_MAX];
            if (settings_value(rows_e, m->screen, part, sv, sizeof sv)) {
                _snprintf(out->item, sizeof(out->item), "%s", sv);
                out->item[sizeof(out->item) - 1] = '\0';
                out->named = TRUE;
                out->why = MC_NAMED_OK;
            }
        }
        return;
    }

    /* ⭐⭐ A FLAT LIST MAY ALSO NEED A MAP. Until Online Play, every MC_LIST
     * screen was a contiguous run and only settings screens carried `labels`.
     * Online Play is a plain vertical menu whose six items are 68,69,70,71 and
     * then 1076,1078 -- ~1000 labels away. So `labels` is honoured here too.
     *
     * ⛔ A map REPLACES the base; it never falls back to base+index. That is
     * the whole point: on this screen a "close" base would read HELP_ strings
     * out as item names at positions 5 and 6. The header states the same rule
     * for settings screens and it applies identically here. */
    int gmd_index;
    if (s->labels) {
        int i = m->new_index - m->min_index;
        if (s->nlabels != out->total || i < 0 || i >= s->nlabels) {
            /* The map does not describe this screen -- refuse it rather than
             * index outside it. */
            out->why = MC_UNNAMED_COUNT_MISMATCH;
            return;
        }
        gmd_index = s->labels[i];
        /* ⭐ An unestablished row in an otherwise-mapped screen: position only,
         * never a neighbour's label. See MC_LABEL_UNKNOWN. */
        if (gmd_index == MC_LABEL_UNKNOWN) {
            out->why = MC_UNNAMED_NO_LABELS;
            return;
        }
    } else if (s->base_index >= 0) {
        gmd_index = s->base_index + (m->new_index - m->min_index);
    } else {
        /* No map and no base: nothing to name with. */
        out->why = MC_UNNAMED_NO_LABELS;
        return;
    }

    char text[128];
    if (!gmd_text_by_index(gmd_index, text, sizeof(text)) || !text[0]) {
        out->why = MC_UNNAMED_TEXT_UNREADABLE;
        return;                        /* could not read -> position only */
    }

    _snprintf(out->item, sizeof(out->item), "%s", text);
    out->item[sizeof(out->item) - 1] = '\0';
    out->named = TRUE;
}

/* Does this captured string look like a SETTING VALUE rather than prose?
 *
 * ⭐ Deliberately strict. This is the one place a string is spoken WITHOUT
 * corpus verification, so the shape test is what stops a help sentence or a
 * button strip being read out as if it were a value.
 *
 * A value is short, single-line, carries no markup, and does not end in
 * sentence punctuation -- "Off", "1024x768", "English [en]", "Original".
 * A caption ("Adjust the volume of background music.") fails on both length
 * and the terminal period. */
static BOOL is_value_shaped(const char *s)
{
    if (!s || !*s)
        return FALSE;

    size_t n = strlen(s);
    if (n > 40)                       /* a sentence, not a value */
        return FALSE;
    if (strchr(s, '<'))               /* markup: a strip, not a value */
        return FALSE;
    if (strchr(s, '\r') || strchr(s, '\n'))
        return FALSE;

    char last = s[n - 1];
    if (last == '.' || last == '?' || last == '!')
        return FALSE;                 /* prose */

    /* Require at least one printable, non-space character. */
    for (const char *p = s; *p; p++) {
        if (!isspace((unsigned char)*p))
            return TRUE;
    }
    return FALSE;
}

/* Test seam: this predicate is the ONLY guard on speaking a string the corpus
 * has not verified, so it is exercised directly rather than through a movement
 * the offline harness cannot stage. */
BOOL menu_speak_test_value_shaped(const char *s)
{
    return is_value_shaped(s);
}

/* ⛔⛔ IS THIS CAPTION THE SCREEN'S, NOT THE ROW'S?
 *
 * A per-row caption is the whole point of the channel. A caption that does NOT
 * change as the cursor moves is describing the SCREEN, and repeating it after
 * every movement is pure noise -- worse on a long list, where the user hears
 * the same sentence 40 times while trying to scan.
 *
 * ⚠ MEASURED, not hypothetical. Fighter Awards sets "View all of your
 * challenges." and nothing else: eight different rows across two lists, one
 * caption. Once ACHIEVEMENT_*_NAME entered the prompt table (so the achievement
 * titles could be spoken), that static sentence started resolving too and would
 * have been appended to all 40 grid positions.
 *
 * ⭐ gt_take_caption already refuses markup STRIPS on exactly this reasoning --
 * "the strip describes the screen and does not change per row". The principle
 * was right and the test was too narrow: it keyed on markup, and this one is
 * plain prose. So test the PROPERTY directly -- did it change? -- rather than a
 * proxy for it.
 *
 * The rule: the FIRST sighting on a screen is spoken (it is genuinely new
 * information on arrival), and an identical repeat is suppressed until either
 * the caption or the screen changes. A screen with real per-row captions is
 * unaffected, because consecutive rows differ. */
static BOOL caption_is_stale_repeat(const char *cap, const char *screen_name)
{
    static char last_cap[GT_CAPTION_MAX];
    static char last_screen[64];

    if (!cap || !*cap)
        return TRUE;

    /* A change of screen always re-arms: arriving somewhere new, the caption is
     * information even if the words happen to match. */
    const char *scr = screen_name ? screen_name : "";
    if (strcmp(scr, last_screen) != 0) {
        strncpy(last_screen, scr, sizeof(last_screen) - 1);
        last_screen[sizeof(last_screen) - 1] = '\0';
        strncpy(last_cap, cap, sizeof(last_cap) - 1);
        last_cap[sizeof(last_cap) - 1] = '\0';
        return FALSE;                      /* first sighting here -- speak it */
    }

    if (strcmp(cap, last_cap) == 0)
        return TRUE;                       /* unchanged -- the screen's, not
                                              this row's */

    strncpy(last_cap, cap, sizeof(last_cap) - 1);
    last_cap[sizeof(last_cap) - 1] = '\0';
    return FALSE;
}

/* Test seam: the suppressor keeps static state, so a test must be able to
 * drive it as a sequence rather than one call at a time. */
BOOL menu_speak_test_caption_repeat(const char *cap, const char *screen_name)
{
    return caption_is_stale_repeat(cap, screen_name);
}


/* ⭐⭐⭐ MODAL PROMPTS: announce the QUESTION, not a selection.
 *
 * ⛔⛔ WHY THIS IS A SEPARATE CHANNEL. The quit dialogs move their selection
 * WITHOUT the cursor helpers -- measured, 3/8/12 arrow presses with ZERO
 * movements logged -- so everything else in this file is unreachable for them:
 * menu_speak() is driven by movements, and gt_take_caption() is keyed on the
 * movement stamp. A modal that never moves gets nothing, however it is walked.
 *
 * ⛔ AND THE SELECTION IS GENUINELY NOT AVAILABLE. Measured and closed in
 * run-2026-09-11-1322: both options are live text objects, but the fields that
 * separate them (+0x124, +0x13c) are byte-identical across four arrow presses,
 * and the differ -- reporting freely on other objects in the same run, which is
 * the control that makes this a real negative -- saw no change on either. So
 * this speaks WHAT IS BEING ASKED and deliberately claims nothing about which
 * button is highlighted. ⚠ Saying "Yes" or "No" here would state a selection
 * the mod cannot observe, on a prompt that ends the game.
 *
 * ⭐ Both prompts are ordinary GMD strings, so they are matched by TEXT against
 * the corpus rather than by a hardcoded English literal -- the mod follows the
 * game's language, which a literal would break for 13 of the 14 languages.
 *
 * ⚠ Announced ONCE per appearance: the dialog re-sets its text while open, and
 * a prompt repeating every redraw would talk over the user. */
/* ⛔ USER: "You also are not reading many of the yes/no prompts." Correct --
 * this list held exactly TWO, the quit dialogs it was built for, so every other
 * confirmation in the collection was silent. A blind player was being asked a
 * question they could not hear, and some of these END A MATCH or OVERWRITE A
 * SAVE.
 *
 * ⭐ Chosen by STEM FAMILY (POP_*, SYS_*, ASK_*), not by "the text ends in a
 * question mark". Two song titles in the corpus end in '?' -- SOUND_CFJ_06
 * "06. At Ease!?" and SOUND_MJUS_46 "46. Who's the Number one?" -- and a
 * punctuation rule would announce a music track as a dialog the moment the
 * player opened the sound test.
 *
 * ⚠ These are INDICES, resolved against the live corpus at match time, so the
 * mod follows the game's language. A hardcoded English literal would break for
 * 13 of the 14 shipped languages. */
static const int K_MODAL_PROMPTS[] = {
    384,    /* SYS_EXIT_APP     'If you exit the application, ...Continue?'  */
    383,    /* SYS_BACK_TITLE   'If you return to the title screen, ...'     */
    278,    /* POP_QUIT         'Exit the game?'                             */
    279,    /* POP_QUIT_TR      'Exit training mode?'                        */
    280,    /* POP_QUIT_HOST    'Exit the game and return to the lobby?'     */
    281,    /* POP_QUIT_GUEST   'Exit the game and leave the lobby?'         */
    4117,   /* POP_RESET_GAME   'Restart the game?'                          */
    269,    /* POP_LOAD_CHECK   'Load quick save data?'                      */
    270,    /* POP_SAVE_CHECK   'Overwrite game data?'                       */
    /* ⚠ 271 POP_SAVE_CHECK2 is DELIBERATELY ABSENT. Its corpus text contains a
     * "%s" that the game substitutes with the save name, so the live string can
     * never equal the template and an exact match would never fire. Listing it
     * would look like coverage while changing nothing -- worse than the gap,
     * because it hides it. Matching it needs a prefix rule, which is a wider
     * change than this one and would need its own evidence that no other
     * string shares the prefix. */
    385,    /* SYS_SCORE_RESET  'Reset high score data?'                     */
    389,    /* SYS_NOTFIND_SAVEDATA 'Could not find save data. Create new?'  */
    400,    /* SYS_SAVEDATA_NOCREATE 'You will not be able to create ...'    */
    402,    /* SYS_AUTOSAVE_NOSAVING_01 'Unable to save. Return to title?'   */
    150,    /* POP_LEAVE_LOBBY  'Leave this lobby?'                          */
    933,    /* POP_SEARCH_NO_MATCH 'Unable to find an opponent. Search again?' */
    935,    /* ASK_MATCH        'Begin match?'                               */
    945,    /* ASK_REMATCH      'Rematch?'                                   */
    946,    /* POP_ASK_MATCH    'Stop waiting for opponents and exit standby?' */
    1062,   /* POP_DECLINED_MATCH 'You have left the match. Search again?'   */
    1082,   /* ASK_REPLAY       'Play again?'                                */
};

/* ⭐ Collapse the corpus strings' CR/LF layout to single spaces.
 *
 * ⚠ The prompts carry "\r\n" as visual line breaks. Spoken verbatim those
 * become pauses or literal characters depending on the reader, and the run of
 * whitespace they leave behind reads as a stutter. Exposed as a test seam
 * because it is the only part of the modal path that can run offline --
 * gmd_text_by_index() has no table in the test process. */
void menu_speak_flatten_prompt(const char *in, char *out, int outlen)
{
    int o = 0;
    if (!out || outlen <= 0)
        return;
    out[0] = '\0';
    if (!in)
        return;
    for (int i = 0; in[i] && o < outlen - 1; i++) {
        char c = in[i];
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
        if (c == ' ' && (o == 0 || out[o - 1] == ' '))
            continue;                   /* collapse runs, and never lead with one */
        out[o++] = c;
    }
    while (o > 0 && out[o - 1] == ' ')  /* and never trail with one */
        o--;
    out[o] = '\0';
}

/* Test seam: is this text one of the modal prompts, given a resolver?
 * ⭐ The resolver is passed in so the suite can supply the corpus strings the
 * test process cannot load. The SHIPPING caller passes gmd_text_by_index. */
BOOL menu_speak_is_modal_prompt(const char *text,
                                BOOL (*resolve)(int, char *, unsigned))
{
    if (!text || !text[0] || !resolve)
        return FALSE;
    for (size_t i = 0; i < sizeof(K_MODAL_PROMPTS) / sizeof(K_MODAL_PROMPTS[0]);
         i++) {
        char want[GT_CAPTION_MAX];
        if (resolve(K_MODAL_PROMPTS[i], want, sizeof(want)) &&
            want[0] && strcmp(want, text) == 0)
            return TRUE;
    }
    return FALSE;
}

/* ⭐ The resolver menu_speak_check_modal() uses. Overridable ONLY by the test
 * harness, which has no GMD table -- so the end-to-end probe can drive the real
 * function instead of reimplementing its logic and testing its own copy. */
static BOOL (*g_modal_resolver)(int, char *, unsigned);

static BOOL modal_resolve(int idx, char *out, unsigned outlen)
{
    if (g_modal_resolver)
        return g_modal_resolver(idx, out, outlen);
    return gmd_text_by_index(idx, out, outlen);
}

void menu_speak_test_set_modal_resolver(BOOL (*fn)(int, char *, unsigned))
{
    g_modal_resolver = fn;
}

void menu_speak_check_modal(void)
{
    /* ⭐⭐⭐ READ THE MODAL SLOT -- do not search a history.
     *
     * ⛔⛔ USER (2026-10-04): "sometimes the confirm quit yes/no prompts are not
     * reading", then: "we shouldn't be relying on a buffer that can fill up to
     * determine what's currently being spoken." This used to scan the 48-entry
     * focus record for a known prompt. The record filled ~48 strings into a
     * session (only a diagnostics-only report ever emptied it), and it kept
     * closed dialogs' prompts, so a dialog opened twice matched the first time
     * and was silent.
     *
     * Now the hook recognises the prompt as it is SET and fills one slot (see
     * gt_modal_latest): the prompt, its two options, and a serial bumped once
     * per opening. A new serial is a new opening; nothing can fill or linger. */
    static const void *published_for;   /* language the hook's table is for */
    static ULONGLONG   next_lang_check;
    static LONG        last_serial;
    static int         last_sel = -2;
    static LONG        pending_serial;
    static ULONGLONG   pending_since;
    gt_modal_info m;
    char flat[GT_CAPTION_MAX];

    /* 1. Give the hook the prompt texts in the game's CURRENT language.
     *
     * ⚠ The hook cannot read the GMD table itself (it runs on the game's
     * thread), so the worker resolves the indices and publishes the strings,
     * and republishes when the language changes. Checked once a second: a
     * language change is a human-speed event, and the check is a guarded read. */
    {
        ULONGLONG now = GetTickCount64();
        if (g_modal_resolver || now >= next_lang_check) {
            const void *key = g_modal_resolver
                                  ? (const void *)g_modal_resolver
                                  : (const void *)gmd_text_language();
            next_lang_check = now + 1000;
            if (key && key != published_for) {
                enum { NP = sizeof(K_MODAL_PROMPTS) / sizeof(K_MODAL_PROMPTS[0]) };
                static char texts[NP][GT_CAPTION_MAX];
                const char *ptrs[NP];
                int k = 0;
                for (int i = 0; i < NP; i++) {
                    if (modal_resolve(K_MODAL_PROMPTS[i], texts[k],
                                      sizeof(texts[k])) && texts[k][0]) {
                        ptrs[k] = texts[k];
                        k++;
                    }
                }
                gt_set_modal_prompts(ptrs, k);
                published_for = key;
                log_line("mcspeak: yes/no prompts: %d of %d resolved and handed "
                         "to the text hook.", k, (int)NP);
            }
        }
    }

    if (!gt_modal_latest(&m))
        return;                         /* no dialog has opened yet */

    BOOL is_new = (m.serial != last_serial);

    /* 2. A new opening whose options have not all arrived yet: wait a moment.
     * The dialog sets prompt, Yes, No back to back, so this is normally one
     * poll; ⚠ bounded, so a dialog with no options is still spoken. */
    if (is_new && m.pending) {
        ULONGLONG now = GetTickCount64();
        if (pending_serial != m.serial) {
            pending_serial = m.serial;
            pending_since = now;
            return;
        }
        if (now - pending_since < 300)
            return;
    }

    /* 3. The highlighted option, from the dialog's own state (parent+0x38c,
     * 0 = TOP). See gt_modal_selection() for the two sources behind that. */
    int sel = -1;
    BOOL have_sel = gt_modal_selection(&sel) && (sel == 0 || sel == 1);

    if (!is_new) {
        /* ⭐ The same opening: speak only a READABLE change of option.
         * ⚠ An unreadable selection is most likely the dialog having closed --
         * the slot keeps its last opening. Speaking then would describe a
         * dialog that is no longer there. */
        if (!have_sel || sel == last_sel)
            return;
    }
    last_serial = m.serial;
    last_sel = have_sel ? sel : -1;

    /* 4. Say it. The prompt only on a new opening; afterwards just the option
     * -- USER: "it repeats the whole dialog, it should just say yes or no". */
    if (is_new)
        menu_speak_flatten_prompt(m.prompt, flat, (int)sizeof(flat));
    else
        flat[0] = '\0';

    if (m.nopts == 2) {
        size_t used = strlen(flat);
        const char *sep = used ? " " : "";
        if (have_sel)
            _snprintf(flat + used, sizeof(flat) - used - 1, "%s%s", sep,
                      m.opt[sel]);
        else
            /* ⛔ No readable selection: LIST both rather than name one. A wrong
             * option stated confidently on a prompt that quits the game is the
             * failure this function exists to prevent. */
        {
            char either[256];
            mt_fmt8(either, sizeof either, MT_OR, m.opt[0], m.opt[1], NULL);
            _snprintf(flat + used, sizeof(flat) - used - 1, "%s%s", sep, either);
        }
        flat[sizeof(flat) - 1] = '\0';
    }

    if (flat[0])
        menu_speak_line(flat);
}

/* ⭐⭐⭐ THE HINTS VIEW (Fighter Awards -> DECIDE).
 *
 * MEASURED 2026-09-30, 02:38 run. The button strip offers
 * "<ICON DECIDE>Hints"; pressing it collapses the strip to "<ICON CANCEL>Back"
 * and the view sets exactly ONE string -- the achievement DESCRIPTION for the
 * row the cursor is on:
 *
 *     cursor on "The True(r) Final Battle"  (= ACHIEVEMENT_11_NAME)
 *     view sets "Encounter Shin Akuma or Ultimate Rugal in Arcade Mode of
 *                Capcom vs. SNK 2..."       (= ACHIEVEMENT_11_DESC)
 *
 * ⛔⛔ WHY THIS NEEDS ITS OWN ANNOUNCER: the view produces NO CURSOR MOVEMENT,
 * so the movement path never runs and the user sat in silence for 4 seconds
 * with the hint on screen. It is a modal in shape -- but not in content: it has
 * no question and no options, so menu_speak_check_modal()'s prompt list and its
 * "X or Y" option reading do not apply. Folding it in there would have meant
 * teaching that function to sometimes have no options, on a path whose comments
 * record two user-reported bugs about saying too much.
 *
 * ⭐ IDENTIFIED BY THE CORPUS, not by position or timing. All 40 descriptions
 * are unique within the family AND against the whole corpus (0 collisions,
 * verified by the generator), so prompt_identify() resolves one exactly or not
 * at all. A string that is not an achievement description is not this view.
 *
 * ⚠ The guard is the TEXT, so re-opening the view on a DIFFERENT row speaks
 * again while holding the same one stays silent. Closing the view clears it. */
void menu_speak_check_hint(void)
{
    static char last[GT_CAPTION_MAX];
    char text[GT_CAPTION_MAX];
    int i;

    /* ⛔⛔ THE NEWEST STRING ONLY -- i < 1, NOT THE WHOLE RECORD.
     *
     * USER-HEARD BUG: on the achievements grid the mod read out an unlock
     * condition on EVERY movement, with the Hints view never opened once. The
     * strip in the log still said "Toggle Player Progress Page / Back"
     * throughout, and the descriptions were NEVER set as live text in that run
     * (zero TEXT SET lines for them) -- they were already sitting in the
     * record from an earlier visit.
     *
     * ⭐ THE ERROR WAS MINE AND IT WAS CONCEPTUAL: gt_prose_at() enumerates a
     * RETAINED HISTORY, not what is on screen now. I wrote this loop to scan
     * "every prose string" believing that meant "the view's text", so any
     * description ever captured kept re-triggering it. A history is not a
     * viewport.
     *
     * Only index 0 -- the newest prose -- can be the view that just opened,
     * and the guard below then requires it to be NEW. */
    for (i = 0; i < 1 && gt_newest_prose_if_new(text, (int)sizeof(text)); i++) {
        prompt_id id;
        if (!text[0])
            continue;
        id = prompt_identify(text);
        if (id.result != PROMPT_ID_FOUND || !id.label)
            continue;
        /* ⚠ Descriptions ONLY. An achievement NAME reaching here would be the
         * grid's own row text, which the movement path already speaks -- and
         * announcing it again would double every movement. */
        if (strncmp(id.label, "ACHIEVEMENT_", 12) != 0 ||
            !strstr(id.label, "_DESC"))
            continue;
        /* ⭐ No text-equality guard here. gt_newest_prose_if_new() hands each
         * arrival out exactly ONCE, so a repeat is impossible by construction
         * -- and re-opening the view on the SAME achievement sets the text
         * again, which is a new arrival and SHOULD speak. A `last` string
         * comparison would have blocked exactly that, which is the one case
         * the user would notice as broken. */
        {
            char flat[GT_CAPTION_MAX];
            menu_speak_flatten_prompt(text, flat, (int)sizeof(flat));
            if (flat[0])
                menu_speak_line(flat);
        }
        return;
    }
}

/* ⛔⛔ THE ONE-POLL CAPTION DEFERRAL IS GONE. DO NOT BRING IT BACK.
 *
 * It held an unnamed movement for one poll so a caption arriving 0-1 ms late
 * could still name the row. It fixed the race it targeted -- the achievement
 * grid went 0/3 to 7/10 named -- and it was WRONG ANYWAY, twice over:
 *
 *   1. A new movement discarded the pending one and the retry fires only on an
 *      IDLE poll, which a moving user never has. 12 of 25 movements spoke
 *      NOTHING. User: "it takes that much longer before I can hear anything."
 *   2. Fixed so a supersede speaks immediately -- and it was STILL worse. User:
 *      "now it doesn't read any labels at all, and when it reads the item
 *      count, that is getting slower."
 *
 * ⭐⭐ THE LESSON, which is about latency and not about this screen: ANY delay
 * on the path between a keypress and speech is paid on EVERY movement, while
 * the thing it buys is paid only on the few that were racing. The user holds a
 * direction and cycles hundreds of items -- "there are hundreds of options in
 * there" -- so a per-movement cost of even one poll is not a trade, it is a
 * tax. Speak immediately, always; if a name is not ready, the position is the
 * answer, not a reason to wait.
 *
 * ⛔ And two of my own tests asserted the broken behaviour as correct before
 * the user felt it. A test written from the author's reasoning rather than the
 * user's experience passes exactly when it should fail. */

void menu_speak(const mc_move *m)
{
    mc_speech sp;

    /* ⚠ No deferral: speech is immediate on every movement. See the note
     * above menu_speak()'s definition for why that is not negotiable. */

    /* ⭐⭐⭐ TIME THE WHOLE ANNOUNCEMENT. User-reported "about a second of lag
     * per keypress" that survived both the log fix and the speech-queue fix.
     *
     * ⛔ Every component measured so far is fast, and two hypotheses about the
     * string read have now been REFUTED by the run's own log (the blob sits in
     * ONE 520 MB region, so mem_readable_run does a single query, not many).
     * Rather than guess a third time, measure the thing the user actually
     * waits for: entry to exit of this function, on the worker thread.
     *
     * ⚠ Threshold-reported, never per call: a log line per movement would put
     * file I/O back on the path this is trying to measure. */
    LARGE_INTEGER t0, t1, qf;
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&t0);

    menu_speak_build(m, &sp);

    /* ⭐ The builder already spoke a whole PANEL (the move list). Saying
     * anything further would repeat it and append an "N of M" that counts
     * panels rather than rows -- the very thing that made the old announcement
     * misleading. */
    if (sp.spoken_already)
        return;

    /* ⚠ The item NAME leads, then the position. A screen reader user wants the
     * word first; the position is context, not the headline. */
    char line[256], where[64];
    pos_text(where, sizeof where, sp.position, sp.total);
    /* ⭐ A named VALUE reads as "row name, value name" -- "Game Version,
     * English Version". The row name is the context a blind user needs in
     * order to know what the value is a value OF, and it leads for the same
     * reason the item name leads elsewhere. */
    if (sp.named && sp.axis == MC_AXIS_VALUE && sp.row_name[0])
        _snprintf(line, sizeof(line), "%s, %s, %s",
                  sp.row_name, sp.item, where);
    else if (sp.named && sp.bare)
        _snprintf(line, sizeof(line), "%s", sp.item);
    else if (sp.named)
        _snprintf(line, sizeof(line), "%s, %s",
                  sp.item, where);
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
        _snprintf(line, sizeof(line), "%s", where);
    else if (sp.axis == MC_AXIS_VALUE && sp.row_name[0] && sp.is_level)
        /* ⭐⭐ A LEVEL ROW: the position IS the value.
         *
         * User, 2026-09-11: "they might be textless but even an internal value
         * is fine for our purposes." That reframed the problem and dissolved
         * it. A volume slider has no label to look up and no string on screen,
         * but the mod is HANDED the index and the count as arguments -- and for
         * a slider the index is not a list position, it is the SETTING.
         *
         * ⛔ "Music Volume, 7 of 10" was already carrying the answer and
         * reading like a list position. "Music Volume, 7, of 10" is the same
         * number stated as a level, which is what a sighted player sees.
         *
         * ⚠ Only for rows whose values are a numeric RANGE (see is_level).
         * Saying "Display, 2 of 3" as a level would be wrong -- that row's
         * values are names, and the position is genuinely just a position. */
    {
        char a[16], b[16], level[64];
        _snprintf(a, sizeof a, "%d", sp.position); a[sizeof a - 1] = 0;
        _snprintf(b, sizeof b, "%d", sp.total);    b[sizeof b - 1] = 0;
        mt_fmt8(level, sizeof level, MT_LEVEL, a, b, NULL);
        _snprintf(line, sizeof(line), "%s, %s", sp.row_name, level);
    }
    else if (sp.axis == MC_AXIS_VALUE && sp.row_name[0])
        /* ⭐ The useful form: "Game Version, 2 of 2" -- the row NAMED, the
         * value's position given. The value text itself is not claimed. */
        _snprintf(line, sizeof(line), "%s, %s",
                  sp.row_name, where);
    else if (sp.axis == MC_AXIS_VALUE && sp.have_row)
    {
        char r[16], row[64];
        _snprintf(r, sizeof r, "%d", sp.row + 1); r[sizeof r - 1] = 0;
        mt_fmt8(row, sizeof row, MT_ROW_N, r, NULL, NULL);
        _snprintf(line, sizeof(line), "%s, %s", row, where);
    }
    else
        _snprintf(line, sizeof(line), "%s", where);
    line[sizeof(line) - 1] = '\0';

    /* ⭐ A tab that just changed is announced before its row (MC_PART_ROWS). */
    if (sp.title[0]) {
        char titled[sizeof(line)];
        _snprintf(titled, sizeof(titled), "%s. %s", sp.title, line);
        titled[sizeof(titled) - 1] = '\0';
        memcpy(line, titled, sizeof(line));
    }

    /* ⭐⭐ THE ROW'S OWN DESCRIPTION, appended when the game supplied one.
     *
     * CFC2 authors a one-line help caption for essentially every menu item
     * ("Play offline.", "Choose the game you want to play."), and a sighted
     * player has it on screen the whole time. Speaking it on focus is parity,
     * not extra verbosity -- which is why it is not behind a hotkey: making the
     * blind player press a key for what the sighted player simply SEES is not
     * parity. (memory: judge-cues-against-the-sighted-experience)
     *
     * ⚠ It comes AFTER the item name and position. The name is the headline; a
     * sentence-long description leading would bury it, and the user can
     * interrupt once they have heard the name.
     *
     * ⛔ IT NEVER REPLACES A NAME. If the item is unnamed, this does not fill
     * the gap -- an unnamed row still says its position, because a description
     * is not a name and speaking one as the other is the substitution that
     * makes a wrong claim sound like a right one.
     *
     * ⚠⚠ AND IT IS ONLY SPOKEN IF THE CORPUS CONFIRMS IT. A captured string is
     * matched against the shipped GMD (prompt_identify): a string the corpus
     * does not contain, or one several screens share, is NOT spoken. So the
     * worst case is silence, never a caption belonging to another row. */
    /* ⭐⭐⭐ THE ROW'S DESCRIPTION AS THE MESSAGE THE GAME SET (2026-11-24).
     * The help-line setter 0x140051b30 is hooked (help_line.c): the refresh
     * picks the focused row's help message and the setter looks it up in the
     * same table the menus use, so the description is exact, in the current
     * language, with no corpus match and no movement-stamp attribution.
     * Spoken when a help message was set since the last movement AND differs
     * from the last one spoken on this screen -- rows sharing one help line
     * (a screen caption) do not repeat it, as before. The caption channel
     * below is now only the fallback for a session where the hook could not
     * be installed. */
    if (help_line_installed()) {
        static const void *h_screen;
        static int  h_last = -1;
        static LONG h_seen;
        int hm = -1;
        LONG hs = 0;
        char drain[GT_CAPTION_MAX];

        (void)gt_take_caption(drain, (int)sizeof(drain));   /* keep it fresh */
        if (m->screen != h_screen) {
            h_screen = m->screen;
            h_last = -1;
        }
        if (help_line_latest(&hm, &hs) && hs != h_seen) {
            h_seen = hs;
            if (hm != h_last) {
                char d[GT_CAPTION_MAX], ds[GM_OUT_MAX];
                h_last = hm;
                if (gmd_text_by_index(hm, d, sizeof(d)) && d[0]) {
                    gm_speak(d, ds, sizeof(ds));
                    if (ds[0] && !gm_has_residue(ds) && !strstr(line, ds)) {
                        size_t used = strlen(line);
                        _snprintf(line + used, sizeof(line) - used - 1,
                                  ". %s", ds);
                        line[sizeof(line) - 1] = '\0';
                    }
                }
            }
        }
    } else
    {
        char cap[GT_CAPTION_MAX];
        BOOL have_cap = gt_take_caption(cap, (int)sizeof(cap));

        /* ⚠ No deferral here: a movement always speaks now. If the caption has
         * not arrived, the position is the answer -- waiting for it costs every
         * movement to rescue a few. */
        if (have_cap &&
            !caption_is_stale_repeat(cap, sp.screen_name)) {
            prompt_id id = prompt_identify(cap);
            /* ⛔⛔ _NAME ONLY, and the suffix test is load-bearing.
             *
             * USER-HEARD REGRESSION: "Everybody Rumored, Nobody Believed,
             * 2 of 40. Everybody Rumored, Nobody Believed" -- the name twice,
             * with the DESCRIPTION spoken as a separate line just before it.
             *
             * ⭐ THE CAUSE WAS MY OWN CHANGE ONE STEP EARLIER. Adding the 40
             * ACHIEVEMENT_*_DESC to the prompt table for the Hints view made
             * this rule -- which tested only the ACHIEVEMENT_ PREFIX -- start
             * matching descriptions too. A prefix that was unambiguous became
             * ambiguous the moment a sibling family joined the table.
             *
             * ⚠ The lesson generalises: widening a lookup table silently
             * widens every prefix test that reads from it. Grep the prefix
             * before adding a family. */
            if (id.result == PROMPT_ID_FOUND && !sp.named && id.label &&
                strncmp(id.label, "ACHIEVEMENT_", 12) == 0 &&
                strstr(id.label, "_NAME")) {
                /* ⭐⭐ AN ACHIEVEMENT TITLE *IS* THE ITEM NAME, so it LEADS
                 * rather than being appended as a description.
                 *
                 * Fighter Awards has no row labels to find: its 7-item list is
                 * a scrolling window over the 40 achievements, and its 40-item
                 * grid is the same set. The rows ARE achievements, and the game
                 * hands us each one's title as live text on every movement
                 * ("The Year We Made Contact", "Teamwork Makes the Dream
                 * Work"). Appending that after a bare position -- "3 of 7. The
                 * Year We Made Contact" -- puts the one word that matters last.
                 *
                 * ⚠ Its cursor is a STACK LOCAL (0x06DAFC50 against a screen at
                 * 0x2BE0B650 -- every other screen's cursor is in the 0x2B-0x2C
                 * heap range), so no values_off arithmetic can address this
                 * screen and no label map can be built for it. This channel is
                 * the only route, which is why it gets a rule of its own.
                 *
                 * ⛔ NARROW ON PURPOSE: only when the corpus positively
                 * identifies the string as an ACHIEVEMENT_* label, and only
                 * when the item is not already named. This is not "speak any
                 * caption as a name" -- that would let a help sentence become
                 * an item name, which is the substitution this file exists to
                 * prevent. */
                char spoken[GM_OUT_MAX];
                gm_speak(cap, spoken, sizeof(spoken));
                if (spoken[0] && !gm_has_residue(spoken))
                    _snprintf(line, sizeof(line), "%s, %d of %d",
                              spoken, sp.position, sp.total);
                line[sizeof(line) - 1] = '\0';
            } else if (id.result == PROMPT_ID_FOUND &&
                       !(id.label &&
                         strncmp(id.label, "ACHIEVEMENT_", 12) == 0 &&
                         strstr(id.label, "_DESC"))) {
                /* ⛔ AN ACHIEVEMENT DESCRIPTION IS NOT A ROW CAPTION.
                 *
                 * It belongs to the HINTS view, which the user opens with
                 * DECIDE and which menu_speak_check_hint() announces on its
                 * own. Appending it here read the unlock condition aloud on
                 * EVERY grid movement -- an extra sentence per keypress while
                 * sweeping 40 items, which is noise on exactly the screen the
                 * user is trying to move through quickly.
                 *
                 * ⚠ The descriptions entered the prompt table for the hint
                 * view; this is where that had a side effect. */
                char spoken[GM_OUT_MAX];
                gm_speak(cap, spoken, sizeof(spoken));
                if (spoken[0] && !gm_has_residue(spoken)) {
                    size_t used = strlen(line);
                    _snprintf(line + used, sizeof(line) - used - 1,
                              ". %s", spoken);
                    line[sizeof(line) - 1] = '\0';
                }
            } else if (!sp.named && sp.game_strip &&
                       (rom_table_has_title(cap) ||
                        rom_table_is_collection_title(cap))) {
                /* ⭐⭐ A GAME-TITLE STRIP: the caption IS the item name.
                 *
                 * Museum > Gallery draws the full arcade title on each
                 * movement -- "Capcom Vs. SNK: Millennium Fight 2000 Pro" --
                 * and the corpus refuses it because those titles are not GMD
                 * message labels. That is a corpus MISS, not an absent signal,
                 * and the screen read as silent because of it.
                 *
                 * ⭐ TWO INDEPENDENT SOURCES, the standard every shipped row
                 * here meets: the game DREW this string on this movement, and
                 * the exe's own ROM table agrees it is a real game title. A
                 * caption that is not in that table is refused -- so a help
                 * sentence or a stray value can never become an item name.
                 *
                 * ⛔ It cannot reuse the value-quote path below: that caps at
                 * 40 characters (two of these titles are 41 and 45) and applies
                 * only to MC_AXIS_VALUE. Widening that guard would loosen every
                 * settings screen to fix this one screen.
                 *
                 * ⚠ Gated on sp.game_strip, so the rule is unreachable on any
                 * screen the table does not declare as a game strip -- being in
                 * the ROM table is not by itself permission to speak. */
                char spoken[GM_OUT_MAX];
                gm_speak(cap, spoken, sizeof(spoken));
                if (spoken[0] && !gm_has_residue(spoken)) {
                    _snprintf(line, sizeof(line), "%s, %d of %d",
                              spoken, sp.position, sp.total);
                    line[sizeof(line) - 1] = '\0';
                }
            } else if (!sp.named && sp.axis == MC_AXIS_VALUE &&
                       is_value_shaped(cap)) {
                /* ⭐⭐ ON A VALUE AXIS, THE CAPTURED TEXT *IS* THE VALUE.
                 *
                 * Changing a setting made the mod say "row 1, 11 of 21" while
                 * the game was displaying "Off" -- the one word that matters.
                 * The corpus check was refusing it, and refusing it was wrong
                 * HERE for a reason that does not apply to captions:
                 *
                 *  - "Off" occurs 22 times in the GMD and "On" 18 times, so
                 *    prompt_identify can never resolve them -- and it does not
                 *    need to. We are not claiming WHICH label it is; we are
                 *    repeating the word the game just put on screen.
                 *  - "1024x768" is not in the corpus AT ALL. Resolutions are
                 *    generated from the display mode, so a corpus test can
                 *    never admit them, however many runs we do.
                 *
                 * ⚠ THE SAFETY ARGUMENT IS DIFFERENT FROM THE CAPTION ONE, and
                 * that is why this branch is narrow. A caption is a CLAIM about
                 * which row you are on, so it must be corpus-verified. A value
                 * is a QUOTE of what the game displayed in response to the
                 * movement we are announcing. The failure mode of a wrong quote
                 * is repeating a stale string, not naming the wrong setting.
                 *
                 * ⛔ Guarded four ways: only on MC_AXIS_VALUE (a horizontal
                 * move that changes a setting), only when the item is NOT
                 * already named, only for value-SHAPED text (short, single
                 * line, no markup -- see is_value_shaped), and only for a
                 * caption stamped with THIS movement, which gt_take_caption
                 * already enforces. */
                /* ⛔⛔ USER-REPORTED, 2026-09-11: "the values in the options
                 * submenus are not correct". They were right, and the fourth
                 * guard above was the hole. Measured in the logs:
                 *
                 *   "Coin Button, 1 of 6, English [en]"
                 *   "Menu Button, 1 of 7, English [en]"
                 *   "Announcer Volume, 1 of 10, Remix"
                 *   "Game, 3 of 9, First to 4"
                 *
                 * Each appends ANOTHER ROW'S value. "Stamped with this
                 * movement" is not the same as "belongs to this row": on a
                 * settings screen several rows write text during one movement,
                 * and gt_take_caption returns whichever arrived last.
                 *
                 * ⭐ THE FIX: only quote text that could actually be a value of
                 * THIS row. If the row has a shipped value set, the quote must
                 * be one of ITS labels; the value is then named by the table
                 * anyway, so the quote only fires for rows with NO table -- and
                 * for those there is nothing to check against, so it is
                 * refused. A wrong value spoken confidently is worse than a
                 * position, which is the whole premise of this file. */
                /* ⛔ SO THIS PATH IS NOW SILENT ON A SETTINGS VALUE AXIS.
                 *
                 * It cannot tell whose value it is holding, and nothing
                 * available here can: the caption channel keys on the movement,
                 * not on the row. Rows WITH a shipped value set are named by
                 * the table (which is checked against the corpus); rows without
                 * one now say "Menu Button, 3 of 7" -- true, and not a claim
                 * about a value belonging to a different setting.
                 *
                 * ⚠ This LOSES the legitimate case it was built for -- quoting
                 * a generated value like "1024x768" or a volume step, which no
                 * table can ever cover. That is a real cost, taken knowingly:
                 * a wrong value is worse than a missing one, and the user heard
                 * the wrong values before I measured them. */
                log_line("mcspeak: value quote SUPPRESSED on a settings axis "
                         "(row %d): \"%.40s\" cannot be attributed to this row.",
                         sp.row, cap);
            } else if (id.result == PROMPT_ID_AMBIGUOUS) {
                /* Logged, never spoken: the string belongs to more than one
                 * screen, so naming one would be a guess. */
                log_line("mcspeak: caption AMBIGUOUS, not spoken: \"%.70s\"",
                         cap);
            } else {
                log_line("mcspeak: caption not in the corpus, not spoken: "
                         "\"%.70s\"", cap);
            }
        }
    }

    menu_speak_line(line);

    /* ⭐ The end-to-end cost of one announcement, on the worker thread. */
    QueryPerformanceCounter(&t1);
    {
        static double worst, total;
        static long   calls;
        static double next_report = 200.0;
        double el = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 /
                    (double)qf.QuadPart;
        calls++;
        total += el;
        if (el > worst) worst = el;
        /* ⚠ 20 ms is the poll period, so anything at or above it is already a
         * whole tick of delay and worth naming. Reported on a growing
         * threshold so a long session cannot flood the log. */
        if (worst >= 20.0 && total >= next_report) {
            next_report = total * 2.0;
            log_line("mcspeak: ⚠ the ANNOUNCEMENT itself is slow -- %ld call(s), "
                     "worst %.1f ms, mean %.1f ms (build + text + speechSay). "
                     "This is time the user waits after the cursor moves.",
                     calls, worst, total / (double)calls);
        }
    }
}



/* ⭐ The one place a line reaches the speech backend. Extracted from
 * menu_speak() so the modal announcer shares it rather than duplicating the
 * conversion, the interrupt policy and the honest logging. */
/* The last line the backend actually accepted, for the read-current-text key.
 * Worker-thread only, like every other menu_speak state. */
static wchar_t g_last_spoken[1024];

void menu_speak_line(const char *line)
{
    /* ⛔⛔ THIS WAS wchar_t[256] AND IT SILENCED THE WHOLE MOVE LIST.
     *
     * USER-REPORTED: "The move list doesn't read." The panel path builds a
     * line[1024] -- a heading plus TEN rows, each now carrying its condition
     * AND its notation -- and every one of them ran past 256 wchars, so
     * MultiByteToWideChar returned 0 (ERROR_INSUFFICIENT_BUFFER) and the
     * readout was DISCARDED at the very last step. The panel was built
     * correctly every time; nothing was ever spoken.
     *
     * ⚠ The regression came from making rows BETTER: single-row callers fit
     * 256 comfortably, and the panel only crossed the limit once conditions
     * were added to the notation. A shared helper sized for one caller's
     * longest string is a trap for the next caller, so this is sized from the
     * LARGEST line any caller can pass, not from a round number.
     *
     * ⭐ The counts in the log are what found this in ONE run: "10 name
     * object(s), 10 pair object(s)" on the SUCCESS path proved the readout was
     * built, so the fault had to be downstream of it.
     *
     * ⭐⭐ THE FIX IS NOT A BIGGER NUMBER -- IT IS NO LIMIT AT ALL. A fixed
     * buffer here is a cap on what the user may hear, chosen by whoever wrote
     * the constant, and every such constant this project has shipped was
     * eventually reached by real content. The conversion is now sized from THIS
     * line: ask the API how many wchars it needs, allocate exactly that, and
     * convert. No caller can outgrow it, so there is nothing to truncate and no
     * truncation path to get wrong.
     *
     * ⚠ A UTF-8 string never needs more wchars than it has bytes, so the size
     * is bounded by strlen and the heap call is small. The stack buffer below
     * covers the ordinary case without allocating at all. */
    wchar_t stackbuf[512];
    wchar_t *w = stackbuf;
    wchar_t *heap = NULL;
    int n, need;

    if (!line || !line[0])
        return;

    need = MultiByteToWideChar(CP_UTF8, 0, line, -1, NULL, 0);
    if (need <= 0) {
        /* ⭐ A conversion failure must not speak mojibake. Say nothing and log,
         * rather than emit bytes the reader will render as noise.
         *
         * ⚠ This is now the ONLY refusal, and it means the string is genuinely
         * not valid UTF-8 -- never that it was too long. Report the reason and
         * the length: "could not convert" alone cost a whole round, because the
         * string it printed was perfectly well-formed and the message pointed
         * at the TEXT when the fault was the BUFFER. */
        log_line("mcspeak: could not convert %d byte(s) to UTF-16 "
                 "(GetLastError=%lu, not a length problem); not speaking: "
                 "\"%s\"", (int)strlen(line), (unsigned long)GetLastError(),
                 line);
        return;
    }
    if (need > (int)(sizeof(stackbuf) / sizeof(stackbuf[0]))) {
        heap = (wchar_t *)malloc((size_t)need * sizeof(wchar_t));
        if (!heap) {
            log_line("mcspeak: out of memory for a %d wchar line; not speaking.",
                     need);
            return;
        }
        w = heap;
    }
    n = MultiByteToWideChar(CP_UTF8, 0, line, -1, w, need);
    if (n <= 0) {
        log_line("mcspeak: conversion failed on the second pass "
                 "(GetLastError=%lu); not speaking.",
                 (unsigned long)GetLastError());
        free(heap);
        return;
    }

    /* interrupt=TRUE: the user has moved on, so a queued previous item is
     * stale. Holding a direction must not build a backlog to read out. */
    /* ⚠ The log says what ACTUALLY happened. Writing `mcspeak: "..."`
     * unconditionally is what let a completely silent mod look healthy in the
     * log for several runs. */
    if (speech_say(w, TRUE)) {
        log_line("mcspeak: spoke \"%s\"", line);
        /* ⭐ Retained so the shared read-current-text key (`) can repeat the
         * current row in the menus, exactly as it reads the screen in-game.
         *
         * ⚠ ONLY what was actually SPOKEN is retained, and only after the
         * backend accepted it. A line the user never heard must not come back
         * as "what is on screen now" -- that would make the ` key report the mod's
         * intentions rather than the user's experience.
         *
         * ⚠⚠ And this is deliberately NOT a whole-screen snapshot. The shell's
         * prose recorder is a HISTORY, not a screen, and the text-source audit
         * declined to add a read-all over it for that reason. Repeating the
         * focused row is a claim we can actually support. */
        lstrcpynW(g_last_spoken, w,
                  (int)(sizeof g_last_spoken / sizeof g_last_spoken[0]));
    } else
        log_line("mcspeak: ⛔ NOT SPOKEN (backend refused): \"%s\"", line);
    free(heap);
}

/* The shell's read-current-text source. Registered from menu_speak_init(). */
static tc_read_result menu_read_current(void)
{
    /* ⚠ INACTIVE, not EMPTY, when nothing has been spoken yet: the registry
     * then falls through to another game's source instead of announcing that
     * the menus are empty while CVS2 is on screen. */
    if (!g_last_spoken[0]) return TC_READ_INACTIVE;
    return tc_say(g_last_spoken, TRUE, "mcspeak (read current)")
               ? TC_READ_SPOKE : TC_READ_REFUSED;
}

BOOL menu_speak_register_reader(void)
{
    return tc_register("the menus", menu_read_current);
}


/* ⭐⭐ THE DISCOVERY GATE IS BACK, and the note that removed it is superseded.
 *
 * The gate was deleted on this reasoning: it had been added for a cost that was
 * later fixed, and with the file absent -- the normal state -- "the only
 * mechanism that can name a screen never ran at all". That was sound WHEN
 * WRITTEN. It is false now, and the evidence is one-sided:
 *
 *   - ⛔ The sweep has stalled the user's session TWICE (37 s on 2026-09-09
 *     23:13, 11.9 s at 23:52). Both times the mod stopped speaking and the
 *     user navigated blind.
 *   - ⛔ In every run it has ever made, it printed ONE candidate list:
 *     "248:Heavy Kick  659:Single-player Rounds  730:Damage:" -- three wrong
 *     answers for the music screen.
 *   - ⛔ NOT ONE shipped g_screens row was named by it. Every row cites live
 *     text, the disassembly, or the caption channel.
 *   - ⭐ And the premise is gone: the CAPTION channel names screens now, and
 *     it is what named Online Play (tools/name_screens.py, 3 verified bases
 *     reproduced). Discovery is no longer "the only mechanism"; it is the
 *     weakest one.
 *
 * ⚠ This is not a retreat from the earlier lesson -- a guard must be
 * re-measured when its cost is fixed. It IS that lesson applied again with new
 * measurements: the cost came back, the benefit never arrived, and a better
 * mechanism exists. Re-measure means re-measure, in both directions.
 *
 * ⭐⭐ AND THE PRIORITY THAT WAS INVERTED: this is a DEVELOPMENT AID. A stall
 * in the user's speech is a broken mod; a diagnostic that does not run is an
 * inconvenience to ME. Two rounds went into making the diagnostic fast while
 * the user's actual bug -- "it stopped reading" -- stayed open. Off by default;
 * create cfc2_discover.on next to the exe when deliberately naming a screen. */
/* Set once at startup by mod.c, which owns the opt-in-file logic. Default OFF:
 * a session that never touches it never pays for discovery. */
static BOOL g_discovery_enabled;

void menu_speak_set_discovery(BOOL on)
{
    g_discovery_enabled = on;
}

BOOL menu_speak_discovery_enabled(void)
{
    return g_discovery_enabled;
}

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
/* ⭐⭐ THE OPTION-ID PROBE -- ONE SCREEN ONLY, and that is the point.
 *
 * The 3-item options strip (caller 0x1400b2729, layout 11_option/option_2)
 * cannot be named from the GMD: its three labels are non-contiguous
 * (MENU_OP_GAME_SETTING 225, PAD_SETTING 226, DISPLAY_SOUND_SETTINGS 574) so
 * no contiguous-run sweep can propose them, and the function holds no label
 * constants -- the text comes through layout data.
 *
 * But the screen keeps an ARRAY OF OPTION IDS at screen+0x340, indexed by the
 * cursor, and dispatches on it: id 1 sets +0xd0338, id 2 sets +0xd0330=1 with
 * +0xd0334=0, id 3 sets +0xd0330/+0xd0334 from the value. Read off
 * 0x1400b27ab..0x1400b280c. Those ids say WHICH option each item is, which is
 * what a label map needs.
 *
 * ⛔⛔ SCOPED TO THIS ONE CALLER, deliberately. I previously dumped this same
 * offset for EVERY screen and it printed pointers and padding formatted as
 * menu semantics -- 741845392, 1090385120 -- because +0x340 means this only
 * here. A field offset read out of one function describes that function. */
/* ⭐⭐ WHAT THE SETUP CODE SAYS -- read at 0x1400b2460, no run needed.
 *
 *     [rbx+0x340] = 0                    ; item 0 -> option id 0
 *     [rbx+0x33c] = 1                    ; count = 1
 *     if (mode != 1 || [+0xd037c] < 2)   ; CONDITIONAL
 *         [rbx+rdx*4+0x344] = 1          ; item 1 -> option id 1
 *         count++
 *     [rbx+rax*4+0x340] = 2              ; last item -> option id 2
 *     count++
 *
 * So the strip is ids 0, 1, 2 in cursor order and the MIDDLE item is
 * conditional -- which is why the item count varies between 2 and 3. The
 * dispatch at 0x1400b27b2 then treats them distinctly: id 0 writes no flag,
 * id 1 sets +0xd0338, id 2 sets +0xd0330=1 with +0xd0334=0.
 *
 * ⛔ AND THAT STILL DOES NOT NAME THEM. The ids are positions in a switch,
 * not label indices, and the flags they set are consumed somewhere that does
 * not name them either. The live probe below therefore confirms the ORDER and
 * the COUNT, not the words. Naming this screen needs the runtime GUI text,
 * which is a bigger piece of work than a label map. */
static void probe_option_ids(const mc_move *m)
{
    if (!m || !m->screen || !m->cursor)
        return;
    if ((ULONGLONG)(ULONG_PTR)m->caller != 0x1400b2729ULL)
        return;

    int count = m->max_index - m->min_index + 1;
    if (count <= 0 || count > 8)
        return;

    static BOOL done;
    if (done)
        return;

    const char *ids = (const char *)m->screen + 0x340;
    if (!mem_readable(ids, (SIZE_T)count * 4))
        return;
    int v[8];
    if (!mem_copy(v, ids, (SIZE_T)count * 4))
        return;
    done = TRUE;

    char b[128];
    size_t u = 0;
    b[0] = 0;
    for (int i = 0; i < count; i++) {
        int k = _snprintf(b + u, sizeof(b) - u, "%s%d", u ? " " : "", v[i]);
        if (k > 0 && (size_t)k < sizeof(b) - u)
            u += (size_t)k;
    }
    log_line("mcspeak: [OPTPROBE] options strip (caller 0x1400b2729): "
             "%d items, option ids [%s]. ⚠ These identify WHICH setting each "
             "item is; the dispatch at 0x1400b27b2 treats 1, 2 and 3 as three "
             "different destinations.", count, b);
}

void menu_speak_probe_rest(const mc_move *m)
{
    if (!m || !m->screen)
        return;

    probe_option_ids(m);

    const mc_screen *s =
        menu_speak_lookup_n((ULONGLONG)(ULONG_PTR)m->caller,
                            m->max_index - m->min_index + 1);
    if (!s || s->kind != MC_SETTINGS_VALUE)
        return;

    /* ⛔⛔ GATE ON THE CALLER, not merely on "row 0 of a settings screen".
     *
     * Row 0 is the GAME LIST on exactly two screens; everywhere else it is
     * Language, Music Volume, Microphone, Resolution... This probe was firing
     * on EIGHTEEN different callers across the logs -- 16 times on PC Settings
     * alone -- announcing "IF the mapping holds, this is Capcom Fighting Jam"
     * while the user moved through display languages.
     *
     * ⚠ Nothing was ever SPOKEN, so this never reached the ear. But it polluted
     * the logs that every offline audit in this project reads, and a diagnostic
     * that cries wolf on 16 screens is worse than no diagnostic.
     *
     * ⭐ Same rule the speaking path already follows (case 25): the caller list
     * is the key, because row 0 means different things on different screens. */
    if (s->caller != 0x1400b1bd9ULL && s->caller != 0x140098986ULL)
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

    /* ⭐⭐ ONE SIGHTING NAMES NOTHING -- KEEP WATCHING A SCREEN AS IT MOVES.
     *
     * Reporting once per caller gave one position of live text per screen, and
     * one position cannot discriminate: for a 7-item menu, base 250 says
     * "Resume" and base 587 says "Original (4:3)", and BOTH are consistent
     * with a single reading. Every screen then cost the user a question, which
     * does not scale to 21 of them.
     *
     * ⭐ But the rival bases disagree at OTHER positions. So a screen is
     * re-examined while its candidate set is still ambiguous: each new
     * position the user visits prints what each surviving base would call it,
     * and a wrong base is eliminated the moment its guess stops making sense
     * as a menu. Three positions of "Resume / Restart / How to Play" identify
     * base 250 with no input from anyone.
     *
     * ⚠ Bounded: MAX_REPORTS sightings per caller, so a screen the user
     * lingers on cannot flood the log. After that it goes quiet for good. */
    /* ⭐⭐ THROTTLE ON NEW POSITIONS, NOT ON SIGHTINGS.
     *
     * ⛔ The bug this replaces: the cap counted REPORTS, so a screen the user
     * moved around on spent its budget re-reporting positions already seen.
     * The 13:09 run visited positions 4 AND 5 of 0x14009ba21 and only 4 was
     * ever swept -- the second reading, the one that would have discriminated,
     * was thrown away.
     *
     * ⭐ And the data was never missing: every `mc:` line already records the
     * exact index and count of every movement. The mod knew each position; it
     * just declined to look anything up for them. The user pointed this out.
     *
     * So the budget is now spent only on positions NOT yet seen for this
     * screen, which is exactly the information that narrows the candidates. */
    enum { MAX_REPORTS = 8 };
    static int reports[MAX_SEEN];
    static unsigned seen_pos[MAX_SEEN];   /* bitmask of positions swept */
    BOOL first = TRUE;

    for (int i = 0; i < nseen; i++) {
        if (seen[i] == m->caller) {
            int off0 = m->new_index - m->min_index;
            unsigned bit = (off0 >= 0 && off0 < 32) ? (1u << off0) : 0u;
            /* ⭐ Nothing new to learn from a position already swept. */
            if (bit && (seen_pos[i] & bit))
                return;
            if (reports[i] >= MAX_REPORTS)
                return;
            reports[i]++;
            seen_pos[i] |= bit;
            first = FALSE;
            goto sweep;
        }
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
    reports[nseen] = 1;
    {
        int off0 = m->new_index - m->min_index;
        seen_pos[nseen] = (off0 >= 0 && off0 < 32) ? (1u << off0) : 0u;
    }
    seen[nseen++] = m->caller;

sweep:
    ;
    int count = m->max_index - m->min_index + 1;
    if (first)
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
    /* ⛔⛔ A NOTE THAT WAS HERE CLAIMED "ON BY DEFAULT ... MEASURED, not
     * estimated ... under 1 ms". It was written in good faith and it was
     * wrong: the benchmark behind it had drifted from this code (it capped at
     * 600 bases and never ran the relaxed second pass), and the real cost
     * stalled the user for 37 s and then 11.9 s. Removed rather than left to
     * reassure the next reader. The history it described is preserved in the
     * v1/v2/v3 list above and in git.
     *
     * ⛔ OFF unless deliberately enabled. This is the sweep that stalled the
     * user twice for zero findings -- see the note on menu_speak_set_discovery.
     * The "unknown screen" header above still logs either way, so a run always
     * records WHICH screens are unnamed; only the expensive candidate hunt is
     * gated. */
    if (!g_discovery_enabled) {
        static BOOL said;
        if (!said) {
            said = TRUE;
            log_line("mcspeak: [DISCOVERY] candidate sweep is OFF (create "
                     "cfc2_discover.on next to the exe to enable it). Unknown "
                     "screens are still logged; naming is better served by "
                     "the caption channel -- see tools/name_screens.py.");
        }
        return;
    }

    if (gmd_text_count() > 0) {
        /* ⭐⭐ TWO PASSES: strict first, relaxed if strict finds too little.
         *
         * ⛔ THE FAILURE THIS FIXES, measured on a real run. Requiring a full
         * parallel HELP run leaves exactly ONE candidate for a 9-item screen
         * -- base 248, which reads "Heavy Kick / Pause Menu" and is plainly a
         * slide, not the menu. Meanwhile 76 loose 9-item runs exist and the
         * true answer is among them. The strict rule did not sharpen the
         * shortlist there; it DELETED the answer and left only a wrong one,
         * which is worse than a long list.
         *
         * ⭐ The user challenged my calling the extra candidates "nonsense",
         * and was right: they are coherent runs, and the filter is choosing
         * among real possibilities rather than removing junk. A chooser that
         * can discard the true answer must therefore be able to back off.
         *
         * So: run strict; if it yields fewer than 2 candidates, run again
         * accepting one trailing sentence, and SAY which pass produced the
         * list -- a shortlist means something different depending on how hard
         * it had to try. */
        int off = m->new_index - m->min_index;
        /* ⛔⛔ THE CAP THAT HID A REAL MENU. This was 600, justified as "the
         * highest real menu run is ~590". That was wrong: MODE_SELECT and its
         * Arcade / Versus / Training items sit at 4103..4108, and the screen
         * reached from Offline Play is that menu. No number of runs could have
         * found it, because the sweep never looked past label 600.
         *
         * ⭐ The whole table is ~4165 labels and a full sweep measured under a
         * millisecond, so the cap was never buying anything worth this. Sweep
         * everything. A bound chosen from an assumption about the data is the
         * thing to distrust, not the cost. */
        int total = gmd_text_count();
        int shown = 0;
        char batch[512];
        size_t used = 0;
        int strict = 1;

        /* ⛔⛔ A WALL-CLOCK BUDGET, because every COST ESTIMATE for this sweep
         * has been wrong -- three times now, all in this direction.
         *
         *   - "~1100 bases x 5 lookups"      -> 4.8 s in-game (v3).
         *   - "one guarded read per byte"    -> 8 s in-game.
         *   - "under 1 ms, measured"         -> ⛔ 37 SECONDS in-game, on the
         *     Music screen (9 items) in the run of 2026-09-09 23:13. The user
         *     heard the mod stop reading and had to navigate blind.
         *
         * ⚠ That last one is the important one: it WAS measured, on the real
         * blob, on the shipping path -- and the benchmark still understated it
         * by orders of magnitude, because the bench's blob is one clean
         * allocation while the game's sits on a heap with different page
         * boundaries, and because the benchmark had drifted from the code (it
         * still capped at 600 bases and never ran the relaxed second pass).
         *
         * ⭐ So stop predicting the cost and MEASURE IT AS IT RUNS. A budget
         * cannot be wrong about the machine it is running on. The sweep gives
         * up at 25 ms -- a quarter of the poll interval -- and SAYS it gave up,
         * because a shortlist truncated by time is a different finding from a
         * shortlist that is genuinely short.
         * (memory: measure-dont-predict, budget-cap-what-you-encounter) */
        LARGE_INTEGER freq, t0;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t0);
        const double BUDGET_MS = 25.0;
        int timed_out = 0;
        int bases_examined = 0;

        /* ⭐⭐ CLASSIFY EACH STRING ONCE, not once per window it appears in.
         *
         * The sweep asks two yes/no questions of every string: is it
         * ITEM-shaped (short, no markup, no terminal punctuation) and is it
         * BLURB-shaped (ends in . ? !). Both depend only on the string, not on
         * the base -- but the old loop re-read string i once for every window
         * containing it, so each of 4165 strings was read up to `count` times.
         * At count=67 that is ~280,000 guarded reads to answer 8330 questions.
         *
         * ⭐ One pass answers them all: 4165 reads, then the window test is
         * pure array arithmetic. That is what makes a full sweep affordable
         * rather than something the time budget has to cut off at 20%.
         *
         * ⚠ The classification is EXACTLY the old predicate, moved -- not a
         * relaxed version of it. A cheaper filter that admits different
         * candidates would be a behaviour change wearing a performance
         * change's clothes. */
        static unsigned char cls[GMD_MAX_STRINGS];
        static int cls_done;    /* how many strings are classified so far */
        static int cls_total;   /* the table size they were classified for */
        /* ⛔⛔ THE CLASSIFICATION IS INSIDE THE BUDGET, and it is RESUMABLE.
         *
         * The first version of this ran the whole pass before the budget
         * started, and it took ELEVEN AND A HALF SECONDS in-game -- I had moved
         * the stall rather than removed it, and the budget then reported
         * "0 of 4165 bases" because the clock was already spent. Reported by
         * the user as the mod still breaking. (The underlying cost is fixed in
         * gmd_find.c: per-string VirtualQuery in a 2336-region process.)
         *
         * ⭐ So it is bounded like everything else here, and picks up where it
         * left off on the next movement instead of restarting. A screen visited
         * twice therefore classifies further each time rather than doing the
         * same expensive prefix again and never finishing. */
        if (cls_total != total) {          /* table changed -> start over */
            cls_done = 0;
            cls_total = total;
        }
        {
            int n = total < GMD_MAX_STRINGS ? total : GMD_MAX_STRINGS;
            int start = cls_done;
            while (cls_done < n) {
                if ((cls_done & 63) == 0) {
                    LARGE_INTEGER now;
                    QueryPerformanceCounter(&now);
                    if ((now.QuadPart - t0.QuadPart) * 1000.0 /
                        (double)freq.QuadPart > BUDGET_MS) {
                        timed_out = 1;
                        break;
                    }
                }
                char s[128];
                unsigned char f = 0;
                if (gmd_text_by_index(cls_done, s, sizeof(s)) && s[0]) {
                    size_t L = strlen(s);
                    char last = s[L - 1];
                    int terminal = (last == '.' || last == '?' || last == '!');
                    if (s[0] != '<' && L <= 32 && !terminal)
                        f |= CLS_ITEM;
                    if (terminal)
                        f |= CLS_BLURB;
                }
                cls[cls_done++] = f;
            }
            if (cls_done > start) {
                /* ⭐⭐ REPORT THE PER-READ COST FROM THE GAME ITSELF.
                 *
                 * Two offline benchmarks have now failed to reproduce this
                 * cost: a clean-blob one understated it ~7000x, and a
                 * deliberately fragmented one (6132 regions, more than the
                 * game's 2336) still showed 0.0003 ms/read against the ~2.9 ms
                 * the live run implies. So the cause is NOT region count, and
                 * I have guessed wrong twice.
                 *
                 * Stop guessing: make the shipping code state its own measured
                 * rate. This number comes from the machine that actually has
                 * the problem. (memory: dont-diagnose-from-one-log,
                 * measure-dont-predict) */
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                double ms = (now.QuadPart - t0.QuadPart) * 1000.0 /
                            (double)freq.QuadPart;
                int did = cls_done - start;
                log_line("mcspeak: [DISCOVERY] classified %d..%d of %d strings "
                         "in %.1f ms (%.4f ms per string)%s",
                         start, cls_done - 1, n, ms,
                         did > 0 ? ms / did : 0.0,
                         cls_done < n ? " -- budget reached; continues on the "
                                        "next movement"
                                      : " -- COMPLETE");
            }
        }

        /* ⚠⚠ SWEEP ONLY THE CLASSIFIED PREFIX.
         *
         * `cls` is zeroed static storage, so an UNCLASSIFIED entry reads as
         * "neither ITEM nor BLURB" -- indistinguishable from a real negative.
         * Sweeping past cls_done would therefore silently report "no candidate"
         * for a region never examined, which is exactly the "0 failed means 0
         * checked" trap. Bound by what has actually been classified, and the
         * log below reports that number rather than the table size. */
        int swept = cls_done;

    again:
        shown = 0;
        used = 0;
        batch[0] = 0;

        for (int base = 0; base + count < swept && shown < 8; base++) {
            /* Check the clock every 64 bases: often enough to stop promptly,
             * rare enough that the check itself is not the cost. */
            if ((base & 63) == 0) {
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                double ms = (now.QuadPart - t0.QuadPart) * 1000.0 /
                            (double)freq.QuadPart;
                if (ms > BUDGET_MS) {
                    timed_out = 1;
                    break;
                }
            }
            bases_examined++;
            char cur[128];
            int ok = 1;
            /* The item run: `count` consecutive ITEM-shaped strings. */
            for (int i = 0; i < count; i++) {
                if (!(cls[base + i] & CLS_ITEM)) { ok = 0; break; }
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
            /* ⚠ `strict` demands the full parallel run; when the sweep is
             * re-run relaxed, ONE trailing sentence is enough. See the
             * two-pass logic below for why both exist. */
            int need = strict ? count : 1;
            if (base + count + need > swept)
                continue;
            for (int i = 0; i < need; i++) {
                if (!(cls[base + count + i] & CLS_BLURB)) { ok = 0; break; }
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
        /* ⚠ EVERY line carries its caller. The header prints once per screen
         * now, so without this a later sighting is unattributable -- it sits
         * under whatever header happened to come before it, and grouping the
         * log by screen becomes guesswork. A diagnostic whose lines cannot be
         * attributed is much weaker than it looks. */
        /* ⭐ Back off rather than report a shortlist that may exclude the
         * answer. Fewer than two survivors means the strict rule was too
         * sharp for this screen's labels. */
        /* ⚠ Do NOT re-run relaxed after a timeout. The budget is for the whole
         * discovery event, not per pass -- and a strict pass that ran out of
         * time has not established that strict "found too little", only that it
         * did not finish. Backing off there would double the stall AND report a
         * weaker list on false grounds. */
        if (strict && shown < 2 && !timed_out) {
            strict = 0;
            goto again;
        }

        /* ⭐ A TRUNCATED SWEEP MUST SAY SO. "3 candidates from 1200 of 4165
         * bases" and "3 candidates from all 4165" are different findings, and
         * a reader who cannot tell them apart may conclude the answer is not in
         * the table when the sweep simply never reached it.
         * (memory: probe-zero-checked-looks-like-zero-failed) */
        if (timed_out)
            log_line("mcspeak: [DISCOVERY] ⚠ caller=0x%llx  sweep STOPPED at "
                     "the %.0f ms budget after %d of %d bases -- the list below "
                     "is PARTIAL. Absence from it is NOT evidence. (This budget "
                     "exists because an unbounded sweep stalled a live run for "
                     "37 seconds.)",
                     (unsigned long long)(ULONG_PTR)m->caller,
                     BUDGET_MS, bases_examined, total);

        if (used)
            log_line("mcspeak: [DISCOVERY] caller=0x%llx  position %d of %d; "
                     "%s candidate bases (each shows what item %d would be "
                     "called -- ⚠ neighbouring bases can be SLIDES of one real "
                     "run, so match the text to the screen): %s",
                     (unsigned long long)(ULONG_PTR)m->caller,
                     off + 1, count,
                     timed_out ? "[PARTIAL: sweep hit its time budget]"
                     : strict ? "[strict: one blurb per item]"
                            : "[RELAXED: strict found <2, list is longer and "
                              "weaker]",
                     off + 1, batch);
        else
            /* ⚠ Report the count ACTUALLY EXAMINED, not `total`. Saying "no
             * candidate in the first 4165 labels" after a sweep that stopped
             * at 1200 is a false claim about coverage, and this project has
             * already shipped a "0 failed" that meant "0 checked". */
            log_line("mcspeak: [DISCOVERY] caller=0x%llx  position %d of %d; "
                     "no candidate run found in %d of %d labels%s. ⚠ This "
                     "screen's labels may simply lack the one-blurb-per-item "
                     "shape this looks for -- absence here is NOT evidence the "
                     "labels are missing.",
                     (unsigned long long)(ULONG_PTR)m->caller,
                     off + 1, count, bases_examined, total,
                     timed_out ? " (sweep hit its time budget)" : "");
    }

    /* ⭐ Emit a row in the CURRENT shape, including the fields added for
     * settings screens. An older version of this line printed a 4-field row
     * that no longer compiles -- a discovery hint that produces a broken row is
     * worse than none, because it is followed. */
    if (!first)
        return;                 /* the how-to-name-it hint is a one-time note */

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
