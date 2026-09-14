"""
Check every label MAP in src/menu_speak.c against the real corpus.

⭐⭐ WHY THIS EXISTS. The C suite cannot do it: there is no live GMD table in
the test process, so `gmd_text_by_index()` fails there and the C cases can only
assert that the map PATH is taken -- not that the indices name the right
things. An index map is exactly the kind of data that looks fine and is wrong.

⛔ THE FAILURE IT GUARDS. A map entry landing on a HELP_ string or a VALUE
label speaks a description or a setting value where an item name belongs --
confidently, and indistinguishably from a correct name to a blind user. That is
this project's signature bug (see the removed 0x14009ba21 row).

⚠ It checks SHAPE, not truth: that each index names something item-shaped. Only
a live run can establish that a given item is on a given row, and every shipped
row still needs MC_VERIFIED_LIVE_TEXT.

Run:  python tools/test_screen_maps.py
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from name_screens import load_corpus  # noqa: E402

# What each shipped map is expected to name, in cursor order. Taken from the
# run that established it, not from the corpus -- so a corpus reshuffle or a
# fat-fingered index fails here instead of in the user's ear.
EXPECTED = {
    "g_online_play_rows": [
        "Casual Match", "Ranked Match", "Custom Match",
        "Ranked Leaderboard", "High Score Challenge",
        "High Score Challenge Leaderboard",
    ],
    "g_mode_select_rows": [
        "Arcade Mode", "Versus Mode", "Training Mode",
    ],
    # Online Play > Custom Match. Every position confirmed by its own caption
    # in the 2026-09-10 17:32 run (7 sightings, no unverified gaps).
    "g_lobby_rows": [
        "Create Lobby", "Join Lobby", "Search Lobby ID",
    ],
    # Online Play > Casual Match. Three independent sources in the 21:00 run:
    # the instance set's labels, the value-slot arithmetic (8 values on row 0,
    # 3 on row 1, via values_off 0x344) and HELP_GAME_TITLE_ON as the caption.
    # ⭐ Ranked Match: the SAME caller as Casual with one row fewer. Which row
    # drops was MEASURED (Ranked row 3 shows 2 values = Cross-region), not
    # inferred -- all four labels are live in both modes, so the instance set
    # could not decide it.
    # ⭐ The data-collection consent dialog. Shipped on TWO sources: the entry
    # point (all 7 sightings follow "Data Collection, Agree") and this being the
    # only 3-label run in the SYS_WINDOW family. The count alone was never
    # enough -- that is the reasoning that mislabelled two screens here.
    # ⭐ The TRAINING-mode pause menu: the same caller as the arcade one but
    # SEVEN items, so the 8-row map was rejected and the whole screen spoke
    # bare positions. All seven named by their own captions in one run.
    "g_training_pause_rows": [
        "Resume", "Marquee Card", "Move List", "Controller Settings",
        "Display & Sound Settings", "Change Character", "Quit",
    ],
    "g_consent_rows": [
        "Agree", "Decline", "Privacy Policy",
    ],
    "g_ranked_match_rows": [
        "Game", "Game Version", "Cross-region Matchmaking",
    ],
    "g_casual_match_rows": [
        "Game", "Game Version", "Cross-region Matchmaking",
        "One-button Special Moves",
    ],
    # Create Lobby's settings. FIVE positions pinned by their own captions
    # across four runs; the other five are MC_LABEL_UNKNOWN and stay that way --
    # the instance set offers plausible labels but no positions, and the 21:00
    # run DISPROVED the ordering it implied.
    # ⭐ All ten rows. Rows 5-10 came from captions; rows 1-4 from the row
    # renderer's first-value table at 0x141205e30, which validates itself
    # against those six (each is table[i]-1). Rows 1 and 4 are NOT table[i]-1 --
    # their value runs skip a filter-only "Any" -- so their labels are the ones
    # Lobby Filters' captions independently established.
    "g_create_lobby_rows": [
        "Game", "Game Version", "Cross-region Matchmaking", "Comment",
        "Event Mode", "One-button Special Moves", "Victory Condition",
        "Number of Players", "Game Start Privilege", "Passcode",
    ],
    # Join Lobby > search filters. Five of six named by their own captions in
    # the 19:45 run; position 2 never produced one and is MC_LABEL_UNKNOWN.
    # ⚠ The <UNKNOWN> slot is listed so the other five stay pinned to their
    # POSITIONS -- dropping it would silently shift rows 3-6 up by one.
    # All six rows. Row 2 came from the renderer's first-value table at
    # 0x141206e30, which four caption-established rows validate (each table[i]-1).
    # The IN-GAME pause menu (0x1400a749d). All 8 rows from their own captions,
    # 20 sightings. NOT the predicted 250-256 contiguous run: it has eight rows,
    # and INST_CARD (1070) / DISPLAY_SOUND_SETTINGS (574) are far from the rest.
    # In-game Keyboard Settings (0x14009ec58), 16 rows. Two captions anchor it;
    # the live object list gives the order; the corpus confirms the ten
    # movement/attack labels are MENU_KBD_* (239-248).
    # In-game Display & Sound Settings (0x14009c755). The two volume rows use
    # SOUND_* (592/593), not the Options MENU_OP_* pair -- they sit inside this
    # screen's own family block with HELP_SOUND_* captions.
    "g_ingame_av_rows": [
        "Wallpaper", "Display Filter", "Display Size", "Internal Resolution",
        "Music Volume", "Sound Effects Volume", "Default",
    ],
    "g_kbd_settings_rows": [
        "Key Bindings", "Default", "Menu", "Start", "Coin",
        "Left", "Up", "Right", "Down",
        "Light Punch", "Medium Punch", "Heavy Punch",
        "Light Kick", "Medium Kick", "Heavy Kick", "Type A",
    ],
    "g_pause_menu_rows": [
        "Resume", "Marquee Card", "Move List", "Controller Settings",
        "Display & Sound Settings", "Load Quick Save", "Quick Save", "Quit",
    ],
    # Training Options (0x1400a41ef), reached from the training pause menu's
    # "Training Menu" row. Established by COORDINATES, not captions -- the
    # caption channel is structurally blind on this screen (55 movements at
    # ~1s dwell, zero captions, while other objects captioned freely).
    # ⭐ Grouped by X then sorted by Y: "Dummy Behavior" is the screen HEADING
    # at X=960, and the six rows sit at X=320, 80px apart. Confirmed on TWO
    # games (CVS1 and CVS2) with identical labels and ordering.
    # ⚠ Rows 2 and 4 are separated by POSITION ALONE -- both have 3 values and
    # both value lists read "Off / On / Random".
    "g_training_option_rows": [
        "Action", "Block", "Throw Escape", "Recovery", "Counter Hit",
        "Default",
    ],
    # ⭐⭐ The OTHER TWO Training Options tabs, reached with LB/RB from the tab
    # above. Same caller (0x1400a41ef) AND same screen object, and both have FIVE
    # rows -- so (caller, count) matches both and they are told apart only by the
    # heading the game drew (mc_screen.heading_index; 712 DUMMY_SETTING and 713
    # PLAYER_SETTING). Exactly one heading object is resident at a time for this
    # screen, measured across the dump blocks of the 01:2x run.
    # ⭐ Rows read from the game's own live text, grouped by X=320 and ordered by
    # Y (366/446/526/606/686) -- the method the tab above validated against a
    # known-good control.
    # ⚠ Both maps are NON-CONTIGUOUS in the GMD (714, 2925, 2955, 720, 168), so
    # no base_index could have named them.
    "g_dummy_settings_rows": [
        "Health Bar", "Groove Gauge", "Guard Crush", "Stun", "Default",
    ],
    "g_player_settings_rows": [
        "Attack Data", "Input Display", "Hit Boxes", "Network Delay", "Default",
    ],
    "g_lobby_filter_rows": [
        "Game", "Game Version", "Cross-region Matchmaking", "Comment",
        "Event Mode", "One-button Special Moves",
    ],
    # ⚠ TEST-ONLY (MC_TEST_CALLER). Exists solely to keep the MC_LABEL_UNKNOWN
    # path covered now that every shipped map is fully named; its caller is not
    # a real call site, so no movement can reach it.
    "g_sentinel_test_rows": ["Game", "<UNKNOWN>", "Cross-region Matchmaking"],
    "g_select_game_rows": [
        "Game", "Game Version", "Player",
    ],
    "g_pc_settings_rows": [
        "Resolution", "Display", "VSync", "Default",
    ],
    "g_network_settings_rows": [
        "Microphone", "Voice Chat Volume", "Input Delay",
        "Connection Strength", "Default",
    ],
    "g_system_settings_rows": [
        "Language", "Menu Button", "Start Button", "Coin Button",
        "Data Collection", "Default",
    ],
    # ⛔ g_consent_dialog_rows removed: those labels were a guess and the
    # screen speaks position again.
    "g_sound_settings_rows": [
        "Music Volume", "Sound Effects Volume", "Announcer Volume",
        "Game Volume", "Challenger Jingle", "Default",
    ],
    # ⛔ g_game_options_candidate was REMOVED 2026-09-10 -- the live text
    # proved that screen is Arcade / Versus / Training, not the three settings
    # categories it guessed. Its entry is gone from here too.
    #
    # ⚠ A lesson worth keeping: this tool scrapes by regex and does NOT skip
    # comments, so it was happily "validating" that array while it sat
    # commented out -- which made a disproven guess look endorsed. Validation
    # confirms a map matches the CORPUS; it says nothing about whether the map
    # belongs to the screen.
}

# ⭐⭐ VALUE SETS GET THE SAME TREATMENT AS ROW MAPS -- they did not before.
#
# An `mc_value_set` is `{row, count, first}`: the values for one row of a
# settings screen are `count` consecutive GMD labels starting at `first`.
# Nothing validated their CONTENTS, so a wrong `first` would have spoken the
# wrong words for a setting with no test catching it -- the same class of bug
# the row-map check exists to prevent, on the other half of the same screens.
#
# Keyed by array name -> {(row, count): [expected value texts in order]}.
EXPECTED_VALUES = {
    "g_pcset_values": {
        (1, 3): ["Windowed", "Borderless Windowed", "Fullscreen"],
    },
    "g_network_values": {
        (0, 2): ["Off", "On"],
        (3, 5): ["Any", "2+", "3+", "4+", "5"],
    },
    "g_sound_values": {
        (4, 2): ["Original", "Remix"],
    },
    # Training Options. ⭐ The screen is PER-GAME: the row LABELS are identical
    # on CVS1 and CVS2, only the value lists differ, which is exactly what
    # keying on the live count is for. Both counts MEASURED, not inferred.
    # ⛔ Rows 2 (Throw Escape) and 4 (Counter Hit) get NO variant on either
    # game: DUMMY_UKEMI and COUNTER_HIT are both "Off / On / Random", three
    # identical strings, so a corpus check cannot say which family a 3-value
    # list came from. Shipping either would be a guess dressed as a lookup.
    "g_training_option_values": {
        (0, 8): ["Stand", "Crouch", "Jump", "Record", "Play", "Human",
                 "CPU", "Spectate"],
        (1, 4): ["None", "Auto", "All", "Random"],
        (1, 13): ["Off", "Auto", "All", "Random", "Blocking",
                  "Blocking (random)", "Perfect Guard",
                  "Perfect Guard (random)", "Guard Cancel Attack",
                  "Guard Cancel Attack (Random)", "Guard Cancel Move (Forward)",
                  "Guard Cancel Move (Backward)", "Guard Cancel Move (random)"],
        (3, 3): ["Normal", "Delayed Stand", "Random"],
        (3, 6): ["Normal", "Safe Fall", "Safe Fall (random)", "Delayed Stand",
                 "Delayed Stand (random)", "Random"],
        # ⭐⭐ THE OTHER TABS. `row` is the slot derived from values_off, and the
        # slots are ABSOLUTE across the screen object: each tab owns a block of
        # EIGHT, so slot = tab_base + row. Measured live -- the row axis on
        # "Guard Crush, 3 of 5" produced slot 10, "Groove Gauge, 2 of 5" gave
        # slot 9; slot - (row-1) == 8 on all six pairings.
        #
        # Dummy Settings (row cursor +0x354 / +0x358), base 8:
        (10, 3): ["Normal", "Minimal", "Off"],          # Guard Crush
        (11, 2): ["Normal", "None"],                    # Stun
        # ⚠ slot 8 Health Bar has NO entry: VITAL_GAUGE_00..02 are 715-717 but
        # _03 ("Fixed 100%") is at 4136, so a {first, count} run cannot express
        # it -- {8, 4, 715} would name the fourth value "Super Combo Gauge".
        # ⚠ slot 9 Groove Gauge is per-game, like Block/Recovery above.
        #
        # Player Settings (row cursor +0x35c), base 24:
        (24, 2): ["Off", "On"],                         # Attack Data
        (25, 2): ["Off", "On"],                         # Input Display
        # ⚠ REVERSED relative to the two above -- TR_ON_OFF, not TR_OFF_ON.
        # This is exactly why the values come from the table and never from the
        # order they "should" have.
        (26, 2): ["On", "Off"],                         # Hit Boxes
        # ⚠ slot 27 Network Delay is a GENERATED 11-level range, not labels; it
        # carries a `levels` bit on the screen entry instead of a value set.
    },
    "g_system_values": {
        # Row 0 is the 14 display languages, a contiguous run from index 0.
        (0, 14): [
            "Japanese [jp]", "English [en]", "French [fr]", "Italian [it]",
            "German [de]", "Spanish (Spain) [es-ES]",
            "Spanish (Mexico) [es-MX]", "Korean [ko]", "Polish [pl]",
            "Portuguese (Brazil) [pt-BR]", "Russian [ru]",
            "Traditional Chinese [zh-TW]", "Simplified Chinese [zh-CN]",
            "Arabic [ar]",
        ],
        (4, 2): ["Decline", "Agree"],
    },
    # Create Lobby, row 8 (0-based) = "Game Start Privilege". The 21:00 run
    # shows value position 1 = "Challenger" and 2 = "Host", three times each,
    # and both strings are UNIQUE in the corpus (LOBBY_PERM_00/01) -- so the
    # order is confirmed twice over. The other 2-value rows on that screen are
    # On/Off, which the corpus can never resolve, and get no variant.
    "g_lobby_filter_values": {
        (1, 3): ["Japanese Version", "English Version", "Either"],
        (2, 2): ["Off", "On"],
        (3, 9): ["Any", "Everyone is welcome!", "Beginners, unite!",
                 "Intermediates only!", "Advanced players only!",
                 "Just for fun!", "Let's get serious!",
                 "Looking to make friends!", "Let's chat!"],
        (4, 2): ["On (Quick Match)", "Off (Arcade Style)"],
        (5, 3): ["On", "Off", "Either"],
    },
    # Counts derived from the VALUE CURSOR's address ((cursor-screen-0x344)/4),
    # which gives exactly one count per row across every log -- unlike pairing
    # to the row cursor, which lags and showed row 1 with three different counts.
    "g_casual_match_values": {
        (1, 3): ["Japanese Version", "English Version", "Either"],
        (2, 2): ["Off", "On"],
        (3, 3): ["On", "Off", "Either"],
    },
    "g_ingame_av_values": {
        (2, 5): ["Full", "Full (4:3)", "Original", "Original (4:3)", "Wide"],
    },
    "g_create_lobby_values": {
        (1, 2): ["Japanese Version", "English Version"],
        (2, 2): ["Off", "On"],
        (3, 8): ["Everyone is welcome!", "Beginners, unite!",
                 "Intermediates only!", "Advanced players only!",
                 "Just for fun!", "Let's get serious!",
                 "Looking to make friends!", "Let's chat!"],
        (4, 2): ["On (Quick Match)", "Off (Arcade Style)"],
        (5, 2): ["On", "Off"],
        (6, 10): ["First to 1", "First to 2", "First to 3", "First to 4",
                  "First to 5", "First to 6", "First to 7", "First to 8",
                  "First to 9", "First to 10"],
        (7, 8): ["2", "3", "4", "5", "6", "7", "8", "9"],
        (8, 2): ["Challenger", "Host"],
        (9, 2): ["Off", "On"],
    },
    "g_select_game_values": {
        (1, 2): ["Japanese Version", "English Version"],
        (1, 3): ["Japanese Version", "English Version", "Either"],
        (2, 2): ["P1", "P2"],
        (2, 4): ["P1", "P2", "P3", "P4"],
    },
}


def check_value_axes_reach_a_row_map(src):
    """⛔⛔ EVERY MC_SETTINGS_VALUE ENTRY MUST HAVE A REACHABLE ROW HALF.

    menu_speak_sibling_row_for() only considers MC_SETTINGS_ROW entries, so a
    row map declared MC_LIST is INVISIBLE to its own value axis -- every row
    without a shipped value set then speaks the row NUMBER ("row 5, 1 of 11")
    instead of its name ("Music Volume, 1 of 11").

    ⚠ USER-REPORTED, and it had shipped on TWO screens: the in-game Display &
    Sound settings and Keyboard Settings. The first sounded partly correct
    because ONE of its rows has a value set and spoke properly -- which is
    exactly how it survived a read-through, and why this is a mechanical check
    rather than something to notice by reading.
    """
    m = re.search(r"g_screens\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        return ["g_screens[] not found"]
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    kinds = {}
    for entry in re.findall(r"\{([^{}]*)\}", body):
        f = [x.strip() for x in entry.split(",")]
        if len(f) < 6 or not f[0].startswith("0x"):
            continue
        kinds.setdefault(f[3].strip('"'), []).append(f[5])
    bad = []
    for name, ks in kinds.items():
        if "MC_SETTINGS_VALUE" in ks and "MC_SETTINGS_ROW" not in ks:
            bad.append("%s has a VALUE axis but no MC_SETTINGS_ROW half "
                       "(kinds: %s) -- its values will speak the row NUMBER"
                       % (name, ", ".join(ks)))
    return bad


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

    table = load_corpus()
    labels = list(table.keys())
    src = open(os.path.join(ROOT, "src", "menu_speak.c"),
               encoding="utf-8").read()

    failed = 0
    checked = 0

    # Find every `static const int g_..._rows[] = { ... };`
    maps = dict(re.findall(r"static const int (g_\w+)\[\]\s*=\s*\{([^}]*)\}",
                           src))
    if not maps:
        sys.exit("no label maps found in src/menu_speak.c -- "
                 "did the declaration style change?")

    print("label maps found: %s" % ", ".join(sorted(maps)))
    print()

    for name, body in sorted(maps.items()):
        # ⚠⚠ Parse entries IN ORDER, keeping MC_LABEL_UNKNOWN as a real element.
        # A bare `\d+` scan silently DROPS the sentinel, which would turn a
        # 6-row map into a 5-element list and check the wrong rows against the
        # wrong positions -- while still printing "ok". Strip comments first so
        # an index mentioned in a /* ... */ note is never mistaken for an entry.
        clean = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
        idxs = []
        for tok in re.findall(r"MC_LABEL_UNKNOWN|-?\d+", clean):
            idxs.append(None if tok in ("MC_LABEL_UNKNOWN", "-1")
                        else int(tok))
        want = EXPECTED.get(name)

        checked += 1
        if want is None:
            print("FAIL  %s has no expectation here -- add one so a new map "
                  "cannot ship unchecked" % name)
            failed += 1
            continue

        got = []
        bad = []
        for i in idxs:
            # ⭐ An UNESTABLISHED row. It is a real position that speaks its
            # number, so it must occupy a slot in the expectation too -- that is
            # what keeps the remaining rows aligned to their positions.
            if i is None:
                got.append("<UNKNOWN>")
                continue
            if not (0 <= i < len(labels)):
                bad.append("index %d out of range" % i)
                got.append("<OUT OF RANGE>")
                continue
            lab = labels[i]
            txt = table[lab]
            got.append(txt)
            # ⛔ The three shapes that must never appear in a map.
            if lab.startswith("HELP_"):
                bad.append("index %d is %s -- a DESCRIPTION, not an item"
                           % (i, lab))
            if "<" in txt:
                bad.append("index %d carries MARKUP (%s)" % (i, lab))
            if not txt.strip():
                bad.append("index %d is blank (%s)" % (i, lab))

        if got == want and not bad:
            print("ok    %-22s %s" % (name, " | ".join(got)))
        else:
            print("FAIL  %s" % name)
            for b in bad:
                print("        %s" % b)
            if got != want:
                print("        got  : %s" % got)
                print("        want : %s" % want)
            failed += 1

    # --- VALUE SETS -------------------------------------------------------
    # `static const mc_value_set g_x[] = { { row, count, first }, ... };`
    print()
    vsets = dict(re.findall(
        r"static const mc_value_set (g_\w+)\[\]\s*=\s*\{(.*?)\n\};",
        src, re.S))
    if not vsets:
        sys.exit("no mc_value_set tables found -- did the declaration style "
                 "change? (this check silently passing would be worse than "
                 "no check)")
    print("value sets found: %s" % ", ".join(sorted(vsets)))
    print()

    for name, body in sorted(vsets.items()):
        want = EXPECTED_VALUES.get(name)
        checked += 1
        if want is None:
            print("FAIL  %s has no expectation here -- add one so a new value "
                  "set cannot ship unchecked" % name)
            failed += 1
            continue

        # Strip comments so a `/* 3 values */` note is not read as data.
        clean = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
        entries = re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", clean)
        bad = []
        seen = {}
        for row, count, first in ((int(a), int(b), int(c))
                                  for a, b, c in entries):
            texts = []
            for k in range(count):
                i = first + k
                if not (0 <= i < len(labels)):
                    bad.append("row %d: index %d out of range" % (row, i))
                    texts.append("<OUT OF RANGE>")
                    continue
                lab = labels[i]
                txt = table[lab]
                texts.append(txt)
                # ⛔ A value must be an item-shaped label, exactly as a row is.
                if lab.startswith("HELP_"):
                    bad.append("row %d: index %d is %s -- a DESCRIPTION"
                               % (row, i, lab))
                if "<" in txt:
                    bad.append("row %d: index %d carries MARKUP (%s)"
                               % (row, i, lab))
                if not txt.strip():
                    bad.append("row %d: index %d is blank (%s)" % (row, i, lab))
            seen[(row, count)] = texts

        if seen == want and not bad:
            print("ok    %-22s %s" % (name, "; ".join(
                "row %d x%d: %s" % (r, c, " | ".join(v))
                for (r, c), v in sorted(seen.items()))))
        else:
            print("FAIL  %s" % name)
            for b in bad:
                print("        %s" % b)
            if seen != want:
                for k in sorted(set(seen) | set(want)):
                    if seen.get(k) != want.get(k):
                        print("        row %d x%d: got %s want %s"
                              % (k[0], k[1], seen.get(k), want.get(k)))
            failed += 1

    # ⛔ Every value axis must be able to REACH its row map. See the docstring
    # of check_value_axes_reach_a_row_map: this shipped broken on TWO screens
    # and was reported by the user, because it is invisible to a read-through.
    checked += 1
    axis_bad = check_value_axes_reach_a_row_map(src)
    if axis_bad:
        failed += 1
        print("FAIL  value axes with no reachable row map:")
        for b in axis_bad:
            print("        %s" % b)
    else:
        print("ok    every MC_SETTINGS_VALUE entry has an MC_SETTINGS_ROW half")

    print()
    print("%d checked, %d failed" % (checked, failed))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
