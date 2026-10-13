"""Controls for the character-name extractor; reads the image dump only."""
import json
import re
import unittest
from pathlib import Path

import extract_roster_names as ex
from collection_lifecycle_audit import Dump, DEFAULT_DUMP

ROOT = Path(__file__).resolve().parents[1]
ROSTERS = 0x1412048C0     # move-list roster lists, read at 0x14007FFA0


class Mutated:
    """A dump whose bytes at one address are replaced."""

    def __init__(self, dump, address, raw):
        self.dump, self.address, self.raw = dump, address, raw

    def read(self, address, size):
        data = bytearray(self.dump.read(address, size))
        for i, b in enumerate(self.raw):
            if address <= self.address + i < address + size:
                data[self.address + i - address] = b
        return bytes(data)


class ExtractorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dump = Dump(DEFAULT_DUMP)
        cls.data = ex.extract(cls.dump)
        cls.games = {g['tag']: g for g in cls.data['games']}

    def test_cvs2_positive_control_against_shipped_names(self):
        """CVS2's names come from independent prior art, partly checked live.
        The only differences are the three deliberately region-neutral labels
        and one spelling; each is pinned so a new difference fails."""
        src = (ROOT / 'src' / 'cvs2_select.c').read_text(encoding='utf-8')
        shipped = re.findall(r'L"([^"]+)"', src[src.index('names[54]'):src.index('stage_names')])[:48]
        norm = lambda s: s.replace('. ', '.').replace(' ', '').lower()
        differs = {i: (s, tuple(self.games['CVS2']['names'][i])) for i, s in enumerate(shipped)
                   if norm(s) not in map(norm, self.games['CVS2']['names'][i])}
        self.assertEqual(differs, {
            8: ('Claw', ('Balrog', 'Vega')), 10: ('Dictator', ('Vega', 'M.Bison')),
            32: ('Boxer', ('M.Bison', 'Balrog')), 40: ('Todoh', ('Todo', 'Todo'))})

    def test_every_roster_id_is_named_in_every_game(self):
        for g in self.data['games']:
            pointer = int.from_bytes(self.dump.read(ROSTERS + 8 * g['game'], 8), 'little')
            ids = list(self.dump.read(pointer + 1, self.dump.read(pointer, 1)[0]))
            for native in ids:
                n = native + 1 if g['tag'] == 'Z3U' and native >= 0x26 else native
                self.assertLess(n, g['limit'], f"{g['tag']} {native}")
                self.assertTrue(any(g['names'][n]), f"{g['tag']} {native}")

    def test_cvs1_regional_names_and_bounds(self):
        cvs1 = self.games['CVS1']['names']
        self.assertEqual(len(cvs1), 0x23)
        self.assertEqual(cvs1[13], ['Gouki', 'Akuma'])
        self.assertEqual([cvs1[8], cvs1[10], cvs1[32]],
                         [['Balrog', 'Vega'], ['Vega', 'M.Bison'], ['M.Bison', 'Balrog']])
        self.assertEqual(cvs1[:2], [['Ryu', 'Ryu'], ['Ken', 'Ken']])

    def test_changed_case_bytes_refuse(self):
        with self.assertRaisesRegex(ValueError, 'CFJ: case bytes differ'):
            ex.extract(Mutated(self.dump, 0x14004B4D1, b'\x83\xff\x2f'))   # limit 0x2E -> 0x2F

    def test_changed_jump_table_refuses(self):
        with self.assertRaisesRegex(ValueError, 'jump table'):
            ex.extract(Mutated(self.dump, ex.JUMP_TABLE + 4 * 7, b'\x00\x00\x00\x00'))

    def test_generated_outputs_are_current(self):
        self.assertEqual(ex.JSON_OUT.read_text(encoding='utf-8'),
                         json.dumps(self.data, indent=1, ensure_ascii=True) + '\n')
        self.assertEqual(ex.HEADER_OUT.read_text(encoding='utf-8'), ex.header(self.data))


if __name__ == '__main__':
    unittest.main()
