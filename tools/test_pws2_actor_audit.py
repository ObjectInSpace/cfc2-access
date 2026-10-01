import copy
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import collection_lifecycle_audit as lifecycle
import pws2_actor_audit as audit


class ActorAuditTests(unittest.TestCase):
    def test_pinned_manifest_reproduces(self):
        manifest = Path(__file__).resolve().parents[1] / 'docs/pws2-actor-manifest.json'
        self.assertEqual(audit.compact(audit.produce()), json.loads(manifest.read_text()))

    def test_changed_code_refused_before_extraction(self):
        sections = [copy.copy(s) for s in lifecycle.read_manifest(audit.DEFAULT_DUMP)]
        code = next(s for s in sections if s.name == 'sec_00001000_code.bin')
        changed = bytearray(code.data)
        changed[0x140A4C3A7 - code.live] ^= 1
        code.data = bytes(changed)
        with patch.object(lifecycle, 'read_manifest', return_value=sections):
            with self.assertRaisesRegex(ValueError, 'Code differs'):
                audit.produce()

    def test_partial_instruction_cannot_be_accepted_as_anchor(self):
        with patch.object(audit, 'ANCHORS', ((0x140A4C3A7, 0x140A4C3AD, 'add', 'rdi, 0x42b8'),)):
            with self.assertRaisesRegex(ValueError, 'Incomplete instruction span'):
                audit.produce()

    def test_changed_processing_order_table_refused(self):
        original = audit.Dump.read
        def changed(dump, address, size):
            data = original(dump, address, size)
            return bytes([data[0] ^ 1]) + data[1:] if address == 0x1417308D0 else data
        with patch.object(audit.Dump, 'read', changed):
            with self.assertRaisesRegex(ValueError, 'processing-order table differs'):
                audit.produce()


if __name__ == '__main__':
    unittest.main()
