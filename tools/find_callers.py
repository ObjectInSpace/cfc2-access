"""
Find callers of a confirmed function in the CFC2 image dump.

The GMD label hash primitive is CONFIRMED at 0x1402ef1e0 (probe_hash.c ran it
in-process and it reproduced hashes known in advance). Whatever looks a message
up by label must call it, so its callers are a small, high-value candidate set
-- a far better search space than 30 MB of code.

An x64 direct call is E8 <rel32>, target = address_of_next_insn + rel32. That
is an exact test, not a heuristic: either the arithmetic lands on the function
or it does not. (A byte pattern that merely resembles a call still has to
compute the right target, which is the structural corroboration this project
learned to demand.)

For each caller we also report whether the CALL SITE looks like GMD lookup:
the GMD hash is ~crc32(label*2) / ~crc32(label*3), so a real lookup calls the
primitive TWICE on the same string, or calls it once and doubles/triples the
buffer first.

Usage:
    python tools/find_callers.py 0x1402ef1e0 [--dump DIR]
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_dump import read_manifest, DEFAULT_DUMP  # noqa: E402
from disasm import find_entry  # noqa: E402

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
except ImportError:
    raise SystemExit("capstone is required:  pip install capstone")


def find_direct_calls(sections, target):
    """Every `call rel32` whose computed target is `target`."""
    out = []
    for s in sections:
        if s.kind != "code":
            continue
        data = s.data
        base = s.live
        for off in range(0, len(data) - 5):
            if data[off] != 0xE8:
                continue
            rel = struct.unpack_from("<i", data, off + 1)[0]
            if base + off + 5 + rel == target:
                out.append((s, off, base + off))
    return out


def count_calls_to(section, lo, hi, target):
    """How many direct calls to `target` inside [lo, hi) of this section?"""
    n = 0
    data = section.data
    base = section.live
    for off in range(max(0, lo), min(hi, len(data) - 5)):
        if data[off] != 0xE8:
            continue
        rel = struct.unpack_from("<i", data, off + 1)[0]
        if base + off + 5 + rel == target:
            n += 1
    return n


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target", help="confirmed function address")
    ap.add_argument("--dump", default=DEFAULT_DUMP)
    ap.add_argument("--context", type=int, default=8,
                    help="instructions of context to show per caller")
    args = ap.parse_args()

    target = int(args.target, 0)
    sections = read_manifest(args.dump)

    calls = find_direct_calls(sections, target)
    print("=" * 72)
    print("DIRECT CALLERS OF 0x%x" % target)
    print("=" * 72)
    if not calls:
        print("  none. The function may be reached only indirectly (through a")
        print("  vtable or function pointer), which needs a runtime hook on")
        print("  the function itself to capture return addresses.")
        return
    print("  %d direct call sites\n" % len(calls))

    md = Cs(CS_ARCH_X86, CS_MODE_64)

    # Group by enclosing function so repeated calls from one routine collapse.
    seen = {}
    for s, off, addr in calls:
        entry = find_entry(s, off)
        key = (s.name, entry if entry is not None else off)
        seen.setdefault(key, []).append((s, off, addr))

    print("  grouped into %d enclosing functions:\n" % len(seen))

    ranked = []
    for (sname, entry), hits in seen.items():
        s = hits[0][0]
        # How many times does this function call the hash primitive? Twice is
        # the GMD signature (label*2 and label*3).
        span_lo, span_hi = entry, entry + 0x600
        n = count_calls_to(s, span_lo, span_hi, target)
        ranked.append((n, sname, entry, hits, s))

    ranked.sort(key=lambda r: -r[0])

    for n, sname, entry, hits, s in ranked:
        flag = ""
        if n >= 2:
            flag = "  <== calls it %d times (GMD hashes twice!)" % n
        print("  function %s+0x%x  live 0x%x   %d call(s)%s"
              % (sname, entry, s.addr(entry), len(hits), flag))
        for _, off, addr in hits[:4]:
            print("      call at live 0x%x" % addr)

    print()
    print("=" * 72)
    print("Disassemble a promising one:")
    top = ranked[0]
    print("  python tools/disasm.py 0x%x --before 0 --count 60"
          % top[4].addr(top[2]))
    print("=" * 72)


if __name__ == "__main__":
    main()
