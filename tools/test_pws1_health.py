import gzip
import json
from pathlib import Path
import struct
import unittest

import pws1_health_audit as audit
from pws1_health_decode import decode

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'tools/fixtures/pws1-observations-20261022'


class HealthTests(unittest.TestCase):
    def test_static_manifest_reproduces(self):
        self.assertEqual(audit.compact(audit.produce()),
                         json.loads((ROOT / 'docs/pws1-health-manifest.json').read_text()))

    def test_recorded_loss_and_reset_preserve_fixed_owners(self):
        expected = {7:240, 8:156, 9:156, 10:32, 11:32, 12:12, 13:240}
        for index, hp in expected.items():
            with self.subTest(snapshot=index):
                raw = gzip.decompress((FIXTURE / f'state-{index:02d}.bin.gz').read_bytes())
                result = decode(raw)
                self.assertFalse(result['audio_eligible'])
                self.assertEqual([a['health_raw'] for a in result['actors']], [hp, 240])
                self.assertEqual([a['raw_slot'] for a in result['actors']], [0, 1])

    def test_signed_floor_and_unknown_range(self):
        raw = bytearray(0x428F0)
        for value, expected in [(-32768,None), (-2,None), (-1,0), (0,0), (1,1), (240,240), (241,None), (32767,None)]:
            struct.pack_into('<3h', raw, 0x18428+0x7790, value, 77, 88)
            result = decode(raw)['actors'][0]
            self.assertEqual(result['health_raw'], value)
            self.assertEqual(result['displayed_health_candidate'], expected)
            self.assertEqual((result['recovery_target_raw'], result['trailing_display_raw']), (77,88))

    def test_recorded_collectible_and_form_resource_candidates(self):
        expected = {7:(1,2,0,0), 8:(1,2,0,0), 9:(3,7,1,2307),
                    10:(2,3,0,0), 11:(3,7,1,2292), 12:(3,7,1,1349), 13:(1,2,0,0)}
        for index, values in expected.items():
            raw = gzip.decompress((FIXTURE / f'state-{index:02d}.bin.gz').read_bytes())
            actor = decode(raw)['actors'][1]
            self.assertEqual(tuple(actor[k] for k in ('collectible_count_raw', 'collectible_bits_raw',
                                                     'model_form_raw', 'form_resource_raw')), values)

    def test_wrong_root_size_refused(self):
        for n in (0, 0x428EF, 0x428F1, 0x8CD40):
            with self.assertRaises(ValueError):
                decode(bytes(n))


if __name__ == '__main__':
    unittest.main()
