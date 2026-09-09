"""
Check the Select Game row map against the SHIPPED data, in every language.

⭐⭐ WHY THIS EXISTS AS A SEPARATE TEST
The C suite cannot check this: it runs with no live GMD table, so it can verify
the map's SHAPE (three entries, no base index, not contiguous) but never that
index 52 is actually "Game Version". This reads msg.arc and asks.

⛔ It exists because the screen this map names was previously named by GUESS --
"Game settings" -- and shipped, telling the user "setting 2 of 3" when they
entered Offline Play. A guess is what this file replaces.

⚠ Checks the LABEL KEYS, not the translated strings: the strings differ per
language (that is the point), the keys do not. A map that drifted onto a value
label would show up as a key like GAME_VERSION_J.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmd import iter_gmds, parse_gmd

ARC = (r"C:\Program Files (x86)\Steam\steamapps\common"
       r"\CAPCOM FIGHTING COLLECTION 2\nativeDX11x64\arc\pc\msg.arc")

# Must match g_select_game_rows in src/menu_speak.c.
ROW_MAP = [51, 52, 56]
EXPECT_KEYS = ["GAME_TITLE", "GAME_VERSION", "PLAYER"]


def main():
    if not os.path.isfile(ARC):
        sys.exit("msg.arc not found -- is the game installed?")

    failures = 0
    checked = 0
    for name, raw in iter_gmds(ARC):
        short = name.split("\\")[-1]
        if not short.startswith("menu_"):
            continue
        _n, _lang, d = parse_gmd(raw)
        items = list(d.items())
        checked += 1

        keys = [items[i][0] for i in ROW_MAP]
        if keys != EXPECT_KEYS:
            print("  FAIL  %-12s map lands on %s, expected %s"
                  % (short, keys, EXPECT_KEYS))
            failures += 1
            continue

        vals = [items[i][1] for i in ROW_MAP]
        if not all(v and v.strip() for v in vals):
            print("  FAIL  %-12s a row label is empty: %r" % (short, vals))
            failures += 1
            continue
        print("  PASS  %-12s %s" % (short, " | ".join(vals)))

    # ⭐ A positive count. "0 failed" over 0 languages is not a pass -- this
    # project has been bitten by a probe that reported success having examined
    # nothing.
    print("\nlanguages checked: %d" % checked)
    if checked == 0:
        sys.exit("FAILED: no menu_* GMD was examined")

    # ⭐⭐ The map must NOT be contiguous. If it ever became 51,52,53 then a
    # base index would do, and -- far worse -- 53 is "Japanese Version", a
    # VALUE label. Contiguity here means the map has drifted onto values.
    if ROW_MAP == list(range(ROW_MAP[0], ROW_MAP[0] + len(ROW_MAP))):
        sys.exit("FAILED: the row map is contiguous, which means it has "
                 "drifted onto the value labels between the rows")

    if failures:
        sys.exit("FAILED: %d language(s)" % failures)
    print("all languages OK")


if __name__ == "__main__":
    main()
