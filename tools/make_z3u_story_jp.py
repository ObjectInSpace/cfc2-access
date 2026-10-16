"""Generate src/z3u_story_jp.h: Alpha 3's Japanese pre-fight story text.

The Japanese version draws each story line from glyph numbers, not text
(docs/port-tasks/game-text.md, "Alpha 3 Japanese story"):
  - message object +0x38 points at the entry (0x1408A92DF: [context+8] +
    0xA390B4 + line*0x4A); the US version's ASCII table is 0x800 later;
  - an entry is: character count, then per line an indent word, glyph words,
    a 0 (drawer 0x1408849FD); two lines per entry;
  - glyph g is cell g & 0xFF of the story file's own 256x256 PVR sheet,
    16 px cells (0x140884B80); bit 15 only narrows the advance.
Each character's story file (AFS files 0x7E..0xA1 of game_30.arc) holds the
entries, the English table and the sheet. The sheets were transcribed by eye
(tools/z3u_story_sheets.py) and checked: every row 16 cells, each sheet's
cell count equal to its story's highest glyph, and the decoded Japanese read
against the English line it pairs with.

Output: for each distinct entry, its 0x4A raw bytes as 37 words (the key the
reader matches in memory) and its Japanese text.

Usage: python tools/make_z3u_story_jp.py [--check]
"""
import argparse
from pathlib import Path
import struct
import sys

from gmd import read_arc, extract
from z3u_story_sheets import SHEETS, ALIAS

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / 'src' / 'z3u_story_jp.h'
ARC = Path(r'C:\Program Files (x86)\Steam\steamapps\common\CAPCOM FIGHTING COLLECTION 2'
           r'\nativeDX11x64\arc\pc\game_30.arc')
FILES = range(0x7E, 0xA2)
ENTRY = 0x4A
WORDS = ENTRY // 2
US_PREFIX = 16      # bytes of the paired English line that disambiguate


def afs_files():
    data, entries = read_arc(ARC)
    blob = extract(data, entries[0])
    a = blob.find(b'AFS\x00')
    n = struct.unpack_from('<I', blob, a + 4)[0]
    for k in FILES:
        if k >= n:
            continue
        o, s = struct.unpack_from('<II', blob, a + 8 + 8 * k)
        yield k, blob[a + o:a + o + s]


def entries(f):
    """The Japanese table: the English table sits 0x74A bytes before the end
    of the first section (header +4 = its size, data from 0x800), and the
    Japanese one 0x800 before that, from its count word (-2)."""
    size1 = struct.unpack_from('<I', f, 4)[0]
    us = 0x800 + size1 - 0x74A
    jp = us - 0x800 - 2
    for i in range(0x800 // ENTRY):
        # A story ends where its English pair does: past it the table holds
        # stale data (file 0x88 decoded one such entry two ways).
        e = f[us + i * 0x45: us + (i + 1) * 0x45]
        if not e or not (0x20 <= e[0] < 0x7F):
            break
        raw = f[jp + i * ENTRY: jp + (i + 1) * ENTRY]
        words = struct.unpack('<%dH' % WORDS, raw)
        if not any(words[1:]):
            break
        yield i, words, bytes(e[:US_PREFIX])


def lines(words):
    out, i = [], 2                    # skip the count and the first indent
    for _ in range(2):
        cur = []
        while i < len(words) and words[i]:
            cur.append(words[i])
            i += 1
        i += 2                        # the 0 and the next indent
        out.append(cur)
    return out


def build():
    table = {}
    for k, f in afs_files():
        name = f'{k:02x}'
        rows = SHEETS[ALIAS.get(name, name)]
        if any(len(r) != 16 for r in rows[:-1]):
            raise SystemExit(f'{name}: a sheet row is not 16 cells')
        sheet = ''.join(rows)
        for line, words, us in entries(f):
            glyphs = [g & 0xFF for l in lines(words) for g in l]
            if glyphs and max(glyphs) >= len(sheet):
                raise SystemExit(f'{name}: glyph {max(glyphs)} past the transcription')
            text = ''.join(''.join(sheet[g & 0xFF] for g in l) for l in lines(words))
            text = ' '.join(text.replace('\u3000', ' ').split())
            if not text:
                continue
            # Characters' numbering restarts at 1, so identical glyph words
            # occur in different stories (Chun-Li's and Bison's first lines);
            # the paired English line, at entry + 0x802 - 5*line in the same
            # loaded file, tells them apart.
            key = (words, line, us)
            if key in table and table[key][0] != text:
                raise SystemExit(f'{name}: one entry, two texts')
            # The story files are in fighter order, the alternate copy's
            # fallback key: file 0x7E + native ID up to 18 (Ryu 0x7E ... the
            # second Chun-Li 0x90); native 19 (the name table's second Gen)
            # has none, so from 20 it is 0x7D + ID. Checked by each file's
            # English subject against the name table, and live: the select
            # reader named fighter 23 E.Ryu, whose story ("There is an evil
            # energy...") is file 0x94.
            fighter = k - 0x7E if k <= 0x90 else k - 0x7D
            mask = table.get(key, (text, 0))[1] | (1 << fighter)
            table[key] = (text, mask)
    rows = []
    for (words, line, us), (text, mask) in sorted(table.items()):
        # The English prefix as bytes, so a short line's NUL is kept exactly.
        usc = ', '.join(f'0x{b:02x}' for b in us)
        esc = ''.join(c if ord(c) < 0x80 and c not in '\\"' else f'\\x{ord(c):04x}" L"' for c in text)
        rows.append('    { { ' + ', '.join(f'0x{w:x}' for w in words) + ' },\n'
                    f'      {line}, {{ {usc} }}, 0x{mask:x}ull,\n'
                    '      L"' + esc + '" },')
    return ('/* Generated by tools/make_z3u_story_jp.py from game_30.arc and\n'
            ' * tools/z3u_story_sheets.py -- do not edit. Alpha 3\'s Japanese\n'
            ' * pre-fight story lines, keyed by their raw 0x4A-byte entries and\n'
            ' * the English line paired with them. */\n'
            '#ifndef CFC2_Z3U_STORY_JP_H\n#define CFC2_Z3U_STORY_JP_H\n'
            f'#define Z3U_STORY_JP_WORDS {WORDS}\n'
            f'#define Z3U_STORY_JP_COUNT {len(rows)}\n'
            f'#define Z3U_STORY_JP_US {US_PREFIX}\n'
            'static const struct {\n'
            '    unsigned short key[Z3U_STORY_JP_WORDS]; /* the raw entry           */\n'
            '    unsigned char line;                     /* English pair at entry + 0x802 - 5*line */\n'
            '    unsigned char us[Z3U_STORY_JP_US];      /* its first bytes         */\n'
            '    unsigned long long fighters;            /* bit n: native fighter n */\n'
            '    const wchar_t *text;\n'
            '} z3u_story_jp[Z3U_STORY_JP_COUNT] = {\n' + '\n'.join(rows) + '\n};\n#endif\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    text = build()
    if args.check:
        if not OUT.exists() or OUT.read_text(encoding='utf-8') != text:
            sys.exit(f'Stale: {OUT}')
        print('z3u_story_jp.h is current.')
        return
    OUT.write_text(text, encoding='utf-8', newline='\n')
    print('Wrote', OUT)


if __name__ == '__main__':
    main()
