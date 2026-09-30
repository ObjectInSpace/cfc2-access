"""Reject copy-direction, callback identity and replay-boundary regressions."""
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import collection_snapshot_audit as audit


class SnapshotAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = audit.produce(audit.DEFAULT_DUMP)

    def test_manifest_reproduces(self):
        path = Path(__file__).resolve().parents[1] / 'docs/collection-snapshot-manifest.json'
        self.assertEqual(audit.compact(self.report), json.loads(path.read_bytes()))

    def assert_mutation_refused(self, address, replacement):
        original = audit.Dump.read

        def changed(dump, start, size):
            raw = bytearray(original(dump, start, size))
            if start <= address < start+size:
                raw[address-start] = replacement
            return bytes(raw)

        with patch.object(audit.Dump, 'read', changed):
            with self.assertRaisesRegex(ValueError, 'snapshot (operation|reference) differs'):
                audit.produce(audit.DEFAULT_DUMP)

    def test_load_destination_cannot_become_source(self):
        # mov rcx,[rsi] -> mov rdx,[rsi].
        self.assert_mutation_refused(0x1400B6629, 0x16)

    def test_payload_header_cannot_disappear(self):
        self.assert_mutation_refused(0x1400B6626, 0)

    def test_registration_callback_cannot_shift(self):
        self.assert_mutation_refused(0x1400B68E1, 0x6c)

    def test_replay_flag_must_cover_loop(self):
        self.assert_mutation_refused(0x1400BA2F6, 0)

    def test_load_cannot_call_save_slot(self):
        self.assert_mutation_refused(0x1400BA340, 0x10)

    def test_replay_cannot_skip_advance_callback(self):
        self.assert_mutation_refused(0x1400BA514, 0x28)


if __name__ == '__main__':
    unittest.main()
