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

    def test_request_enables_callback_mode(self):
        self.assert_mutation_refused(0x14003C926, 0)

    def test_request_constructor_cannot_use_adjacent_type(self):
        self.assert_mutation_refused(0x140FDE940, 0xa0)

    def test_internal_type_label_must_match_dump(self):
        original = audit.Dump.read
        def changed(dump, address, size):
            raw = original(dump, address, size)
            return b'X' + raw[1:] if address == 0x140FDE918 else raw
        with patch.object(audit.Dump, 'read', changed):
            with self.assertRaisesRegex(ValueError, 'snapshot request type name differs'):
                audit.produce(audit.DEFAULT_DUMP)

    def test_join_match_state_cannot_point_to_adjacent_case(self):
        original = audit.Dump.read
        def changed(dump, address, size):
            raw = original(dump, address, size)
            return (0x90062).to_bytes(4, 'little') if address == 0x1400902AC else raw
        with patch.object(audit.Dump, 'read', changed):
            with self.assertRaisesRegex(ValueError, 'snapshot join state target differs'):
                audit.produce(audit.DEFAULT_DUMP)

    def test_join_match_request_cannot_select_other_descriptor(self):
        self.assert_mutation_refused(0x14009013A, 0xab)

    def assert_mutation_refused(self, address, replacement):
        original = audit.Dump.read

        def changed(dump, start, size):
            raw = bytearray(original(dump, start, size))
            if start <= address < start+size:
                raw[address-start] = replacement
            return bytes(raw)

        with patch.object(audit.Dump, 'read', changed):
            with self.assertRaisesRegex(ValueError, 'snapshot (operation|reference|virtual slot) differs'):
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

    def test_finish_cannot_skip_final_poll(self):
        self.assert_mutation_refused(0x1400B9543, 0x20)

    def test_secondary_finish_cannot_use_primary_schema(self):
        self.assert_mutation_refused(0x140FF03B8, 0x20)


if __name__ == '__main__':
    unittest.main()
