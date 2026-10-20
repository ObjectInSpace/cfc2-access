"""Extract each Project Justice story scene's cast into data/mjus_story/cast.tsv.

Read from the image dump (cfc2_dump/), not the running game. The per-scene
dialogue struct (table 0x1417B3240 English / 0x1417B2F20 Japanese, indexed
by scene ID; both point at the same scripts) starts with the scene script.
Its header, as parsed by 0x140BCBEB4 and 0x140BCBF80:

    bg, A, count1, count1 bytes,
    count2, count2 cast bytes (character IDs; shell ID = cast + 1),
    count3, count3 bytes,
    n0, n0 cast indices   -- side 0's portrait slots, slot k -> cast[list[k]]
    n1, n1 cast indices   -- side 1's

A message record's portrait slot (R+0x2600 + side*0x5C, +0x0C first in the
front-to-back order) indexes the side's list.

Output, one line per scene: SS<TAB>side-0 character IDs<TAB>side-1 IDs,
comma-separated hex, in slot order.

Usage: python tools/extract_mjus_cast.py [--check]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_dump import read_manifest, DEFAULT_DUMP  # noqa: E402

BASE = 0x140000000
TABLE_EN, TABLE_JA = 0x1417B3240, 0x1417B2F20
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   'data', 'mjus_story', 'cast.tsv')


class Image:
    def __init__(self):
        self.secs = [(BASE + s.rva, s.size, open(s.path, 'rb').read()) for s in read_manifest(DEFAULT_DUMP)]

    def rd(self, addr, n):
        for start, size, data in self.secs:
            if start <= addr and addr + n <= start + size:
                return data[addr - start:addr - start + n]
        raise SystemExit('address %#x is not in the dump' % addr)

    def u64(self, addr):
        return int.from_bytes(self.rd(addr, 8), 'little')


def parse(b):
    i = 2                                   # bg, A
    for _ in range(1):                      # count1 list
        i += 1 + b[i]
    c2 = b[i]
    cast = list(b[i + 1:i + 1 + c2])
    i += 1 + c2
    i += 1 + b[i]                           # count3 list
    sides = []
    for _ in range(2):
        n = b[i]
        sides.append(list(b[i + 1:i + 1 + n]))
        i += 1 + n
    return cast, sides


def build():
    img = Image()
    lines = []
    for sc in range(100):
        script = img.u64(img.u64(TABLE_EN + sc * 8))
        if script != img.u64(img.u64(TABLE_JA + sc * 8)):
            raise SystemExit('scene %d: the language tables point at different scripts' % sc)
        cast, sides = parse(img.rd(script, 96))
        out = []
        for side in sides:
            if len(side) > 8 or any(k >= len(cast) for k in side):
                raise SystemExit('scene %d: a slot outside its cast %r %r' % (sc, cast, side))
            out.append(','.join('%x' % cast[k] for k in side))
        lines.append('%02d\t%s\t%s' % (sc, out[0], out[1]))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    text = build()
    if '--check' in sys.argv:
        cur = open(OUT, encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if cur != text:
            raise SystemExit('cast.tsv differs from the dump')
        print('cast.tsv matches the dump')
    else:
        with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        print('wrote', OUT)
