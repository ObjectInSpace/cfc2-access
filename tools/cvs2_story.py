"""Decode CVS2's ENDING / story text from the SHIPPED archive -- no game launch.

WHAT THIS IS, AND WHY IT IS DIFFERENT FROM WIN QUOTES
-----------------------------------------------------
Win quotes (tools/cvs2_quotes.py) are 8 per character, chosen at RANDOM at
display time, so knowing the character gives a 1-in-8 guess and the text is not
safely speakable from the ID alone.

⭐⭐ ENDINGS ARE ONE PER CHARACTER (user-confirmed). So character ID fully
determines the text, and the selection reader ALREADY validates that ID. There
is no runtime-selection unknown here -- that is what makes endings shippable
where quotes are not.

THE REGION: file offset 0x204040 in bin/cvs2 (extracted from game_10.arc).
It sits immediately after the quote region and uses the SAME two-level shape:

    +0x0000  u32  0x3EAC -- offset of the entry index
    +0x0008  per-line width/advance bytes (01/02/03)
    +0x04B8  text: NUL-terminated lines
    +0x3EAC  256 u32 -- entry -> an array of u32 LINE offsets

An entry's line array runs until the NEXT entry's array begins, so entries are
variable length (1-3 lines). 250 of the 256 entries carry text.

⚠ THIS TABLE IS NOT ONLY ENDINGS. It is the whole story script:
    0..47    boss/pre-fight dialogue and the Osaka news interlude
    48..191  THE 48 CHARACTER ENDINGS, exactly 3 entries each
    192..239 pre-final-match lines, one per character
    240..249 the closing ceremony
Only 48..191 is keyed by character ID. Do not speak the rest by ID.

⭐ THE MAPPING IS DERIVED, NOT ASSUMED: endings start at index 48 and run 3
entries per character in the SAME ID order the shipping selection reader uses
(src/cvs2_select.c). Ryu 48-50, Ken 51-53, Chun-Li 54-56 ... Rolento 189-191.
Sagat lands at 75-77 = ID 9, which is the ID the user identified live.

⚠ 12 of the 48 endings do not contain the character's roster name, and that is
CORRECT, not a misalignment: Claw's text says "Vega", Dictator's says "Bison",
Kyo's is Shingo addressing "Mr. Kusanagi", Mai's is about Andy, Haohmaru's says
"the samurai", Cammy's is the Shadaloo doll monologue. The control below
therefore requires a NAME MATCH RATE, plus exact anchors on characters whose ID
is independently live-verified -- a rate alone would pass a shifted table.

⚠ Entries 243-245 contain 0x81-prefixed placeholder bytes where the game
substitutes the winning team's names at runtime. They decode as replacement
characters; they are not corruption.
"""
import argparse
import struct
import sys
import zlib

REGION_OFF = 0x204040
REGION_LEN = 0x42AC
IDX_OFF = 0x3EAC
N_ENTRIES = 256

ENDINGS_FIRST = 48          # entry index of Ryu's first ending line
ENTRIES_PER_CHAR = 3
N_CHARS = 48

# The shipping roster order (src/cvs2_select.c). Endings are in THIS order.
NAMES = [
    "Ryu", "Ken", "Chun-Li", "Guile", "Zangief", "Dhalsim",
    "E. Honda", "Blanka", "Claw", "Sagat", "Dictator", "Sakura",
    "Cammy", "Akuma", "Morrigan", "Evil Ryu", "Kyo", "Iori",
    "Terry", "Ryo", "Mai", "Kim", "Geese", "Yamazaki",
    "Raiden", "Rugal", "Vice", "Benimaru", "Yuri", "King",
    "Nakoruru", "Orochi Iori", "Boxer", "Dan", "Joe", "Eagle",
    "Maki", "Kyosuke", "Athena", "Chang", "Todoh", "Rock",
    "Hibiki", "Haohmaru", "Yun", "Shin Akuma", "Ultimate Rugal", "Rolento",
]

# ⭐ ANCHORS: characters whose ID is independently verified live (Ryu 0, Sagat 9,
# Yun 44 -- see docs/cvs2-state-access-trace.md), plus the two table endpoints.
# A uniform shift would keep the name-match RATE high while breaking these.
ANCHORS = {
    0:  "Utilizing skill and power",
    9:  "emperor of Muay Thai",
    44: "rebuild Osaka City",
    47: "Rolento is upset",
}


def extract_blob(arc_path):
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


def decode_entries(blob):
    """-> list of 256 entries, each a list of lines (possibly empty)."""
    if blob[:4] != b'IBIS':
        raise ValueError("expected an IBIS container, got %r" % blob[:4])
    r = blob[REGION_OFF:REGION_OFF + REGION_LEN]
    if len(r) != REGION_LEN:
        raise ValueError("region truncated: %d bytes" % len(r))
    if struct.unpack_from('<I', r, 0)[0] != IDX_OFF:
        raise ValueError("unexpected index offset")

    idx = [struct.unpack_from('<I', r, IDX_OFF + i * 4)[0]
           for i in range(N_ENTRIES)]

    def cstr(o):
        e = r.find(b'\0', o)
        return r[o:e].decode('ascii', 'replace')

    out = []
    for i in range(N_ENTRIES):
        s = idx[i]
        if not 0 < s < IDX_OFF:
            out.append([])
            continue
        nxt = idx[i + 1] if i + 1 < N_ENTRIES else 0
        stop = nxt if 0 < nxt < IDX_OFF and nxt > s else s + 8
        lines = []
        o = s
        while o < stop and len(lines) < 12:
            v = struct.unpack_from('<I', r, o)[0]
            if not 0 < v < IDX_OFF:
                break
            lines.append(cstr(v))
            o += 4
        out.append(lines)
    return out


def ending(entries, cid):
    """The full ending text for one character ID, lines joined."""
    if not 0 <= cid < N_CHARS:
        raise IndexError(cid)
    s = ENDINGS_FIRST + cid * ENTRIES_PER_CHAR
    lines = []
    for e in range(s, s + ENTRIES_PER_CHAR):
        lines.extend(entries[e])
    return ' '.join(x.strip() for x in lines if x.strip())


def self_test(entries):
    ok = True
    live = sum(1 for e in entries if e)
    print("  entries with text: %d of %d" % (live, N_ENTRIES))
    if live < 240:
        print("    FAIL -- expected ~250 populated entries")
        ok = False

    texts = [ending(entries, c) for c in range(N_CHARS)]
    empty = [c for c, t in enumerate(texts) if len(t) < 30]
    print("  endings decoded: %d, too-short: %d" % (N_CHARS, len(empty)))
    if empty:
        print("    FAIL -- short/empty endings for IDs %r" % empty[:6])
        ok = False

    # ANCHORS: exact phrases on live-verified IDs. These break under any shift.
    bad = [(c, p) for c, p in ANCHORS.items() if p.lower() not in texts[c].lower()]
    print("  live-verified ID anchors: %d/%d" % (len(ANCHORS) - len(bad), len(ANCHORS)))
    if bad:
        for c, p in bad:
            print("    FAIL -- ID %d (%s) should contain %r\n      got: %s"
                  % (c, NAMES[c], p, texts[c][:70]))
        ok = False

    # RATE: most endings name their own character. 12 legitimately do not.
    def key(n):
        return 'Honda' if n.startswith('E.') else n.split()[-1]
    hits = sum(1 for c, t in enumerate(texts) if key(NAMES[c]).lower() in t.lower())
    print("  endings naming their own character: %d/%d" % (hits, N_CHARS))
    if hits < 30:
        print("    FAIL -- too few; the table is probably misaligned")
        ok = False

    # Distinctness: a stuck pointer would repeat one ending.
    if len(set(texts)) != N_CHARS:
        print("    FAIL -- only %d distinct endings" % len(set(texts)))
        ok = False
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--arc', default=r'C:\Program Files (x86)\Steam\steamapps'
                                     r'\common\CAPCOM FIGHTING COLLECTION 2'
                                     r'\nativeDX11x64\arc\pc\game_10.arc')
    ap.add_argument('--blob')
    ap.add_argument('--char', type=int, help='one character ID only')
    ap.add_argument('--raw', action='store_true',
                    help='dump all 256 entries, not just the endings')
    args = ap.parse_args()

    blob = (open(args.blob, 'rb').read() if args.blob
            else extract_blob(args.arc))
    entries = decode_entries(blob)

    print("CONTROLS")
    if not self_test(entries):
        print("\nREFUSING to report: the decoder failed its own controls.")
        return 2
    print()

    if args.raw:
        for i, e in enumerate(entries):
            if e:
                print("[%3d] %s" % (i, ' / '.join(e)))
        return 0

    ids = [args.char] if args.char is not None else range(N_CHARS)
    for cid in ids:
        print("=== ID %d  %s ===" % (cid, NAMES[cid]))
        print("  %s" % ending(entries, cid))
        print()
    return 0


if __name__ == '__main__':
    sys.exit(main())
