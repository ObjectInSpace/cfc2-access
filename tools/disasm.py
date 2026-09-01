"""
Disassemble around a live address in the CFC2 image dump, and find the
enclosing function's entry point.

trace_crc.py ranks candidate hash sites by the SHAPE of the surrounding bytes.
Shape is a heuristic; this tool is how a lead gets turned into something
readable, and -- with --entry -- into the address you would actually hook,
since you hook a function's first instruction, not the middle of its loop.

Finding the entry point: walk backwards looking for the end of the previous
function (a ret/int3 run) followed by a plausible prologue. x64 functions are
padded to 16 bytes with 0xCC, which makes the boundary unusually clear.

Usage:
    python tools/disasm.py 0x1400ff4aa [--before 64] [--count 60] [--entry]
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_dump import read_manifest, DEFAULT_DUMP  # noqa: E402

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
except ImportError:
    raise SystemExit("capstone is required:  pip install capstone")


def locate(sections, addr):
    """Which dumped section contains this live address?"""
    for s in sections:
        if s.live <= addr < s.live + len(s.data):
            return s, addr - s.live
    return None, None


def find_entry(section, off, max_back=0x3000):
    """Walk back to the start of the enclosing function.

    A function boundary in MSVC x64 output looks like a run of int3 (0xCC)
    padding, or a ret followed by padding, before the next function begins.
    Returns a file offset, or None if no clean boundary is found -- in which
    case say so rather than guessing, because a wrong entry point silently
    produces a hook on the middle of an instruction.
    """
    data = section.data
    lo = max(0, off - max_back)

    i = off
    while i > lo:
        # A run of >=2 int3 is padding between functions.
        if data[i] == 0xCC and data[i - 1] == 0xCC:
            j = i
            while j < len(data) and data[j] == 0xCC:
                j += 1
            if j <= off:
                return j
        i -= 1
    return None


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("address", help="live address, e.g. 0x1400ff4aa")
    ap.add_argument("--dump", default=DEFAULT_DUMP)
    ap.add_argument("--before", type=int, default=48,
                    help="bytes of context before the address")
    ap.add_argument("--count", type=int, default=60,
                    help="instructions to print")
    ap.add_argument("--entry", action="store_true",
                    help="find and disassemble from the function entry")
    args = ap.parse_args()

    addr = int(args.address, 0)
    sections = read_manifest(args.dump)
    section, off = locate(sections, addr)
    if section is None:
        raise SystemExit("0x%x is not inside any dumped section." % addr)

    print("# 0x%x -> %s+0x%x" % (addr, section.name, off))

    start_off = max(0, off - args.before)
    if args.entry:
        entry = find_entry(section, off)
        if entry is None:
            print("# no clean function boundary found within 0x3000 bytes;")
            print("# falling back to raw context. Do NOT hook from this.")
        else:
            start_off = entry
            print("# function entry: %s+0x%x  ->  live 0x%x"
                  % (section.name, entry, section.addr(entry)))
    print()

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = False
    blob = section.data[start_off:start_off + args.count * 15]

    shown = 0
    for insn in md.disasm(blob, section.addr(start_off)):
        mark = "  <== " if insn.address <= addr < insn.address + insn.size else "      "
        print("%s0x%012x  %-24s %s %s"
              % (mark, insn.address,
                 insn.bytes.hex(), insn.mnemonic, insn.op_str))
        shown += 1
        if shown >= args.count:
            break


if __name__ == "__main__":
    main()
