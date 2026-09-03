"""
Dump the decompressed menu_eng GMD blob from the shipped msg.arc.

Used by the heap-scanner self-test, which plants this exact blob in a test
process and requires the scanner to find it.

Usage:  python tools/dump_menu_eng.py --out PATH [--lang eng]

Any shipped language can be dumped. Planting a NON-English one is the
regression test for the locale bug: a live Italian run reported
text_verified=no because the expectation table held only English.
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
    ap.add_argument("--lang", default="eng",
                    help="language suffix in msg.arc, e.g. eng, ita, fre")
    args = ap.parse_args()

    arc = os.path.join(args.game_dir, "nativeDX11x64", "arc", "pc", "msg.arc")
    if not os.path.isfile(arc):
        sys.exit("msg.arc not found at %s" % arc)

    want = "menu_" + args.lang
    raw, entries = read_arc(arc)
    available = []
    for e in entries:
        short = e.name.split("\\")[-1]
        if short.startswith("menu_"):
            available.append(short)
        if short == want:
            blob = zlib.decompress(raw[e.offset:e.offset + e.comp_size])
            with open(args.out, "wb") as f:
                f.write(blob)
            print("wrote %s (%d bytes, %s)" % (args.out, len(blob), short))
            return
    sys.exit("%s not found in msg.arc -- available: %s"
             % (want, ", ".join(sorted(available))))


if __name__ == "__main__":
    main()
