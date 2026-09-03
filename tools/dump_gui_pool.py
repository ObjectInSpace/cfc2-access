"""
Dump one GUI layout's name pool for the offline finder test.

The harness plants this in a test process and requires gui_find() to locate it
and name the right screen. Dumping the REAL bytes (rather than synthesising a
pool) is what makes the test meaningful: a synthetic pool would agree with
whatever assumptions the finder happens to make.

Usage:  python tools/dump_gui_pool.py --layout main_menu_2 --out PATH
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gui import iter_layouts, DEFAULT_GAME_DIR  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--game-dir", default=DEFAULT_GAME_DIR)
    ap.add_argument("--layout", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    for lay in iter_layouts(args.game_dir):
        if lay.name != args.layout:
            continue
        # The pool plus a little context either side, so the planted buffer
        # looks like what the scanner meets in memory (NUL-delimited names
        # with a boundary at each end).
        blob = b"\0" + lay.raw[lay.pool_start:lay.pool_end] + b"\0"
        with open(args.out, "wb") as f:
            f.write(blob)
        print("wrote %s (%d bytes, %d names, layout %s)"
              % (args.out, len(blob), len(lay.names), lay.name))
        return
    sys.exit("layout %s not found in menu.arc" % args.layout)


if __name__ == "__main__":
    main()
