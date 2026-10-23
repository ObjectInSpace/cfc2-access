import json
from pathlib import Path
import unittest
from unittest.mock import patch

import pws_camera_audit as audit


class CameraAuditTests(unittest.TestCase):
    def test_manifest_reproduces(self):
        path = Path(__file__).resolve().parents[1] / 'docs/pws-camera-evidence.json'
        self.assertEqual(audit.produce(), json.loads(path.read_text()))

    def test_translation_template_change_refused(self):
        original = audit.Dump.read
        def changed(dump, address, size):
            raw = original(dump, address, size)
            return bytes([raw[0] ^ 1]) + raw[1:] if address == 0x141C00250 else raw
        with patch.object(audit.Dump, 'read', changed):
            with self.assertRaisesRegex(ValueError, 'identity template differs'):
                audit.produce()


if __name__ == '__main__':
    unittest.main()
