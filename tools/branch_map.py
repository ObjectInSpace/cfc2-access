"""
Build Alpha 3's ordinary -> alternate function map by walking both call
graphs in step (see branch_align.py for why and how a pair is aligned).

Seeds are the four pairs the lifecycle trace already established (the
initializer, setup, frame callback and update). From each aligned pair the
walk takes:
  - call targets that sit at aligned positions;
  - rip-relative targets that are tables of code pointers, paired entry by
    entry (the game's screen and state dispatch tables).
A pair is kept only when its own alignment ratio clears --min; the report
lists every pair with its ratio so a weak one is visible, not hidden.

Usage:
    python tools/branch_map.py [--min 0.6] [--out build/z3u-branch-map.txt]
    python tools/branch_map.py --where 0x1408AA2FD   # alt site for an ord insn
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from branch_align import align, targets, read  # noqa: E402
from disasm import find_entry, locate  # noqa: E402
from analyze_dump import read_manifest, DEFAULT_DUMP  # noqa: E402

SEEDS = [
    (0x14086BC20, 0x140DCB360),   # initializer
    (0x14086CFC0, 0x140DCC280),   # setup
    (0x14086BE70, 0x140DCB970),   # per-frame callback
    (0x14086D160, 0x140DCC3F0),   # update
]
ORD_LO, ORD_HI = 0x14086B000, 0x140960000
ALT_LO, ALT_HI = 0x140DC0000, 0x140EC0000
TABLE_MAX = 64


def in_ord(a):
    return ORD_LO <= a < ORD_HI


def in_alt(a):
    return ALT_LO <= a < ALT_HI


def code_table(addr, lo, hi):
    out = []
    for i in range(TABLE_MAX):
        raw = read(addr + 8 * i, 8)
        if not raw:
            break
        v = struct.unpack("<Q", raw)[0]
        if not (lo <= v < hi):
            break
        out.append(v)
    return out


def build(min_ratio):
    fmap, ratios, rips = {}, {}, {}
    queue = list(SEEDS)
    while queue:
        o, a = queue.pop(0)
        if o in fmap:
            continue
        _, _, pairs, ratio = align(o, a)
        ratios[o] = ratio
        if ratio < min_ratio:
            fmap[o] = None
            continue
        fmap[o] = a
        for x, y in pairs:
            tx, ty = targets(x), targets(y)
            if x.mnemonic in ("call", "jmp") and tx["call"] and ty["call"] \
                    and in_ord(tx["call"]) and in_alt(ty["call"]):
                queue.append((tx["call"], ty["call"]))
            if tx["rip"] and ty["rip"]:
                rips.setdefault(tx["rip"], set()).add(ty["rip"])
                to = code_table(tx["rip"], ORD_LO, ORD_HI)
                ta = code_table(ty["rip"], ALT_LO, ALT_HI)
                if to and ta:
                    for p, q in zip(to, ta):
                        queue.append((p, q))
            # lea of a function (callbacks published by the initializer)
            if x.mnemonic == "lea" and tx["rip"] and ty["rip"] \
                    and in_ord(tx["rip"]) and in_alt(ty["rip"]):
                queue.append((tx["rip"], ty["rip"]))
    return fmap, ratios, rips


def entry_of(addr):
    secs = read_manifest(DEFAULT_DUMP)
    s, off = locate(secs, addr)
    e = find_entry(s, off)
    return None if e is None else s.live + e


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--min", type=float, default=0.6)
    ap.add_argument("--out")
    ap.add_argument("--where", type=lambda s: int(s, 0), action="append", default=[])
    args = ap.parse_args()
    fmap, ratios, rips = build(args.min)
    lines = []
    for o in sorted(fmap):
        a = fmap[o]
        lines.append("0x%x -> %s  ratio %.3f" % (o, "0x%x" % a if a else "REJECTED", ratios[o]))
    lines.append("## rip-relative")
    for k in sorted(rips):
        if rips[k] != {k}:
            lines.append("  0x%x -> %s" % (k, ", ".join("0x%x" % v for v in sorted(rips[k]))))
    if args.out:
        with open(args.out, "w") as f:
            f.write("\n".join(lines) + "\n")
    for w in args.where:
        e = entry_of(w)
        a = fmap.get(e)
        if not a:
            print("WHERE 0x%x: function 0x%s not mapped" % (w, "%x" % e if e else "?"))
            continue
        _, _, pairs, ratio = align(e, a)
        hit = [p for p in pairs if p[0].address == w]
        if hit:
            x, y = hit[0]
            print("WHERE 0x%x (fn 0x%x -> 0x%x, %.3f): %s %s  <->  0x%x %s %s"
                  % (w, e, a, ratio, x.mnemonic, x.op_str, y.address, y.mnemonic, y.op_str))
        else:
            print("WHERE 0x%x (fn 0x%x -> 0x%x, %.3f): instruction not aligned" % (w, e, a, ratio))
    if not args.where:
        print("\n".join(lines))
    print("# %d functions mapped, %d rejected" % (sum(1 for v in fmap.values() if v),
                                                  sum(1 for v in fmap.values() if not v)))


if __name__ == "__main__":
    main()
