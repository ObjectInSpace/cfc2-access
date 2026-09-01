"""
Trace outward from the CRC-32 tables to find CFC2's GMD label-hash routine.

Why trace from the tables
-------------------------
analyze_dump.py established three things about the unpacked image, two of them
negative:

  * 0 of 4165 label strings appear in it   -> messages are referenced by hash
  * 0 embedded label-hash immediates       -> no baked-in hash beside a call
  * no GMD v2 header in any section        -> parsed message files live on the
                                              heap, not in the image

So the "find a constant next to the call site" route is closed. What survives
is that the lookup MUST hash its label at runtime, and hashing means reaching
one of the five verified CRC-32 tables. This script finds the code that touches
them and ranks the candidates.

How the xrefs are found
-----------------------
x64 addresses static data RIP-relatively, so a reference to a table looks like

    48 8D 0D <rel32>      lea rcx, [rip+rel32]
    8B 04 90              mov eax, [rax+rdx*4]      (the table load itself)

The absolute target is (address of next instruction) + rel32. Rather than
disassemble 30 MB, scan every position for a plausible rel32 whose computed
target lands on a table, then require a RIP-relative-capable opcode in front of
it. That is the same discriminator lesson analyze_dump.py had to learn the hard
way: a 4-byte match is not evidence, so demand structural corroboration.

Ranking
-------
A CRC-32 routine has a recognizable shape, and the GMD label hasher has an even
more specific one -- it hashes the SAME string twice, as label*2 and label*3.
Candidates are scored on how much of that shape is present near the xref.

Usage:
    python tools/trace_crc.py [--dump DIR] [--top N]
"""

import argparse
import os
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_dump import (read_manifest, find_all, DEFAULT_DUMP)  # noqa: E402

# Verified reflected CRC-32 table signature (first three entries).
TABLE_SIG = struct.pack("<III", 0x00000000, 0x77073096, 0xEE0E612C)
TABLE_LAST = 0x2D02EF8D
TABLE_BYTES = 256 * 4

# Opcodes that can carry a RIP-relative memory operand with modrm mod=00 rm=101.
# Keyed by the byte immediately preceding the modrm byte.
RIP_OPCODES = {
    0x8D: "lea",
    0x8B: "mov r,m",
    0x89: "mov m,r",
    0x03: "add",
    0x0F: "two-byte",
    0xFF: "call/jmp m",
}


def find_tables(sections):
    """Every verified CRC-32 table in the dump."""
    tables = []
    for s in sections:
        for off in find_all(s.data, TABLE_SIG, limit=32):
            if off + TABLE_BYTES > len(s.data):
                continue
            last = struct.unpack_from("<I", s.data, off + 255 * 4)[0]
            if last != TABLE_LAST:
                continue
            tables.append((s, off, s.addr(off)))
    return tables


def scan_rip_xrefs(sections, targets, window):
    """Find RIP-relative references landing inside any target range.

    targets: list of (low, high) live-address ranges.
    Returns {target_low: [(section, offset, insn_addr, target_addr, kind)]}.
    """
    xrefs = defaultdict(list)

    for s in sections:
        if s.kind != "code":
            continue
        data = s.data
        n = len(data)
        base = s.live

        # Walk every byte position; treat the 4 bytes there as a rel32 and see
        # where it would point if the instruction ended right after it.
        for off in range(2, n - 4):
            rel = struct.unpack_from("<i", data, off)[0]
            # Cheap reject before the expensive range test: a rel32 into our
            # tables is at most +-32 MB away in this image.
            if rel > 0x4000000 or rel < -0x4000000:
                continue

            next_insn = base + off + 4
            tgt = next_insn + rel

            for low, high in targets:
                if not (low <= tgt < high):
                    continue

                # Structural check: a RIP-relative operand needs modrm with
                # mod=00, rm=101 immediately before the rel32, and a plausible
                # opcode before that (optionally behind a REX prefix).
                modrm = data[off - 1]
                if (modrm & 0xC7) != 0x05:
                    continue
                op = data[off - 2]
                kind = RIP_OPCODES.get(op)
                if kind is None:
                    continue

                xrefs[low].append((s, off - 2, base + off - 2, tgt, kind))
                break
    return xrefs


def score_candidate(data, off, table_lo, table_hi, base):
    """Score how much a site looks like a GMD label hasher.

    The signals, in rough order of how much they mean:

      +4  two distinct references to the CRC table nearby
          -> hashing twice, which is exactly what label*2 / label*3 needs
      +3  a `not` / xor-with-0xFFFFFFFF nearby
          -> the un-finalized CRC the GMD hash uses (~crc32)
      +2  a shift-right-by-8 (the CRC inner loop: c = (c>>8) ^ tbl[...])
      +2  a byte-widening load (movzx) -- walking a string
      +1  a loop backward branch
    """
    score = 0
    why = []

    lo = max(0, off - 0x200)
    hi = min(len(data), off + 0x400)
    win = data[lo:hi]

    # Count distinct RIP refs to this table in the window.
    refs = 0
    for i in range(2, len(win) - 4):
        rel = struct.unpack_from("<i", win, i)[0]
        if abs(rel) > 0x4000000:
            continue
        tgt = base + lo + i + 4 + rel
        if table_lo <= tgt < table_hi and (win[i - 1] & 0xC7) == 0x05:
            refs += 1
    if refs >= 2:
        score += 4
        why.append("%d table refs (hashes twice?)" % refs)
    elif refs == 1:
        why.append("1 table ref")

    # `not r32` = F7 /2  (modrm reg field == 2)
    for i in range(len(win) - 1):
        if win[i] == 0xF7 and ((win[i + 1] >> 3) & 7) == 2:
            score += 3
            why.append("not (un-finalized crc)")
            break

    # shr r32, 8  =  C1 /5 08
    for i in range(len(win) - 2):
        if win[i] == 0xC1 and ((win[i + 1] >> 3) & 7) == 5 and win[i + 2] == 8:
            score += 2
            why.append("shr ..,8 (crc inner loop)")
            break

    # movzx r32, r/m8 = 0F B6
    if b"\x0f\xb6" in win:
        score += 2
        why.append("movzx (string walk)")

    # short backward jump = EB/7x with negative displacement
    for i in range(len(win) - 1):
        if win[i] in (0xEB, 0x75, 0x74, 0x72, 0x77) and win[i + 1] >= 0x80:
            score += 1
            why.append("backward branch (loop)")
            break

    return score, why


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", default=DEFAULT_DUMP)
    ap.add_argument("--top", type=int, default=15)
    args = ap.parse_args()

    sections = read_manifest(args.dump)

    print("=" * 72)
    print("CRC-32 TABLES")
    print("=" * 72)
    tables = find_tables(sections)
    if not tables:
        raise SystemExit("No verified CRC-32 tables in the dump.")
    for s, off, addr in tables:
        print("  %-26s +0x%-8x live 0x%x" % (s.name, off, addr))

    targets = [(addr, addr + TABLE_BYTES) for _, _, addr in tables]

    print()
    print("=" * 72)
    print("RIP-RELATIVE XREFS INTO THOSE TABLES")
    print("=" * 72)
    xrefs = scan_rip_xrefs(sections, targets, TABLE_BYTES)

    total = sum(len(v) for v in xrefs.values())
    if total == 0:
        print("  none found.")
        print("  => the tables may be reached via a register loaded elsewhere,")
        print("     or the hashing code lives in the section that stayed")
        print("     packed (rva 0x3b8b000). Fall back to the heap-scan plan.")
        return
    print("  %d xrefs across %d tables\n" % (total, len(xrefs)))

    # Score and rank.
    ranked = []
    for low, hits in xrefs.items():
        for s, off, insn_addr, tgt, kind in hits:
            score, why = score_candidate(s.data, off, low, low + TABLE_BYTES,
                                         s.live)
            ranked.append((score, insn_addr, s, off, tgt, kind, why))
    ranked.sort(key=lambda r: -r[0])

    print("  Ranked by how much the surrounding code looks like a CRC/label")
    print("  hasher. A high score is a LEAD to disassemble, not a finding.\n")
    for score, insn_addr, s, off, tgt, kind, why in ranked[:args.top]:
        print("    score %-3d live 0x%-12x %-10s -> table 0x%x"
              % (score, insn_addr, kind, tgt))
        print("               %s+0x%x   %s"
              % (s.name, off, ", ".join(why) if why else "no shape signals"))

    if len(ranked) > args.top:
        print("\n    ... %d more (use --top)" % (len(ranked) - args.top))

    print()
    print("=" * 72)
    print("Addresses are live and permanent (ASLR off, no relocations).")
    print("Next: disassemble the top sites and confirm one hashes a string")
    print("TWICE (label*2 and label*3) -- that is the GMD label hasher.")
    print("=" * 72)


if __name__ == "__main__":
    main()
