import copy
import gzip
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from pws_camera_pair import context_records, diagnose, project

FIXTURE = Path(__file__).parent / 'fixtures/pws1-observations-20261022'


class PairTests(unittest.TestCase):
    def setUp(self):
        self.report = json.loads((FIXTURE / 'camera-brackets.json').read_text())
        self.row = self.report['snapshots'][6]
        self.state = gzip.decompress((FIXTURE / (self.row['block'] + '.gz')).read_bytes())

    def test_retained_brackets_reproduce(self):
        self.assertEqual(len(self.report['snapshots']), 13)
        for row in self.report['snapshots']:
            with self.subTest(block=row['block']):
                raw = gzip.decompress((FIXTURE / (row['block'] + '.gz')).read_bytes())
                self.assertEqual(hashlib.sha256(raw).hexdigest(), row['sha256'])
                result = diagnose('pws1', raw, row['t'], row['before'], row['after'])
                self.assertEqual(result, row['diagnostics'])
                self.assertFalse(result['audio_eligible'])
        self.assertFalse(self.row['diagnostics']['bracket_camera_bytes_equal'])
        self.assertTrue(self.report['snapshots'][10]['diagnostics']['bracket_camera_bytes_equal'])

    def test_missing_stale_unreadable_and_replaced_context_refuse(self):
        for change in ('missing', 'stale', 'unreadable', 'replaced', 'zero', 'future'):
            before, after = copy.deepcopy(self.row['before']), copy.deepcopy(self.row['after'])
            if change == 'missing': before = None
            if change == 'stale': before['t'] -= 1
            if change == 'unreadable': before['raw_hex'] = ''
            if change == 'replaced': before['pointer'] = '0x1000'
            if change == 'zero': before['pointer'] = '0x0'
            if change == 'future': before['t'] += 1
            with self.subTest(change=change):
                result = diagnose('pws1', self.state, self.row['t'], before, after)
                self.assertFalse(result['bracket_usable'])
                self.assertEqual(result['observations'], [])

    def test_alternate_layout_does_not_pick_a_view(self):
        state = bytearray(self.state)
        state[0x280c4] = 1
        result = diagnose('pws1', state, self.row['t'], self.row['before'], self.row['after'])
        self.assertIsNone(result['ordinary_selected_index'])
        self.assertEqual(result['observations'], [])

    def test_projection_orientation_and_refusal(self):
        view = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
        perspective = [2,0,0,0, 0,2,0,0, 0,0,-1,-1, 0,0,-1,0]
        self.assertEqual(project(view, perspective, [1,2,-4])['ndc'], [0.5,1,0.75])
        self.assertIsNone(project(view, perspective, [1,2,4]))
        self.assertIsNone(project(view, perspective, [1,2,0]))
        self.assertIsNone(project(view, view, [1,2,-4]))
        for bad in (float('nan'), float('inf')):
            for target in ('view', 'projection', 'point'):
                v, p, xyz = view.copy(), perspective.copy(), [1,2,-4]
                {'view': v, 'projection': p, 'point': xyz}[target][0] = bad
                self.assertIsNone(project(v, p, xyz))

    def test_interrupted_gzip_keeps_only_complete_records(self):
        raw = bytes(512)
        one = struct.pack('<dQI', 1, 0x1000, 512) + raw
        data = gzip.compress(one + struct.pack('<dQI', 2, 0x1000, 512) + raw[:13])
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'context.gz'
            path.write_bytes(data[:-8])
            self.assertEqual(list(context_records(path)),
                             [dict(t=1, pointer='0x1000', raw_hex=raw.hex())])

    def test_error_record_is_retained_as_a_pairing_barrier(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'context.gz'
            path.write_bytes(gzip.compress(struct.pack('<dQI', 1, 0, 0)))
            self.assertEqual(list(context_records(path)), [dict(t=1, pointer='0x0', raw_hex='')])


if __name__ == '__main__':
    unittest.main()
