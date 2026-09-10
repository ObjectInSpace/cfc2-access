"""
Write the GMD's raw string blob to a file, for tools/bench_discovery.c.

The benchmark must run against the REAL data: this project already hid a
per-byte reader behind 6-byte synthetic strings, and a benchmark whose data is
unlike the real data measures nothing.

The blob is exactly what the game holds in memory -- the strings section,
NUL-separated, in index order -- so `gmd_test_set_blob()` can point the
shipping reader at it unchanged.

Usage:  python tools/dump_string_blob.py [out.bin]
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gmd import iter_gmds, parse_gmd  # noqa: E402

GAME_DIR = (r"C:\Program Files (x86)\Steam\steamapps\common"
            r"\CAPCOM FIGHTING COLLECTION 2")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(HERE), "build", "strings.bin")

    arc = os.path.join(GAME_DIR, "nativeDX11x64", "arc", "pc", "msg.arc")
    if not os.path.isfile(arc):
        sys.exit("msg.arc not found: %s" % arc)

    table = None
    for name, raw in iter_gmds(arc):
        if name.endswith("menu_eng"):
            _, _, table = parse_gmd(raw)
            break
    if table is None:
        sys.exit("menu_eng not found")

    os.makedirs(os.path.dirname(out), exist_ok=True)
    blob = bytearray()
    for text in table.values():
        blob += text.encode("utf-8") + b"\x00"

    with open(out, "wb") as f:
        f.write(blob)
    print("wrote %s: %d strings, %d bytes" % (out, len(table), len(blob)))


if __name__ == "__main__":
    main()
