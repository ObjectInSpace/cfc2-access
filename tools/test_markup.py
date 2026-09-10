"""
Regression tests for the markup translator.

Every case here is a bug that actually occurred while writing markup.py. The
spacing rules interact (icon->icon, icon->word, word->icon, legend strings),
and each fix broke another case, so they are pinned rather than trusted.

Run:  python tools/test_markup.py
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from markup import (Translator, load_legends, load_legends_per_game,  # noqa: E402
                    strip_hint_stutter, unknown_tags)

CASES = [
    # An input sequence must read as DISCRETE presses. This is the whole point
    # of translating icons: stripped, the string conveys no combination at all.
    ("ACHIEVEMENT_35_TIPS", None,
     "down, down, forward, forward, back, back, Start"),

    # An icon run straight into its label must not fuse ("StartTraining").
    ("HB_TRAINING", None, "Start Training"),
    ("HB_LOBBY_WATCH", None, "LB Spectate"),

    # An icon mid-sentence must not chop it ("PRESS Confirm, TO START").
    ("MENU_PRESS_A", None, "PRESS Confirm TO START"),
    ("MID_IGAI", None, "Any Direction but up"),
    ("PRE_SDASH", None, "Hold Special and"),

    # Legends are icon->name tables; speaking both stutters
    # ("Punch, Punch Kick, Kick").
    ("HELP_MOVES_PWS1", "legend", "Punch, Kick, Jump"),
    ("HELP_MOVES_6BTN", "legend",
     "Charge, Any Attack, Light Attack, Medium Attack, Heavy Attack"),

    # Game context: C_A/C_B mean different things per game, and the legend
    # must use the game's OWN names, not another game's.
    ("HELP_MOVES_STG2", "legend",
     "Vertical Slash, Horizontal Slash, Kick, Side Step"),

    # --- Button hint strips: the shape the LIVE setText channel captures. ---
    # Each case below is a defect found by running the translator against real
    # captures, not a hypothetical.

    # ⭐ The stutter. "<ICON DECIDE>Confirm" spoke as "Confirm Confirm",
    # because the button is named after what it does.
    ("HELP_BUTTON_3", "hint", "Confirm; Cancel Back"),

    # ⚠ Drop only the DUPLICATED WORD, by prefix: "Cancel Ready" must not
    # become "Ready" (meaning lost) or "Cancel Cancel Ready" (stutter).
    ("HB_LOBBY_CANCEL", "hint", "Cancel Ready"),

    # ⚠ Pairs must be separated or the strip is unparseable by ear.
    ("HB_SOUND_MENU", "hint",
     "Confirm; X Random Shuffle (All Games); Cancel Back"),

    # ⛔ "/" between two icons is EITHER-OF, one hint, not two.
    ("HB_AWARD_TITLE", "hint", "LB or RB Change Game"),

    # ⛔ "+" between two icons is a CHORD, one hint, not two.
    ("HB_RANKING_100", "hint", "LT plus d-pad Move 100"),

    # A single-pair strip must survive the pair logic unchanged.
    ("HB_TRAINING", "hint", "Start Training"),
]

# Live setText captures, verbatim from the run of 2026-09-09 21:19. These are
# NOT in the GMD -- they are what the game builds at runtime -- so they are
# pinned literally rather than looked up by label.
#
# ⭐⭐ The <FONT> tag appears here and NOWHERE in the offline corpus (0 of
# 58310 strings, all 14 languages). The live vocabulary is a superset of the
# file's, which is why these cases exist at all.
LIVE_CASES = [
    ("title prompt",
     "<FONT 2>PRESS <SIZE 80><ICON DECIDE></SIZE> TO START</FONT>",
     "PRESS Confirm TO START"),
    ("confirm/back strip",
     "<SIZE 58><ICON DECIDE></SIZE>Confirm <SIZE 58><ICON CANCEL></SIZE>Back",
     "Confirm; Cancel Back"),
    ("select game strip",
     "Choose the game you want to play.  <SIZE 58><ICON BTN_START></SIZE>"
     "Training <SIZE 58><ICON BTN_X></SIZE>Game Settings "
     "<SIZE 58><ICON CANCEL></SIZE>Back",
     "Choose the game you want to play. Start Training; X Game Settings; "
     "Cancel Back"),
    # A plain caption with no markup at all must pass through untouched.
    ("caption", "P1", "P1"),
]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(os.path.dirname(here), "data", "text_english.json")
    if not os.path.isfile(path):
        sys.exit("run tools/extract_gmd.py first")
    with open(path, encoding="utf-8") as f:
        strings = json.load(f)

    tr = Translator(load_legends(strings), load_legends_per_game(strings))

    failed = 0
    for label, mode, expected in CASES:
        raw = strings.get(label)
        if raw is None:
            print("MISSING  %s (not in corpus)" % label)
            failed += 1
            continue
        if mode == "hint":
            got = strip_hint_stutter(raw, tr)
        else:
            got = tr.clean(raw, legend=(mode == "legend"))
        # ⭐ Hint strips are asserted EXACTLY; everything else by containment.
        #
        # ⛔ This distinction was paid for. With containment, HB_LOBBY_CANCEL
        # expecting "Cancel Ready" PASSED while the stutter dropper was
        # sabotaged to equality-only -- because the broken output
        # "Cancel Cancel Ready" still CONTAINS "Cancel Ready". A sabotage that
        # leaves the suite green is a hole in the suite, not a working
        # mechanism. Hint strips are short and fully specified, so there is no
        # reason to accept a substring; long prose cases keep containment
        # because the surrounding sentence is not what they are testing.
        ok = (got == expected) if mode == "hint" else (expected in got)
        print("%-5s %-22s %s" % ("ok" if ok else "FAIL", label, got[:88]))
        if not ok:
            print("      expected to contain: %s" % expected)
            failed += 1

    # The live captures: pinned literally, since they are absent from the GMD.
    print()
    for name, raw, expected in LIVE_CASES:
        got = strip_hint_stutter(raw, tr)
        ok = got == expected
        print("%-5s live: %-20s %s" % ("ok" if ok else "FAIL", name, got[:70]))
        if not ok:
            print("      expected exactly: %s" % expected)
            failed += 1

    # ⭐ No markup may reach the user as tag soup. The safety net drops unknown
    # tags, but an unknown tag is still a fact worth failing on: it means the
    # vocabulary moved again, exactly as <FONT> did.
    print()
    residue = [c for _, raw, _ in LIVE_CASES
               for c in ["<"] if "<" in strip_hint_stutter(raw, tr)]
    if residue:
        print("FAIL  markup residue survived into spoken output")
        failed += 1
    else:
        print("ok    no markup residue in any live capture")

    live_tags = unknown_tags({str(i): raw for i, (_, raw, _)
                              in enumerate(LIVE_CASES)})
    if live_tags:
        print("FAIL  live captures carry unknown tags: %s" % live_tags)
        failed += 1
    else:
        print("ok    live capture tags are all in the known vocabulary")

    total = len(CASES) + len(LIVE_CASES) + 2
    print()
    print("%d checked, %d failed" % (total, failed))
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
