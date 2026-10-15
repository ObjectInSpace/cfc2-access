"""
Align a function of Alpha 3's ordinary branch with its counterpart on the
alternate branch (z3uu/a3uu) and report what corresponds.

WHY THIS EXISTS
---------------
The game carries two separately compiled copies of Alpha 3. They come from
the same source, so the instruction STREAMS mostly line up, but registers,
call targets, globals and -- because the alternate state block is 0x1B528
bytes against 0x12F98 -- many struct displacements differ. Every address the
ordinary adapters rely on needs its alternate equivalent, and guessing one
from "the code looks similar" is the shape of error this project already
paid for. This tool makes the correspondence mechanical: it decodes both
functions, aligns them on an opcode-shape key, and for each aligned pair
reports the call targets, rip-relative targets and displacements that
differ. A displacement map is only as good as the alignment, so every pair
is printed with both instructions for a human to read.

METHOD AND LIMIT
----------------
Decoding is a recursive walk from the entry (branches followed, calls not),
so jump-table cases reached only indirectly are missed; the report says how
many instructions each side decoded. Alignment uses difflib on
mnemonic + operand kinds, so a reordered block shows as unmatched rather
than as a wrong pair.

Usage:
    python tools/branch_align.py ORD ALT [--show] [--disp]
    python tools/branch_align.py --calls ORD ALT     # map the call trees
"""

import argparse
import difflib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_dump import read_manifest, DEFAULT_DUMP  # noqa: E402

from capstone import Cs, CS_ARCH_X86, CS_MODE_64  # noqa: E402
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_OP_REG, X86_REG_RIP  # noqa: E402

_md = Cs(CS_ARCH_X86, CS_MODE_64)
_md.detail = True
_sections = None


def sections():
    global _sections
    if _sections is None:
        _sections = read_manifest(DEFAULT_DUMP)
    return _sections


def read(addr, n):
    for s in sections():
        if s.live <= addr < s.live + len(s.data):
            o = addr - s.live
            return s.data[o:o + n]
    return None


def decode_function(entry, limit=6000):
    """Recursive walk inside one function. Returns {addr: insn}."""
    seen = {}
    work = [entry]
    while work and len(seen) < limit:
        a = work.pop()
        while a not in seen:
            code = read(a, 16)
            if not code:
                break
            insn = next(_md.disasm(code, a, 1), None)
            if insn is None:
                break
            seen[a] = insn
            m = insn.mnemonic
            if m in ("ret", "int3") or m.startswith("ud"):
                break
            if m == "jmp":
                op = insn.operands[0]
                if op.type == X86_OP_IMM:
                    t = op.imm
                    # A jmp far outside is a tail call: do not follow.
                    if abs(t - entry) < 0x20000:
                        a = t
                        continue
                break
            if m.startswith("j") and insn.operands and insn.operands[0].type == X86_OP_IMM:
                work.append(insn.operands[0].imm)
            a += insn.size
    return seen


def key(insn):
    parts = [insn.mnemonic]
    for op in insn.operands:
        if op.type == X86_OP_REG:
            parts.append("r%d" % op.size)
        elif op.type == X86_OP_IMM:
            # Small immediates are semantic (bit masks, counts); large ones
            # are addresses and differ by branch.
            parts.append("i" if insn.mnemonic.startswith(("j", "call")) or abs(op.imm) > 0x100000
                         else "i%x" % op.imm)
        elif op.type == X86_OP_MEM:
            parts.append("m%d%s" % (op.size, "rip" if op.mem.base == X86_REG_RIP else ""))
    return " ".join(parts)


def ordered(fn):
    return [fn[a] for a in sorted(fn)]


def align(ord_entry, alt_entry):
    a = ordered(decode_function(ord_entry))
    b = ordered(decode_function(alt_entry))
    sm = difflib.SequenceMatcher(None, [key(i) for i in a], [key(i) for i in b], autojunk=False)
    pairs = []
    for blk in sm.get_matching_blocks():
        for k in range(blk.size):
            pairs.append((a[blk.a + k], b[blk.b + k]))
    return a, b, pairs, sm.ratio()


def targets(insn):
    """call/jmp target, rip-relative address, displacements, immediates."""
    out = {"call": None, "rip": None, "disp": [], "imm": []}
    for op in insn.operands:
        if op.type == X86_OP_IMM:
            if insn.mnemonic.startswith(("call", "j")):
                out["call"] = op.imm
            else:
                out["imm"].append(op.imm)
        elif op.type == X86_OP_MEM:
            if op.mem.base == X86_REG_RIP:
                out["rip"] = insn.address + insn.size + op.mem.disp
            elif op.mem.index != 0 and 0x1000000 <= op.mem.disp < 0x3000000:
                # [image + index*8 + rva]: the game's image-relative tables.
                out["rip"] = 0x140000000 + op.mem.disp
            else:
                out["disp"].append(op.mem.disp)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ord", type=lambda s: int(s, 0))
    ap.add_argument("alt", type=lambda s: int(s, 0))
    ap.add_argument("--show", action="store_true", help="print every aligned pair")
    ap.add_argument("--find", type=lambda s: int(s, 0), action="append", default=[],
                    help="print the alt instruction aligned with this ord address")
    args = ap.parse_args()

    a, b, pairs, ratio = align(args.ord, args.alt)
    print("# ord 0x%x: %d insns   alt 0x%x: %d insns   aligned %d   ratio %.3f"
          % (args.ord, len(a), args.alt, len(b), len(pairs), ratio))
    calls, rips, disps = {}, {}, {}
    for x, y in pairs:
        tx, ty = targets(x), targets(y)
        if tx["call"] and ty["call"] and tx["call"] != ty["call"] and x.mnemonic == "call":
            calls.setdefault(tx["call"], set()).add(ty["call"])
        if tx["rip"] and ty["rip"]:
            rips.setdefault(tx["rip"], set()).add(ty["rip"])
        for dx, dy in zip(tx["disp"], ty["disp"]):
            if dx != dy:
                disps.setdefault(dx, set()).add(dy)
    for f in args.find:
        hit = [p for p in pairs if p[0].address == f]
        if hit:
            x, y = hit[0]
            print("FIND 0x%x %s %s  <->  0x%x %s %s" % (x.address, x.mnemonic, x.op_str,
                                                        y.address, y.mnemonic, y.op_str))
        else:
            print("FIND 0x%x: not aligned" % f)
    print("## calls (ord -> alt)")
    for k in sorted(calls):
        print("  0x%x -> %s" % (k, ", ".join("0x%x" % v for v in sorted(calls[k]))))
    print("## rip-relative (ord -> alt)")
    for k in sorted(rips):
        if rips[k] != {k}:
            print("  0x%x -> %s" % (k, ", ".join("0x%x" % v for v in sorted(rips[k]))))
    print("## displacements that differ (ord -> alt)")
    for k in sorted(disps):
        print("  0x%x -> %s" % (k, ", ".join("0x%x" % v for v in sorted(disps[k]))))
    if args.show:
        for x, y in pairs:
            print("  0x%x %-8s %-40s | 0x%x %-8s %s" % (x.address, x.mnemonic, x.op_str[:40],
                                                      y.address, y.mnemonic, y.op_str))


if __name__ == "__main__":
    main()
