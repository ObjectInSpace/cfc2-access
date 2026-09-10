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
    "g_select_game_rows": [
        "Game", "Game Version", "Player",
    ],
    # ⚠ Declared in the source as a CANDIDATE and referenced by no shipped row.
    # Checked anyway: an unreferenced map is exactly what gets wired up later
    # by someone who assumes it was validated when it was written.
    "g_game_options_candidate": [
        "Game Settings", "Controller Settings", "Display & Sound Settings",
    ],
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
        idxs = [int(x) for x in re.findall(r"\d+", body)]
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

    print()
    print("%d checked, %d failed" % (checked, failed))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
