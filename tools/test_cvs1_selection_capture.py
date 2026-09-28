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
        phase = bytearray(256)
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
        self.assertLess(sum(size for _, size in memory.calls), 10000)
        self.assertLess(len(memory.calls), 60)

    def test_default_never_reads_asset(self):
        memory = SelectionMemory(self.lifecycle)
        row = capture.capture(memory.read, self.lifecycle)
        self.assertEqual(row['assets'], [])
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


if __name__ == '__main__':
    unittest.main()
