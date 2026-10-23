import json
from pathlib import Path
import unittest
from unittest.mock import patch
import pws_selection_audit as audit


class SelectionEvidenceTests(unittest.TestCase):
    def test_committed_evidence_reproduces(self):
        path = Path(__file__).resolve().parents[1] / 'docs/pws-selection-manifest.json'
        self.assertEqual(audit.compact(audit.produce()), json.loads(path.read_text()))

    def test_changed_name_map_or_dispatch_or_grid_refused(self):
        original = audit.Dump.read
        for _, target, _ in audit.DATA:
            with self.subTest(address=hex(target)):
                def changed(dump, address, size):
                    data = original(dump, address, size)
                    return bytes([data[0] ^ 1]) + data[1:] if address == target else data
                with patch.object(audit.Dump, 'read', changed):
                    with self.assertRaisesRegex(ValueError, 'differs from reviewed data'):
                        audit.produce()


if __name__ == '__main__':
    unittest.main()
