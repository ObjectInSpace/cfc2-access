"""Synthetic controls for collection_state_capture; never attaches to the game."""
import json
import struct
import tempfile
import unittest
from pathlib import Path

from PIL import Image

import collection_state_capture as cap
from test_collection_metadata_capture import Memory
from test_cvs1_selection_capture import SelectionMemory

ROOT = 0x400000


class Keys:
    def __init__(self):
        self.down = set()

    def __call__(self, vk):
        return vk in self.down


class Clock:
    def __init__(self):
        self.now = 100.0

    def __call__(self):
        return self.now


def block(size):
    return bytes(i * 7 % 251 for i in range(size))


class Harness:
    def __init__(self, test, memory, game, plan=('first state', 'second state')):
        self.tmp = tempfile.TemporaryDirectory()
        test.addCleanup(self.tmp.cleanup)
        self.memory, self.keys, self.pad, self.clock, self.said = memory, Keys(), set(), Clock(), []
        self.image = Image.new('RGB', (32, 24), (200, 40, 40))
        self.cap = cap.Capture(memory.read, test.lifecycle, game, self.tmp.name,
                               lambda: self.image, self.keys, lambda: set(self.pad),
                               self.said.append, list(plan), clock=self.clock)
        test.addCleanup(self.cap.close)

    def poll(self, n=1, dt=0.05):
        for _ in range(n):
            self.cap.poll()
            self.clock.now += dt

    def rows(self, kind):
        return [r for r in self.cap.rows if r['kind'] == kind]


class StateCaptureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lifecycle = json.loads(cap.metadata.MANIFEST.read_bytes())

    def cvs1(self):
        memory = SelectionMemory(self.lifecycle)
        memory.data[ROOT] = block(0x32AB0)
        return Harness(self, memory, 'cvs1')

    def cfj(self):
        memory = Memory(self.lifecycle, game=2, rom=4, name=b'cfj\0')
        path = cap.game_paths(self.lifecycle)['cfj']
        header = bytearray(memory.data[0x100000])
        for offset, row in path['callbacks'].items():
            struct.pack_into('<Q', header, int(offset, 16), int(row['target'], 16))
        memory.data[0x100000] = bytes(header)
        memory.data[0x1001B0] = struct.pack('<QIIQII', ROOT, 0x26E80, 0, ROOT + 0x26E80, 0x26E88, 0)
        memory.data[ROOT] = block(0x26E80)
        return Harness(self, memory, 'cfj')

    def set_byte(self, h, offset, value):
        raw = bytearray(h.memory.data[ROOT])
        raw[offset] = value
        h.memory.data[ROOT] = bytes(raw)

    # ---- identity ----------------------------------------------------------

    def test_identity_per_path_from_manifest(self):
        h = self.cfj()
        root, size, info = cap.identify(h.memory.read, self.lifecycle, cap.game_paths(self.lifecycle)['cfj'])
        self.assertEqual((root, size, info['rom_name']), (ROOT, 0x26E80, 'cfj'))
        root, _, reason = cap.identify(h.memory.read, self.lifecycle, cap.game_paths(self.lifecycle)['cvs1'])
        self.assertIsNone(root, 'another path never matches CFJ state')
        self.assertIn('CVS1', reason)

    def test_wrong_size_refused(self):
        h = self.cfj()
        h.memory.data[0x1001B0] = struct.pack('<QIIQII', ROOT, 0x32AB0, 0, 0, 0, 0)
        h.poll(2)
        self.assertEqual(h.cap.trace.samples, 0)
        self.assertFalse(h.rows('status')[0]['active'])

    # ---- trace -------------------------------------------------------------

    def test_trace_keyframe_then_deltas_and_segments(self):
        h = self.cfj()
        h.poll()
        self.set_byte(h, 0x1234, 99)
        h.poll()
        h.poll()                                  # unchanged
        h.memory.data[0x1001B0] = struct.pack('<QIIQII', ROOT + 0x100000, 0x26E80, 0, 0, 0, 0)
        for p in cap.game_paths(self.lifecycle).values():
            h.memory.data[int(p['root_slot'], 16)] = struct.pack('<Q', ROOT + 0x100000)
        h.memory.data[ROOT + 0x100000] = block(0x26E80)
        h.poll()
        h.cap.trace.close()
        records = list(cap.read_trace(Path(h.tmp.name) / 'trace.bin.gz'))
        self.assertEqual([(r[2], len(r[3])) for r in records],
                         [(True, 0x26E80), (False, 1), (False, 0), (True, 0x26E80)])
        self.assertEqual(records[1][3], [(0x1234, 99)])
        self.assertEqual(records[3][1], ROOT + 0x100000)

    def test_root_moving_mid_read_is_not_traced(self):
        h = self.cfj()
        read = h.memory.read
        slot = int(cap.game_paths(self.lifecycle)['cfj']['root_slot'], 16)
        def moving(address, size):
            raw = read(address, size)
            if address == ROOT:
                h.memory.data[slot] = struct.pack('<Q', 0x900000)
            return raw
        h.cap.read = moving
        h.poll()
        self.assertEqual(h.cap.trace.samples, 0)

    # ---- inputs and pictures ----------------------------------------------

    def test_presses_logged_once_and_pictured_after_settling(self):
        h = self.cfj()
        h.poll()
        h.keys.down = {0x27}                      # right arrow
        h.poll(3)                                 # held: one press
        h.keys.down = set()
        h.pad.add('down')
        h.poll()
        h.pad.clear()
        self.assertEqual([(r['direction'], r['source']) for r in h.rows('input')],
                         [('right', 'keyboard'), ('down', 'pad')])
        self.assertEqual(h.rows('cursor_picture'), [], 'no picture while still moving')
        h.poll(int(cap.SETTLE_SECONDS / 0.05) + 2)
        pics = h.rows('cursor_picture')
        self.assertEqual(len(pics), 1, 'one picture per burst of movement')
        self.assertEqual(pics[0]['after_input'], 2)
        self.assertTrue((Path(h.tmp.name) / pics[0]['image']).exists())

    def test_snapshot_labels_and_refusal(self):
        h = self.cfj()
        h.keys.down = {cap.SNAPSHOT_KEY}
        h.poll(2)                                 # held: one snapshot
        snaps = h.rows('snapshot')
        self.assertEqual([s['label'] for s in snaps], ['first state'])
        self.assertEqual((Path(h.tmp.name) / snaps[0]['block']).read_bytes(), h.memory.data[ROOT])
        self.assertIn('Next: second state', h.said[-1])
        h.keys.down = set()
        h.poll()
        header = bytearray(h.memory.data[0x100000])
        struct.pack_into('<I', header, 8, 1)      # the session is now another game
        h.memory.data[0x100000] = bytes(header)
        h.keys.down = {cap.SNAPSHOT_KEY}
        h.poll()
        self.assertTrue(h.rows('snapshot')[-1]['status'].startswith('refused'))
        self.assertEqual(h.cap.snapshots, 1, 'a refusal does not consume the plan')
        self.assertIn('CFJ is not active', h.said[-1])

    def test_unusable_screenshot_warned_once_by_voice(self):
        h = self.cfj()
        h.image = Image.new('RGB', (32, 24), (0, 0, 0))
        for _ in range(2):
            h.keys.down = {0x25}
            h.poll()
            h.keys.down = set()
            h.poll(10)
        self.assertTrue(h.rows('cursor_picture')[0]['image_status'].startswith('black frame'))
        self.assertEqual(sum('unusable' in s for s in h.said), 1)

    def test_screenshots_off_records_state_without_pictures(self):
        h = self.cvs1()
        h.cap.grab = None
        h.keys.down = {0x27}
        h.poll()
        h.keys.down = {cap.SNAPSHOT_KEY}
        h.poll(12)
        self.assertEqual(h.rows('cursor_picture'), [])
        self.assertTrue(all(r['image_status'] == 'screenshots off'
                            for r in h.rows('selection') + h.rows('snapshot')))
        self.assertEqual(h.rows('snapshot')[0]['status'], 'read')
        self.assertFalse(any('unusable' in x for x in h.said))

    # ---- the traced CVS1 decoder (carried over from the CVS1-only tool) -----

    def select(self, h, side, native_id, slot=0):
        phase = bytearray(h.memory.data[0x403E58])
        phase[0xA0 + side] = slot
        phase[0x78 + 4 * side + slot] = native_id & 0xFF
        h.memory.data[0x403E58] = bytes(phase)

    def test_cvs1_selection_pictured_once_after_settling(self):
        h = self.cvs1()
        h.poll()
        self.assertEqual(h.rows('selection'), [], 'first sighting waits one poll')
        h.poll()
        self.assertEqual(sorted((r['side'], r['native_id']) for r in h.rows('selection')),
                         [(0, 9), (1, 9)])
        h.poll()
        self.assertEqual(len(h.rows('selection')), 2, 'an unchanged ID is not re-pictured')
        self.select(h, 0, 16)
        h.poll()
        self.assertEqual(len(h.rows('selection')), 2, 'a new ID also waits one poll')
        h.poll()
        self.assertEqual(h.rows('selection')[-1]['native_id'], 16)

    def test_cvs1_no_selection_byte_is_never_pictured(self):
        h = self.cvs1()
        self.select(h, 0, 0xFF)
        self.select(h, 1, 0xFF)
        h.poll(3)
        self.assertEqual(h.rows('selection'), [])

    def test_games_without_a_traced_decoder_take_no_selection_pictures(self):
        h = self.cfj()
        self.assertIsNone(h.cap.decoder)
        h.poll(3)
        self.assertEqual(h.rows('selection'), [])


if __name__ == '__main__':
    unittest.main()
