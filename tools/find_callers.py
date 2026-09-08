"""
Find every direct `call` / `jmp` to a given live address in the CFC2 dump.

Why not linear disassembly: capstone's linear sweep stops at the first
undecodable byte, and a 16 MB section full of data islands and jump tables has
many. This project already lost a sweep that way (5716 instructions of a 16 MB
section, and it looked complete).

So this scans for the ENCODING instead. `call rel32` is E8 + 4-byte signed
displacement from the end of the instruction; `jmp rel32` is E9. For a given
target we compute, at every byte offset, what displacement would be required
and compare -- so the scan is exhaustive by construction, no decoding involved.

⚠ That exhaustiveness costs precision: a matching 5-byte sequence can occur
inside data or in the middle of a longer instruction. Every hit is therefore a
CANDIDATE. The report says so, and prints the preceding bytes so a false
positive is recognisable -- a real call site is preceded by instruction
boundaries, not by an operand tail.

Usage:
    python tools/find_callers.py 0x14007cdc0 [0x14007c9f0 ...]
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_dump import read_manifest, DEFAULT_DUMP  # noqa: E402


def scan(section, target, opcode):
    """Every offset where `opcode rel32` would reach `target`."""
    data = section.data
    out = []
    base = section.live
    # call at file offset i occupies i..i+5; displacement is relative to i+5.
    # required_disp = target - (base + i + 5)
    start = 0
    while True:
        i = data.find(opcode, start)
        if i < 0 or i + 5 > len(data):
            break
        start = i + 1
        disp = target - (base + i + 5)
        if -0x80000000 <= disp < 0x80000000 and \
                data[i + 1:i + 5] == struct.pack("<i", disp):
            out.append(i)
    return out


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("targets", nargs="+", help="live addresses")
    ap.add_argument("--dump", default=DEFAULT_DUMP)
    ap.add_argument("--jmp", action="store_true",
                    help="also report tail-call jmp rel32 sites")
    args = ap.parse_args()

    sections = [s for s in read_manifest(args.dump) if "code" in s.name]
    if not sections:
        raise SystemExit("no code sections in the dump")
    print("# scanning %d code section(s), %d bytes total"
          % (len(sections), sum(len(s.data) for s in sections)))

    for t in args.targets:
        target = int(t, 0)
        print("\n=== callers of 0x%x" % target)
        total = 0
        for s in sections:
            kinds = [(b"\xe8", "call")]
            if args.jmp:
                kinds.append((b"\xe9", "jmp "))
            for opcode, label in kinds:
                for off in scan(s, target, opcode):
                    live = s.addr(off)
                    prev = s.data[max(0, off - 8):off].hex()
                    print("  %s 0x%012x   (preceded by %s)"
                          % (label, live, prev))
                    total += 1
        print("  -- %d candidate site(s)" % total)


if __name__ == "__main__":
    main()
