"""
Propose GMD label runs for a menu of N items.

Why this exists: the cursor hook reports WHICH screen (a caller address) and
HOW MANY items it has, but the screens carry no identifying constant, so the
caller -> label-run mapping cannot be read out of the code. Menu item labels
DO sit in contiguous runs in the GMD (verified on the main, options and pause
menus), so a run of exactly N labels followed by a parallel HELP_ run is the
structural signature of a menu.

⚠ These are CANDIDATES. Several menus share an item count -- one run produced
two different 6-item screens -- so the count alone cannot pick between them.
Confirm which screen you were in before adding a row to g_screens.

Usage:
    python tools/find_label_runs.py 6          # menus with 6 items
    python tools/find_label_runs.py 6 --near 37
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmd import iter_gmds, parse_gmd  # noqa: E402

ARC = (r"C:\Program Files (x86)\Steam\steamapps\common"
       r"\CAPCOM FIGHTING COLLECTION 2\nativeDX11x64\arc\pc\msg.arc")


def load(arc, which="menu_eng"):
    for entry, raw in iter_gmds(arc):
        if entry.split("\\")[-1] == which:
            _, _, table = parse_gmd(raw)
            return list(table.items())
    raise SystemExit("%s not found in %s" % (which, arc))


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("count", type=int, help="items on the screen")
    ap.add_argument("--arc", default=ARC)
    ap.add_argument("--near", type=int, default=None,
                    help="only show runs starting within 400 of this index")
    args = ap.parse_args()

    items = load(args.arc)
    n = args.count
    hits = []

    for base in range(len(items) - n * 2):
        names = [items[base + i][0] for i in range(n)]
        # An item run holds no HELP_ entries itself...
        if any(x.startswith("HELP_") for x in names):
            continue
        # ...and is followed by a run of HELP_ entries. Allow the HELP run to be
        # LONGER than the item run: the pause menu has context-dependent extras
        # (HELP_QUIT_TR, HELP_QUIT_HOST) with no menu item of their own.
        helps = 0
        j = base + n
        while j < len(items) and items[j][0].startswith("HELP_"):
            helps += 1
            j += 1
        if helps < n:
            continue
        if args.near is not None and abs(base - args.near) > 400:
            continue
        hits.append((base, names, helps))

    if not hits:
        print("No %d-item run with a parallel HELP_ run was found." % n)
        print("That does NOT mean the screen has no labels -- it may not use")
        print("HELP_ blurbs at all. Widen the search by hand.")
        return

    print("%d candidate run(s) of %d items:\n" % (len(hits), n))
    for base, names, helps in hits:
        print("  base %d   (%d HELP_ entries follow)" % (base, helps))
        for i, k in enumerate(names):
            print("      %d  %-28s %r" % (i, k, items[base + i][1][:46]))
        print()
    if len(hits) > 1:
        print("⚠ %d candidates -- the item count does NOT identify the screen." % len(hits))
        print("  Confirm which menu you were in before adding a row.")


if __name__ == "__main__":
    main()
