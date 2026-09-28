"""Offline falsifiers for collection restore evidence; no game attachment."""
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import collection_restore_audit as audit


class RestoreAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = audit.produce(audit.DEFAULT_DUMP)

    def test_manifest_reproduces(self):
        path = Path(__file__).resolve().parents[1] / 'docs/collection-restore-manifest.json'
        self.assertEqual(audit.compact(self.report), json.loads(path.read_bytes()))

    def test_all_runtime_branches_and_real_differences_retained(self):
        rows = {r['game']: r for r in self.report['paths']}
        self.assertEqual(set(rows), {p[0] for p in audit.PATHS})
        self.assertEqual({k for k,v in rows.items() if v['payload_header_bytes'] == 16},
                         {'PWS1','PWS2'})
        self.assertEqual({k for k,v in rows.items() if v['restore_kind'] == 'selective'},
                         {'Z3U','Z3U-alternate','MJUS'})
        for row in rows.values():
            self.assertEqual([g['offset'] for g in row['bypass_gates']], ['0x7b4','0x828','0x80c'])

    def assert_mutation_refused(self, address, replacement, message):
        original = audit.Dump.read

        def changed(dump, start, size):
            raw = bytearray(original(dump, start, size))
            if start <= address < start+size:
                raw[address-start] = replacement
            return bytes(raw)

        with patch.object(audit.Dump, 'read', changed):
            with self.assertRaisesRegex(ValueError, message):
                audit.produce(audit.DEFAULT_DUMP)

    def test_signed_control_cannot_become_unsigned(self):
        # CVS2 jg -> ja: negative save controls must not become restore events.
        self.assert_mutation_refused(0x1406AAB82, 0x77, 'signed restore branch missing')

    def test_power_stone_header_cannot_be_copied_from_cvs(self):
        # lea rcx,[rdx+0x10] -> lea rcx,[rdx+8].
        self.assert_mutation_refused(0x1409741C7, 8, 'save payload header differs')

    def test_bypass_polarity_cannot_be_reversed(self):
        # First near jne -> je, preserving instruction size and target.
        self.assert_mutation_refused(0x1406AAB53, 0x84, 'bypass gate differs')


if __name__ == '__main__':
    unittest.main()
