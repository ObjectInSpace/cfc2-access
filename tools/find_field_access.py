"""
Find every instruction that touches a given displacement in the CVS2 range.

WHY THIS EXISTS
---------------
The player-X candidate `B+0x0D2` was derived from capture DIFFS -- i.e. from
the shape of values. This project is 0-for-4 on shape and 4-for-4 on reading
the answer out of the instructions, and a live probe of 0x0D2 behaved in ways
the "fighter X position" reading does not explain (values drifting while the
fighters stood still, and matching a different session's mid-stage captures).

So: find the code that WRITES the field, and let the instructions say what it
is. A field written by a camera routine is a camera field, whatever its values
happen to look like.

METHOD, AND ITS LIMIT
---------------------
WARNING: Linear disassembly is useless here: CLAUDE.md records capstone stopping after
0.9% of this range, and treating that as coverage is how this project produced
false negatives before. Capstone is still used, but ONLY on short windows
anchored at candidate offsets found by byte search -- never as a sweep.

The scan looks for the x86 ModRM encodings that reach `[reg + disp]` with the
target displacement, in both disp8 and disp32 forms, then decodes a window at
each hit and keeps the ones capstone confirms really do carry that
displacement. That confirmation is what separates this from a raw byte hunt:
a matching byte pair inside a longer instruction or inside data is discarded
because it does not decode to an access with that displacement.

WARNING: Hits are still CANDIDATES in one sense: capstone decoding a window starting
at a byte does not prove that byte is an instruction boundary in the real
stream. The report prints the preceding bytes so a mid-instruction match is
recognisable, exactly as find_callers.py does.

Usage:
    python tools/find_field_access.py 0xD2 [0xD8 ...]
    python tools/find_field_access.py 0xD2 --writes-only

THE POSITIVE CONTROL -- run this before trusting any result:

    python tools/find_field_access.py 0x16C --writes-only

0x16C is the super meter, traced to its writers and behaviourally confirmed in
docs/cvs2-state-access-trace.md. The control passes when the output contains
0x1406C26AA (gain, inside the documented 0x1406C26A0 sequence) and 0x1406C4241
(spend, and as a `sub`, matching the doc), with 41 hits in total.

A scanner that finds nothing and a scanner that is broken are otherwise
indistinguishable, and this project has shipped a tool that printed a warning
while every assertion stayed green. If the control stops naming those two
addresses, fix the tool before believing anything it says about a new field.
"""

import argparse
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_dump import read_manifest, DEFAULT_DUMP  # noqa: E402

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
except ImportError:
    raise SystemExit("capstone is required:  pip install capstone")

# CVS2's code range, from the state trace. Restricting to it is what keeps the
# result readable: 0xD2 is an utterly ordinary displacement elsewhere.
CVS2_LO = 0x1406A0000
CVS2_HI = 0x140740000

# Mnemonics that STORE to their first operand.
WRITE_MNEMONICS = {
    "mov", "movzx", "movsx", "add", "sub", "and", "or", "xor", "inc", "dec",
    "imul", "shl", "shr", "sar", "neg", "not", "adc", "sbb", "lea",
}


def modrm_candidates(disp):
    """Byte offsets where a ModRM with this displacement could begin.

    Returns a list of (pattern, kind) where pattern is the byte sequence that
    must appear starting at the ModRM byte itself.

    ModRM mod=01 is [reg+disp8], mod=10 is [reg+disp32]. rm=100 means a SIB
    byte follows, so those are matched separately -- missing the SIB form
    would silently drop every access through an indexed base, which is
    precisely how a player struct reached by `base + player*stride` is
    addressed.
    """
    pats = []
    fits8 = -128 <= disp <= 127
    for reg in range(8):          # the reg field: any register
        for rm in range(8):
            modrm8 = 0x40 | (reg << 3) | rm
            modrm32 = 0x80 | (reg << 3) | rm
            if rm == 4:
                # SIB follows; the displacement sits after it, so the pattern
                # is not contiguous. Handled by the decode pass instead.
                continue
            if fits8:
                pats.append((bytes([modrm8, disp & 0xFF]), "disp8"))
            pats.append((bytes([modrm32]) + disp.to_bytes(4, "little"),
                         "disp32"))
    return pats


def scan(section, disp, writes_only):
    """Find accesses with this displacement, confirmed by decoding."""
    data = section.data
    base = section.live
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    # Collect byte offsets worth decoding. Both the contiguous ModRM forms and
    # -- for the SIB case -- the displacement bytes on their own, which is
    # broader but is filtered by the decode.
    marks = set()
    disp8 = disp.to_bytes(1, "little") if -128 <= disp <= 127 else None
    disp32 = disp.to_bytes(4, "little")

    for pat, _kind in modrm_candidates(disp):
        start = 0
        while True:
            i = data.find(pat, start)
            if i < 0:
                break
            marks.add(i)
            start = i + 1

    # SIB forms: the displacement follows a SIB byte, so look for the raw
    # displacement and back up far enough to cover prefix+opcode+modrm+sib.
    for raw in filter(None, (disp8, disp32)):
        start = 0
        while True:
            i = data.find(raw, start)
            if i < 0:
                break
            marks.add(i)
            start = i + 1

    hits = []
    seen = set()
    for m in sorted(marks):
        # Decode from a few bytes before the mark: the instruction begins at
        # its prefixes/opcode, not at the ModRM or displacement.
        for back in range(0, 12):
            off = m - back
            if off < 0:
                continue
            addr = base + off
            if not (CVS2_LO <= addr < CVS2_HI):
                continue
            if addr in seen:
                continue
            try:
                ins = next(md.disasm(data[off:off + 16], addr, count=1))
            except StopIteration:
                continue
            # Keep it only if the decoded instruction really carries this
            # displacement in a memory operand. This is the filter that makes
            # the byte search trustworthy.
            ok = False
            for op in ins.operands:
                if op.type == 3 and op.mem.disp == disp:  # X86_OP_MEM
                    ok = True
                    break
            if not ok:
                continue
            if writes_only:
                if ins.mnemonic not in WRITE_MNEMONICS:
                    continue
                # A write means the MEMORY operand is the destination, i.e.
                # the first operand. `mov eax,[rcx+0xd2]` is a read.
                ops = ins.operands
                if not ops or ops[0].type != 3:
                    continue
            seen.add(addr)
            pre = data[max(0, off - 8):off].hex()
            hits.append((addr, ins.size, ins.mnemonic, ins.op_str, pre))
            break

    # ⛔ Collapse overlapping decodes. A `66` operand-size prefix makes the SAME
    # bytes decode as `word ptr [r+d]` at one offset and `dword ptr [r+d]` one
    # byte later; both carry the displacement, so both survive the filter. At
    # most one is a real instruction boundary. Reporting both inflates the
    # count and would make a single write look like two independent writers --
    # the kind of miscount that turns into a wrong conclusion about how many
    # places touch a field.
    #
    # Keep the EARLIER start when a later hit falls inside its extent: an
    # instruction's prefixes belong to it.
    hits.sort()
    kept = []
    for h in hits:
        if kept and h[0] < kept[-1][0] + kept[-1][1]:
            continue
        kept.append(h)
    return [(a, m, o, p) for a, _sz, m, o, p in kept]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("displacements", nargs="+",
                    help="field offsets, e.g. 0xD2 0xD8")
    ap.add_argument("--dump", default=DEFAULT_DUMP)
    ap.add_argument("--writes-only", action="store_true",
                    help="keep only instructions storing TO the field")
    args = ap.parse_args()

    sections = read_manifest(args.dump)
    code = [s for s in sections
            if s.kind == "code" and s.live <= CVS2_LO < s.live + s.size]
    if not code:
        raise SystemExit("No dumped code section covers the CVS2 range")

    for d in args.displacements:
        disp = int(d, 0)
        print("=" * 70)
        print("displacement +0x%02X  in %s  [%s]"
              % (disp, "CVS2 range 0x%X..0x%X" % (CVS2_LO, CVS2_HI),
                 "writes only" if args.writes_only else "reads and writes"))
        print("=" * 70)
        total = 0
        for s in code:
            hits = scan(s, disp, args.writes_only)
            total += len(hits)
            by_mnem = defaultdict(int)
            for addr, mnem, ops, pre in hits:
                by_mnem[mnem] += 1
                print("  0x%X  %-6s %-34s   prev: %s"
                      % (addr, mnem, ops, pre))
            if by_mnem:
                print("  --")
                print("  by mnemonic: " + ", ".join(
                    "%s=%d" % kv for kv in sorted(by_mnem.items())))
        print("  total: %d" % total)
        if total == 0:
            print("  WARNING: ZERO hits is not automatically a negative: an "
                  "access through a register-held\n"
                  "    offset carries no displacement at all and cannot be "
                  "found this way.")


if __name__ == "__main__":
    main()
