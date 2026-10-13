"""Synthetic controls for cvs1_name_capture; never attaches to the game."""
import json
import struct
import tempfile
import unittest
from pathlib import Path

from PIL import Image

import cvs1_name_capture as names
from test_cvs1_selection_capture import SelectionMemory


class Keys:
    def __init__(self):
        self.down = False

    def __call__(self, vk):
        return self.down and vk == names.SNAPSHOT_KEY


class NameCaptureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lifecycle = json.loads(names.metadata.MANIFEST.read_bytes())

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.memory = SelectionMemory(self.lifecycle)
        self.memory.data[0x400000] = bytes(range(256)) * (names.PRIMARY_SIZE // 256) + \
            bytes(names.PRIMARY_SIZE % 256)
        self.keys, self.said = Keys(), []
        self.image = Image.new('RGB', (32, 24), (200, 40, 40))
        self.session = names.Session(self.memory.read, self.lifecycle, self.tmp.name,
                                     lambda: self.image, self.keys, self.said.append,
                                     ['first state', 'second state'])

    def tearDown(self):
        self.session.close()
        self.tmp.cleanup()

    def select(self, side, native_id, slot=0):
        phase = bytearray(self.memory.data[0x403E58])
        phase[0xA0 + side] = slot
        phase[0x78 + 4 * side + slot] = native_id & 0xFF
        self.memory.data[0x403E58] = bytes(phase)

    def rows(self, kind):
        return [r for r in self.session.rows if r['kind'] == kind]

    def test_selection_pictured_once_after_settling(self):
        self.session.poll()
        self.assertEqual(self.rows('selection'), [], 'first sighting waits one poll')
        self.session.poll()
        shots = self.rows('selection')
        self.assertEqual(sorted((r['side'], r['native_id']) for r in shots), [(0, 9), (1, 9)])
        self.assertTrue(all(r['image_status'] == 'ok' for r in shots))
        self.assertTrue((Path(self.tmp.name) / shots[0]['image']).exists())
        self.session.poll()
        self.assertEqual(len(self.rows('selection')), 2, 'an unchanged ID is not re-pictured')
        self.select(0, 16)
        self.session.poll()
        self.assertEqual(len(self.rows('selection')), 2, 'a new ID also waits one poll')
        self.session.poll()
        self.assertEqual(self.rows('selection')[-1]['native_id'], 16)

    def test_no_selection_byte_is_never_pictured(self):
        self.select(0, 0xFF)
        self.select(1, 0xFF)
        for _ in range(3):
            self.session.poll()
        self.assertEqual(self.rows('selection'), [])

    def test_snapshot_on_key_edge_with_plan_label(self):
        self.session.poll()
        self.keys.down = True
        self.session.poll()
        self.session.poll()                       # held: no repeat
        snaps = self.rows('snapshot')
        self.assertEqual(len(snaps), 1)
        self.assertEqual(snaps[0]['status'], 'read')
        self.assertEqual(snaps[0]['label'], 'first state')
        block = (Path(self.tmp.name) / snaps[0]['block']).read_bytes()
        self.assertEqual(block, self.memory.data[0x400000])
        self.assertIn('Next: second state', self.said[-1])
        self.keys.down = False
        self.session.poll()
        self.keys.down = True
        self.session.poll()
        self.assertEqual(self.rows('snapshot')[-1]['label'], 'second state')
        self.assertIn('Plan complete', self.said[-1])

    def test_snapshot_refused_when_root_moves_during_read(self):
        read = self.memory.read
        def moving(address, size):
            raw = read(address, size)
            if address == 0x400000:              # after the block read, the slot changes
                self.memory.data[names.selection.ROOT_SLOT] = struct.pack('<Q', 0x900000)
            return raw
        self.session.read = moving
        self.keys.down = True
        self.session.poll()
        snap = self.rows('snapshot')[-1]
        self.assertTrue(snap['status'].startswith('refused: root changed'))
        self.assertNotIn('block', snap)
        self.assertEqual(self.session.snapshots, 0, 'a refused snapshot does not consume the plan')

    def test_snapshot_refused_outside_cvs1(self):
        header = bytearray(self.memory.data[0x100000])
        struct.pack_into('<I', header, 8, 1)       # game 1 = CVS2
        self.memory.data[0x100000] = bytes(header)
        self.keys.down = True
        self.session.poll()
        self.assertTrue(self.rows('snapshot')[-1]['status'].startswith('refused'))
        self.assertEqual(self.rows('selection'), [])

    def test_black_frame_and_missing_window_are_reported(self):
        self.image = Image.new('RGB', (32, 24), (0, 0, 0))
        self.session.poll(); self.session.poll()
        self.assertTrue(self.rows('selection')[0]['image_status'].startswith('black frame'))
        self.assertEqual(sum('unusable' in x for x in self.said), 1, 'warned once, by voice')
        self.session.grab = lambda: None
        self.select(0, 16)
        self.session.poll(); self.session.poll()
        self.assertEqual(self.rows('selection')[-1]['image_status'], 'no game window found')


if __name__ == '__main__':
    unittest.main()
