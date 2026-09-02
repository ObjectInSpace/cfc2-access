"""
Dump the decompressed menu_eng GMD blob from the shipped msg.arc.

Used by the heap-scanner self-test, which plants this exact blob in a test
process and requires the scanner to find it.

Usage:  python tools/dump_menu_eng.py --out PATH
"""

import argparse
import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmd import read_arc  # noqa: E402

DEFAULT_GAME_DIR = (r"C:\Program Files (x86)\Steam\steamapps\common"
                    r"\CAPCOM FIGHTING COLLECTION 2")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--game-dir", default=DEFAULT_GAME_DIR)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    arc = os.path.join(args.game_dir, "nativeDX11x64", "arc", "pc", "msg.arc")
    if not os.path.isfile(arc):
        sys.exit("msg.arc not found at %s" % arc)

    raw, entries = read_arc(arc)
    for e in entries:
        if e.name.endswith("menu_eng"):
            blob = zlib.decompress(raw[e.offset:e.offset + e.comp_size])
            with open(args.out, "wb") as f:
                f.write(blob)
            print("wrote %s (%d bytes)" % (args.out, len(blob)))
            return
    sys.exit("menu_eng not found in msg.arc")


if __name__ == "__main__":
    main()
