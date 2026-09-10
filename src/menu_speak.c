/*
 * Turning a cursor movement into speech -- see menu_speak.h for the rules.
 */

#include "menu_speak.h"
#include "gmd_find.h"
#include "rom_table.h"
#include "memsafe.h"
#include "speech.h"
#include "log.h"
#include "gui_text.h"     /* the live caption channel */
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

/* ⛔ NOT NAMEABLE FROM THE GMD -- the Museum GALLERY lists (0x14009a482 and
 * 0x14009af21, 9 items each, cursor +0x33c, count from +0x340).
 *
 * Checked, so nobody repeats it: there is no 9-item run of game names in the
 * GMD at all, and the confirm handler at 0x14009a990 indexes tables at
 * 0x12072f8 / 0x1207300 / 0x1207328 / 0x1207350 by the cursor -- resource and
 * layout ids, not label indices. The nine entries are the eight games plus the
 * collection, drawn as logos, exactly like Select Game's Game row.
 *
 * ⭐ So the route is the ROM table (src/rom_table.h), not a label run -- but
 * only once something establishes that the Gallery's 0..8 uses the same
 * ordering as Select Game's 0..7, which nothing yet does. Until then these
 * speak position only, which is correct. */

/* ⭐⭐ Select Mode, reached from Offline Play -> Select Game.
 *
 * ESTABLISHED FROM THE CODE, not from a candidate list. The main menu's
 * dispatch sends Offline Play to screen id 4 (0x140097b6a), and the function
 * holding this caller (0x14009b920) contains the confirm dispatch at
 * 0x14009bb34, whose branches write mode 0, 1 and 2 to +0xd0374 for items 0,
 * 1 and 2. The GMD names those modes at 4105..4107: Arcade, Versus, Training.
 * MODE_SELECT "Select Mode" sits at 4103 with its blurb at 4104.
 *
 * ⛔⛔ A MAP, NOT A BASE, and only THREE entries for a SIX-item screen. The
 * mode labels are not a contiguous run covering the whole list -- items 3..5
 * are something else (4108 is "Training Mode (Boss)", but nothing establishes
 * it is item 3 here). So the map names what the dispatch proves and leaves the
 * rest unnamed; menu_speak_build refuses a map whose length disagrees with the
 * live count, which is exactly the guard that keeps this honest.
 *
 * ⚠ Consequence: this row currently names NOTHING at 6 items, by design. It
 * ships so that the moment a run shows the screen at 3 items -- or a later
 * session establishes items 3..5 -- the mapping is already correct rather than
 * guessed. ⛔ Do NOT pad it to six with plausible labels.
 *
 * ⚠⚠ 0x14009ba21 is the exact caller a GUESSED row was once shipped for, which
 * made a different 6-item screen read out the main menu's labels. That is why
 * nothing here is inferred from item count or from base 37 being "close". */
/* ⭐⭐ AND THE RUN THAT NOTE WAS WAITING FOR ARRIVED -- at a DIFFERENT CALLER.
 *
 * 2026-09-10: caller 0x1400b2729, the 3-item horizontal strip reached after
 * choosing a game, sets its own item text live:
 *     position 3 -> "Training Mode"   (MODE_TRAINING 4107)
 *     position 1 -> "Arcade Mode"     (MODE_ARCADE   4105)
 * Both read straight off `gt: * TEXT SET` lines stamped with that movement, so
 * this map's first and last entries are confirmed BY THE GAME at the exact
 * positions it claims. The same array now serves that caller at 3 items.
 *
 * ⚠ Position 2 ("Versus Mode", 4106) was never visited and its text has never
 * appeared. It is shipped on two facts, not on plausibility: it is BRACKETED
 * by two confirmed endpoints one index apart, and the setup code at
 * 0x1400b2460 builds the option-id array in ascending order. That is weaker
 * than the endpoints and is said so here.
 *
 * ⛔⛔ I ALSO HAD THIS SCREEN WRONG FOR DAYS. g_game_options_candidate below
 * guessed it was Game Settings / Controller Settings / Display & Sound
 * Settings, reasoned from the layout name `11_option/option_2`. The live text
 * says Arcade / Versus / Training. The layout name proved the screen was an
 * OPTIONS screen and told us nothing about which labels it draws -- exactly
 * the gap that comment warned about, and I still nearly shipped it. */
static const int g_mode_select_rows[] = { 4105, 4106, 4107 };

/* ⭐⭐ ONLINE PLAY -- established 2026-09-09 (23:13 run) from the game's OWN
 * captions, one per row. Not inferred, not from item count, not from a
 * contiguous run.
 *
 * Each row was named by the caption the shell set while the cursor stood on
 * it, matched to its item by stem (HELP_CASUAL_MATCH -> CASUAL_MATCH):
 *
 *   pos 1  idx   68  CASUAL_MATCH       "Play a friendly match against other players."
 *   pos 2  idx   69  RANK_MATCH         "Test your skills in a competitive match..."
 *   pos 3  idx   70  CUSTOM_MATCH       "Create your own lobby with your own game rules..."
 *   pos 4  idx   71  RANK_LEADERBOARD   "View leaderboards based on the League Points..."
 *   pos 5  idx 1076  SCORE_ATTACK       "Compete for high scores under set rules."
 *   pos 6  idx 1078  SCORE_LEADERBOARD  "View High Score Challenge leaderboards."
 *
 * ⭐⭐ IT IS NOT A CONTIGUOUS RUN, which is why no base could ever have named
 * it: rows 1-4 come from 68..71 but rows 5-6 come from 1076/1078, ~1000 labels
 * away. tools/name_screens.py reported ⛔ SIGHTINGS DISAGREE and refused to
 * pick a base -- correctly. A "close-looking" base would have spoken
 * HELP_ strings as item names at positions 5 and 6.
 *
 * ⚠ Row 3's caption arrived too late to be spoken with its movement, so the
 * log's spoken line says only "3 of 6". It is established anyway, from the
 * FOCUS RECORD, which captured "Create your own lobby..." under that
 * movement's own sequence number -- and independently from the game setting
 * the literal string "Custom Match". Two sources, neither of them arithmetic.
 * (The gap 68,69,?,71 also predicts 70, but a prediction is not why it ships.) */
static const int g_online_play_rows[] = { 68, 69, 70, 71, 1076, 1078 };

/* ⭐⭐ PC SETTINGS (Options > PC Settings), 4 rows. Established 2026-09-10 from
 * the game's OWN caption on EVERY position -- not one sighting extrapolated:
 *
 *   pos 1  idx  218  MENU_OP_PC_RESO   "Change the resolution."
 *   pos 2  idx  219  MENU_OP_PC_DISP   "Change the display mode."
 *   pos 3  idx 1090  MENU_OP_PC_VSYNC  "Adjust vertical sync settings."
 *   pos 4  idx  168  MENU_OP_DEFAULT   "Reset to default settings."
 *
 * ⛔ NOT a contiguous run, and the near-miss is instructive: base 218 fits
 * positions 1-2 and then reads "Windowed" / "Borderless Windowed" -- the
 * VALUES of the Display setting, not rows. A value label is short, unmarked
 * and unpunctuated, so it passes every shape test a base validator can apply.
 * Only a caption per position rules it out. VSync sits ~870 labels away. */
static const int g_pc_settings_rows[] = { 218, 219, 1090, 168 };

/* ⭐⭐ SOUND SETTINGS (Options > Music), 6 rows. Same standard -- every
 * position carries its own caption:
 *
 *   1  190  MENU_OP_BGM_VOLUME    "Adjust the volume of background music."
 *   2  191  MENU_OP_SE_VOLUME     "Adjust the volume of sound effects."
 *   3  192  MENU_OP_VOICE_VOLUME  "Adjust the volume of lines spoken by..."
 *   4  193  MENU_OP_GAME_VOLUME   "Adjust the overall volume within each..."
 *   5  194  MENU_OP_HERE_SE       "Set the type of music jingle that plays..."
 *   6  168  MENU_OP_DEFAULT       "Reset to default settings."
 *
 * ⚠ Rows 1-5 ARE contiguous (190..194) but row 6 is at 168, so a base still
 * cannot address the screen. "Default" being the last row of several settings
 * screens is why it keeps appearing far from its siblings.
 *
 * ⚠ Four of these six were unnameable until the caption join learned that the
 * HELP label is sometimes ABBREVIATED: HELP_OP_BGM_VOL -> MENU_OP_BGM_VOLUME.
 * The evidence was already in the log; the rule was wrong. */
static const int g_sound_settings_rows[] = { 190, 191, 192, 193, 194, 168 };

/* ⭐⭐ NETWORK SETTINGS (Options > Network), 5 rows. Established 2026-09-10
 * from the game's OWN caption at every position, and corroborated a second way
 * by the VALUE COUNT of each row:
 *
 *   pos 1  idx 202  MENU_OP_MIC          "Adjust your microphone settings."   2 values
 *   pos 2  idx 206  MENU_OP_CHAT_VOLUME  "Adjust the volume of voice chat."  11 values
 *   pos 3  idx 208  MENU_OP_FRAME_DELAY  "Set the multiplayer input delay..." 11 values
 *   pos 4  idx 211  CONNECT_STR          "Select a connection strength."      5 values
 *   pos 5  idx 168  MENU_OP_DEFAULT      "Reset to default settings."         none
 *
 * ⭐ The second source is what makes this more than a caption reading: the row
 * selector's own index and the derived value-slot index agreed on EVERY move
 * in the run (values_off 0x35c, slot = base + 4*row). Microphone having 2
 * values and Connection Strength having 5 is exactly what those labels
 * predict, and Default having no slot at all is what an ACTION row should do.
 *
 * ⛔ Not a contiguous run -- Default sits at 168, ~40 labels before the rest. */
static const int g_network_settings_rows[] = { 202, 206, 208, 211, 168 };

/* Network Settings VALUE labels, keyed by the LIVE count.
 *
 * ⛔ Only the two rows whose values are a real GMD run are listed:
 *   row 0 Microphone         -> MENU_OP_DISABLE 203 / MENU_OP_ENABLE 204
 *   row 3 Connection Strength -> CONNECT_STR_00..04 212..216 (Any/2+/3+/4+/5)
 * Rows 1-2 (Voice Chat Volume, Input Delay) show ELEVEN values each: those are
 * generated numbers, not labels, and there is no run to point at. They name
 * nothing here and are spoken by the live-caption quote path instead, which is
 * the correct division -- a quote of what the game drew, not a claim about a
 * label that does not exist. */
static const mc_value_set g_network_values[] = {
    { 0, 2, 203 },   /* Microphone: Off / On                    -- MEASURED */
    { 3, 5, 212 },   /* Connection Strength: Any/2+/3+/4+/5      -- MEASURED */
};

/* Sound Settings VALUE labels.
 *
 * ⛔ Only row 4 (Challenger Jingle) has a nameable list: MENU_OP_HERE_SE_00
 * "Original" / _01 "Remix" at 195-196, and the run measured exactly 2 values
 * there. Rows 0-3 are the four volume sliders -- 10 generated steps each, no
 * labels -- and row 5 (Default) has no value slot at all, which the run
 * confirms by never producing one for it. */
/* ⭐⭐ OPTIONS > SYSTEM, 6 rows. Established 2026-09-10 from the game's OWN
 * caption at EVERY position -- the run that finally did a slow pass:
 *
 *   pos 1  idx 180  MENU_OP_LANGUAGE   "Choose the display language..."   14 values
 *   pos 2  idx 547  MENU_OP_MENU_BTN   "Change the button used to open..." 6 values
 *   pos 3  idx 549  MENU_OP_START_BTN  "Change the start button..."        6 values
 *   pos 4  idx 551  MENU_OP_COIN_BTN   "Change the coin button..."         6 values
 *   pos 5  idx 182  DATA_UPLOAD        "Change data sharing settings."     2 values
 *   pos 6  idx 168  MENU_OP_DEFAULT    "Reset to default settings."        none
 *
 * ⭐ Corroborated a second way on every move: the row selector's index and
 * (cursor - screen - 0x338)/4 agreed, and row 0 having FOURTEEN values matches
 * LANG_NAME_* being exactly 14 labels. Default having no value slot is what an
 * ACTION row should do.
 *
 * ⛔ Scattered across 168..551 -- no base can address this screen, and
 * name_screens.py correctly refused to propose one.
 *
 * ⚠ THE EARLIER RUN GOT ONLY 1 OF 6 ROWS, and nothing about the code changed
 * in between. It was walked at ~300 ms per row and the game's caption does not
 * arrive before the next movement; this run paused on each row. A screen
 * walked quickly yields position-only lines and looks like a mod bug. */
static const int g_system_settings_rows[] = { 180, 547, 549, 551, 182, 168 };

/* System VALUE labels.
 *
 * ⛔ Only the two rows with a real GMD run:
 *   row 0 Language  -> LANG_NAME_* 0..13, a contiguous 14 starting at index 0
 *                      (Japanese, English, French ... Arabic)
 *   row 4 Data Collection -> DATA_UPLOAD_00 "Decline" / _01 "Agree" at 183-184
 * Rows 1-3 are the button bindings (6-7 values each): those are controller
 * glyphs, not label runs, so they name nothing here and the live-caption quote
 * path speaks them. Row 5 (Default) has no value slot at all. */
static const mc_value_set g_system_values[] = {
    { 0, 14, 0   },   /* Language: Japanese .. Arabic        -- MEASURED */
    { 4, 2,  183 },   /* Data Collection: Decline / Agree    -- MEASURED */
};

static const mc_value_set g_sound_values[] = {
    { 4, 2, 195 },   /* Challenger Jingle: Original / Remix      -- MEASURED */
};

/* ⛔⛔ KILLED 2026-09-10: g_game_options_candidate = { 225, 226, 574 }.
 *
 * It proposed that caller 0x1400b2729 -- the 3-item strip after choosing a
 * game -- was "Game Settings / Controller Settings / Display & Sound
 * Settings", reasoned from the layout name `11_option/option_2`. Three labels,
 * matching the live count, non-contiguous, all very plausible.
 *
 * ⭐ THE LIVE TEXT SAYS ARCADE / VERSUS / TRAINING. That caller now ships
 * with g_mode_select_rows instead, confirmed at positions 1 and 3 by the
 * game's own `TEXT SET` lines.
 *
 * ⭐⭐ Its own comment named the flaw exactly -- "the layout name proves the
 * screen is an options screen, not which labels it draws" -- and I still spent
 * days believing it and nearly wired it up. A hypothesis that fits the COUNT
 * and the CATEGORY can still be about a different screen entirely. Deleted
 * rather than left commented out: a plausible array sitting in the file is an
 * invitation, and tools/test_screen_maps.py validated it by regex even while
 * commented, which made it look endorsed. */

/* PC Settings, row 1 (Display): Windowed / Borderless Windowed / Fullscreen.
 * ⚠ Only the row MEASURED in a run is listed. Resolution (row 0) has a
 * variable value list that nobody has observed, so it is absent and names
 * nothing rather than guessing at a count. */
static const mc_value_set g_pcset_values[] = {
    { 1, 3, 220 },   /* Display: Windowed / Borderless / Fullscreen -- MEASURED */
};

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

    /* Select Mode. ⚠ nlabels 3 vs a live count of 6 means menu_speak_build
     * REFUSES the map and speaks position only -- correct until items 3..5 are
     * established. See the comment on g_mode_select_rows. */
    { 0x14009ba21ULL, -1, 3, "Select Mode", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_mode_select_rows, 3, NULL, 0 },

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
    { 0x1400b2729ULL, -1, 3, "Select Mode", MC_VERIFIED_LIVE_TEXT,
      MC_LIST, 0, g_mode_select_rows, 3, NULL, 0 },

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
    { 0x14009ba21ULL, -1, 6, "Online Play", MC_VERIFIED_LIVE_TEXT,
      MC_LIST, 0, g_online_play_rows, 6, NULL, 0 },

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
    { 0x1400a1715ULL, -1, 4, "PC Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_pc_settings_rows, 4, NULL, 0 },

    /* ⭐ Sound Settings -- 6 rows, every one named by its own live caption,
     * and the row ORDER corroborated by the value-slot arithmetic: the row
     * selector's index and (cursor - screen - 0x358)/4 agreed on every move.
     *
     * ⚠ Same ⚠ as PC Settings above: derived from a run, not yet heard back. */
    { 0x1400a2729ULL, -1, 6, "Sound Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_sound_settings_rows, 6, NULL, 0 },

    /* ⭐ Sound Settings -- the VALUE half.
     *
     * ⚠ values_off is 0x358, NOT the 0x338 that PC Settings and Select Game
     * use. Read from this screen's own offsets in the log (row 0 -> +0x358,
     * row 4 -> +0x368), which is exactly why the header insists this be read
     * per screen and never copied. */
    { 0x1400a2771ULL, -1, 0, "Sound Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x358, NULL, 0, g_sound_values,
      (int)(sizeof(g_sound_values) / sizeof(g_sound_values[0])) },

    /* ⭐⭐ NETWORK SETTINGS -- the ROW half. See g_network_settings_rows. */
    { 0x14009f48eULL, -1, 5, "Network Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_network_settings_rows, 5, NULL, 0 },

    /* ⭐⭐ NETWORK SETTINGS -- the VALUE half.
     *
     * ⚠ values_off 0x35c, and note this screen's ROW cursor is at +0x334
     * rather than the +0x330 the others use -- two more reasons never to
     * assume one settings screen's offsets apply to another. */
    { 0x14009f4c1ULL, -1, 0, "Network Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x35c, NULL, 0, g_network_values,
      (int)(sizeof(g_network_values) / sizeof(g_network_values[0])) },

    /* ⭐⭐ OPTIONS > SYSTEM -- the ROW half. See g_system_settings_rows.
     *
     * ⛔ I called this screen "Controls" in earlier notes. It is Options >
     * SYSTEM, and the log settles it in two lines: the mod spoke
     * "System, 1 of 5" on the Options list, the user confirmed, and this
     * caller appeared. ⭐ The screen a caller belongs to is proved by what the
     * user SELECTED to reach it, and that is in every log for free -- I named
     * it from its CONTENTS (rows that look like button bindings) instead, and
     * was wrong. */
    { 0x1400a33b8ULL, -1, 6, "System Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_system_settings_rows, 6, NULL, 0 },

    /* ⭐⭐ OPTIONS > SYSTEM -- the VALUE half.
     *
     * ⚠ FOUR callers drive this screen's values, one per row group
     * (0x1400a3431 row 0, 0x1400a3538 rows 1 and 3, 0x1400a3578 row 2,
     * 0x1400a34a8 row 4). They share values_off 0x338, so each gets its own
     * row here and menu_speak_derive_row sorts out which row it is. All four
     * carry the same `name` so the sibling link finds the row map above. */
    { 0x1400a3431ULL, -1, 0, "System Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x338, NULL, 0, g_system_values,
      (int)(sizeof(g_system_values) / sizeof(g_system_values[0])) },
    { 0x1400a3538ULL, -1, 0, "System Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x338, NULL, 0, g_system_values,
      (int)(sizeof(g_system_values) / sizeof(g_system_values[0])) },
    { 0x1400a3578ULL, -1, 0, "System Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x338, NULL, 0, g_system_values,
      (int)(sizeof(g_system_values) / sizeof(g_system_values[0])) },
    { 0x1400a34a8ULL, -1, 0, "System Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x338, NULL, 0, g_system_values,
      (int)(sizeof(g_system_values) / sizeof(g_system_values[0])) },

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
    { 0x1400a1757ULL, -1, 0, "PC Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x338, NULL, 0, g_pcset_values,
      (int)(sizeof(g_pcset_values) / sizeof(g_pcset_values[0])) },

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
    { 0x140099b23ULL, 992, 2, "Museum", MC_VERIFIED_LIVE_TEXT, MC_LIST, 0,
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
    const mc_screen *caller_only = NULL;

    for (int i = 0; i < NSCREENS; i++) {
        if (g_screens[i].caller != caller)
            continue;
        if (g_screens[i].count > 0 && g_screens[i].count == item_count)
            return &g_screens[i];          /* exact: caller AND count agree */
        if (caller_only == NULL)
            caller_only = &g_screens[i];   /* fall back to the first match */
    }
    return caller_only;
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

    /* ⭐ Count-aware: one caller can serve several screens (0x14009ba21 is both
     * "Select Mode" at 3 items and the Online Play menu at 6). See
     * menu_speak_lookup_n(). */
    const mc_screen *s = menu_speak_lookup_n((ULONGLONG)(ULONG_PTR)m->caller,
                                             out->total);
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
    {
        char cap[GT_CAPTION_MAX];
        if (gt_take_caption(cap, (int)sizeof(cap))) {
            prompt_id id = prompt_identify(cap);
            if (id.result == PROMPT_ID_FOUND) {
                char spoken[GM_OUT_MAX];
                gm_speak(cap, spoken, sizeof(spoken));
                if (spoken[0] && !gm_has_residue(spoken)) {
                    size_t used = strlen(line);
                    _snprintf(line + used, sizeof(line) - used - 1,
                              ". %s", spoken);
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
                char spoken[GM_OUT_MAX];
                gm_speak(cap, spoken, sizeof(spoken));
                if (spoken[0] && !gm_has_residue(spoken)) {
                    size_t used = strlen(line);
                    _snprintf(line + used, sizeof(line) - used - 1,
                              ", %s", spoken);
                    line[sizeof(line) - 1] = '\0';
                }
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
