"""Replay memory-only PWS1 observations through the production C decoder."""
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'tools/fixtures/pws1-observations-20261022'


class ObservationTests(unittest.TestCase):
    def test_recorded_samples(self):
        manifest = json.loads((FIXTURE / 'manifest.json').read_text())
        self.assertFalse(manifest['visual_validation'])
        self.assertEqual(len(manifest['snapshots']), 13)
        with tempfile.TemporaryDirectory() as folder:
            state = Path(folder) / 'state.bin'
            for row, expected in zip(manifest['snapshots'], manifest['expected_slot_zero'], strict=True):
                with self.subTest(snapshot=row['block']):
                    raw = gzip.decompress((FIXTURE / (row['block'] + '.gz')).read_bytes())
                    self.assertEqual(len(raw), 0x428F0)
                    self.assertEqual(hashlib.sha256(raw).hexdigest(), row['sha256'])
                    state.write_bytes(raw)
                    output = subprocess.check_output([str(ROOT / 'build/test/test_pws_select.exe'),
                                                      '4', '1', str(state)], text=True)
                    actual = [int(v) for v in re.findall(r'decode=(-?\d+)', output)]
                    self.assertEqual(actual, [expected, -1, -1, -1])


if __name__ == '__main__':
    unittest.main()
