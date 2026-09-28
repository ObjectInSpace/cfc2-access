"""Synthetic selection-capture failure controls; never attach to the game."""
import json
import struct
import unittest

import cvs1_selection_capture as capture
from collection_lifecycle_audit import Dump, DEFAULT_DUMP
from test_collection_metadata_capture import Memory


class SelectionMemory(Memory):
    def __init__(self, manifest):
        super().__init__(manifest)
        path = next(p for p in manifest['paths'] if p['tag'] == 'CVS1')
        header = bytearray(self.data[0x100000])
        for offset, row in path['callbacks'].items():
            struct.pack_into('<Q', header, int(offset, 16), int(row['target'], 16))
        self.data[0x100000] = bytes(header)
        self.data[capture.CONTEXT_SLOT] = struct.pack('<Q', 0x500000)
        self.data[capture.PHASE_SLOT] = struct.pack('<Q', 0x403E58)
        phase = bytearray(0x112)
        phase[:4] = bytes((2, 1, 1, 2))
        phase[0x48] = 4
        phase[0x78] = phase[0x7C] = 9
        self.data[0x403E58] = bytes(phase)
        for side in range(2):
            actor = 0x403FB8 + side * 0x700
            raw = bytearray(0x38)
            raw[1], raw[4], raw[5] = 9, 6, 6
            self.data[actor] = bytes(raw)
            self.data[actor+0x688] = bytes((side,))
            self.data[actor+0x5BA] = bytes(0x20)
        self.data[0x4327BC] = struct.pack('<8H', *range(1, 9))
        self.data[0x430BFB] = bytes(2)
        self.data[0x1422D2B85] = bytes((1,))
        self.data[0x500008] = struct.pack('<Q', 0x1000000)
        self.data[0x4052B0] = struct.pack('<Q', 0x1A46800)
        self.data[0x1AB0000 + (9 << 15)] = b'A' * 4096
        self.data[0x1AB0000 + (0x23 << 15)] = b'B' * 4096


class SelectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lifecycle = json.loads(capture.metadata.MANIFEST.read_bytes())
        cls.acquisition = json.loads(capture.ACQUISITION.read_bytes())

    def test_real_dump_signatures_and_data_mutation(self):
        dump = Dump(DEFAULT_DUMP)
        self.assertTrue(capture.verify(dump.read, self.lifecycle, self.acquisition))
        def changed(address, size):
            raw = dump.read(address, size)
            return bytes(size) if address == 0x1405C8BB0 else raw
        with self.assertRaisesRegex(ValueError, 'dispatch/data differs'):
            capture.verify(changed, self.lifecycle, self.acquisition)

    def test_raw_fields_and_side_remap(self):
        memory = SelectionMemory(self.lifecycle)
        row = capture.capture(memory.read, self.lifecycle, 4096)
        self.assertEqual(row['status'], 'matching')
        self.assertEqual([a['mapped_id'] for a in row['assets']], [9, 0x23])
        self.assertEqual([bytes.fromhex(a['hex']) for a in row['assets']], [b'A'*4096, b'B'*4096])
        self.assertEqual(sum(size for _, size in memory.calls), 9670)
        self.assertEqual(len(memory.calls), 60)

    def test_default_never_reads_asset(self):
        memory = SelectionMemory(self.lifecycle)
        row = capture.capture(memory.read, self.lifecycle)
        self.assertEqual(row['assets'], [])
        self.assertEqual(sum(size for _, size in memory.calls), 1478)
        self.assertEqual(len(memory.calls), 58)
        self.assertFalse(any(a >= 0x1000000 and a < 0x2000000 for a, _ in memory.calls))

    def test_wrong_identity_never_dereferences_root_or_context(self):
        memory = SelectionMemory(self.lifecycle)
        raw = bytearray(memory.data[0x100000])
        struct.pack_into('<I', raw, 8, 1)
        memory.data[0x100000] = bytes(raw)
        row = capture.capture(memory.read, self.lifecycle, 256)
        self.assertEqual(row['status'], 'outside_scope')
        self.assertFalse(any(0x400000 <= a < 0x600000 for a, _ in memory.calls))

    def test_root_changes_after_asset_read(self):
        memory = SelectionMemory(self.lifecycle)
        def changed(address, size):
            raw = memory.read(address, size)
            if address == 0x1AB0000 + (9 << 15):
                memory.data[capture.ROOT_SLOT] = struct.pack('<Q', 0x700000)
            return raw
        self.assertEqual(capture.capture(changed, self.lifecycle, 256)['status'], 'changed')

    def test_unreadable_state_and_partial_asset(self):
        memory = SelectionMemory(self.lifecycle)
        del memory.data[0x403E58]
        self.assertEqual(capture.capture(memory.read, self.lifecycle)['status'], 'unreadable')
        memory = SelectionMemory(self.lifecycle)
        memory.data[0x1AB0000 + (9 << 15)] = b'x'
        self.assertEqual(capture.capture(memory.read, self.lifecycle, 256)['status'], 'unreadable')

    def test_special_id_invalid_slot_and_owner_skip_assets(self):
        for kind in ('special', 'slot', 'owner', 'cooldown'):
            with self.subTest(kind=kind):
                memory = SelectionMemory(self.lifecycle)
                if kind in ('special', 'slot'):
                    p = bytearray(memory.data[0x403E58])
                    p[0x78 if kind == 'special' else 0xA0] = 0x23 if kind == 'special' else 4
                    memory.data[0x403E58] = bytes(p)
                if kind in ('special', 'cooldown'):
                    a = bytearray(memory.data[0x403FB8])
                    a[1 if kind == 'special' else 0x30] = 0x23 if kind == 'special' else 1
                    memory.data[0x403FB8] = bytes(a)
                if kind == 'owner':
                    memory.data[0x404640] = b'\xff'
                row = capture.capture(memory.read, self.lifecycle, 256)
                self.assertEqual(row['assets'][0]['status'], 'skipped')
                self.assertFalse(any(a == 0x1AB0000 + (9 << 15) for a, _ in memory.calls))

    def test_outside_phase_and_bad_phase_pointer(self):
        memory = SelectionMemory(self.lifecycle)
        p = bytearray(memory.data[0x403E58]); p[0] = 1
        memory.data[0x403E58] = bytes(p)
        self.assertEqual(capture.capture(memory.read, self.lifecycle, 256)['assets'], [])
        memory = SelectionMemory(self.lifecycle)
        memory.data[capture.PHASE_SLOT] = struct.pack('<Q', 0x999000)
        self.assertEqual(capture.capture(memory.read, self.lifecycle)['status'], 'outside_scope')
        self.assertFalse(any(a == 0x999000 for a, _ in memory.calls))

    def test_asset_cap_rejected_before_reads(self):
        memory = SelectionMemory(self.lifecycle)
        with self.assertRaises(ValueError):
            capture.capture(memory.read, self.lifecycle, 4097)
        self.assertEqual(memory.calls, [])

    def test_navigation_raw_values_and_invalid_group_are_bounded(self):
        memory = SelectionMemory(self.lifecycle)
        p = bytearray(memory.data[0x403E58])
        p[:5] = bytes((2, 1, 1, 6, 6)); p[0x48] = 0
        p[0x104:0x112] = bytes((0, 1, 255, 3, 4, 5, 6, 7, 255, 127, 255, 1, 0, 0xAB))
        memory.data[0x403E58] = bytes(p)
        inputs = bytearray(0x20)
        struct.pack_into('<H', inputs, 4, 0x400)
        struct.pack_into('<H', inputs, 0x1E, 0x800)
        memory.data[0x404572] = bytes(inputs)
        row = capture.capture(memory.read, self.lifecycle, 4096)
        self.assertEqual(row['status'], 'outside_scope')
        self.assertEqual(row['assets'], [])
        nav = row['before']['navigation']
        self.assertEqual(nav['enabled_mask'], 0xAB)
        self.assertEqual(nav['sides'][0], dict(side=0, group=-1, delta=-1,
                                             positions=[0, 1, -1, 3], counts=[1, 2, 3, 4]))
        self.assertEqual(nav['sides'][1]['group'], 127)
        self.assertEqual(nav['sides'][1]['counts'], [5, 6, 7, 8])
        words = row['before']['actors'][0]['input_words']
        self.assertEqual((words['0x5be'], words['0x5d8']), (0x400, 0x800))
        self.assertEqual(len(memory.calls), 58)

    def test_navigation_failures_and_between_observation_changes(self):
        for address in (0x4327BC, 0x404572, 0x404C72):
            with self.subTest(address=hex(address)):
                memory = SelectionMemory(self.lifecycle)
                memory.data[address] = b'\0'  # Partial reads must fail closed.
                self.assertEqual(capture.capture(memory.read, self.lifecycle)['status'], 'unreadable')
        for address, offset in ((0x403E58, 0x10C), (0x4327BC, 0), (0x404572, 4)):
            with self.subTest(changed=hex(address)):
                memory = SelectionMemory(self.lifecycle)
                def changed(a, size):
                    raw = memory.read(a, size)
                    if a == address:
                        value = bytearray(memory.data[a]); value[offset] ^= 1
                        memory.data[a] = bytes(value)
                    return raw
                self.assertEqual(capture.capture(changed, self.lifecycle)['status'], 'changed')


if __name__ == '__main__':
    unittest.main()
