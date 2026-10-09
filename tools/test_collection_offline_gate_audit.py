"""Evidence regression controls; these do not certify native thread safety."""
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import collection_offline_gate_audit as audit


class OfflineGateAuditTests(unittest.TestCase):
    def test_manifest_reproduces(self):
        path = Path(__file__).resolve().parents[1] / 'docs/collection-offline-gate-manifest.json'
        self.assertEqual(audit.compact(audit.produce()), json.loads(path.read_bytes()))

    def refuse(self, address, byte):
        original = audit.Dump.read

        def changed(dump, start, size):
            raw = bytearray(original(dump, start, size))
            if start <= address < start + size:
                raw[address - start] = byte
            return bytes(raw)

        with patch.object(audit.Dump, 'read', changed):
            with self.assertRaisesRegex(ValueError, 'offline .* differs'):
                audit.produce()

    def test_offline_menu_dispatch(self):
        self.refuse(0x140098098, 0x94)

    def test_mode_label_table(self):
        self.refuse(0x1412084D8, 0x0a)

    def test_versus_branch_polarity(self):
        self.refuse(0x1400B27BC, 0x75)

    def test_training_variant_derivation(self):
        self.refuse(0x140038532, 0xc9)  # inc -> dec

    def test_session_mode_derivation(self):
        self.refuse(0x140037AE7, 0x95)  # sete -> setne

    def test_special_mode_clear(self):
        self.refuse(0x140037B8C, 0xb8)  # change destination displacement

    def test_deferred_replacement_call(self):
        self.refuse(0x1401A628A, 0x50)

    def test_manager_update_slot(self):
        self.refuse(0x141018B90, 0x10)

    def test_corpus_identity(self):
        corpus = audit.load_corpus()
        corpus['MODE_ARCADE'] = 'Versus Mode'
        with patch.object(audit, 'load_corpus', return_value=corpus):
            with self.assertRaisesRegex(ValueError, 'offline corpus label differs'):
                audit.produce()

    def test_no_lifetime_claim_from_ordering(self):
        report = audit.produce()
        self.assertTrue(report['pilot']['requires_positive_entry_record'])
        self.assertFalse(report['pilot']['runtime_support_implemented'])
        self.assertFalse(report['boundary']['lifetime_protection_proven'])
        self.assertFalse(report['boundary']['worker_native_read_allowed'])


if __name__ == '__main__':
    unittest.main()
