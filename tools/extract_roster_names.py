"""Extract every game's character names, keyed by native ID, from the CFC2 image.

Source: the shell's own name function 0x14004B2F0(ctx, game, variant, id),
which the pause-menu move list calls with the current fighter's native ID
(0x140082241). It switches on the game (jump table 0x14004B5A0) and reads a
table of C-string pointers. Traced in docs/port-tasks/character-names.md.

- variant is the shell's selected game version, [*0x141C14F90 + 0x1B0],
  stored at move-list setup 0x14007FF39. Two-variant tables hold the
  Japanese-ROM name first and the US-ROM name second (Vega/M.Bison/Balrog).
- An ID at or above a game's limit yields the empty string: no name.
- CVS1 alone prefixes "Ex " when the ID's high byte is set; selection bytes
  carry no high byte, so that prefix is recorded, not applied.
- Project Justice maps ID 0x12 to ID 0 when variant != 0 (0x14004B522).
- The move-list CALLER adds 1 to Alpha 3 IDs >= 0x26 before calling; that is a
  caller convention, recorded per game, not applied to the table.

Every byte the extraction depends on is checked first; a mismatch refuses.
Writes data/character_names.json and src/character_name_table.h.

Usage: python tools/extract_roster_names.py [--dump DIR] [--check]
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

from collection_lifecycle_audit import Dump, DEFAULT_DUMP

ROOT = Path(__file__).resolve().parents[1]
JSON_OUT = ROOT / 'data' / 'character_names.json'
HEADER_OUT = ROOT / 'src' / 'character_name_table.h'

NAME_FN = 0x14004B2F0
NAME_FN_END = 0x14004B56A
JUMP_TABLE = 0x14004B5A0
EMPTY_STRING = 0x140FDE6E8   # lea rax,[rip+0xf93198] at 0x14004B549
EX_PREFIX = 0x140FE169C      # "Ex ", CVS1 high-byte branch

# (game, tag, case address, limit, table, variants, quirks), each re-checked
# against the instruction bytes at its case address.
CASES = [
    (0, 'CVS1', 0x14004B340, 0x23, 0x1411F6640, 2,
     bytes.fromhex('83ff23'), 'Ex prefix when id>>8 != 0'),
    (1, 'CVS2', 0x14004B4BA, 0x30, 0x1411F6640, 2,
     bytes.fromhex('83ff300f8386000000498d047e498b84c440661f01'), ''),
    (2, 'CFJ', 0x14004B4D1, 0x2E, 0x1411F6940, 2,
     bytes.fromhex('83ff2e7373498d047e498b84c440691f01'), ''),
    (3, 'Z3U', 0x14004B4E4, 0x2B, 0x1411F6C20, 2,
     bytes.fromhex('83ff2b7360498d047e498b84c4206c1f01'),
     'move-list caller adds 1 to ids >= 0x26'),
    (4, 'PWS1', 0x14004B4F7, 0x0B, 0x1411F6ED0, 2,
     bytes.fromhex('83ff0b734d498d047e498b84c4d06e1f01'), ''),
    (5, 'PWS2', 0x14004B50A, 0x10, 0x1411F6F80, 2,
     bytes.fromhex('83ff10733a498d047e498b84c4806f1f01'), ''),
    (6, 'MJUS', 0x14004B51D, 0x42, 0x1411F7080, 1,
     bytes.fromhex('83ff42732783ff1275074585c0410f45ff8bc7498b84c480701f01'),
     'id 0x12 reads id 0 when variant != 0'),
    (7, 'STG2', 0x14004B53A, 0x1D, 0x1411F7290, 1,
     bytes.fromhex('83ff1d730a498b84fc90721f01'), ''),
]
# CVS1's case falls through to the shared read at 0x14004B485.
CVS1_READ = (0x14004B485, bytes.fromhex('498d147e498b94d440661f01'))


def u64(dump, address):
    return int.from_bytes(dump.read(address, 8), 'little')


def cstring(dump, address, limit=64):
    raw = dump.read(address, limit)
    if b'\0' not in raw:
        raise ValueError(f'Unterminated name at {address:#x}')
    text = raw.split(b'\0', 1)[0]
    try:
        return text.decode('ascii')
    except UnicodeDecodeError:
        raise ValueError(f'Non-ASCII name at {address:#x}: {text!r}')


def verify(dump):
    """Refuse unless every instruction the extraction relies on matches."""
    if dump.read(NAME_FN, 16) != bytes.fromhex('48895c2410488974241848897c242041'):
        raise ValueError('Name function prologue differs')
    for game, tag, case, *_ , expected, _quirk in CASES:
        target = 0x140000000 + int.from_bytes(dump.read(JUMP_TABLE + 4 * game, 4), 'little')
        if target != case:
            raise ValueError(f'{tag}: jump table points at {target:#x}, not {case:#x}')
        if dump.read(case, len(expected)) != expected:
            raise ValueError(f'{tag}: case bytes differ at {case:#x}')
    address, expected = CVS1_READ
    if dump.read(address, len(expected)) != expected:
        raise ValueError('CVS1 shared read differs')
    if dump.read(EMPTY_STRING, 1) != b'\0' or cstring(dump, EX_PREFIX) != 'Ex ':
        raise ValueError('Fallback/prefix strings differ')
    return hashlib.sha256(dump.read(NAME_FN, NAME_FN_END - NAME_FN)).hexdigest()


def extract(dump):
    fn_sha = verify(dump)
    games = []
    for game, tag, case, limit, table, variants, _expected, quirk in CASES:
        names = []
        for native in range(limit):
            row = []
            for v in range(variants):
                pointer = u64(dump, table + 8 * (native * variants + v))
                row.append(cstring(dump, pointer) if pointer else '')
            names.append(row)
        games.append(dict(game=game, tag=tag, limit=limit, variants=variants,
                          table=hex(table), case=hex(case), quirk=quirk, names=names))
    return dict(schema=1, source='shell name function 0x14004B2F0', name_function_sha256=fn_sha,
                jump_table_sha256=hashlib.sha256(dump.read(JUMP_TABLE, 32)).hexdigest(),
                variant_source='[*0x141C14F90 + 0x1B0], the shell game-version setting',
                variant_order=['Japanese ROM', 'US ROM'], games=games)


def c_escape(text):
    return text.replace('\\', '\\\\').replace('"', '\\"')


def header(data):
    lines = ['/* GENERATED by tools/extract_roster_names.py -- do not edit.',
             f" * Name function SHA-256 {data['name_function_sha256']}.",
             ' * Native ID -> name, per game, from the shell\'s own move-list name',
             ' * function 0x14004B2F0. See docs/port-tasks/character-names.md. */',
             '#ifndef CFC2_CHARACTER_NAME_TABLE_H', '#define CFC2_CHARACTER_NAME_TABLE_H', '']
    seen = {}
    for g in data['games']:
        key = g['table']
        if key in seen:
            continue
        rows = max(x['limit'] for x in data['games'] if x['table'] == key)
        src = next(x for x in data['games'] if x['table'] == key and x['limit'] == rows)
        seen[key] = f"character_names_{src['tag'].lower()}"
        lines.append(f"static const char *const {seen[key]}[{rows * src['variants']}] = {{")
        for native, row in enumerate(src['names']):
            cells = ', '.join(f'"{c_escape(n)}"' for n in row)
            lines.append(f'    {cells}, /* {native} */')
        lines.append('};')
    lines += ['', 'typedef struct {', '    unsigned limit, variants;',
              '    const char *const *names;   /* names[id * variants + variant] */',
              '} character_name_table;', '',
              'static const character_name_table character_name_tables[8] = {']
    for g in data['games']:
        lines.append(f"    {{ {g['limit']:#x}, {g['variants']}, {seen[g['table']]} }}, /* {g['tag']} */")
    lines += ['};', '',
              '/* The name the shell shows, or NULL: unknown game/id/variant or an empty',
              ' * entry. Mirrors 0x14004B2F0, including the MJUS 0x12 remap; the CVS1',
              ' * "Ex " prefix and the Alpha 3 caller +1 are the caller\'s concern. */',
              'static inline const char *character_name(unsigned game, unsigned id, unsigned variant)',
              '{',
              '    const character_name_table *t;',
              '    if (game >= 8 || variant > 1) return 0;',
              '    t = &character_name_tables[game];',
              '    if (id >= t->limit) return 0;',
              '    if (game == 6 && id == 0x12 && variant) id = 0;',
              '    if (t->variants == 1) variant = 0;',
              '    return t->names[id * t->variants + variant][0] ? t->names[id * t->variants + variant] : 0;',
              '}', '', '#endif', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump', type=Path, default=DEFAULT_DUMP)
    parser.add_argument('--check', action='store_true', help='Fail if outputs are stale')
    args = parser.parse_args()
    data = extract(Dump(args.dump))
    text_json = json.dumps(data, indent=1, ensure_ascii=True) + '\n'
    text_h = header(data)
    if args.check:
        stale = [p for p, t in ((JSON_OUT, text_json), (HEADER_OUT, text_h))
                 if not p.exists() or p.read_text(encoding='utf-8') != t]
        if stale:
            sys.exit('Stale: ' + ', '.join(map(str, stale)))
        print('Character names are current.')
        return
    JSON_OUT.write_text(text_json, encoding='utf-8')
    HEADER_OUT.write_text(text_h, encoding='utf-8', newline='\n')
    for g in data['games']:
        named = sum(1 for row in g['names'] if any(row))
        print(f"{g['tag']}: {named} of {g['limit']} IDs named, {g['variants']} variant(s)")


if __name__ == '__main__':
    main()
