"""Falsifiers for the user-facing quick-save command trace."""
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import collection_quicksave_audit as audit


class QuickSaveAuditTests(unittest.TestCase):
    def test_manifest_reproduces(self):
        path = Path(__file__).resolve().parents[1] / 'docs/collection-quicksave-manifest.json'
        self.assertEqual(audit.compact(audit.produce()), json.loads(path.read_bytes()))

    def refuse(self, address, value):
        original = audit.Dump.read
        def changed(dump, start, size):
            raw = bytearray(original(dump, start, size))
            if start <= address < start + size:
                raw[address-start] = value
            return bytes(raw)
        with patch.object(audit.Dump, 'read', changed):
            with self.assertRaisesRegex(ValueError, 'quick-save operation differs'):
                audit.produce()

    def test_load_cannot_skip_cleanup_by_calling_update(self):
        self.refuse(0x14005004B, 0x10)

    def test_save_cannot_silently_read_primary_root(self):
        self.refuse(0x1400B615F, 0xb0)

    def test_restore_cannot_silently_write_primary_root(self):
        self.refuse(0x1400B6069, 0xb0)

    def test_load_confirmation_cannot_be_another_prompt(self):
        self.refuse(0x1400A736D, 0x0e)

    def test_secondary_payload_header_cannot_disappear(self):
        self.refuse(0x1400B604B, 0)


if __name__ == '__main__':
    unittest.main()
