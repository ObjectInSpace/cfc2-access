"""
Locate the GMD message-lookup path inside a dump of CFC2's unpacked image.

Why this tool exists
--------------------
The exe on disk is Enigma-packed: every code section is entropy 8.00 and every
string is garbage, so a disassembler sees nothing. The mod waits for Enigma to
decrypt the image in memory and dumps it (see src/image.c). This script reads
that dump.

The strategy is anchored on something we can prove rather than something we
guess. We already parsed all 14 GMD files offline and verified all 4165 label
hashes, so we know:

  * every label string the shell can look up,
  * the exact hash of each one (~crc32(label*2) and ~crc32(label*3)),
  * the GMD header bytes of each loaded message file.

Any of those appearing in the dump is a positive identification, not a
heuristic match. That matters: a byte pattern that "looks like" a hash function
is a mechanism, not a diagnosis, and confirming a mechanism is not the same as
finding the right function.

What it reports
---------------
1. Which section each artifact lives in, with the live address (the manifest
   records the load address, and ASLR is off, so file offset -> live address is
   exact and permanent).
2. Embedded label-hash CONSTANTS in code -- the game hashing a literal label at
   compile time is the strongest single lead to a call site.
3. Label strings and GMD magics present in data.
4. Candidate hash-function sites: the CRC-32 polynomial constant and the
   256-entry CRC table, which the lookup must use to hash a runtime label.

Usage:
    python tools/analyze_dump.py [--dump DIR] [--data DIR]
"""

import argparse
import json
import os
import struct
import sys
import zlib
from collections import defaultdict

DEFAULT_DUMP = (r"C:\Program Files (x86)\Steam\steamapps\common"
                r"\CAPCOM FIGHTING COLLECTION 2\cfc2_dump")
DEFAULT_DATA = os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "data")

# The reflected CRC-32 polynomial, as it appears as an immediate in code that
# builds or uses a CRC table. Both the normal and reversed forms are searched
# because either can show up depending on how the routine is written.
CRC32_POLY_REVERSED = 0xEDB88320
CRC32_POLY_NORMAL = 0x04C11DB7


def crc(data):
    """The GMD label hash: an un-finalized reflected CRC-32."""
    return (zlib.crc32(data) & 0xFFFFFFFF) ^ 0xFFFFFFFF


class Section:
    """One dumped section, with the mapping back to its live address."""

    def __init__(self, path, rva, live, size, distinct, kind, protect):
        self.path = path
        self.rva = rva
        self.live = live
        self.size = size
        self.distinct = distinct
        self.kind = kind
        self.protect = protect
        with open(path, "rb") as f:
            self.data = f.read()

    @property
    def name(self):
        return os.path.basename(self.path)

    def addr(self, offset):
        """File offset -> live virtual address. Exact: ASLR is off."""
        return self.live + offset

    def __repr__(self):
        return "<%s rva=0x%x %s %d bytes>" % (
            self.name, self.rva, self.kind, len(self.data))


def read_manifest(dump_dir):
    """Parse manifest.txt written by image_dump()."""
    path = os.path.join(dump_dir, "manifest.txt")
    if not os.path.exists(path):
        raise SystemExit(
            "No manifest at %s\n"
            "Run the game once with the mod deployed to produce a dump."
            % path)

    sections = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) != 7:
                print("  warning: skipping malformed manifest line: %r" % line)
                continue
            leaf, rva, live, size, distinct, kind, protect = parts
            full = os.path.join(dump_dir, leaf)
            if not os.path.exists(full):
                print("  warning: manifest lists %s but it is missing" % leaf)
                continue
            sections.append(Section(full, int(rva, 16), int(live, 16),
                                    int(size), int(distinct), kind,
                                    int(protect, 16)))
    return sections


def find_all(haystack, needle, limit=None):
    """Every offset of needle in haystack."""
    out = []
    start = 0
    while True:
        i = haystack.find(needle, start)
        if i < 0:
            break
        out.append(i)
        start = i + 1
        if limit and len(out) >= limit:
            break
    return out


def load_labels(data_dir):
    """Every GMD label, from the English extraction."""
    path = os.path.join(data_dir, "text_english.json")
    if not os.path.exists(path):
        raise SystemExit(
            "No %s\nRun: python tools/extract_gmd.py" % path)
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def report_sections(sections):
    print("=" * 70)
    print("SECTIONS")
    print("=" * 70)
    for s in sections:
        print("  %-24s rva=0x%-8x live=0x%-12x %9d bytes  %-4s "
              "entropy=%d/256" % (s.name, s.rva, s.live, len(s.data),
                                  s.kind, s.distinct))
    code = sum(len(s.data) for s in sections if s.kind == "code")
    print("\n  %d sections, %d bytes of code" % (len(sections), code))


def report_label_strings(sections, labels):
    """Which labels appear literally in the dump, and where.

    A label string in .rdata means the game references it by name somewhere --
    the anchor for finding the code that looks it up.
    """
    print()
    print("=" * 70)
    print("LABEL STRINGS PRESENT IN THE DUMP")
    print("=" * 70)

    hits = defaultdict(list)
    # Test a sample first: scanning 4165 labels across every section is slow,
    # and if none of a representative sample is present, none will be.
    sample = list(labels.keys())[:200]
    for label in sample:
        needle = label.encode("ascii") + b"\0"
        for s in sections:
            for off in find_all(s.data, needle, limit=1):
                hits[label].append((s, off))

    if not hits:
        print("  none of the first 200 labels appear as literal strings.")
        print("  => the shell probably references messages by HASH, not name.")
        print("     That is the expected MT Framework design; see the hash")
        print("     constant search below, which is the real lead.")
        return {}

    print("  %d of %d sampled labels found. Scanning all %d..."
          % (len(hits), len(sample), len(labels)))
    hits = defaultdict(list)
    for label in labels:
        needle = label.encode("ascii") + b"\0"
        for s in sections:
            for off in find_all(s.data, needle, limit=1):
                hits[label].append((s, off))

    print("  %d/%d labels present\n" % (len(hits), len(labels)))
    for label in list(hits)[:25]:
        s, off = hits[label][0]
        print("    %-40s %s+0x%-6x  live 0x%x"
              % (label, s.name, off, s.addr(off)))
    if len(hits) > 25:
        print("    ... and %d more" % (len(hits) - 25))
    return hits


def _is_imm32_operand(data, off):
    """Do the 4 bytes at off plausibly sit in an imm32 operand slot?

    Cheap x64 sanity check, not a disassembler. A real baked-in hash reaches
    the code as an immediate, overwhelmingly via one of:

      B8+r imm32      mov r32, imm32          (the common case by far)
      68 imm32        push imm32
      3D imm32        cmp eax, imm32
      81 /r imm32     cmp/and/add r/m32, imm32
      C7 /0 imm32     mov r/m32, imm32

    Checking the preceding byte(s) for those opcodes throws out the bulk of
    coincidental matches. It is deliberately permissive -- the goal is to cut
    obvious noise, and anything surviving still has to be disassembled before
    it counts as a finding.
    """
    if off < 1:
        return False
    p1 = data[off - 1]

    # mov r32, imm32 -- B8..BF, optionally with a REX prefix before it.
    if 0xB8 <= p1 <= 0xBF:
        return True
    if p1 in (0x68, 0x3D, 0xA9):        # push imm32 / cmp eax / test eax
        return True

    if off >= 2:
        p2 = data[off - 2]
        # 81 /r  and  C7 /0  with a modrm byte between opcode and imm32.
        if p2 in (0x81, 0xC7):
            return True
        # REX-prefixed mov r32, imm32
        if 0xB8 <= p1 <= 0xBF and 0x40 <= p2 <= 0x4F:
            return True
    return False


def report_hash_constants(sections, labels):
    """Find embedded label hashes -- the strongest lead to a call site.

    MT Framework looks messages up by hash. Where the game asks for a specific
    message it knows at compile time, the hash is baked in as a 32-bit
    immediate. Finding one gives a code address that is, by construction, at or
    beside a message lookup.

    Both hash1 (~crc32(label*2)) and hash2 (~crc32(label*3)) are searched
    because the lookup compares both.
    """
    print()
    print("=" * 70)
    print("EMBEDDED LABEL-HASH CONSTANTS IN CODE")
    print("=" * 70)
    print("  (a baked-in hash sits at or next to a message lookup call)")
    print()

    # hash -> label. Collisions across the two hash forms are possible in
    # principle; keep every label that maps to a value so a hit is never
    # silently attributed to the wrong label.
    wanted = defaultdict(list)
    for label in labels:
        b = label.encode("ascii")
        wanted[crc(b * 2)].append((label, "hash1"))
        wanted[crc(b * 3)].append((label, "hash2"))

    code = [s for s in sections if s.kind == "code"]
    if not code:
        print("  no code sections in the dump -- nothing to search.")
        return []

    found = []
    for s in code:
        data = s.data
        for off in range(0, len(data) - 4):
            val = struct.unpack_from("<I", data, off)[0]
            if val not in wanted:
                continue
            if not _is_imm32_operand(data, off):
                continue
            for label, which in wanted[val]:
                found.append((s, off, val, label, which))

    # ⚠ A bare 4-byte match is NOT evidence. Searching 8330 target values over
    # 30 MB of x64 code yields matches by chance alone -- an early version of
    # this report was dominated by one value, 0x84894401, appearing 25+ times,
    # which is simply the common encoding `mov [rsp+imm8], eax` (89 44 24 ..)
    # straddling an instruction boundary. Requiring the bytes to actually BE an
    # imm32 operand of a mov/push/cmp is the discriminator. Even so, treat a
    # single hit as a LEAD to disassemble, never as a confirmed call site.
    if not found:
        print("  no embedded label hashes found in code.")
        print("  => lookups are likely driven by data (a table of hashes in")
        print("     .rdata) rather than by immediates. The CRC machinery")
        print("     below is then the primary lead.")
    else:
        # Group by value: a hash that appears once is far more interesting
        # than one appearing many times (which suggests a common encoding).
        by_val = defaultdict(list)
        for s, off, val, label, which in found:
            by_val[val].append((s, off, label, which))

        singles = {v: h for v, h in by_val.items() if len(h) <= 3}
        print("  %d candidate immediates; %d values appear <=3 times "
              "(the credible ones):\n" % (len(found), len(singles)))
        for val, hits in list(singles.items())[:40]:
            for s, off, label, which in hits:
                print("    0x%08x  %-5s %-38s %s+0x%x  live 0x%x"
                      % (val, which, label, s.name, off, s.addr(off)))
        noisy = {v: h for v, h in by_val.items() if len(h) > 3}
        if noisy:
            print("\n  suppressed as noise (same value many times, almost")
            print("  certainly an instruction encoding, not a hash):")
            for val, hits in list(noisy.items())[:10]:
                print("    0x%08x  x%-4d %s"
                      % (val, len(hits), hits[0][2]))
    return found


def report_hash_in_data(sections, labels):
    """Label hashes sitting in data -- i.e. a loaded GMD's label table.

    The GMD label entry is 0x20 bytes: sectionId, hash1, hash2, pad(0xCDCDCDCD),
    labelOffset, listLink. That 0xCDCDCDCD padding is a distinctive marker, and
    finding the live label table tells us where the parsed message file lives
    in memory -- which is what a lookup function walks.
    """
    print()
    print("=" * 70)
    print("GMD ARTIFACTS IN DATA")
    print("=" * 70)

    # ⚠ "GMD\0" is only 4 bytes and occurs by chance in an instruction stream
    # (an earlier version reported two such hits inside .text, with garbage
    # version fields, as if they were message files). Require the version word
    # to be the real GMD v2 value before believing it.
    GMD_V2 = 0x00010302
    any_real = False
    for s in sections:
        for off in find_all(s.data, b"GMD\0", limit=64):
            if off + 8 > len(s.data):
                continue
            ver = struct.unpack_from("<I", s.data, off + 4)[0]
            if ver != GMD_V2:
                continue
            any_real = True
            print("  GMD v2 header in %s at +0x%-8x live 0x%x"
                  % (s.name, off, s.addr(off)))
    if not any_real:
        print("  no GMD v2 headers in the dump.")
        print("  => message files are parsed into heap structures, not kept")
        print("     in the image. Expected: the ARC is decompressed at load.")

    # The 0x20-byte label entry's signature padding.
    print()
    # The signal is the SPACING, not the count. 0xCDCDCDCD is also MSVC's
    # uninitialized-heap fill, so isolated hits mean nothing; a long run of
    # them exactly 0x20 apart is a label table and little else. Gate on the
    # run length alone -- an earlier version also required 50+ total pads,
    # which would have missed a small table outright.
    for s in sections:
        pads = find_all(s.data, struct.pack("<I", 0xCDCDCDCD), limit=20000)
        if len(pads) < 2:
            continue
        # Longest consecutive run spaced exactly one label entry apart.
        best = run = 1
        best_start = start = pads[0]
        for i in range(len(pads) - 1):
            if pads[i + 1] - pads[i] == 0x20:
                run += 1
                if run > best:
                    best, best_start = run, start
            else:
                run = 1
                start = pads[i + 1]
        if best >= 16:
            print("  possible GMD label table in %s: run of %d entries "
                  "spaced 0x20 apart (%d pads total)"
                  % (s.name, best, len(pads)))
            print("    first at +0x%x (live 0x%x)"
                  % (best_start, s.addr(best_start)))


def report_crc_machinery(sections):
    """Find the CRC-32 table/polynomial the lookup must use.

    To look a label up at runtime the game has to hash it, and that means a
    reflected CRC-32. The 256-entry table is unmistakable: entry 1 of a
    reflected table is 0x77073096 and entry 255 is 0x2D02EF8D.
    """
    print()
    print("=" * 70)
    print("CRC-32 MACHINERY (the hash the lookup must compute)")
    print("=" * 70)

    # A reflected CRC-32 table always starts 00000000 77073096 EE0E612C.
    table_sig = struct.pack("<III", 0x00000000, 0x77073096, 0xEE0E612C)
    for s in sections:
        for off in find_all(s.data, table_sig, limit=8):
            # Verify it really is the full table, not a coincidence.
            ok = True
            if off + 256 * 4 <= len(s.data):
                last = struct.unpack_from("<I", s.data, off + 255 * 4)[0]
                ok = last == 0x2D02EF8D
            print("  CRC-32 table in %s at +0x%-8x live 0x%-12x  %s"
                  % (s.name, off, s.addr(off),
                     "VERIFIED (entry 255 matches)" if ok
                     else "partial/unverified"))

    for name, poly in (("reversed", CRC32_POLY_REVERSED),
                       ("normal", CRC32_POLY_NORMAL)):
        needle = struct.pack("<I", poly)
        for s in sections:
            if s.kind != "code":
                continue
            hits = find_all(s.data, needle, limit=12)
            for off in hits:
                print("  poly %s 0x%08X in %s at +0x%-8x live 0x%x"
                      % (name, poly, s.name, off, s.addr(off)))


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", default=DEFAULT_DUMP,
                    help="directory holding manifest.txt and sec_*.bin")
    ap.add_argument("--data", default=DEFAULT_DATA,
                    help="directory holding text_english.json")
    args = ap.parse_args()

    sections = read_manifest(args.dump)
    if not sections:
        raise SystemExit("Manifest listed no usable sections.")
    labels = load_labels(args.data)
    print("Loaded %d labels from the verified GMD extraction.\n" % len(labels))

    report_sections(sections)
    report_label_strings(sections, labels)
    report_hash_constants(sections, labels)
    report_hash_in_data(sections, labels)
    report_crc_machinery(sections)

    print()
    print("=" * 70)
    print("Every 'live 0x...' address above is valid in future runs:")
    print("ASLR is off and the exe has no relocations.")
    print("=" * 70)


if __name__ == "__main__":
    main()
