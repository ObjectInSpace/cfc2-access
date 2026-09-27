"""Offline falsifiers for CVS1 evidence extraction; never attach to CFC2."""
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import cvs1_acquisition_audit as audit


class AcquisitionAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = audit.produce(audit.DEFAULT_DUMP)

    def test_manifest_reproduces(self):
        manifest = Path(__file__).resolve().parents[1] / 'docs/cvs1-acquisition-manifest.json'
        self.assertEqual(audit.compact(self.report), json.loads(manifest.read_bytes()))

    def test_restore_route_keeps_nested_selector(self):
        edges = self.report['dispatch_edges']
        restore = next(e for e in edges if e['target'] == '0x1405c5d50')
        self.assertEqual((restore['table'], restore['index'], restore['selector']),
                         ('0x141222430', 0, 'P+2 signed byte'))
        parent = next(e for e in edges if e['target'] == '0x1405c5d30')
        self.assertEqual((parent['table'], parent['index'], parent['selector']),
                         ('0x141222420', 1, 'P+1 signed byte'))
        self.assertFalse(any(e['table'] == '0x141222420' and e['index'] == 2 for e in edges))

    def test_changed_nested_pointer_refused(self):
        original = audit.Dump.read
        def changed(dump, address, size):
            raw = original(dump, address, size)
            if address == 0x141222430:
                return bytes(size)
            return raw
        with patch.object(audit.Dump, 'read', changed):
            with self.assertRaisesRegex(ValueError, 'Dispatch entry differs'):
                audit.produce(audit.DEFAULT_DUMP)

    def test_changed_prompt_refused(self):
        original = audit.Dump.read
        def changed(dump, address, size):
            raw = original(dump, address, size)
            if address == 0x14106E608:
                return b'X' + raw[1:]
            return raw
        with patch.object(audit.Dump, 'read', changed):
            with self.assertRaisesRegex(ValueError, 'Prompt literal differs'):
                audit.produce(audit.DEFAULT_DUMP)

    def test_mode_table_cannot_escape_guarded_branches(self):
        original = audit.Dump.read
        def changed(dump, address, size):
            if address == 0x1405C5E80:
                return bytes(size)
            return original(dump, address, size)
        with patch.object(audit.Dump, 'read', changed):
            with self.assertRaisesRegex(ValueError, 'Mode jump-table target'):
                audit.produce(audit.DEFAULT_DUMP)


if __name__ == '__main__':
    unittest.main()
