"""Decode CVS2's win-quote table from the SHIPPED archive -- no game launch.

WHY THIS EXISTS
---------------
docs/cvs2-state-access-trace.md located the quote region live at
`arena+0x1FD800` and then concluded:

    "NEITHER HEADER TABLE IS THE QUOTE INDEX. `+0x62CC` holds u32s (0x5238,
     0x5290, 0x52F0, ...) that point PAST the text into another region"

⭐⭐ THAT WAS A ONE-LEVEL READ OF A TWO-LEVEL TABLE. The u32s at +0x62CC do not
point at text; they point at PER-CHARACTER SUB-TABLES, and those point at the
lines. Dereferencing one level lands on the sub-table's own pointer bytes --
which is exactly the "binary garbage" the trace reported. The table was right
there; the indirection was one deeper than the read.

⚠ The general lesson, since this project has paid for it before: "the pointer
resolves to garbage" can mean the pointer is wrong, OR that it is a pointer to
POINTERS. Check whether the "garbage" is itself a plausible offset array before
concluding a table is not an index. Here every u32 was a small ascending
in-region value -- the signature of an index, not of text.

THE FORMAT (all offsets relative to the region base, 25,676 bytes)

    +0x0000  u32  0x62CC -- offset of the 48-entry character index
    +0x0004  u32  0x638C -- offset of 48 per-character width-data pointers
    +0x0008  character remap table (font glyph indices, NOT text)
    +0x0074  per-line width/advance bytes
    +0x01F4  text: NUL-terminated lines
    +0x62CC  48 u32 -- character ID -> that character's quote sub-table
    +0x638C  48 u32 -- character ID -> width data at +0x0074

Each character's sub-table is 8 u32 pointers, one per quote. Each of those
points at an array of u16 LINE OFFSETS, terminated by the following quote's
pointer (for quote 7, by the sub-table itself). Zero entries are padding.

⚠ QUOTES ARE MULTI-LINE and the line break is presentational, not semantic:
    +0x01F4 "Stand up and face me again. I have"
    +0x0218 "yet to see your best..."
is ONE sentence. Join with a space; do not speak the lines separately.

⚠ EXACTLY 8 QUOTES PER CHARACTER, 48 characters, 384 total. The game picks one
at RANDOM at display time (user-confirmed). So this file tells you the eight
CANDIDATES for a winner -- it does NOT tell you which was shown. Reading by
character ID alone would speak one of eight at random, and a wrong quote is
indistinguishable from a right one to a blind user. Do not ship that.

WHAT THIS IS FOR: the candidate set, and the ground truth a runtime reader must
be checked against. The remaining unknown is the RUNTIME SELECTION, not the
text or its layout.

CONTROLS run in the same invocation (--self-test). A decoder that cannot
reproduce the five phrases independently confirmed live is not trustworthy, and
"found nothing" must never read like "looked at nothing".
"""
import argparse
import os
import struct
import sys
import zlib

REGION_OFF = 0x1FD840        # in bin/cvs2; = live arena+0x1FD800 + 0x40 IBIS hdr
REGION_LEN = 25676
IDX_CHARS = 48
QUOTES_PER_CHAR = 8

# Independently confirmed LIVE in the running game (see the trace). These are
# the positive control: the decoder must reproduce them from the shipped file.
LIVE_PHRASES = [
    "Stand up and face me again",
    "Shinryuken",
    "Sumo is the greatest",
    "Psycho power",
    "Kyokugen style",
]


def extract_blob(arc_path):
    """bin/cvs2 out of game_10.arc. ARC v7, 80-byte entries, zlib payload."""
    b = open(arc_path, 'rb').read()
    if b[:4] != b'ARC\x00':
        raise ValueError("not an ARC file: %r" % b[:4])
    n = struct.unpack_from('<H', b, 6)[0]
    off = 8
    for _ in range(n):
        rec = b[off:off + 80]
        off += 80
        name = rec[:64].split(b'\0')[0].decode('ascii', 'replace')
        _ext, csize, _dsize, doff = struct.unpack_from('<IIII', rec, 64)
        if name.replace('\\', '/').endswith('bin/cvs2'):
            return zlib.decompress(b[doff:doff + csize])
    raise ValueError("bin/cvs2 not found in %s" % arc_path)


def load_region(blob):
    if blob[:4] != b'IBIS':
        raise ValueError("expected an IBIS container, got %r" % blob[:4])
    r = blob[REGION_OFF:REGION_OFF + REGION_LEN]
    if len(r) != REGION_LEN:
        raise ValueError("region truncated: %d bytes" % len(r))
    return r


def decode(region):
    """-> list of 48 lists of 8 strings. Raises if the layout disagrees."""
    idx, _width = struct.unpack_from('<II', region, 0)
    if idx != 0x62CC:
        raise ValueError("unexpected index offset 0x%x" % idx)

    def cstr(o):
        e = region.find(b'\0', o)
        return region[o:e].decode('ascii', 'replace')

    out = []
    for cid in range(IDX_CHARS):
        sub = struct.unpack_from('<I', region, idx + cid * 4)[0]
        if not 0 < sub < len(region):
            raise ValueError("char %d: sub-table 0x%x out of region" % (cid, sub))
        ptrs = [struct.unpack_from('<I', region, sub + j * 4)[0]
                for j in range(QUOTES_PER_CHAR)]
        quotes = []
        for j, p in enumerate(ptrs):
            # The line array ends where the next quote's array begins; the last
            # one ends at the sub-table itself (the arrays sit just below it).
            stop = ptrs[j + 1] if j < QUOTES_PER_CHAR - 1 else sub
            lines, o = [], p
            while o < stop:
                v = struct.unpack_from('<H', region, o)[0]
                if v:
                    lines.append(cstr(v))
                o += 2
            quotes.append(' '.join(lines).strip())
        out.append(quotes)
    return out


def self_test(quotes):
    """Controls, in the same run as any reported result."""
    ok = True
    flat = [q for c in quotes for q in c]

    print("  shape: %d characters x %d quotes = %d"
          % (len(quotes), QUOTES_PER_CHAR, len(flat)))
    if len(quotes) != IDX_CHARS or any(len(c) != QUOTES_PER_CHAR for c in quotes):
        print("    FAIL -- expected %d x %d" % (IDX_CHARS, QUOTES_PER_CHAR))
        ok = False

    # POSITIVE CONTROL: reproduce phrases confirmed live in the running game.
    joined = '\n'.join(flat)
    missing = [p for p in LIVE_PHRASES if p not in joined]
    print("  live-confirmed phrases found: %d/%d"
          % (len(LIVE_PHRASES) - len(missing), len(LIVE_PHRASES)))
    if missing:
        print("    FAIL -- not reproduced: %r" % missing)
        ok = False

    # Ryu's first quote is known in full from the live trace, across two lines.
    expect = ("Stand up and face me again. I have yet to see your best...")
    if quotes[0][0] != expect:
        print("    FAIL -- multi-line join wrong.\n      got %r\n      want %r"
              % (quotes[0][0], expect))
        ok = False
    else:
        print("  multi-line join: Ryu quote 0 matches the live two-line text")

    # NEGATIVE CONTROL: a decoder that returned padding or binary would show up
    # as empties or non-printable text.
    bad = [q for q in flat if not q or any(ord(c) < 32 or ord(c) > 126 for c in q)]
    print("  empty or non-printable quotes: %d" % len(bad))
    if bad:
        print("    FAIL -- %r" % bad[:3])
        ok = False

    # Distinctness: 8 identical strings would mean the pointers were not varying.
    if len(set(flat)) < len(flat) * 0.9:
        print("    FAIL -- %d distinct of %d; pointers may not be advancing"
              % (len(set(flat)), len(flat)))
        ok = False
    return ok


# Module-level so tools/gen_quote_table.py uses the same path rather than
# keeping a second copy of it that can drift.
DEFAULT_ARC =(r'C:\Program Files (x86)\Steam\steamapps'
               r'\common\CAPCOM FIGHTING COLLECTION 2'
               r'\nativeDX11x64\arc\pc\game_10.arc')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--arc', default=DEFAULT_ARC)
    ap.add_argument('--blob', help='pre-extracted bin/cvs2 instead of --arc')
    ap.add_argument('--char', type=int, help='print one character ID only')
    ap.add_argument('--self-test', action='store_true')
    args = ap.parse_args()

    blob = (open(args.blob, 'rb').read() if args.blob
            else extract_blob(args.arc))
    quotes = decode(load_region(blob))

    print("CONTROLS")
    ok = self_test(quotes)
    if not ok:
        print("\nREFUSING to report: the decoder failed its own controls.")
        return 2
    print()

    if args.char is not None:
        ids = [args.char]
    else:
        ids = range(IDX_CHARS)
    for cid in ids:
        print("=== character ID %d ===" % cid)
        for j, q in enumerate(quotes[cid]):
            print("  [%d] %s" % (j, q))
        print()
    return 0


if __name__ == '__main__':
    sys.exit(main())
