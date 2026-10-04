"""Offline reproduction and falsifying controls, never live HUD acceptance."""
import copy
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import collection_lifecycle_audit as lifecycle
import pws2_hud_audit as audit


class HudAuditTests(unittest.TestCase):
    def test_manifest_reproduces(self):
        path = Path(__file__).resolve().parents[1] / 'docs/pws2-hud-manifest.json'
        self.assertEqual(audit.produce(), json.loads(path.read_bytes()))

    def test_changed_layout_and_gauge_operands_rejected(self):
        original = audit.Dump.read
        for _, target, _ in audit.DATA:
            with self.subTest(address=hex(target)):
                def changed(dump, address, size):
                    raw = original(dump, address, size)
                    return bytes([raw[0] ^ 1]) + raw[1:] if address == target else raw
                with patch.object(audit.Dump, 'read', changed):
                    with self.assertRaisesRegex(ValueError, 'HUD operand differs'):
                        audit.produce()

    def test_swapped_owner_and_layout_store_rejected(self):
        sections = [copy.copy(s) for s in lifecycle.read_manifest(audit.DEFAULT_DUMP)]
        code = next(s for s in sections if s.name == 'sec_00001000_code.bin')
        raw = bytearray(code.data)
        # Change object +2 (actor owner) to +1 (layout) at publication.
        raw[0x140AFB458 - code.live] = 1
        code.data = bytes(raw)
        with patch.object(lifecycle, 'read_manifest', return_value=sections):
            with self.assertRaisesRegex(ValueError, 'Code differs'):
                audit.produce()

    def test_incomplete_publication_instruction_rejected(self):
        with patch.object(audit, 'SPANS', (('partial', 0x140AFB455, 0x140AFB458),)):
            with self.assertRaisesRegex(ValueError, 'Incomplete instruction span'):
                audit.produce()


if __name__ == '__main__':
    unittest.main()
