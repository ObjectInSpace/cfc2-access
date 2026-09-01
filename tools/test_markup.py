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
from markup import Translator, load_legends, load_legends_per_game  # noqa: E402

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
        got = tr.clean(raw, legend=(mode == "legend"))
        # Long strings are checked by containment; the interesting part is the
        # icon handling, not the surrounding prose.
        ok = expected in got
        print("%-5s %-22s %s" % ("ok" if ok else "FAIL", label, got[:88]))
        if not ok:
            print("      expected to contain: %s" % expected)
            failed += 1

    print()
    print("%d checked, %d failed" % (len(CASES), failed))
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
