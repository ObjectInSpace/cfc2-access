"""Synthetic ownership-capture controls; no live fixtures or camera proof."""
import json
import struct
import subprocess
import sys
import unittest

import pws2_ownership_capture as capture
from test_collection_metadata_capture import Memory


class OwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads(capture.metadata.MANIFEST.read_bytes())

    def memory(self):
        m = Memory(self.manifest, game=5, rom=10, name=b'pws2\0')
        header = bytearray(m.data[0x100000])
        path = next(p for p in self.manifest['paths'] if p['tag'] == 'PWS2')
        for off, callback in path['callbacks'].items():
            struct.pack_into('<Q', header, int(off, 16), int(callback['target'], 16))
        m.data[0x100000] = bytes(header)
        m.data[0x1001B0] = struct.pack('<QIIQII', 0x400000, 0x8cd40, 0, 0x48cd40, 0x8cd48, 0)
        for _, offset, size in capture.RANGES:
            m.data[0x400000 + offset] = bytes(size)
        return m

    def test_bounded_raw_capture_never_authorizes_audio(self):
        m = self.memory()
        row = capture.capture(m.read, self.manifest)
        self.assertEqual(row['status'], 'matching_metadata_only')
        self.assertFalse(row['audio_eligible'])
        self.assertEqual(set(row['records']), {'actors', 'transforms', 'inputs'})
        self.assertEqual(len(m.calls), 35)
        self.assertEqual(sum(size for _, size in m.calls), 72232)

    def test_wrong_identity_never_dereferences_root(self):
        for kind in ('game', 'rom', 'region', 'callback', 'size', 'root', 'null_session'):
            with self.subTest(kind=kind):
                m = self.memory()
                if kind in ('game', 'rom', 'region', 'callback'):
                    header = bytearray(m.data[0x100000])
                    offset = dict(game=8, rom=0x44, region=12, callback=0x10)[kind]
                    struct.pack_into('<I', header, offset, 99)
                    m.data[0x100000] = bytes(header)
                elif kind == 'size':
                    m.data[0x1001B0] = bytes(32)
                elif kind == 'root':
                    m.data[int(capture.ROOT_SLOT, 16)] = bytes(8)
                else:
                    m.data[capture.metadata.SESSION] = bytes(8)
                self.assertEqual(capture.capture(m.read, self.manifest)['status'], 'identity_rejected')
                self.assertFalse(any(0x400000 <= address < 0x48cd40 for address, _ in m.calls))

    def test_short_and_unreadable_records(self):
        for short in (False, True):
            m = self.memory()
            address = 0x400000 + capture.RANGES[0][1]
            if short:
                m.data[address] = b'\0'
            else:
                del m.data[address]
            self.assertEqual(capture.capture(m.read, self.manifest)['status'], 'unreadable')

    def test_changed_identity_and_failed_after_bracket(self):
        for failure in (False, True):
            m = self.memory()
            def read(address, size):
                raw = m.read(address, size)
                if address == 0x400000 + capture.RANGES[-1][1]:
                    if failure:
                        del m.data[capture.metadata.SESSION]
                    else:
                        m.data[int(capture.ROOT_SLOT, 16)] = struct.pack('<Q', 0x500000)
                return raw
            self.assertEqual(capture.capture(read, self.manifest)['status'],
                             'unreadable' if failure else 'identity_changed')

    def test_us_revision(self):
        m = self.memory()
        header = bytearray(m.data[0x100000])
        struct.pack_into('<I', header, 0x44, 11)
        struct.pack_into('<I', header, 12, 1)
        m.data[0x100000] = bytes(header)
        m.data[0x300000] = b'pws2u\0'.ljust(16, b'\0')
        self.assertEqual(capture.capture(m.read, self.manifest)['status'], 'matching_metadata_only')

    def test_cli_rejects_unbounded_schedule_before_attachment(self):
        result = subprocess.run([sys.executable, capture.__file__, '1', '--out', 'unused.jsonl',
                                 '--label', 'synthetic', '--mode', 'unknown', '--language', 'unknown',
                                 '--count', '120', '--interval', '5'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('exceeds 30 seconds', result.stderr)


if __name__ == '__main__':
    unittest.main()
