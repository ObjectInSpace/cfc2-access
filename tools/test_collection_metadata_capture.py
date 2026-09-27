"""Synthetic failure controls for the metadata tool; not live game fixtures."""
import json
import ctypes
import os
import struct
import unittest

import collection_metadata_capture as capture
from collection_lifecycle_audit import Dump, DEFAULT_DUMP


class Memory:
    def __init__(self, manifest, game=0, rom=0, name=b'cvs1\0', alpha=0):
        self.calls = []
        self.data = {capture.SESSION: struct.pack('<Q', 0x100000)}
        header = bytearray(0x60)
        struct.pack_into('<QII', header, 0, 0x200000, game, 0)
        struct.pack_into('<Q', header, 0x30, 0x300000)
        struct.pack_into('<I', header, 0x44, rom)
        self.data[0x100000] = bytes(header)
        self.data[0x1001B0] = struct.pack('<QIIQII', 0x400000, 0x32AB0, 0, 0x432AB0, 0x32AB8, 0)
        self.data[0x1002B4] = struct.pack('<I', alpha)
        self.data[0x300000] = name.ljust(16, b'\0')
        for path in manifest['paths']:
            for field in ('root_slot', 'context_slot'):
                self.data[int(path[field], 16)] = struct.pack('<Q', 0x400000)

    def read(self, address, size):
        self.calls.append((address, size))
        if address not in self.data:
            raise OSError('Synthetic unreadable range')
        return self.data[address][:size]


class MetadataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads(capture.MANIFEST.read_bytes())
        cls.dump = Dump(DEFAULT_DUMP)

    def test_real_dump_signatures_and_mismatch(self):
        self.assertEqual(len(capture.verify_build(self.dump.read, self.manifest)), len(self.manifest['spans']))
        def changed(address, size):
            raw = self.dump.read(address, size)
            return bytes([raw[0] ^ 1]) + raw[1:]
        with self.assertRaisesRegex(ValueError, 'differs'):
            capture.verify_build(changed, self.manifest)

    def test_cvs1_zero_index_and_bounded_work(self):
        memory = Memory(self.manifest)
        row = capture.capture(memory.read, self.manifest)
        self.assertEqual(row['bracket_status'], 'matching')
        self.assertEqual(row['before']['decoded']['rom_index'], 0)
        self.assertEqual(row['before']['decoded']['rom_name'], 'cvs1')
        # Per bracket: session slot, three session ranges, name, eleven slots.
        self.assertEqual(len(memory.calls), 32)
        self.assertEqual(sum(size for _, size in memory.calls), 488)
        self.assertFalse(any(address == 0x400000 for address, _ in memory.calls))

    def test_all_four_alpha_identities_remain_raw(self):
        for rom, name, alpha in ((6, b'z3u\0', 0), (7, b'a3u\0', 0),
                                 (16, b'z3uu\0', 1), (17, b'a3uu\0', 2)):
            with self.subTest(rom=rom):
                row = capture.capture(Memory(self.manifest, 3, rom, name, alpha).read, self.manifest)
                self.assertEqual(row['before']['decoded']['alpha_discriminator'], alpha)
                self.assertEqual(row['before']['decoded']['rom_index'], rom)

    def test_cfj_separate_context_retained(self):
        memory = Memory(self.manifest, 2, 4, b'cfj\0')
        memory.data[0x142CF9D48] = struct.pack('<Q', 0x999000)
        row = capture.capture(memory.read, self.manifest)
        self.assertEqual(row['before']['decoded']['slots']['0x142cf9d48'], '0x999000')
        self.assertEqual(row['before']['decoded']['slots']['0x142c79b40'], '0x400000')

    def test_null_session_keeps_stale_roots_without_dereference(self):
        memory = Memory(self.manifest)
        memory.data[capture.SESSION] = bytes(8)
        row = capture.capture(memory.read, self.manifest)
        self.assertEqual(row['bracket_status'], 'matching')
        self.assertEqual(row['before']['decoded']['session'], '0x0')
        self.assertEqual(len(memory.calls), 24)
        self.assertNotIn('rom_index', row['before']['decoded'])

    def test_persistent_failure_is_not_matching(self):
        memory = Memory(self.manifest)
        del memory.data[0x100000]
        row = capture.capture(memory.read, self.manifest)
        self.assertEqual(row['bracket_status'], 'unreadable')
        self.assertIn('session_header', row['before']['errors'])

    def test_short_read_is_explicit_failure(self):
        memory = Memory(self.manifest)
        memory.data[capture.SESSION] = b'\0'
        row = capture.capture(memory.read, self.manifest)
        self.assertEqual(row['bracket_status'], 'unreadable')
        self.assertIn('Partial read', row['before']['records']['session_slot']['error'])

    def test_same_session_changed_rom_detected(self):
        memory = Memory(self.manifest)
        count = 0
        def read(address, size):
            nonlocal count
            if address == 0x100000:
                count += 1
                if count == 2:
                    header = bytearray(memory.data[address])
                    struct.pack_into('<I', header, 0x44, 3)
                    memory.data[address] = bytes(header)
            return memory.read(address, size)
        self.assertEqual(capture.capture(read, self.manifest)['bracket_status'], 'changed')

    def test_changed_session_detected(self):
        memory = Memory(self.manifest)
        count = 0
        def read(address, size):
            nonlocal count
            if address == capture.SESSION:
                count += 1
                if count == 2:
                    return bytes(8)
            return memory.read(address, size)
        self.assertEqual(capture.capture(read, self.manifest)['bracket_status'], 'changed')

    def test_malformed_name_retains_raw_failure(self):
        memory = Memory(self.manifest)
        memory.data[0x300000] = b'x' * 16
        row = capture.capture(memory.read, self.manifest)
        self.assertEqual(row['bracket_status'], 'unreadable')
        self.assertIn('rom_name_format', row['before']['errors'])
        self.assertEqual(row['before']['records']['rom_name']['hex'], (b'x' * 16).hex())

    @unittest.skipUnless(os.name == 'nt', 'Windows process API')
    def test_windows_transport_self_process_only(self):
        # Exercise real ctypes widths, handle and RPM; never attach to CFC2.
        buffer = ctypes.create_string_buffer(b'bounded metadata')
        reader = capture.ProcessReader(os.getpid())
        try:
            self.assertEqual(reader.read(ctypes.addressof(buffer), 16), b'bounded metadata')
            with self.assertRaises(OSError):
                reader.read(0, 8)
        finally:
            reader.close()


if __name__ == '__main__':
    unittest.main()
