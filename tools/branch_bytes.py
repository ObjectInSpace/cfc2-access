"""
Find an ordinary-branch byte run in the alternate Alpha 3 code range.

Companion to branch_map.py for functions its call-graph walk does not reach
(small leaf routines reached only through tables it cannot pair). The search
is a literal byte match inside the alternate code range only; a hit count
other than one is reported, never resolved by picking.

Usage:
    python tools/branch_bytes.py ORD_ADDR LENGTH
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from branch_align import read  # noqa: E402

ALT_LO, ALT_HI = 0x140DC0000, 0x140EC0000


def main():
    addr, n = int(sys.argv[1], 0), int(sys.argv[2], 0)
    pat = read(addr, n)
    blob = read(ALT_LO, ALT_HI - ALT_LO)
    hits, i = [], blob.find(pat)
    while i != -1:
        hits.append(ALT_LO + i)
        i = blob.find(pat, i + 1)
    print("0x%x (%d bytes): %d hit(s) %s" % (addr, n, len(hits), " ".join("0x%x" % h for h in hits)))
    return 0 if len(hits) == 1 else 1


if __name__ == "__main__":
    sys.exit(main())
