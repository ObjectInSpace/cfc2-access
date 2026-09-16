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
#include "move_list.h"    /* the in-game move list's own indirection */

#include <stdio.h>
#include <stdlib.h>       /* malloc/free: menu_speak_line sizes to its line */
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

/* ⛔ NOT NAMEABLE FROM THE GMD -- the Museum media-chooser strips (0x14009a482
 * and 0x14009af21, 9 items each, cursor +0x33c, count from +0x340).
 *
 * ⚠ CORRECTED 2026-09-10: this note used to call BOTH of these "the Gallery
 * lists". The 13:43 run settles 0x14009af21 -- the spoken trail is
 * "Museum, 3 of 6" -> "Music, 2 of 2" -> this strip, so it is the chooser under
 * MUSIC, and confirming an entry opens that game's track list (0x1400b2ebb).
 * 0x14009a482 is presumably Gallery's equivalent, but NOTHING has entered
 * Gallery, so that is an expectation and not a finding. The two share a shape
 * and a confirm handler; they are not the same screen.
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

/* ⭐⭐ THE LOBBY MENU (Online Play > Custom Match), caller 0x140083e09, 3 items.
 * Established 2026-09-10 from the 17:32 run -- EVERY position named by the
 * game's own caption on that row, 7 sightings, no gaps:
 *
 *   pos 1  'Select game rules and create your own lobby.' -> CREATE_LOBBY   78
 *   pos 2  'Select game rules and join another player's.' -> JOIN_LOBBY     79
 *   pos 3  "Search for another player's lobby by ID."     -> SEARCH_LOBBY_ID 80
 *
 * ⭐ A contiguous run, so base 78 would also reach it -- but the map is what
 * the evidence actually supports, and a map cannot drift onto a neighbouring
 * label if the run is ever reordered. Same standard as every other row here.
 *
 * ⚠ This is the 78-80 "lobby" cluster predicted in docs/naming-screens-
 * generally.md. ⭐ The prediction is NOT why it ships -- each position was
 * independently confirmed by its own caption; the cluster merely agrees. */
static const int g_lobby_rows[] = { 78, 79, 80 };

/* ⭐⭐ JOIN LOBBY'S SEARCH FILTERS (0x14008e799 rows / 0x14008e813 values),
 * 6 rows. Established 2026-09-10 (19:45 run) -- FIVE of the six named by the
 * game's own caption on that row, each resolving uniquely:
 *
 *   pos 1  'Choose the game you want to play.'          -> GAME_TITLE     51
 *   pos 2  ⛔ NO CAPTION EVER ARRIVED                    -> UNKNOWN
 *   pos 3  'Turn cross-region matchmaking on or off.'   -> LOBBY_AREA     84
 *   pos 4  'Choose a lobby comment.'                    -> LOBBY_TITLE    93
 *   pos 5  'Turn event mode on or off.'                 -> LOBBY_EVENT   106
 *   pos 6  'Turn one-button special moves on or off.'   -> LOBBY_SPMOVE   88
 *
 * ⚠ WILDLY non-contiguous (51, 84, 93, 106, 88) -- no base could ever reach
 * these, which is exactly why name_screens.py reported SIGHTINGS DISAGREE. That
 * verdict is the tool working, not a failure.
 *
 * ⛔ Position 2 is MC_LABEL_UNKNOWN and stays that way. It has 3 values and
 * produced no caption across the whole run. The LOBBY_* family contains
 * plausible fillers -- Number of Players, Victory Condition, Game Start
 * Privilege -- and picking one because it "fits a 3-value row" is precisely the
 * reasoning that mislabelled two screens here. It speaks its position. */
/* ⭐⭐ ROW 2 RESOLVED from the renderer's own table (0x141206e30), read via the
 * `lea r12, [rip+0x1177f11]` at 0x14008ef18. Per row it holds the FIRST VALUE
 * index: 14, 53, 85, 94, 107, 89.
 *
 * ⭐ FOUR rows validate it against labels captions already established --
 * 85-1 = LOBBY_AREA · 94-1 = LOBBY_TITLE · 107-1 = LOBBY_EVENT ·
 * 89-1 = LOBBY_SPMOVE. Row 1 is the game-names case (values start at
 * GAME_NAME_ANY, label GAME_TITLE), identical to Lobby Settings' row 1.
 *
 * So row 2 -> 53 -> GAME_VERSION (52), "Game Version".
 *
 * ⛔⛔ AND I HAD REJECTED THIS ONCE ON BAD EVIDENCE. Pairing row 2's value
 * movements to live text gave "On" three times, which is not a Game Version
 * value, so I recorded the inference as refuted. Tracing the OBJECT shows it
 * ever held "On", "Off", "Either" -- that is LOBBY_SPMOVE's family (89-91),
 * i.e. ROW 6's value object, which is also a 3-value row. The pairing had
 * attributed row 6's text to row 2.
 *
 * ⭐ The lesson: a same-timestamp pairing is still only as good as the OBJECT
 * identity behind it. When a value looks wrong for the row, check which object
 * set it before concluding the row is wrong. */
static const int g_lobby_filter_rows[] = {
    51, 52, 84, 93, 106, 88
};

/* ⭐⭐⭐ THE IN-GAME PAUSE MENU (0x1400a749d), 8 rows. The first screen in this
 * project that sits BEHIND the emulated ROM -- and the shell's cursor hooks
 * drive it normally, which was the open question the run answered.
 *
 * Every row named by its OWN caption, 20 sightings:
 *   1 'Return to the game.'                        -> CONTINUE     250
 *   2 'Displays the marquee card.'                 -> INST_CARD   1070
 *   3 'View the move list.'                        -> COMMAND_LIST 253
 *   4 "Set the game's controls."                   -> PAD_SETTING  226
 *   5 'Change various display and sound settings.' -> DISPLAY_SOUND_SETTINGS 574
 *   6 'Load your quick save data.'                 -> LOAD         254
 *   7 'Save your current game to the quick save slot.' -> SAVE     255
 *   8 'Exit the game.'                             -> QUIT         256
 *
 * ⛔ THE PREDICTED CLUSTER WAS WRONG, and usefully so. docs/naming-screens-
 * generally.md predicted "250-256, 7 items: Resume, Restart, How to Play, Move
 * List, Load Quick Save, Quick Save, Quit" -- a clean contiguous run, which is
 * exactly what made it convincing. The real screen has EIGHT rows, RESTART and
 * HOW_TO_PLAY are not on it at all, and two of its labels (INST_CARD 1070,
 * DISPLAY_SOUND_SETTINGS 574) sit hundreds of indices away. No base could have
 * reached it; only the captions could.
 *
 * ⚠ Row 4 is PAD_SETTING (226) and not PAD_SETTING_XB (227): both read
 * "Controller Settings" and the caption HELP_OP_PAD is shared by three platform
 * variants, so the caption alone cannot separate them. 226 is the non-suffixed
 * base label, the same convention every other row here follows.
 * ⚠ Row 8 is QUIT (256), not EXIT (42): both read "Quit", and 256 sits with the
 * rest of this screen's family. */
/* ⭐⭐⭐ THE TRAINING-MODE PAUSE MENU (0x1400a749d at SEVEN items) -- the SAME
 * caller as the arcade pause menu, which has eight.
 *
 * ⛔⛔ USER-REPORTED: "still a lot of missing text in the pause menu". The
 * 8-row map was the only entry for this caller, so on a 7-item screen the
 * count check rejected it and EVERY row fell back to a bare position. Nothing
 * was wrong with the map; it belonged to the other screen.
 *
 * ⭐ NAMED BY ITS OWN CAPTIONS, one per position, all seven captured in the
 * 15:17 run with no gaps:
 *     1 "Return to training mode."              -> Resume
 *     2 "Displays the marquee card."            -> Marquee Card
 *     3 "View the move list."                   -> Move List
 *     4 "Set the game's controls."              -> Controller Settings
 *     5 "Change various display and sound settings." -> Display & Sound Settings
 *     6 "Return to the character select screen." -> Change Character
 *     7 "Exit training mode."                   -> Quit
 *
 * ⭐ It is the arcade menu with Load Quick Save and Quick Save replaced by
 * Change Character -- which is exactly what training mode offers instead, and
 * is a check on the reading rather than a coincidence.
 *
 * ⚠ CONTINUE (250) and QUIT (256) are shared with the arcade menu even though
 * their CAPTIONS differ ("Return to training mode" vs "Return to the game"):
 * the game reuses the item labels and varies only the help text. */
/* ⚠⚠ NEITHER MAP IS WIRED TO A SCREEN ANY MORE, deliberately. Training mode
 * also draws EIGHT rows (…Training Menu, Change Character, Quit), so the count
 * could not tell it from the arcade menu and the 8-row map spoke "Load Quick
 * Save" where "Training Menu" belongs -- user-reported. The pause menu is now
 * one MC_ROW_COLUMN entry that quotes the live column; see its g_screens row.
 *
 * ⭐ They are KEPT as the offline EXPECTATION that tools/test_screen_maps.py
 * validates against the real corpus, which is what proves these indices are the
 * strings the screen actually draws. ⚠ `used` keeps -Wunused quiet without
 * deleting the evidence -- and deleting them would not be neutral: that script
 * scrapes by regex and does not skip comments, so a commented-out array would
 * still be reported as `ok`. */
__attribute__((used))
static const int g_training_pause_rows[] = {
    250, 1070, 253, 226, 574, 756, 256
};

__attribute__((used))
static const int g_pause_menu_rows[] = {
    250, 1070, 253, 226, 574, 254, 255, 256
};

/* ⭐⭐⭐ TRAINING OPTIONS (0x1400a41ef rows + 0x1400a4241 values), 6 rows --
 * reached from the TRAINING pause menu's "Training Menu" row. This is the
 * settings screen the user was looking for; there is no separate game-settings
 * menu, which was the premise that sent two runs looking for one.
 *
 * ⭐⭐ ESTABLISHED BY COORDINATES, not by the caption channel, which is
 * STRUCTURALLY BLIND here: 55 movements at ~600ms-1s dwell produced ZERO
 * captions for this screen while 30 prose captions flowed for other objects in
 * the same window. Same class as the Music chooser and the Lobby ID field.
 *
 * The join (cfc2_layout.on, runs 2026-09-11 16:26 and 16:35):
 *
 *   X=960  "Dummy Behavior"  Y=265   <- the screen HEADING, a DIFFERENT column
 *   X=320  the six rows, 80.0 px apart:
 *
 *     row 0  Y=326  Action         DUMMY_ACTION   683
 *     row 1  Y=406  Block          DUMMY_GUARD    694
 *     row 2  Y=486  Throw Escape   DUMMY_NNUKE    700
 *     row 3  Y=566  Recovery       DUMMY_OKI      705
 *     row 4  Y=646  Counter Hit    COUNTER_HIT   2960
 *     row 5  Y=726  Default        MENU_OP_DEFAULT 168
 *
 * ⛔⛔ THE INDICES ABOVE WERE WRONG ON FIRST WRITE, and test_screen_maps.py
 * caught it. I derived them from enumerate() over data/text_english.json --
 * a DICT INSERTION POSITION, which is not the GMD index the mod looks up. The
 * map resolved to fighting-move names ("Psychic Throw", "Crystal Shoot") and
 * would have spoken those where training settings belong. The index must come
 * from the parsed menu_eng table (tools/name_screens.load_corpus), whose order
 * the control confirms: 250 -> CONTINUE -> "Resume".
 *
 * ⭐⭐ THE X GROUPING IS LOAD-BEARING. The instance set holds SEVEN labels for
 * a SIX-row screen, and insertion order cannot say which is not a row -- it has
 * flipped between runs elsewhere on this project. "Dummy Behavior" sits in the
 * heading column (the same X=960 that carries "Ken Moves" and "P1 Pause Menu")
 * and ABOVE the first row, and it is the only member of the family with no
 * HELP_ caption. Three independent signals, one conclusion.
 *
 * ⭐⭐ AND THE CONTROL THAT VALIDATES THE METHOD: sorting the training pause
 * menu by +0x044 reproduces its already-established order 8 of 8, zero
 * contradictions. The answer was known before the coordinates were read.
 *
 * ⚠ Rows 2 and 4 are separated by POSITION ALONE. Both have 3 values, and
 * DUMMY_UKEMI and COUNTER_HIT both read "Off / On / Random" -- identical
 * strings. Neither count nor text can tell them apart; only Y-order can. */
static const int g_training_option_rows[] = {
    683, 694, 700, 705, 2960, 168
};

/* ⭐⭐⭐ THE OTHER TWO TRAINING OPTIONS TABS -- the screens the user asked for.
 *
 * Training Options is THREE screens on ONE caller (0x1400a41ef) and ONE screen
 * object. The tab is switched with LB/RB and the rows change underneath:
 *
 *   Dummy Behavior   6 rows   y=326..726   (above, already shipped)
 *   Dummy Settings   5 rows   y=366..686
 *   Player Settings  5 rows   y=366..686
 *
 * ⛔⛔ THE TWO NEW TABS HAVE THE SAME ROW COUNT, so menu_speak_lookup_n()'s
 * (caller, count) key CANNOT separate them -- one would shadow the other and
 * speak its labels on the wrong screen. That is why mc_screen gained
 * heading_index and why these two rows carry one. Shipping them on count alone
 * was available and would have been wrong.
 *
 * ⭐ The heading is readable and UNAMBIGUOUS: across the dump blocks of the
 * 01:2x run, EXACTLY ONE heading object is resident at a time for this screen
 * object (Dummy Behavior at y=265, the other two at y=305), so
 * gt_widget_of(screen, 960, -1) returns the live tab name with no contradiction.
 *
 * ⭐⭐ ESTABLISHED FROM THE GAME'S OWN LIVE TEXT, grouped by the X column and
 * ordered by Y -- the method the Dummy Behavior tab above already validated
 * against a known-good control:
 *
 *   DUMMY SETTINGS            PLAYER SETTINGS
 *     y=366 Health Bar    714   y=366 Attack Data    724
 *     y=446 Groove Gauge 2925   y=446 Input Display  727
 *     y=526 Guard Crush  2955   y=526 Hit Boxes     1064
 *     y=606 Stun          720   y=606 Network Delay  729
 *     y=686 Default       168   y=686 Default        168
 *
 * ⚠ Both maps are NON-CONTIGUOUS (714, 2925, 2955, 720, 168), so no base_index
 * could ever have named them -- the same shape as Online Play.
 *
 * ⚠ The indices come from the PARSED menu_eng table, not from enumerate() over
 * data/text_english.json. That dict-insertion mistake shipped once on the tab
 * above and resolved to fighting-move names; test_screen_maps.py caught it.
 *
 * ▶ NOT SHIPPED, and deliberately: the VALUES for these two tabs.
 *
 * The value cursors form a clean 8-slot block per tab (values_off 0x3F0, stride
 * 0x20), rows 0-3 used and row 4 "Default" being a button with no value:
 *
 *   block 0  slots  0-7   totals 8, 13        <- Dummy Behavior (shipped)
 *   block 1  slots  8-11  totals 3, 3, 3, 2
 *   block 2  slots 16-19  totals 3, 3, 3, 2
 *   block 3  slots 24-27  totals 2, 2, 2, 11
 *
 * ⛔⛔ THERE ARE FOUR BLOCKS AND ONLY THREE TABS, and the heading attribution in
 * the log LAGS the movement by one (slot 16 was recorded under "Dummy Settings"
 * while 24-27 came under "Player Settings"), so which block belongs to which tab
 * is NOT established. Blocks 1 and 2 even share the same shape (3,3,3,2), so a
 * wrong assignment is undetectable by count.
 *
 * Naming a value from the wrong block would say "Health Bar, Off" where "On"
 * belongs -- a confidently wrong setting, which is the one outcome worse than a
 * number. The rows above are independently evidenced by X-grouping and Y-order;
 * the values need a run that moves the VALUE axis on a known tab with the
 * heading read at the same moment. Until then these rows carry no value set and
 * the live-caption quote path speaks what the game drew.
 *
 * ⚠⚠ THESE TWO MAPS ARE NOT WIRED TO A SCREEN, deliberately. Nothing reads them
 * at runtime -- MC_ROW_COLUMN speaks the label the game drew instead. They are
 * kept as the OFFLINE EXPECTATION that tools/test_screen_maps.py validates
 * against the real corpus, which is what proves the indices above are the
 * strings actually on screen. Wiring them would require naming the tab, and the
 * tab cannot be named (see the g_screens entry).
 *
 * ⚠ `used` to keep -Wunused-variable quiet without deleting the evidence. */
__attribute__((used))
static const int g_dummy_settings_rows[] = {
    714, 2925, 2955, 720, 168
};

__attribute__((used))
static const int g_player_settings_rows[] = {
    724, 727, 1064, 729, 168
};

/* ⭐⭐ TRAINING OPTIONS values. values_off = 0x3F0, re-derived on a FRESH
 * screen object in the CVS1 run (cursors +0x490/494/498/49C/4A0 -> rows 0-4),
 * so it is not a CVS2-specific constant.
 *
 * ⭐⭐ THE SCREEN IS PER-GAME, and this is measured on TWO games rather than
 * assumed from one:
 *
 *                    CVS1            CVS2
 *   row 0 Action      8 values        8 values   DUMMY_ACTION  (same)
 *   row 1 Block       4 values       13 values   DUMMY_GUARD / CVS2_GUARD
 *   row 2 Throw Esc.  3 values        3 values   -- no variant, see below
 *   row 3 Recovery    3 values        6 values   CVS1_OKI / CVS2_OKI
 *   row 4 Counter Hit 3 values        3 values   -- no variant, see below
 *   row 5 Default     no value axis
 *
 * The ROW LABELS are identical on both games -- same X column, same Y values,
 * same 80px spacing -- so one row map serves both and only the value lists
 * differ. That is exactly what keying on the LIVE count is for.
 *
 * ⛔ ROWS 2 AND 4 GET NO VARIANT ON EITHER GAME. DUMMY_UKEMI_00 (701) and
 * COUNTER_HIT_00 (2962) are both "Off / On / Random" -- three identical strings.
 * A corpus check cannot confirm which family a 3-value list came from, so
 * shipping either would be a guess dressed as a lookup. Same rule that keeps
 * every On/Off row here unnamed. They speak the row name and position, which
 * is true. */
static const mc_value_set g_training_option_values[] = {
    { 0, 8,   684 },  /* Action: Stand..Spectate        -- MEASURED both games */
    { 1, 4,   695 },  /* Block (CVS1): None/Auto/All/Random      -- MEASURED */
    { 1, 13, 2927 },  /* Block (CVS2): CVS2_GUARD_00..12         -- MEASURED */
    { 3, 3,  4077 },  /* Recovery (CVS1): CVS1_OKI_00..02        -- MEASURED */
    { 3, 6,  2940 },  /* Recovery (CVS2): CVS2_OKI_00..05        -- MEASURED */

    /* ⭐⭐⭐ THE OTHER TABS' VALUES. `row` here is the slot index derived from
     * values_off, and the slots are ABSOLUTE across the whole screen object:
     * each tab owns a block of EIGHT, so slot = tab_base + row.
     *
     * ⭐ MEASURED LIVE, not inferred. In the 17:02 run the row axis sat on
     * "Guard Crush, 3 of 5" and the next value movement used slot 10; "Groove
     * Gauge, 2 of 5" gave slot 9. slot - (row-1) == 8 on every pairing, six for
     * six. That is the arithmetic below.
     *
     * ⚠ THE 4-VALUE HEALTH BAR IS NON-CONTIGUOUS IN THE GMD: VITAL_GAUGE_00..02
     * are 715-717 but _03 ("Fixed 100%") is at 4136 -- appended long after its
     * siblings. A {first, count} run CANNOT express it, so Health Bar gets no
     * entry here and the live-caption quote path speaks it. Shipping
     * {8, 4, 715} would have named the fourth value "Super Combo Gauge" (718).
     *
     * ⚠ Guard Crush's values sit at 2957 -- nowhere near its 2955 label -- and
     * Hit Boxes reads On/Off (1066) where Attack Data reads Off/On (725). Both
     * come from the table, never from the ordering they "should" have. */

    /* --- tab at row-cursor +0x354 / +0x358 (Dummy Settings), base 8 --------- */
    /* slot 8  = Health Bar    -- NO ENTRY, see above (non-contiguous) */
    /* slot 9  = Groove Gauge  -- per-game, like Block/Recovery above; the live
     *                            quote path speaks it until a run measures it */
    { 10, 3, 2957 },  /* Guard Crush: GUARD_CRASH_00..02  Normal/Minimal/Off  */
    { 11, 2,  721 },  /* Stun:        STUN_GAUGE_00..01   Normal/None         */

    /* --- tab at row-cursor +0x35c (Player Settings), base 24 --------------- */
    { 24, 2,  725 },  /* Attack Data:   TR_OFF_ON_00..01  Off/On              */
    { 25, 2,  725 },  /* Input Display: TR_OFF_ON_00..01  Off/On              */
    { 26, 2, 1066 },  /* Hit Boxes:     TR_ON_OFF_00..01  On/Off  ⚠ REVERSED  */
    /* slot 27 = Network Delay -- 11 GENERATED levels ("the level of network
     * delay to simulate"), not labels. It is a LEVEL row; see `levels` below. */
};

/* ⭐⭐⭐ IN-GAME DISPLAY & SOUND SETTINGS (0x14009c755), 7 rows -- reached from
 * the pause menu. Two captions anchor it (position 1 'Change the background
 * artwork.' -> WALLPAPER 575, position 2 'Change your display filter type.' ->
 * SCREEN_FILTER 580) and the live object list gives the rest in row order.
 *
 * ⭐ THE FAMILY CONFIRMS IT: 575-593 is one in-game block -- WALLPAPER,
 * SCREEN_FILTER, SCREEN_SIZE, SCREEN_ROTATE, then SOUND_BGM_VOLUME and
 * SOUND_SE_VOLUME -- each with its own HELP_ caption at 595-599.
 *
 * ⚠ The two volume rows are AMBIGUOUS by text: "Music Volume" is both
 * MENU_OP_BGM_VOLUME (190, the Options screen) and SOUND_BGM_VOLUME (592).
 * 592/593 are chosen because they sit INSIDE this screen's own family block
 * beside WALLPAPER and SCREEN_*, with HELP_SOUND_* captions -- the 190/191 pair
 * belongs to Options > Music. Text alone could not separate them; the family
 * neighbourhood does.
 *
 * ⚠ INTERNAL_RESO is 4114, far outside the block -- so no base could name this
 * screen, the same shape as the pause menu. */
static const int g_ingame_av_rows[] = {
    575, 580, 583, 4114, 592, 593, 168
};

/* ⭐⭐ THE SAME SCREEN WITHOUT "Internal Resolution" -- it is PER-GAME, and the
 * six-row variant is why the user still heard "1 of 6" here.
 *
 * ⛔ The 7-row entry above is count-gated, so on a game with no
 * internal-resolution option the map disagreed with the live count and the
 * whole screen fell back to a bare position. Correct behaviour from the guard,
 * and a missing row from me.
 *
 * ⭐ MEASURED from the layout dumps, not assumed: the six-row variant holds
 * EXACTLY the seven labels minus Internal Resolution, and the survivors re-space
 * to a clean 80px grid rather than leaving a gap --
 *
 *     7 rows  y = 286 366 446 526 606 686 766   (Internal Resolution at 526)
 *     6 rows  y = 326 406 486 566 646 726
 *
 * so it is a different LAYOUT, not the same layout with one row hidden. A
 * position-based patch on the 7-row map would have named rows 4-6 wrongly.
 *
 * ⚠ 4114 is the dropped index. Note it is the outlier in the family (575-593
 * otherwise), i.e. added later -- consistent with it being the optional one. */
static const int g_ingame_av_rows6[] = {
    575, 580, 583, 592, 593, 168
};

/* ⭐⭐ IN-GAME DISPLAY & SOUND values. values_off = 0x398, derived from the
 * cursor addresses: rows 0-5 sit at +0x398, 39c, 3a0, 3a4, 3a8, 3ac.
 *
 * ⭐ Row 2 (Display Size) is EXACT: 5 live values and SCREEN_SIZE_00..04 is
 * exactly five -- Full, Full (4:3), Original, Original (4:3), Wide.
 *
 * ⛔ Row 0 (Wallpaper) is NOT shipped: the row has FIVE live values but
 * WALLPAPER_00..03 is only four, and index 580 is SCREEN_FILTER -- the next
 * ROW's label, not a fifth wallpaper. A count that disagrees with the family is
 * a count nobody has explained, and indexing past it would speak "Display
 * Filter" as a wallpaper name.
 * ⛔ Row 1 (Display Filter) likewise: 9 live values, SCREEN_FILTER_00..01 is
 * two. */
static const mc_value_set g_ingame_av_values[] = {
    { 2, 5, 584 },   /* Display Size: Full .. Wide -- MEASURED */
};

/* ⭐⭐⭐ IN-GAME KEYBOARD SETTINGS (0x14009ec58), 16 rows -- reached from the
 * pause menu's "Controller Settings".
 *
 * Two captions anchor it: position 1 'Change key bindings.' -> MENU_KBD_TYPE
 * (464) and position 9 'Reset to default settings.' -> MENU_OP_DEFAULT (168).
 * The other fourteen come from the live object list, which holds the whole
 * screen in row order:
 *
 *   Key Bindings · Default · Menu · Start · Coin · Left · Up · Right · Down ·
 *   Light/Medium/Heavy Punch · Light/Medium/Heavy Kick · Type A
 *
 * ⭐ THE CORPUS CORROBORATES THE FAMILY: MENU_KBD_UP/DOWN/LEFT/RIGHT occupy
 * 239-242 and MENU_KBD_SHOT1..6 occupy 243-248, one unbroken block covering
 * exactly the ten movement/attack rows this screen shows.
 *
 * ⚠ But the corpus ORDER is Up, Down, Left, Right while the SCREEN lists Left,
 * Up, Right, Down -- so the map is not the corpus order and a base would place
 * all four wrongly. The screen's order comes from the live object list; the
 * corpus only confirms WHICH ten labels belong here. Two sources, each
 * answering the half it can.
 *
 * ⛔ NOT a contiguous map overall: the row labels are scattered (464, 168, 573,
 * 238, 572, then the 239-248 block, then 234), so no base could name this
 * screen -- the same shape as the pause menu above.
 *
 * ⚠ Six rows have an ambiguous corpus match (Light Punch is both
 * MENU_KBD_SHOT1 and PAD_OPT_6BUTTON_01). The MENU_KBD_* family is chosen
 * because this is the KEYBOARD screen and that family is contiguous here; the
 * PAD_OPT_* family belongs to the controller screen.
 *
 * ⛔⛔ AND ITS SECOND VALUE AXIS (0x14009ed68, 2 items) CANNOT BE JOINED AT
 * ALL -- not for want of a run. Its cursor address is 0x0000000006F5FC80 while
 * the screen object is at 0x2C498050: the pointer is on the STACK, a local
 * variable the game passes to the generic cursor helper, not a field of the
 * screen. `(cursor - screen - values_off)/4` is therefore meaningless for it
 * (it computes as a negative offset, 0x-255383d0), and no value of values_off
 * can rescue it.
 *
 * ⭐ That is a STRUCTURAL fact about this call site, so it is worth writing
 * down rather than re-deriving: a stack-local cursor has no stable identity
 * across calls, and every join this project trusts keys on one that does. If
 * this axis is ever to be named it needs a different anchor entirely -- the
 * caption channel or the instance set -- not a better offset.
 *
 * ⭐⭐ ANSWERED 2026-09-14, and by the instance set exactly as predicted above.
 * IT IS NOT A VALUE AXIS AT ALL -- it is a Yes/No CONFIRMATION DIALOG, which is
 * why its cursor is a stack local unrelated to any row and why the same "2 of 2"
 * appeared on Start, Heavy Kick and Menu alike. Trying to join it to a row was
 * answering the wrong question.
 *
 * Measured in the layout dumps: while the dialog is up, column x=960 holds
 * EXACTLY 'Yes' (y=540) and 'No' (y=600) and the screen heading is GONE; in
 * every other dump that column holds only 'Keyboard Settings'. So MC_ROW_COLUMN
 * at x=960, count-gated to 2, reads it with no map.
 *
 * ⛔ A label MAP was available and refused: SYSTEM_YES/NO (19/20) and
 * SYS_WINDOW_YES/NO (25/26) are both exactly "Yes"/"No" and nothing in the
 * corpus separates them. Picking one would be the shape of guess that has
 * mislabelled two screens on this project. The game draws the words; quote them. */
static const int g_kbd_settings_rows[] = {
    464, 168, 573, 238, 572, 241, 239, 242, 240,
    243, 244, 245, 246, 247, 248, 234
};

/* See MC_TEST_CALLER: a map that deliberately keeps one row UNKNOWN so the
 * sentinel path stays covered now that no shipped screen has a gap. */
static const int g_sentinel_test_rows[] = { 51, MC_LABEL_UNKNOWN, 84 };

/* ⭐⭐ CASUAL MATCH's game/filter rows (Online Play > Casual Match), 4 rows,
 * caller 0x14009890d. Established 2026-09-10 (21:00 run) on THREE independent
 * sources, which is what finally made it shippable:
 *
 *  1. The instance set holds exactly these four labels, and only these:
 *     Game (51) · Game Version (52) · Cross-region Matchmaking (84) ·
 *     One-button Special Moves (88).
 *  2. ⭐ THE VALUE-SLOT ARITHMETIC CONFIRMS THE ORDER, which is the source that
 *     was missing until this run. values_off = 0x344, so
 *     (cursor - screen - 0x344)/4 puts the 8-value list on ROW 0 and the
 *     3-value list on ROW 1 -- i.e. Game has 8 values (the eight games) and
 *     Game Version has 3. Both match the labels above.
 *  3. The screen's own caption is HELP_GAME_TITLE_ON -- "Choose one or more
 *     games to play." -- the ONLINE variant of the Game row's help, which also
 *     explains why row 0 takes eight values: you pick several games for
 *     matchmaking.
 *
 * ⛔ Until source 2 existed this was refused for days as "fits the count and
 * the category" -- the reasoning behind g_game_options_candidate. The labels
 * alone were never enough; nothing tied them to THIS caller rather than to the
 * two 3-item lists sharing the screen object. */
static const int g_casual_match_rows[] = { 51, 52, 84, 88 };

/* ⭐⭐⭐ THE DATA-COLLECTION CONSENT DIALOG (0x140087cda), 3 items, HORIZONTAL.
 * Agree · Decline · Privacy Policy.
 *
 * ⛔⛔ THIS WAS CLOSED FOR DAYS AS "NOT SHIPPABLE", and the note was right at
 * the time: the dialog sets NO item text -- only its button strip
 * ("Confirm / Back") -- so the caption channel can never name it, and
 * SYS_WINDOW_YES2/NO2/PP "fitting the count and the category" is exactly the
 * reasoning that mislabelled two screens here.
 *
 * ⭐ WHAT CHANGED IS THE ENTRY POINT -- the identification this project trusts
 * over every other (see the memory note: identify by what was SELECTED, not by
 * contents). Across every log, ALL 7 sightings are immediately preceded by
 *     spoke "Data Collection, Agree, 2 of 2"
 * with no exceptions. The dialog is reached by confirming System > Data
 * Collection, which is what a consent dialog is, and the user independently
 * reported reaching it exactly that way.
 *
 * So there are now TWO independent facts, which is the standard every other
 * row here meets: the entry point says WHICH dialog, and the corpus run
 * 29/30/31 is the only 3-label run in the SYS_WINDOW family that matches it.
 * Neither alone was enough; that is why this waited.
 *
 * ⚠ USER-REPORTED, and it explains the remaining confusion: "you can't press up
 * or down to go to another row". Correct -- the axis is HORIZONTAL. Every
 * sighting in every log is the horizontal helper, so up/down doing nothing is
 * the GAME's design, not a gap in the mod. All three positions are reached
 * (16/13/12 movements), so the third option is real and not a phantom.
 *
 * ⚠ Its cursor is a stable field at screen+0x340 -- a real object field, not a
 * stack local, so the position is trustworthy. */
static const int g_consent_rows[] = { 29, 30, 31 };

/* ⭐⭐⭐ RANKED MATCH'S ROWS -- the SAME CALLER as Casual Match, three rows.
 *
 * ⛔⛔ USER-REPORTED: "the number of rows is not constant when I arrow up or
 * down ... maybe there's a phantom row". There is no phantom: 0x14009890d
 * serves TWO screens, and the shipped 4-row map was being applied to both.
 *
 * ⭐ IDENTIFIED BY ENTRY POINT, which is the only identification that has held
 * up on this project. Across 17 visits in every log, with ZERO exceptions:
 *     entered from "Casual Match" -> ALWAYS 4 rows
 *     entered from "Ranked Match" -> ALWAYS 3 rows
 * Same screen OBJECT and same cursor field either side of the change, so the
 * game itself reports a different row count -- this is not a mis-read.
 *
 * ⚠⚠ AND WHICH ROW DROPS WAS MEASURED, NOT INFERRED. The obvious reading is
 * that Ranked is "Casual minus its last row", and the value counts appear to
 * agree (Casual 8,3,2,3 vs Ranked 8,3,2). But the instance set shows all FOUR
 * labels live in BOTH modes, so the labels cannot decide it. Pairing each
 * Ranked ROW position with the value count that follows it settles it:
 *     RANKED row 3 -> 2 values (8 sightings)
 * Two values is Cross-region Matchmaking; One-button Special Moves has three.
 * So row 3 IS Cross-region and the row that disappears is the LAST one.
 * ⭐ That is the same conclusion the inference reached, by evidence rather than
 * by plausibility -- and the labels being present in both modes is exactly the
 * kind of detail that would have made the guess look wrong.
 *
 * ⚠ Game Version is PRESENT on Ranked (row 2, three values: JP/EN/Either),
 * which is worth stating because "ranked forces a version" would be a natural
 * assumption and the data refutes it. */
static const int g_ranked_match_rows[] = { 51, 52, 84 };

/* ⭐ Casual Match's value labels. Its four rows are the SAME four settings as
 * Lobby Filters' rows 1-3 and 6, and the value runs are the ones that screen's
 * renderer table establishes.
 *
 * ⭐⭐ THE COUNTS COME FROM THE VALUE CURSOR'S ADDRESS, not from pairing.
 * `(cursor - screen - 0x344)/4` names the row directly, and every movement in
 * every log then resolves to exactly ONE count per row:
 *     row 0: 8 values (87 movements) · row 1: 3 (30)
 *     row 2: 2 values (28)           · row 3: 3 (18)
 *
 * ⛔⛔ I PREVIOUSLY EXCLUDED ROW 1 AS "CONTAMINATED" -- I had it showing live
 * counts of 2, 3 AND 8, and called a count I could not trust no count. That was
 * my ANALYSIS, not the data: I was pairing value movements to the ROW cursor,
 * which lags when the user changes a value after moving. Keyed on the value
 * cursor's own address there is no ambiguity at all. The conservative call was
 * right at the time and the evidence behind it was simply wrong.
 *
 * ⛔ Row 0 still gets no variant: its values are GAME NAMES from the ROM table,
 * not a GMD run. */
static const mc_value_set g_casual_match_values[] = {
    /* ⚠ "Either" here is INFERRED, not sighted. See the note in
     * g_lobby_filter_values -- the game has been seen drawing "Either" only for
     * One-button Special Moves, never for Game Version. */
    { 1, 3, 53 },   /* Game Version: Japanese / English / Either   */
    { 2, 2, 85 },   /* Cross-region Matchmaking: Off / On          */
    { 3, 3, 89 },   /* One-button Special Moves: On / Off / Either */
};

/* ⭐⭐ CREATE LOBBY's VALUE labels. Only the rows whose values were actually
 * SEEN as live text, at the same timestamp as the movement.
 *
 * Row 8 (0-based) = "Game Start Privilege", 2 values: the run shows
 * position 1 = "Challenger" and position 2 = "Host", three times each, and the
 * corpus makes both UNIQUE -- LOBBY_PERM_00 = Challenger (137),
 * LOBBY_PERM_01 = Host (138). ⭐ Two independent sources agreeing on the ORDER,
 * and the row itself is already named LOBBY_PERM, so row and values corroborate
 * each other.
 *
 * ⛔ The other 2-value rows (2, 3, 5, 6, 10) are almost certainly On/Off and get
 * NO variant: "On" occurs 18 times in the corpus and "Off" 22, so
 * prompt_identify can never resolve which pair they are, and guessing a `first`
 * would speak a label from a different setting. The live-quote path already
 * speaks those from the game's own text -- which is the right answer, not a
 * fallback.
 *
 * ⚠ Row 8 (1-based 9) is the ONLY row here whose values are unique strings. */
/* ⭐⭐⭐ NINE OF TEN ROWS' VALUES, from the renderer's own first-value table.
 *
 * ⛔⛔ THE USER REPORTED VALUES "MISSING OR INACCURATE, PARTICULARLY ON THE
 * ONLINE SCREENS", and they were right in a way my metrics could not see:
 * counting named-vs-bare says nothing about whether a NAME is correct or
 * whether a VALUE was spoken at all. Measured on the value axis:
 *     Lobby Settings 74 movements with NO value spoken (of 84)
 *     Casual Match   54 (of 75)
 *     Lobby Filters  20 (of 24)
 * Eight of ten rows here had no variant, so the mod said "2 of 2" while the
 * screen showed "On".
 *
 * ⭐ The table at 0x141205e30 gives each row's FIRST value index directly --
 * the exact field an mc_value_set needs -- and pairing it with each row's
 * MEASURED live count yields a single coherent label family for nine rows.
 * Every previously shipped entry is reproduced (row 8 = 2..9, row 9 =
 * Challenger/Host), which is the check that this is the right table.
 *
 * ⛔ Row 0 is excluded: its values are GAME NAMES, which live in the ROM table,
 * not in a GMD run -- reading 9 labels from index 14 gives "Any, Confirm,
 * Cancel, Select..." from four different families. The mixed-family test is
 * what catches that, and it is applied to every row rather than assumed. */
static const mc_value_set g_create_lobby_values[] = {
    { 1, 2,  53 },   /* Game Version: Japanese / English                     */
    { 2, 2,  85 },   /* Cross-region Matchmaking: Off / On                   */
    { 3, 8,  95 },   /* Comment: "Everyone is welcome!" .. 8 presets         */
    { 4, 2, 107 },   /* Event Mode: On (Quick Match) / Off (Arcade Style)    */
    { 5, 2,  89 },   /* One-button Special Moves: On / Off                   */
    { 6,10, 115 },   /* Victory Condition: First to 1 .. First to 10         */
    { 9, 2, 111 },   /* Passcode: Off / On                                   */
    { 8, 2, 137 },   /* Game Start Privilege: Challenger / Host -- MEASURED */
    /* ⭐⭐ Row 7 (0-based) = "Number of Players", 8 values = LOBBY_NUM_00..07,
     * i.e. "2".."9". THREE sources agree:
     *   - the measured live count for that row is exactly 8;
     *   - value positions 1-4 were seen as "2", "3", "4", "5" at the same
     *     timestamp as the movement;
     *   - the corpus family LOBBY_NUM_00..07 is exactly "2".."9", and the row
     *     label LOBBY_NUM ("Number of Players") is the one the instance set
     *     shows on that screen.
     * ⚠ A lobby taking up to NINE players is not a guess about the game -- it
     * is what the corpus lists, and the first four are confirmed live. */
    { 7, 8, 127 },   /* Number of Players: 2..9 -- MEASURED (1-4 live) */
};

/* ⭐⭐ CREATE LOBBY's settings, 10 rows, caller 0x140082c3c. FIVE positions
 * established, each by its OWN caption, pooled across four runs:
 *
 *   pos  5  'Turn event mode on or off.'                 -> LOBBY_EVENT 106
 *   pos  6  'Turn one-button special moves on or off.'   -> LOBBY_SPMOVE 88
 *   pos  7  'The first player to win the indicated...'   -> LOBBY_ROUND 114
 *   pos  9  'Set which player has the ability to...'     -> LOBBY_PERM  136
 *   pos 10  'Choose a passcode for the lobby.'           -> LOBBY_PASS  110
 *
 * ⛔⛔ THE OTHER SIX STAY UNKNOWN, and this is the screen where that discipline
 * matters most. The instance set hands over eight plausible row labels (Game,
 * Game Version, Cross-region Matchmaking, Comment, One-button Special Moves,
 * Number of Players...) and six slots to drop them into. Every one of those
 * assignments would be a guess, and the run that pinned positions 5/9 DISPROVED
 * the ordering the instance set implied -- "Passcode" was absent from the set
 * entirely while sitting at position 10.
 *
 * ⭐ Shipping 4 of 10 is the whole point of MC_LABEL_UNKNOWN: four rows a blind
 * player would otherwise hear as bare numbers, with the remaining six still
 * saying their position, exactly as before. */
/* ⭐ Position 8 = "Number of Players" (LOBBY_NUM 126), established WITHOUT a
 * caption: its value list is 8 long and its live values are "2", "3", "4", "5"
 * at positions 1-4, which is exactly LOBBY_NUM_00..07 ("2".."9"). The row label
 * and the value family corroborate each other, and the instance set shows
 * "Number of Players" on this screen. */
/* ⭐⭐⭐ ALL TEN ROWS, and the last four came from the DISASSEMBLY, not a run.
 *
 * The row renderer at 0x140083040 does, per row:
 *     mov edx, [rbx + row*4 + 0x1205e30]   <- a table of FIRST-VALUE indices
 *     add edx, [r15 + row*4 + 0x33c]       <- plus that row's value cursor
 *     call 0x14007b7e0                     <- returns the string
 *
 * So `0x141205e30` holds, per row, the GMD index its VALUE LIST starts at:
 *     14, 53, 85, 95, 107, 89, 115, 127, 137, 111
 *
 * ⭐ THE TABLE VALIDATES ITSELF against the six rows captions already
 * established: rows 5-10 are each exactly table[i] - 1 --
 *   107-1 = LOBBY_EVENT · 89-1 = LOBBY_SPMOVE · 115-1 = LOBBY_ROUND
 *   127-1 = LOBBY_NUM   · 137-1 = LOBBY_PERM  · 111-1 = LOBBY_PASS
 * Six independent confirmations that this is the right table, read from the
 * instructions rather than inferred from position.
 *
 * ⚠ BUT `table[i] - 1` IS NOT A UNIVERSAL RULE, and two rows prove it:
 *   - row 4 -> 95 is LOBBY_TITLE_01 ("Everyone is welcome!"); its label is
 *     LOBBY_TITLE (93, "Comment"), because value _00 ("Any") is a FILTER-only
 *     option this screen skips. Confirmed by the same label being Lobby
 *     Filters' row 4, which a caption established.
 *   - row 1 -> 14 is GAME_NAME_ANY, i.e. the value list is GAME NAMES; its
 *     label is GAME_TITLE (51), the same label Lobby Filters' row 1 carries
 *     and which a caption established there.
 * Rows 2 and 3 are clean: 53-1 = GAME_VERSION, 85-1 = LOBBY_AREA.
 *
 * ⛔ So the table gives the VALUE RUN, and the row label is derived from it --
 * checked against the six known rows, never assumed from the arithmetic alone.
 * That is why this ships as a map of verified labels and not as a computation. */
static const int g_create_lobby_rows[] = {
     51,  52,  84,  93, 106, 88, 114, 126, 136, 110
};

/* ⭐⭐ LOBBY FILTERS' value labels, from its renderer's table (0x141206e30)
 * paired with each row's MEASURED live count.
 *
 * ⭐ Row 1 is the "Either" case the user would have heard as silence: with 3
 * live values it is Japanese / English / EITHER, the online-only third option.
 * The 2-value variant on Select Game never applied here, so the row said
 * "Game Version, 2 of 3" and named nothing.
 *
 * ⛔ Row 0 gets no variant -- its values are GAME NAMES from the ROM table, and
 * reading 9 labels from index 14 yields "Any, Confirm, Cancel, Select..." from
 * four unrelated families. The mixed-family check is applied to every row, not
 * assumed away. */
/* ⚠⚠ "EITHER": WHAT IS SIGHTED AND WHAT IS INFERRED. The user asked whether it
 * is an assumption or game data. Both, in different places, and the difference
 * is worth keeping straight:
 *
 * ⭐ SIGHTED for One-button Special Moves. Object 0x2C4DB7D0 in the 19:13 run
 * held exactly "On", "Off", "Either" -- a real 3-value set the game DREW -- and
 * the value cursor's address puts that movement on row 5 of this screen, which
 * is LOBBY_SPMOVE. That is the row {5, 3, 89} covers.
 *
 * ⚠ INFERRED for Game Version. "Either" has NEVER been seen drawn for that row.
 * What supports it: the corpus family is exactly GAME_VERSION_J / _E / _ANY
 * (three members, no others), the row's live count is exactly 3 wherever it is
 * measured, and the renderer's own table gives first = 53. What is missing: a
 * sighting. It is a strong inference from the game's own data, not a guess at a
 * word -- "Either" is Capcom's string, carrying an _ANY suffix -- but it is not
 * the same standard as the row above.
 *
 * ▶ One run that moves Game Version to its third value on an ONLINE screen
 * settles it, and the value audit will show it as an agreement. */
static const mc_value_set g_lobby_filter_values[] = {
    { 1, 3,  53 },   /* Game Version: Japanese / English / Either -- INFERRED */
    { 2, 2,  85 },   /* Cross-region Matchmaking: Off / On        */
    { 3, 9,  94 },   /* Comment: Any + the 8 presets              */
    { 4, 2, 107 },   /* Event Mode: On (Quick Match) / Off        */
    { 5, 3,  89 },   /* One-button Special Moves: On / Off / Either */
};

/* ⛔ (superseded note) NO VALUE VARIANTS FOR THIS SCREEN, deliberately.
 *
 * The per-row value COUNTS are measured from the 19:45 run, via
 * (cursor - screen - 0x33c)/4:  row0=9  row1=3  row2=2  row3=9  row4=2  row5=3.
 * But a variant needs `first` -- the GMD index the value list STARTS at -- and
 * no run has shown the value TEXT for any of these rows.
 *
 * ⚠ A count alone is not a value set. Row 0's nine values are presumably the
 * game list and rows 2/4 are presumably On/Off, and "presumably" is what
 * g_game_options_candidate was made of. The live-quote path already speaks
 * these values from the game's own text, so nothing is lost by waiting. */

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

/* ⛔ g_consent_dialog_rows was REMOVED: the labels were a guess and the
 * screen is back to speaking position. See the note in g_screens. */

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
    /* ⭐ The 3-value ONLINE variant. The header predicted "Either" is
     * online-only and the run showed this row at a live count of 3 while only
     * the 2-value variant existed -- so it said "Game Version, 2 of 3" and
     * named nothing. GAME_VERSION_ANY (55) completes the contiguous run 53-55,
     * and the same 3-value set is now confirmed on Lobby Filters, where its
     * renderer's own table gives first = 53. */
    { 1, 3, 53 },   /* Game Version: Japanese / English / Either */
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
    { 0x14009a482ULL, -1, 0, "Gallery", MC_VERIFIED_LIVE_TEXT,
      MC_GAME_STRIP, 0, NULL, 0, NULL, 0 },

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
    { 0x1400ac059ULL, -1, 8, "Ranked Leaderboard", MC_VERIFIED_LIVE_TEXT,
      MC_GAME_STRIP, 0, NULL, 0, NULL, 0 },

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
    { 0x14009c755ULL, -1, 7, "Display & Sound Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_ingame_av_rows, 7, NULL, 0, 0x30u },

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
    { 0x14009c755ULL, -1, 6, "Display & Sound Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_ingame_av_rows6, 6, NULL, 0, 0x18u },

    /* ⭐⭐ THE KEYBOARD SETTINGS CONFIRMATION DIALOG (Yes / No), 2 items.
     * See the long note at g_kbd_settings_rows: this was recorded for months as
     * an unjoinable VALUE axis, and it is not a value axis -- it is a dialog.
     * Its cursor is a stack local, which is exactly what a dialog's would be.
     *
     * ⭐ Read from column x=960, where the game draws both options and where the
     * screen heading is absent while the dialog is up. No map: SYSTEM_YES/NO and
     * SYS_WINDOW_YES/NO are textually identical and the corpus cannot separate
     * them, so the words come from the screen itself. */
    { .caller = 0x14009ed68ULL, .base_index = -1, .count = 2,
      .name = "Confirm", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_ROW_COLUMN, .coord_x = MC_HEADING_X },

    /* ⭐⭐ The in-game AV screen's VALUE axis. Same screen object as the row
     * site above -- five call sites share 0x2C498050, which is why the axes
     * must be told apart by cursor OFFSET and not by screen.
     * ⭐ Rows 4-5 (Music Volume, Sound Effects Volume) are LEVELS: 11 values
     * each, and the position IS the setting. */
    { 0x14009c7a3ULL, -1, 0, "Display & Sound Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x398, NULL, 0, g_ingame_av_values,
      (int)(sizeof(g_ingame_av_values) / sizeof(g_ingame_av_values[0])),
      0x30u },

    /* ⭐⭐⭐ IN-GAME KEYBOARD SETTINGS, 16 rows. See g_kbd_settings_rows: two
     * captions anchor it and the live object list supplies the rest, with the
     * corpus confirming which ten labels belong. Reached from the pause menu's
     * "Controller Settings". */
    /* ⛔ MC_SETTINGS_ROW, not MC_LIST -- the same defect the in-game AV screen
     * had, found by auditing every screen with a value axis for a reachable
     * row half rather than waiting to hit it. A value movement here would have
     * said "row 3" instead of "Menu". */
    { 0x14009ec58ULL, -1, 16, "Keyboard Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_kbd_settings_rows, 16, NULL, 0, 0 },

    /* ⭐⭐ KEYBOARD SETTINGS' VALUE AXIS (0x14009ed20), 4 items, horizontal.
     * Same screen object (0x2C498050) as the row site above -- another AXIS of
     * a screen already named, the shape established in 0795b7f.
     *
     * Its cursor is screen+0x380, a real field of the screen object, so the
     * row join is the usual arithmetic and not a timing guess.
     *
     * ⚠ NO VALUE SET SHIPS YET. It fires while the cursor sits on row 1
     * ("Key Bindings"), whose caption in the 02:12 run was "Type A" -- but the
     * corpus holds only Type A and Type B (234, 235), which is TWO, and this
     * axis has FOUR items. Those numbers disagree, so naming its values from
     * that pair would be exactly the plausible-but-wrong map this file has
     * shipped before. It speaks its row name and position until a run captures
     * all four value strings. */
    { 0x14009ed20ULL, -1, 4, "Keyboard Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x380, NULL, 0, NULL, 0, 0 },

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
     * ⚠ Five snapshots held a BLANK row in that column. A blank still counts,
     * so the count guard passes and the blank row alone falls back to a bare
     * position -- a degraded row, never a misnamed one.
     *
     * ⚠ g_pause_menu_rows / g_training_pause_rows are KEPT as the offline
     * expectation that tools/test_screen_maps.py validates against the corpus;
     * nothing reads them at runtime. */
    { .caller = 0x1400a749dULL, .base_index = -1, .count = 0,
      .name = "Pause Menu", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_ROW_COLUMN, .coord_x = 362.0f },

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
    { 0x1400a41efULL, -1, 6, "Training Options", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_training_option_rows, 6, NULL, 0, 0 },

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
    { .caller = 0x1400a41efULL, .base_index = -1, .count = 0,
      .name = "Training Options", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_ROW_COLUMN, .coord_x = 320.0f, .pair_x = 835.0f },

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
    { .caller = 0x14009d8dfULL, .base_index = -1, .count = 0,
      .name = "Game Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_ROW_COLUMN, .coord_x = 320.0f, .pair_x = 835.0f },

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
    { .caller = 0x14009d929ULL, .base_index = -1, .count = 0,
      .name = "Game Settings", .how = MC_VERIFIED_LIVE_TEXT,
      .kind = MC_ROW_VALUE_LIVE, .coord_x = 320.0f, .pair_x = 835.0f,
      .levels = (1u << 0), .row_off = 0x35C },

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
    { 0x1400a4241ULL, -1, 0, "Training Options", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x3F0, NULL, 0, g_training_option_values,
      (int)(sizeof(g_training_option_values) /
            sizeof(g_training_option_values[0])), (1u << 27) },

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
    { 0x14008a8efULL, -1, 36, "Enter Lobby ID", MC_VERIFIED_LIVE_TEXT,
      MC_CHAR_PICKER, 0, NULL, 0, NULL, 0 },

    /* ⭐⭐⭐ THE DATA-COLLECTION CONSENT DIALOG. See g_consent_rows: entry point
     * (always reached from "Data Collection, Agree") plus the SYS_WINDOW label
     * run, which is two independent sources where the count alone was one. */
    { 0x140087cdaULL, -1, 3, "Data Collection", MC_VERIFIED_LIVE_TEXT,
      MC_LIST, 0, g_consent_rows, 3, NULL, 0 },

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
    { 0x14008a91bULL, -1, 6, "Lobby ID", MC_VERIFIED_LIVE_TEXT,
      MC_COORD_LIVE, 0, NULL, 0, NULL, 0, 0, 1 },

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
    { 0x140080250ULL, -1, 0, "Move List", MC_VERIFIED_LIVE_TEXT,
      MC_MOVE_LIST, 0, NULL, 0, NULL, 0, 0, 0 },

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
    { 0x14008028eULL, -1, 0, "Move List", MC_VERIFIED_LIVE_TEXT,
      MC_MOVE_ROSTER, 0, NULL, 0, NULL, 0, 0, 0 },

    /* ⚠ TEST-ONLY. Not a real call site -- no game movement can reach
     * 0xDEADCA11 -- so this cannot affect speech. It keeps the
     * MC_LABEL_UNKNOWN path covered; see MC_TEST_CALLER in the header. */
    { MC_TEST_CALLER, -1, 3, "Sentinel Test", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_sentinel_test_rows, 3, NULL, 0 },

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
    { 0x140082c3cULL, -1, 10, "Lobby Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_create_lobby_rows, 10, NULL, 0 },
    { 0x140082d16ULL, -1, 0, "Lobby Settings", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x33c, NULL, 0, g_create_lobby_values,
      (int)(sizeof(g_create_lobby_values) / sizeof(g_create_lobby_values[0])) },

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
    { 0x14009890dULL, -1, 4, "Casual Match", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_casual_match_rows, 4, NULL, 0 },

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
    { 0x14009890dULL, -1, 3, "Ranked Match", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_ranked_match_rows, 3, NULL, 0 },

    /* ⚠ sibling_caller = 0x14009890d: this VALUE axis is shared by Casual Match
     * and Ranked Match, so its single `name` cannot identify which row map
     * applies. The sibling search uses (this caller, live row count) instead --
     * see menu_speak_sibling_row_for(). */
    { 0x140098986ULL, -1, 0, "Casual Match", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x344, NULL, 0, g_casual_match_values,
      (int)(sizeof(g_casual_match_values) / sizeof(g_casual_match_values[0])),
      0, 0, 0x14009890dULL },

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
    { 0x14008e799ULL, -1, 6, "Lobby Filters", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_ROW, 0, g_lobby_filter_rows, 6, NULL, 0 },
    { 0x14008e813ULL, -1, 0, "Lobby Filters", MC_VERIFIED_LIVE_TEXT,
      MC_SETTINGS_VALUE, 0x33c, NULL, 0, g_lobby_filter_values,
      (int)(sizeof(g_lobby_filter_values) / sizeof(g_lobby_filter_values[0])) },

    /* ⭐⭐ The LOBBY menu (Online Play > Custom Match). See g_lobby_rows: all
     * three positions named by their own captions across 7 sightings. */
    { 0x140083e09ULL, -1, 3, "Lobby", MC_VERIFIED_LIVE_TEXT,
      MC_LIST, 0, g_lobby_rows, 3, NULL, 0 },

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
    { 0x1400b2ebbULL, -1, 0, "Music", MC_VERIFIED_LIVE_TEXT,
      MC_NUMBERED_LIVE, 0, NULL, 0, NULL, 0 },

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
      (int)(sizeof(g_sound_values) / sizeof(g_sound_values[0])),
      /* ⭐ Rows 0-3 are the four VOLUME sliders -- 10 values each, measured by
       * cursor address across every run. Row 4 (Challenger Jingle) is
       * Original/Remix and is deliberately NOT a level. */
      0x0Fu },

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
      (int)(sizeof(g_network_values) / sizeof(g_network_values[0])),
      /* ⭐ Rows 1-2 are Voice Chat Volume and Input Delay, 11 values each.
       * Row 0 (Microphone) is On/Off and row 3 (Connection Strength) is a
       * named filter list -- neither is a level. */
      0x06u },

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
    const mc_screen *any_count = NULL;     /* an explicit count == 0 entry */
    const mc_screen *caller_only = NULL;   /* last resort: first match */

    for (int i = 0; i < NSCREENS; i++) {
        if (g_screens[i].caller != caller)
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

/* ⭐⭐ THE MEMBERSHIP TEST for MC_COORD_LIVE. The coordinates ORDER a set; this
 * decides what is IN it, and the two jobs must stay separate -- a filter that
 * admits a neighbouring screen's objects produces a perfectly-sorted wrong
 * answer.
 *
 * The instance set holds several screens at once (30 of 64 entries were stale
 * in the run this was built from), so the rule is deliberately narrow: short
 * text only, no markup, no prose. Both screens this serves have SHORT items --
 * the lobby slots are single characters and the music strip's are game titles.
 *
 * ⚠ A blurb or a button-hint strip sharing the screen would otherwise become a
 * candidate, and then the COUNT check in gt_item_at_position() is what refuses
 * the whole reading rather than silently shifting every position by one. */
/* ⭐ The picker's alphabet, needed by build() which sits above K_PICKER's
 * definition. ⚠ ONE literal, referenced twice -- two copies could drift, and a
 * picker speaking a different alphabet from the one its guard checks is exactly
 * the class of bug this file exists to prevent. Case 38 asserts they agree. */
#define K_PICKER_STR "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define K_PICKER_CH(pos) (K_PICKER_STR[(pos) - 1])

typedef struct { int maxlen; } coord_ctx;

static BOOL coord_candidate(const char *text, void *ctx)
{
    const coord_ctx *c = (const coord_ctx *)ctx;
    size_t n;
    if (!text || !text[0])
        return FALSE;
    n = strlen(text);
    if (n > (size_t)c->maxlen)
        return FALSE;
    /* ⛔ Markup means a hint strip or a styled banner, never an item here. */
    if (strchr(text, '<'))
        return FALSE;
    /* ⛔ A sentence is a caption, not an item. */
    if (text[n - 1] == '.' || text[n - 1] == '!' || text[n - 1] == '?')
        return FALSE;
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
    /* ⭐ A GAME-TITLE STRIP. The item name arrives as the movement's caption
     * and is corroborated against the exe's ROM table; that happens in
     * menu_speak() where the caption is in hand. Nothing to resolve here, so
     * the flag is set and the position speaks meanwhile. */
    if (s->kind == MC_GAME_STRIP) {
        out->game_strip = TRUE;
        out->why = MC_UNNAMED_NO_LABELS;
        return;
    }

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
    if (s->kind == MC_CHAR_PICKER) {
        out->char_picker = TRUE;
        if (out->total == 36 && out->position >= 1 && out->position <= 36) {
            out->item[0] = K_PICKER_CH(out->position);
            out->item[1] = '\0';
            out->named = TRUE;
            return;
        }
        out->why = MC_UNNAMED_COUNT_MISMATCH;
        return;
    }

    /* ⭐⭐ A NUMBERED LIVE LIST: the item names itself on screen.
     *
     * Ask the instance set for the object whose text begins with THIS position's
     * number. The match is the whole safety argument: the cursor says "position
     * 2" and the game's own text says "02.", so a name is spoken only when two
     * independent sources agree about which row this is. No match -- a scrolled
     * window, a list from another screen, a table that has evicted the entry --
     * yields position only, which is what this screen already said.
     *
     * ⚠ out->position, not the raw index: the printed numbering is 1-based and
     * so is position. Using m->new_index here would be off by one whenever
     * min_index is 0, which is every screen measured. */
    if (s->kind == MC_NUMBERED_LIVE) {
        char text[GT_CAPTION_MAX];
        if (!gt_numbered_item(out->position, text, (int)sizeof(text)) ||
            !text[0]) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        /* The live text carries engine markup like everything else on this
         * channel, so it goes through the same translator, and residue is
         * refused rather than read aloud as tag soup. */
        char spoken[GM_OUT_MAX];
        gm_speak(text, spoken, sizeof(spoken));
        if (!spoken[0] || gm_has_residue(spoken)) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        _snprintf(out->item, sizeof(out->item), "%s", spoken);
        out->item[sizeof(out->item) - 1] = '\0';
        out->named = TRUE;
        return;
    }

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
    if (s->kind == MC_ROW_COLUMN) {
        char text[GT_CAPTION_MAX], spoken[GM_OUT_MAX];
        int n;

        if (!m->screen || s->coord_x <= 0.0f) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        /* ⚠ The column's own population must agree with the live row count.
         * Without this a tab drawing a DIFFERENT number of rows would still
         * yield the Nth label -- a perfectly-ordered wrong answer, which is the
         * failure this whole file is built to refuse. */
        n = gt_column_count(m->screen, s->coord_x);
        if (n != out->total) {
            log_line("mcspeak: %s -- column x=%.0f holds %d label(s) but the "
                     "cursor says %d; refusing rather than naming a row.",
                     s->name ? s->name : "(unnamed)", (double)s->coord_x, n,
                     out->total);
            out->why = MC_UNNAMED_COUNT_MISMATCH;
            return;
        }
        /* ⭐⭐ pair_x appends the VALUE sharing this row's Y, so a settings row
         * reads "Game Difficulty, Free Play" instead of the label alone. 0
         * keeps the old behaviour (label only) for every screen that shipped
         * before the field existed -- gt_column_item() takes < 0 for "no
         * partner". See the field's comment for why this QUOTES rather than
         * maps: on this screen "Normal" has 13 corpus stems and two of them are
         * its own rows.
         *
         * ⚠ A row with no value (the "Default" action row) yields the label
         * alone, because the pairing is by shared Y and there is nothing at
         * that Y to append -- not a gap to fill. */
        if (!gt_column_item(m->screen, s->coord_x,
                            s->pair_x > 0.0f ? s->pair_x : -1.0f,
                            out->position, text, (int)sizeof(text)) ||
            !text[0]) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        gm_speak(text, spoken, sizeof(spoken));
        if (!spoken[0] || gm_has_residue(spoken)) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        /* ⭐⭐ ANNOUNCE THE PAGE WHEN THE TAB CHANGES. USER: "It also doesn't
         * read the title of each page when switching." Correct -- LB/RB swaps
         * the whole row set and nothing said so, leaving the player to infer the
         * page from its first row.
         *
         * ⭐ THE TAB IS IDENTIFIED BY THE ROW CURSOR'S OFFSET, not by the
         * heading. Each tab owns its OWN cursor field -- measured +0x350 (Dummy
         * Behavior, 6 rows), +0x354 and +0x358 (5 rows each), +0x35c (Player
         * Settings) -- and that offset arrives as an ARGUMENT alongside the
         * movement, so it is atomic with the rows it describes. The heading
         * object is NOT: it is refreshed on a different schedule and leads the
         * rows by one tab (2 of 6 dumps disagreed), which is why a
         * heading_index key was built, shipped and removed. See coord_x.
         *
         * ⚠ The TITLE still comes from the heading widget, because that is the
         * only place the page's name exists -- but it is read only at the moment
         * the cursor offset says the tab CHANGED, and it is spoken as a PREFIX
         * to a row read independently from the row column. So a stale heading
         * can mislabel the page; it can never mis-name the row. A strictly
         * smaller claim than the key it replaces.
         *
         * ⚠ Keyed on the screen POINTER as well: a different screen object
         * reusing the same offset is a different page and must re-announce. */
        {
            static const void *last_screen;
            static ULONG_PTR   last_off = (ULONG_PTR)-1;
            ULONG_PTR off = (m->screen && m->cursor)
                          ? (ULONG_PTR)m->cursor - (ULONG_PTR)m->screen
                          : (ULONG_PTR)-1;

            if (off != (ULONG_PTR)-1 &&
                (off != last_off || m->screen != last_screen)) {
                char head[GT_CAPTION_MAX], head_spoken[GM_OUT_MAX];

                last_off    = off;
                last_screen = m->screen;
                if (gt_widget_of(m->screen, MC_HEADING_X, -1.0f,
                                 head, (int)sizeof(head)) && head[0]) {
                    gm_speak(head, head_spoken, sizeof(head_spoken));
                    /* ⚠ Refuse a heading identical to the row about to be
                     * spoken -- on a one-row page that would stutter. */
                    if (head_spoken[0] && !gm_has_residue(head_spoken) &&
                        strcmp(head_spoken, spoken) != 0) {
                        char line[GT_CAPTION_MAX + GM_OUT_MAX + 32];
                        _snprintf(line, sizeof(line) - 1, "%s. %s, %d of %d",
                                  head_spoken, spoken, out->position,
                                  out->total);
                        line[sizeof(line) - 1] = '\0';
                        log_line("mcspeak: tab changed (row cursor +0x%03x) -- "
                                 "announcing the page \"%s\".",
                                 (unsigned)off, head_spoken);
                        menu_speak_line(line);
                        out->named          = TRUE;
                        out->spoken_already = TRUE;
                        return;
                    }
                }
            }
        }
        _snprintf(out->item, sizeof(out->item), "%s", spoken);
        out->item[sizeof(out->item) - 1] = '\0';
        out->named = TRUE;
        return;
    }

    /* ⭐⭐ A VALUE MOVEMENT: say which row and what it now reads.
     *
     * ⛔ USER: "They don't read when I change them with left or right, only
     * when I leave and come back." The value axis is a DIFFERENT CALLER with no
     * table entry, so left/right spoke bare position at exactly the moment the
     * value changed -- the one moment it matters.
     *
     * The row comes from the screen's ROW CURSOR (see row_off), not from this
     * movement's own cursor: that one indexes a value array shared by both
     * tabs, where tab B's first row is slot 6. The row and its value are then
     * quoted from the live columns, as MC_ROW_COLUMN does. */
    if (s->kind == MC_ROW_VALUE_LIVE) {
        char text[GT_CAPTION_MAX], spoken[GM_OUT_MAX];
        int row = -1, ncol;

        /* ⚠ This IS a value axis, and saying so is load-bearing: the formatter
         * picks the level form ("Game Difficulty, 4, of 9") and the
         * row-plus-value form from `axis`, and an unset axis silently falls
         * through to a bare position. */
        out->axis = MC_AXIS_VALUE;

        if (!m->screen || s->coord_x <= 0.0f || s->row_off <= 0) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        /* ⚠ GUARDED. `screen` is a pointer from a live game thread; this module
         * reads it, never trusts it. mem_readable first, then the copy -- the
         * fault guard is a net for the race, never the check. */
        {
            const void *rc = (const char *)m->screen + s->row_off;
            if (!mem_readable(rc, sizeof(row)) ||
                !mem_copy(&row, rc, sizeof(row))) {
                out->why = MC_UNNAMED_ROW_UNDERIVABLE;
                return;
            }
        }
        /* ⭐⭐ BOUND IT AGAINST THE COLUMN'S OWN POPULATION, not against a
         * constant. A screen laid out differently would otherwise yield SOME
         * row number and quote a real-looking wrong line. The column is the
         * thing being indexed, so it is the thing that decides what is in
         * range. */
        ncol = gt_column_count(m->screen, s->coord_x);
        if (ncol <= 0 || row < 0 || row >= ncol) {
            log_line("mcspeak: %s -- the row cursor at +0x%x reads %d but the "
                     "column x=%.0f holds %d label(s); refusing rather than "
                     "quoting a row.",
                     s->name ? s->name : "(unnamed)", (unsigned)s->row_off,
                     row, (double)s->coord_x, ncol);
            out->why = MC_UNNAMED_ROW_UNDERIVABLE;
            return;
        }
        /* ⭐⭐ A ROW WHOSE VALUE IS NOT TEXT AT ALL.
         *
         * ⛔ USER-VERIFIED GAP: with values working everywhere else, Game
         * Difficulty spoke "Game Difficulty, 9 of 9" -- no value. Measured in
         * the layout dump: every other row has an object at x=835 and that row
         * has NOTHING at any X on its line. It is a 9-position numeric scale
         * the game draws as graphics, which is also why OPT_DIFFICULTY is the
         * one row of this screen with no value run in the corpus.
         *
         * ⭐ So the POSITION IS THE SETTING, exactly as a volume slider's is,
         * and the `levels` mask already says so per row. Marking it here routes
         * it to the "Game Difficulty, 4, of 9" form instead of a list position.
         *
         * ⚠ Read from the mask, never inferred from "the value was missing": a
         * value that failed to READ is not a level, and treating it as one
         * would state a setting where a read error belongs. */
        if (row < 32 && (s->levels & (1u << row))) {
            /* ⭐ A level row is spoken as "<row>, N, of M" by the formatter,
             * which needs row_name set and `named` left FALSE -- the row's
             * value is a NUMBER, so there is no item text to claim. Read the
             * label alone (no pair_x): pairing would append nothing anyway,
             * and asking for a partner that cannot exist invites the read to
             * fail for the wrong reason. */
            if (gt_column_item(m->screen, s->coord_x, -1.0f, row + 1,
                               text, (int)sizeof(text)) && text[0]) {
                gm_speak(text, spoken, sizeof(spoken));
                if (spoken[0] && !gm_has_residue(spoken)) {
                    _snprintf(out->row_name, sizeof(out->row_name), "%s",
                              spoken);
                    out->row_name[sizeof(out->row_name) - 1] = '\0';
                    out->is_level = TRUE;
                    out->why = MC_NAMED_OK;
                    return;
                }
            }
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }

        /* +1: the column helper is 1-based, the cursor field is 0-based. */
        if (!gt_column_item(m->screen, s->coord_x,
                            s->pair_x > 0.0f ? s->pair_x : -1.0f,
                            row + 1, text, (int)sizeof(text)) || !text[0]) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        gm_speak(text, spoken, sizeof(spoken));
        if (!spoken[0] || gm_has_residue(spoken)) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        _snprintf(out->item, sizeof(out->item), "%s", spoken);
        out->item[sizeof(out->item) - 1] = '\0';
        out->named = TRUE;
        return;
    }

    if (s->kind == MC_COORD_LIVE) {
        char text[GT_CAPTION_MAX];
        coord_ctx cc;
        cc.maxlen = s->coord_maxlen ? s->coord_maxlen : 48;
        if (out->total < 1 ||
            !gt_item_at_position(out->position, out->total,
                                 coord_candidate, &cc,
                                 text, (int)sizeof(text)) || !text[0]) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        char spoken[GM_OUT_MAX];
        gm_speak(text, spoken, sizeof(spoken));
        if (!spoken[0] || gm_has_residue(spoken)) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        _snprintf(out->item, sizeof(out->item), "%s", spoken);
        out->item[sizeof(out->item) - 1] = '\0';
        out->named = TRUE;
        return;
    }

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
        int used = 0, n, spoken_rows = 0;

        if (!m->screen) {
            out->why = MC_UNNAMED_TEXT_UNREADABLE;
            return;
        }
        line[0] = '\0';
        /* The screen's own heading names the character -- quoted from the game,
         * so JP/US roster naming never arises. */
        if (gt_widget_of(m->screen, 960.0f, -1.0f, head, (int)sizeof(head)) &&
            head[0]) {
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
            char cand[GT_CAPTION_MAX];
            int learned = 0;
            for (int i = 0; gt_prose_at(i, cand, (int)sizeof(cand)); i++)
                learned += gm_icon_learn_legend(cand);
            if (learned)
                log_line("mcspeak: move list -- learned %d icon name(s) from "
                         "this game's own legend.", learned);
        }

        n = gt_column_count(m->screen, 84.0f);
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
            if (!gt_column_item(m->screen, 84.0f, 633.0f, i,
                                row, (int)sizeof(row)) || !row[0])
                continue;
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
            log_line("mcspeak: move list read NOTHING -- %d name object(s) at "
                     "x=84, %d pair object(s) at x=633, screen %p, cursor %d of "
                     "%d. (names==0 -> the column query found nothing; names>0 "
                     "-> every row was refused after translation.)",
                     n, gt_column_count(m->screen, 633.0f), m->screen,
                     out->position, out->total);
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
        log_line("mcspeak: move list panel -- %d of %d name object(s) SPOKEN, "
                 "%d pair object(s) at x=633, %d of %d bytes, cursor %d of %d.%s",
                 spoken_rows, n, gt_column_count(m->screen, 633.0f),
                 used, (int)sizeof(line), out->position, out->total,
                 spoken_rows < n ? "  ⚠ ROWS WERE DROPPED." : "");
        menu_speak_line(line);
        out->named = TRUE;
        out->spoken_already = TRUE;
        return;
    }

    if (s->kind == MC_SETTINGS_ROW) {
        out->axis = MC_AXIS_ROW;
        /* ⭐⭐ REMEMBER HOW MANY ROWS THIS SCREEN HAS. The count arrives as an
         * argument, and it is what tells two same-named screens apart later,
         * when the VALUE axis needs to know which row map applies. See
         * menu_speak_note_row_total(). */
        menu_speak_note_row_total(m->screen, out->total);
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
        /* ⭐ A mapped screen may still have an unestablished row. Say its
         * position, exactly as an unmapped screen does, rather than letting one
         * unknown row silence its proven neighbours. */
        if (s->labels[idx] == MC_LABEL_UNKNOWN) {
            out->why = MC_UNNAMED_NO_LABELS;
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
            /* ⚠ Pass THIS movement's screen object: when two row entries share
             * a name (Casual Match 4 rows / Ranked Match 3), the live row count
             * observed on this very screen is what picks the right map. Without
             * it the search returns the first match and can name the row from
             * the OTHER screen's map. */
            const mc_screen *sib = menu_speak_sibling_row_for(s, m->screen);
            /* ⭐⭐ THE LEVEL MASK COMES FROM THE SIBLING ROW ENTRY, not from this
             * value entry -- because ROW INDICES SHIFT BETWEEN VARIANTS of the
             * same screen and a value axis carries only ONE mask.
             *
             * ⛔⛔ Measured: Display & Sound Settings has a 7-row form and a
             * 6-row form (games with no "Internal Resolution"). Music Volume and
             * Sound Effects Volume are rows 4-5 in the first and rows 3-4 in the
             * second, so the value entry's 0x30 mask is right for one and names
             * a LEVEL where a NAME belongs in the other -- the exact failure
             * class `levels` is a per-row mask to avoid.
             *
             * The sibling is already chosen by the LIVE row count, so it is the
             * variant actually on screen. Fall back to this entry's own mask
             * when no sibling resolves, which is every single-variant screen. */
            if (row >= 0 && row < 32) {
                unsigned mask = (sib && sib->levels) ? sib->levels : s->levels;
                if (mask & (1u << row))
                    out->is_level = TRUE;
            }
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
            /* ⛔⛔ SCOPED TO SELECT GAME. This branch says "row 0 of a settings
             * screen is the game list" -- which was true when Select Game was
             * the ONLY MC_SETTINGS_VALUE screen, and became false the moment PC
             * Settings, Sound, Network and System joined it. Row 0 there is
             * Resolution / Music Volume / Microphone / Language, and this read
             * out GAME TITLES for all of them: the user heard "Power Stone,
             * 5 of 21" while changing screen resolution.
             *
             * ⭐⭐ A latent bug my own conversion made reachable. The condition
             * described a POSITION (`row == 0`) when what it meant was an
             * IDENTITY (this screen's game list), and a position is only an
             * identity while there is exactly one screen. Nothing failed at the
             * time it was written; it failed when the world grew around it.
             *
             * ⚠ Keyed on the CALLER, not the name: `name` links the two halves
             * of a settings screen and several screens deliberately share one,
             * so it is not a unique key here. */
            /* ⭐⭐ ROW 0 IS THE GAME LIST on more than one screen now.
             *
             * Select Game (0x1400b1bd9) was confirmed 2026-09-09 by walking all
             * eight slots. CASUAL MATCH's value axis (0x140098986) is the same
             * eight games in the same order, and the 21:30 run corroborates it
             * WITHOUT relying on that assumption:
             *
             *   - [ROMPROBE] predicted positions 1, 2, 3 and 8 on that strip;
             *   - the RANKED LEADERBOARD's strip captions properly, so it names
             *     itself from the game's own text, and its positions 1, 2, 3
             *     and 8 are the SAME titles -- including the quirky JP-side
             *     "Capcom Fighting Jam" at 3, which a wrong mapping would not
             *     reproduce.
             *
             * ⚠ Position 8 reads "Star Gladiator 2: Nightmare of Bilstein"
             * (JP) against the screen's "Plasma Sword: Nightmare of Bilstein"
             * (US) -- the SAME game, the known JP/US split, not a mismatch.
             *
             * ⛔ Still keyed on an explicit CALLER LIST, never widened to "any
             * row 0": row 0 means the game list on these screens and something
             * else elsewhere. The comment below explains why the key matters. */
            /* ⭐⭐ THE LOBBY SCREENS' GAME LIST HAS NINE ENTRIES, not eight:
             * position 1 is "Any" (a matchmaking FILTER, not a game) and
             * positions 2-9 are the eight ROMs. Established from the renderer's
             * own table, which gives row 0 first = 14 = GAME_NAME_ANY -- and
             * that family has exactly ONE member, because "Any" is the only
             * game-list entry that is not a ROM. The other 8 labels from index
             * 14 are SYSTEM_OK, SYSTEM_CANCEL... i.e. there is no 9-game run in
             * the GMD at all, so the titles must come from the ROM table here
             * exactly as they do on Select Game.
             *
             * ⚠ The 00:10 run walked all nine positions and named NONE of them.
             *
             * ⛔ NOT assumed from Gallery. Gallery's 9-item chooser is
             * "collection + 8 games", which is a DIFFERENT shape, and borrowing
             * it would have put the collection at position 1 instead of "Any".
             * The table is what distinguishes them. */
            int nine = (s->caller == 0x140082d16ULL ||
                        s->caller == 0x14008e813ULL);
            if (row == 0 && rom_table_ready() &&
                (s->caller == 0x1400b1bd9ULL || s->caller == 0x140098986ULL ||
                 nine)) {
                int game = m->new_index - m->min_index;
                if (nine) {
                    /* Position 1 is "Any"; the ROMs start at position 2. */
                    if (game == 0) {
                        _snprintf(out->item, sizeof(out->item), "Any");
                        out->named = TRUE;
                        out->why = MC_NAMED_OK;
                        return;
                    }
                    game -= 1;
                }
                /* ⭐⭐ SPEAK THE TITLE THE SCREEN IS SHOWING -- version 1, the
                 * US side, not version 0.
                 *
                 * ⛔ This shipped as version 0 and was WRONG IN THE USER'S EAR.
                 * For four games the two regions have different names, so the
                 * mod said "Moero! Justice Gakuen" and "Capcom Fighting Jam"
                 * while an English-language collection displayed "Project
                 * Justice" and "Capcom Fighting Evolution".
                 *
                 * ⭐ The logs settle which side the game actually draws, and it
                 * is not close: Project Justice 29 sightings vs Moero! Justice
                 * Gakuen 1; Capcom Fighting Evolution 39 vs Capcom Fighting
                 * Jam 3; Street Fighter Alpha 3 Upper 18 vs Zero 3 Upper 1.
                 *
                 * ⚠ The original comment called version 0 "a true name for the
                 * game either way" and named following the version selection as
                 * the obvious next step. True is not the standard -- a blind
                 * user hearing a name the sighted screen does not show cannot
                 * match them up, and that is the parity test this project uses.
                 *
                 * ⭐⭐ AND IT FOLLOWS THE GAME VERSION ROW. "Whatever is
                 * displayed" is the standard (user, 2026-09-10): the screen
                 * shows the JP title when the Japanese version is selected, so
                 * defaulting to either side unconditionally is wrong half the
                 * time. Hardcoding US would only have swapped which half.
                 *
                 * The version row sits beside this one on the same screen, and
                 * its current value is the next slot in the value-cursor array
                 * this screen already uses: row 1 at values_off + 1*4.
                 *
                 * ⚠ Guarded read, and only 0/1 are accepted -- "Either" is an
                 * online FILTER value, not a version, so anything else falls
                 * back rather than indexing the ROM table with nonsense. */
                int version = 1;                 /* US unless the row says JP */
                if (s->values_off > 0 && m->screen) {
                    const void *vc =
                        (const char *)m->screen + s->values_off + 4;
                    int sel = 0;
                    if (mem_readable(vc, sizeof(sel)) &&
                        mem_copy(&sel, vc, sizeof(sel)) &&
                        (sel == 0 || sel == 1))
                        version = sel;
                }
                int ri = rom_table_index_for(game, version);
                if (ri < 0)
                    ri = rom_table_index_for(game, version ? 0 : 1);
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
            } else if (sib && sib->labels && sib->nlabels > 0 &&
                       !out->is_level) {
                /* ⭐⭐ NO TABLE FOR THIS ROW -- ask the LIVE OBJECT LIST.
                 *
                 * ⛔ USER-REPORTED: "it still doesn't say anything when I change
                 * resolution." Correct, and the reason was that the join was
                 * verified offline and never wired in. The 02:30 run shows the
                 * data was there the whole time -- the PC Settings block reads
                 * Resolution/Display/VSync/Default then "1280x720",
                 * "Fullscreen", "Off".
                 *
                 * ⭐ This reaches what no GMD table can: the resolution, the
                 * display mode, and the button GLYPHS (<ICON BTN_RB> and
                 * friends, which gm_speak already turns into words).
                 *
                 * ⚠ Level rows are excluded -- their position IS the value and
                 * they are already spoken as a level. */
                /* ⚠ The table stores GMD INDICES; gt_row_value matches on the
                 * live strings, so resolve them here. Bounded and on the worker
                 * thread, so the cost is a handful of table reads. */
                char lbuf[8][64];
                const char *lptr[8];
                int nl = sib->nlabels <= 8 ? sib->nlabels : 8;
                int gotall = 1;
                for (int li = 0; li < nl; li++) {
                    if (sib->labels[li] == MC_LABEL_UNKNOWN ||
                        !gmd_text_by_index(sib->labels[li], lbuf[li],
                                           sizeof(lbuf[li])) || !lbuf[li][0]) {
                        gotall = 0;
                        break;
                    }
                    lptr[li] = lbuf[li];
                }
                char rvtext[128];
                if (gotall && nl == sib->nlabels &&
                    gt_row_value(lptr, nl, row,
                                 rvtext, sizeof(rvtext)) && rvtext[0]) {
                    char spoken[GM_OUT_MAX];
                    gm_speak(rvtext, spoken, sizeof(spoken));
                    if (spoken[0] && !gm_has_residue(spoken)) {
                        _snprintf(out->item, sizeof(out->item), "%s", spoken);
                        out->item[sizeof(out->item) - 1] = '\0';
                        out->named = TRUE;
                        out->why = MC_NAMED_OK;
                    }
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
/* ⭐⭐ Does this caption match the character the picker's POSITION implies?
 *
 * ⛔⛔ MEASURED BUG, 21:17 run. The keypad wrapped to position 36 (which is "Z")
 * and the mod spoke "0" -- the value of the six-slot ID DISPLAY, set 100 ms
 * earlier by the OTHER axis on the SAME screen object. The caption channel
 * cannot tell the two apart, because both write text on that screen.
 *
 * Across every run: 36 picker movements where the nearby single character is
 * the expected one, and 2 where it is a stale character from the ID slots. A
 * ~5% rate of confidently speaking the WRONG character is exactly what this
 * file exists to prevent.
 *
 * ⭐ So the quote is now CHECKED against the position rather than trusted. The
 * mapping is not a guess: positions 1-35 were measured as "0".."9" then
 * "A".."Y" across 83 movements in three runs, and position 36 follows the
 * sequence to "Z".
 *
 * ⚠ This makes the channel SELF-VERIFYING in the same way the music list's
 * numbering is: two independent sources (the cursor's position and the text the
 * game drew) must agree, or nothing is spoken. A mismatch yields the bare
 * position, which is always true. */
/* ⚠ THE SAME literal build() uses, not a second copy -- see K_PICKER_STR. */
static const char *const K_PICKER = K_PICKER_STR;

static BOOL char_picker_matches(const char *s, int position)
{
    if (!s || position < 1 || position > 36)
        return FALSE;
    /* Exactly one character, and it must be the one this position holds. */
    if (s[0] == '\0' || s[1] != '\0')
        return FALSE;
    return s[0] == K_PICKER[position - 1];
}

/* ⭐⭐⭐ RESOLVED 2026-09-11 (run-2026-09-11-0322): THE PICKER SPEAKS, and
 * there was never an offset to correct.
 *
 * The note this replaces said the caption is "ALWAYS the character of a
 * NEIGHBOURING position -- 20 of 20 samples exactly one position off", and
 * asked for a run with UPWARD movements to decide between two candidate
 * rules (the caption lagging the cursor, vs the game setting the character of
 * the position being LEFT).
 *
 * That run happened: **32 upward movements**, and of the 16 positions that
 * carried a character, **16 matched exactly and 0 were off** --
 *     pos 2 -> '1'   pos 11 -> 'A'   pos 17 -> 'G'
 * against K_PICKER, with no offset in either direction.
 *
 * ⛔⛔ SO THE "ALWAYS ONE OFF" FINDING WAS AN ARTEFACT OF THE OLD PAIRING, not
 * a property of the game -- the same ad-hoc-window failure that produced five
 * other wrong readings here and is why tools/logpair.py exists. A measurement
 * of 20 consistent samples was real and its INTERPRETATION was wrong: the
 * samples were being attributed to the wrong movement.
 *
 * ⭐ The lesson worth keeping: a large consistent offset is exactly what a
 * systematically mis-paired sample set looks like. Before modelling an offset,
 * check the pairing that produced it.
 *
 * ⚠ char_picker_matches() is UNCHANGED and still required. It rejects any
 * caption that disagrees with the cursor, so a future regression degrades to a
 * bare position rather than spelling a wrong code. The channel is
 * self-verifying; this run simply shows both sources now agree. */
/* Log what this movement carried, then answer whether it may be spoken.
 * ⚠ Two jobs in one call deliberately: the probe must see EVERY caption the
 * picker delivers, including (especially) the ones the match rejects -- those
 * are the samples that settle the direction. */
static BOOL char_picker_probe_then_match(const mc_move *m, const char *cap,
                                         int position)
{
    menu_speak_probe_picker(m, cap);
    return char_picker_matches(cap, position);
}

void menu_speak_probe_picker(const mc_move *m, const char *cap)
{
    if (!m || !cap || !cap[0] || cap[1] != '\0')
        return;
    int pos   = m->new_index - m->min_index + 1;
    int total = m->max_index - m->min_index + 1;
    if (total != 36 || pos < 1 || pos > 36)
        return;

    const char *p = strchr(K_PICKER, cap[0]);
    int cappos = p ? (int)(p - K_PICKER) + 1 : -1;

    /* ⛔ A caption more than one position away is NOT this movement's character.
     * The 22:45 run logged '0' against positions 35 and 36 (delta -34/-35)
     * because the picker set no text at all that visit and the channel handed
     * back a stale caption from before the screen was entered. Logging those as
     * evidence would pollute the very measurement this probe exists to make --
     * the direction question turns on a delta of +1 vs -1. */
    if (cappos > 0) {
        /* ⚠ Distance measured ROUND THE RING: this list wraps, so 36 -> 1 is a
         * step of one, not 35. Treating the wrap as "far away" would discard
         * exactly the movements the user makes most at the ends of the list. */
        int d = cappos - pos;
        if (d >  18) d -= 36;
        if (d < -18) d += 36;
        if (d < -1 || d > 1) {
            log_line("mcspeak: [PICKERPROBE] position %d: ignoring a STALE "
                     "caption '%c' (position %d, %d away) -- the picker set no "
                     "text for this movement.", pos, cap[0], cappos, d);
            return;
        }
    }
    log_line("mcspeak: [PICKERPROBE] moved to position %d (which holds '%c'), "
             "caption was '%c' = position %d, delta %d, step %+d. ⚠ NOT SPOKEN. "
             "▶ Collect a few UPWARD movements: if delta stays -1 the caption "
             "lags the cursor; if it flips to +1 the game names the position "
             "being LEFT.",
             pos, K_PICKER[pos - 1], cap[0], cappos,
             cappos > 0 ? cappos - pos : 0, m->step);
}

/* Test seam: the picker's position/character agreement check. */
BOOL menu_speak_test_char_picker(const char *s, int position)
{
    return char_picker_matches(s, position);
}

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
    static char last[GT_CAPTION_MAX];
    char text[GT_CAPTION_MAX];
    char flat[GT_CAPTION_MAX];

    /* ⛔⛔ SCAN FOR A MATCH, DO NOT TAKE THE NEWEST.
     *
     * The first version asked gt_latest_text() for the most recent prose and
     * tested that. It never fired once in the game, because the dialog sets its
     * text in this order:
     *     "If you exit the application, ..."   then "Yes"   then "No"
     * All three are prose, so "the newest" is always "No" and the prompt is
     * never looked at. Measured in the user's run: the prompt was in the record
     * and the mod said nothing.
     *
     * ⚠ My end-to-end test passed anyway because it planted ONLY the prompt --
     * a fixture that omits the very strings that break the code. The screen's
     * real neighbours are part of the input, not noise to be left out. */
    int at = -1;
    {
        int i;
        for (i = 0; gt_prose_at(i, text, (int)sizeof(text)); i++) {
            if (menu_speak_is_modal_prompt(text, modal_resolve)) {
                at = i;
                break;
            }
        }
        if (at < 0) {
            /* ⚠ Nothing on screen is a prompt: clear the guard so the same
             * dialog opened again speaks again. */
            last[0] = '\0';
            return;
        }
    }

    /* ⭐⭐ RE-ANNOUNCE WHEN THE SELECTION MOVES, not only when the prompt first
     * appears. The guard keys on the prompt AND the index: without the index
     * the dialog would speak once and then fall silent as the user moved
     * between the options -- which is exactly what the user reported as "it
     * doesn't speak the option I am on". */
    /* ⭐⭐ AND SAY ONLY WHAT CHANGED.
     *
     * ⛔ USER-REPORTED: "it repeats the whole dialog, it should just say yes or
     * no". Re-reading a two-line warning on every toggle buries the one word
     * that changed, and the user is moving between two options several times a
     * second. A sighted player re-reads the warning once and then watches the
     * highlight; the spoken equivalent is the prompt ONCE, then the option.
     *
     * So: the prompt is spoken when the PROMPT changes (a dialog appearing, or
     * a different dialog), and only the option name when just the INDEX moves. */
    BOOL prompt_is_new;
    {
        int sel = -1;
        char key[GT_CAPTION_MAX];
        (void)gt_modal_selection(&sel);
        /* `last` holds "<index>|<prompt>"; compare the prompt half alone to
         * tell "a new dialog" from "the same dialog, other option". */
        const char *bar = strchr(last, '|');
        prompt_is_new = !bar || strcmp(bar + 1, text) != 0;

        _snprintf(key, sizeof(key), "%d|%s", sel, text);
        key[sizeof(key) - 1] = '\0';
        if (strcmp(last, key) == 0)
            return;                     /* same prompt AND same option */
        _snprintf(last, sizeof(last), "%s", key);
        last[sizeof(last) - 1] = '\0';
    }

    /* ⭐ The prompt only when it is NEW; otherwise the line starts empty and
     * carries the option alone. */
    if (prompt_is_new)
        menu_speak_flatten_prompt(text, flat, (int)sizeof(flat));
    else
        flat[0] = ' ';

    /* ⭐⭐ AND THE OPTIONS. The question alone leaves the user to guess what the
     * buttons are; the dialog sets them as live text right after the prompt, so
     * they can be READ rather than assumed.
     *
     * ⭐ THE ORDER IS MEASURED, NOT ASSUMED: across every log that captured a
     * complete dump, the dialog sets prompt -> "Yes" -> "No", 4 of 4 with no
     * counter-example (the other 3 sightings were truncated dumps, not a
     * different order). Since gt_prose_at() walks NEWEST-FIRST, the prompt sits
     * at index i and the options are at i-1 and i-2.
     *
     * ⛔⛔ THIS STILL CLAIMS NOTHING ABOUT WHICH IS SELECTED. The selection is
     * not observable in these objects (measured and closed in
     * run-2026-09-11-1322), so the options are announced as a LIST -- "Yes or
     * No" -- which is true regardless of the highlight. Saying "Yes" alone
     * would state a selection the mod cannot see, on a prompt that ends the
     * game. ⚠ Do not "improve" this into naming one of them without a signal
     * that actually tracks the highlight.
     *
     * ⚠ The strings are quoted from the game, so this follows its language. */
    {
        char a[GT_CAPTION_MAX], b[GT_CAPTION_MAX];
        int i = at, selidx = -1;
        if (i >= 2 &&
            gt_prose_at(i - 1, a, (int)sizeof(a)) && a[0] &&
            gt_prose_at(i - 2, b, (int)sizeof(b)) && b[0] &&
            !strchr(a, '<') && !strchr(b, '<') &&
            strlen(a) <= 24 && strlen(b) <= 24) {
            size_t used = strlen(flat);
            /* ⚠ No leading space when the prompt was suppressed -- the line is
             * then just the option name, and " Yes" is not that. */
            const char *sep = used ? " " : "";
            /* ⚠⚠ WORK THE INDICES OUT, DO NOT REASON ABOUT THEM IN PROSE. The
             * record holds (oldest first) prompt, TOP option, BOTTOM option;
             * gt_prose_at walks NEWEST-FIRST, so with the prompt at index i:
             *     a = prose_at(i-1)  ->  the TOP option
             *     b = prose_at(i-2)  ->  the BOTTOM option
             * My first version had these the wrong way round -- the comment
             * said "a is the bottom" and the test caught it by speaking "Yes"
             * where the planted index meant "No". The arithmetic above is the
             * check; the sentence is only a record of it. */
            if (gt_modal_selection(&selidx) && (selidx == 0 || selidx == 1)) {
                /* ⭐⭐ NAME THE SELECTED OPTION, from the dialog's own state
                 * (parent+0x38c), 0 = the TOP option. See gt_modal_selection()
                 * for the two independent sources that fixed that. */
                _snprintf(flat + used, sizeof(flat) - used - 1, "%s%s", sep,
                          selidx == 0 ? a : b);
            } else {
                /* ⛔ No readable selection: LIST both rather than name one. A
                 * wrong option stated confidently on a prompt that quits the
                 * game is the failure this file exists to prevent. */
                _snprintf(flat + used, sizeof(flat) - used - 1, "%s%s or %s", sep,
                          a, b);
            }
            flat[sizeof(flat) - 1] = '\0';
        }
    }

    /* ⚠ If the prompt was suppressed and no option could be read, there is
     * nothing to say. menu_speak_line() already ignores an empty string, but
     * being explicit here keeps the intent visible: silence is the correct
     * outcome, not an accident of an empty buffer. */
    if (flat[0])
        menu_speak_line(flat);
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

    menu_speak_build(m, &sp);

    /* ⭐ The builder already spoke a whole PANEL (the move list). Saying
     * anything further would repeat it and append an "N of M" that counts
     * panels rather than rows -- the very thing that made the old announcement
     * misleading. */
    if (sp.spoken_already)
        return;

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
        _snprintf(line, sizeof(line), "%s, %d, of %d",
                  sp.row_name, sp.position, sp.total);
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
        BOOL have_cap = gt_take_caption(cap, (int)sizeof(cap));

        /* ⚠ No deferral here: a movement always speaks now. If the caption has
         * not arrived, the position is the answer -- waiting for it costs every
         * movement to rescue a few. */
        if (have_cap &&
            !caption_is_stale_repeat(cap, sp.screen_name)) {
            prompt_id id = prompt_identify(cap);
            if (id.result == PROMPT_ID_FOUND && !sp.named && id.label &&
                strncmp(id.label, "ACHIEVEMENT_", 12) == 0) {
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
            } else if (id.result == PROMPT_ID_FOUND) {
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
            } else if (!sp.named && sp.char_picker &&
                       char_picker_probe_then_match(m, cap, sp.position)) {
                /* ⭐⭐ A CHARACTER PICKER: the caption IS the character.
                 *
                 * The lobby-ID keypad (0x14008a8ef, 36 items) sets the
                 * character under the cursor as live text on EVERY movement.
                 * Measured across 83 movements in three runs, positions 1-35
                 * map exactly to "0".."9" then "A".."Y" -- one character per
                 * position, no gaps, no conflicts.
                 *
                 * ⛔ SPOKEN AS A QUOTE, NOT FROM A TABLE. A 36-entry table
                 * would be a claim about what sits at each position; this
                 * repeats what the game just drew. The two readings that
                 * conflicted in the logs (34 and 36) were an unrelated hint
                 * strip landing in the same window -- exactly the kind of
                 * timing artefact a shipped table would have frozen in, and
                 * which the quote path simply never sees.
                 *
                 * ⚠ Same safety argument as the VALUE axis: a wrong quote
                 * repeats a stale character, it does not misname a menu item.
                 * is_value_shaped() still applies, so markup strips and
                 * sentences are refused -- which is what rejects the "Confirm /
                 * Back" hint that polluted position 36. */
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
}



/* ⭐ The one place a line reaches the speech backend. Extracted from
 * menu_speak() so the modal announcer shares it rather than duplicating the
 * conversion, the interrupt policy and the honest logging. */
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
    if (speech_say(w, TRUE))
        log_line("mcspeak: spoke \"%s\"", line);
    else
        log_line("mcspeak: ⛔ NOT SPOKEN (backend refused): \"%s\"", line);
    free(heap);
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
