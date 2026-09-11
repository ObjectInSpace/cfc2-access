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
    "g_casual_match_rows": [
        "Game", "Game Version", "Cross-region Matchmaking",
        "One-button Special Moves",
    ],
    # Create Lobby's settings. FIVE positions pinned by their own captions
    # across four runs; the other five are MC_LABEL_UNKNOWN and stay that way --
    # the instance set offers plausible labels but no positions, and the 21:00
    # run DISPROVED the ordering it implied.
    "g_create_lobby_rows": [
        "<UNKNOWN>", "<UNKNOWN>", "<UNKNOWN>", "<UNKNOWN>", "Event Mode",
        "One-button Special Moves", "Victory Condition", "Number of Players",
        "Game Start Privilege", "Passcode",
    ],
    # Join Lobby > search filters. Five of six named by their own captions in
    # the 19:45 run; position 2 never produced one and is MC_LABEL_UNKNOWN.
    # ⚠ The <UNKNOWN> slot is listed so the other five stay pinned to their
    # POSITIONS -- dropping it would silently shift rows 3-6 up by one.
    "g_lobby_filter_rows": [
        "Game", "<UNKNOWN>", "Cross-region Matchmaking", "Comment",
        "Event Mode", "One-button Special Moves",
    ],
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
    "g_create_lobby_values": {
        (8, 2): ["Challenger", "Host"],
        (7, 8): ["2", "3", "4", "5", "6", "7", "8", "9"],
    },
    "g_select_game_values": {
        (1, 2): ["Japanese Version", "English Version"],
        (2, 2): ["P1", "P2"],
        (2, 4): ["P1", "P2", "P3", "P4"],
    },
}


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

    print()
    print("%d checked, %d failed" % (checked, failed))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
