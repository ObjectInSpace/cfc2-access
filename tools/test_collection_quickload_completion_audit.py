"""Mutation controls for the Quick Load completion evidence contract."""
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import collection_quickload_completion_audit as audit


class CompletionAuditTests(unittest.TestCase):
    def test_manifest_reproduces(self):
        path = Path(__file__).resolve().parents[1] / 'docs/collection-quickload-completion-manifest.json'
        self.assertEqual(audit.compact(audit.produce()), json.loads(path.read_bytes()))

    def refuse(self, address, value):
        original = audit.Dump.read

        def changed(dump, start, size):
            raw = bytearray(original(dump, start, size))
            if start <= address < start + size:
                raw[address - start] = value
            return bytes(raw)

        with patch.object(audit.Dump, 'read', changed):
            with self.assertRaisesRegex(ValueError, 'completion .* differs'):
                audit.produce()

    def test_secondary_import_gate_polarity(self):
        self.refuse(0x1400B5FA9, 0x84)

    def test_restore_bypass_polarity(self):
        self.refuse(0x1406AAB53, 0x84)

    def test_signed_positive_restore_control(self):
        self.refuse(0x1406AAB82, 0x7e)

    def test_copy_cannot_skip_repair(self):
        self.refuse(0x1406AABF7, 0x66)

    def test_conditional_repair_range(self):
        self.refuse(0x1406AAC09, 1)

    def test_list_repair_count(self):
        self.refuse(0x1406ACE8B, 0x2b)

    def test_preparation_dispatch_identity(self):
        self.refuse(0x14128CD78, 0xd0)

    def test_later_reverse_transfer_polarity(self):
        self.refuse(0x1406A9E61, 0x84)

    def test_callback_exclusion_polarity(self):
        self.refuse(0x14003A30C, 0x75)

    def test_no_offline_certification_from_zero_flags(self):
        self.assertFalse(audit.produce()['exclusion']['zero_values_prove_supported_offline_mode'])


if __name__ == '__main__':
    unittest.main()
