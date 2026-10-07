import unittest
import gzip
import hashlib
from pathlib import Path
import collection_probe_report as report


def event(sequence, site=0, exiting=0, thread=123, readable=1, depth=1):
    return ','.join(str(v) for v in (sequence, sequence*100, thread, site, exiting,
        depth, readable, '100000', '200000', 1, 2, 100, 4, 0, 1, 'abc', -17, -1))


def capture(rows, footer='# complete,dropped,0'):
    return '\n'.join(['# boundary_probe,1,qpc_frequency,1000000,duration_ms,120000',
                      ','.join(report.FIELDS), *rows, *([footer] if footer else [])])


class ProbeReportTests(unittest.TestCase):
    def test_quick_load_schema_2_preserves_full_result(self):
        rows = [event(1, site=5), event(2, site=5, exiting=1)]
        rows[1] = rows[1].rsplit(',', 1)[0] + ',-1234567'
        text = capture(rows).replace('boundary_probe,1,', 'boundary_probe,2,')
        result = report.analyze(text)
        self.assertEqual(result['capture_schema'], 2)
        self.assertEqual([p['site'] for p in result['pairs']], ['quick_load'])
        with self.assertRaisesRegex(ValueError, 'Invalid site'):
            report.analyze(capture(rows))

    def test_quick_load_result_overflow_refused(self):
        row = event(1, site=5, exiting=1).rsplit(',', 1)[0] + ',2147483648'
        with self.assertRaisesRegex(ValueError, 'Out-of-range argument or result'):
            report.analyze(capture([row]).replace('boundary_probe,1,', 'boundary_probe,2,'))

    def test_live_switch_fixture_is_complete_but_has_no_restore_evidence(self):
        packed = (Path(__file__).parent / 'fixtures/collection-boundary-switch-20261006.csv.gz').read_bytes()
        self.assertEqual(hashlib.sha256(packed).hexdigest(),
                         '7ee75d22bd3d2acdef9c0c2064590bff9d58f3ca3339e1d6685937218b1c74ea')
        raw = gzip.decompress(packed)
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         '3a6cacc5849883236398d346165642a04f7b99f829dd32b22e768457c8264432')
        result = report.analyze(raw.decode('utf-8'))
        self.assertTrue(result['complete'])
        self.assertEqual((result['dropped'], result['unreadable']), (0, 0))
        self.assertEqual(result['event_count'], 6738)
        self.assertEqual(len(result['pairs']), 3369)
        self.assertEqual(result['unmatched_entries'], [])
        self.assertEqual(result['unmatched_exits'], [])
        self.assertEqual(result['observed_identities'],
                         [dict(game=0, rom=1), dict(game=1, rom=3)])
        self.assertEqual(result['threads'], [dict(thread=10284,
                         sites=['game_cleanup', 'outer_update', 'screen_release'])])

    def test_nested_load_pairs_inside_update(self):
        result = report.analyze(capture([event(1), event(2,1), event(3,1,1), event(4,0,1)]))
        self.assertEqual([p['site'] for p in result['pairs']], ['snapshot_load','outer_update'])
        self.assertEqual(result['pairs'][0]['depth'], 1)
        self.assertEqual(result['unmatched_entries'], [])
        self.assertEqual(result['unmatched_exits'], [])

    def test_same_site_different_threads_cannot_pair(self):
        result = report.analyze(capture([event(1), event(2,exiting=1,thread=456)]))
        self.assertEqual(result['pairs'], [])
        self.assertEqual(result['unmatched_entries'], [1])
        self.assertEqual(result['unmatched_exits'], [2])

    def test_faulted_partial_identity_is_not_usable(self):
        result = report.analyze(capture([event(1,readable=0)]))
        self.assertEqual(result['observed_identities'], [])
        self.assertEqual(result['unreadable'], 1)

    def test_missing_footer_is_not_clean_capture(self):
        result = report.analyze(capture([event(1)], footer=None))
        self.assertFalse(result['complete'])
        self.assertIsNone(result['dropped'])

    def test_dropped_records_are_reported(self):
        result = report.analyze(capture([event(1)], footer='# complete,dropped,7'))
        self.assertEqual(result['dropped'], 7)
        self.assertFalse(result['pairing_reliable'])
        self.assertTrue(any('dropped' in text for text in result['cautions']))

    def test_truncated_row_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Partial event row'):
            report.analyze(capture(['1,100,123']))

    def test_duplicate_sequence_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Invalid sequence'):
            report.analyze(capture([event(1), event(1)]))

    def test_failed_install_is_not_a_capture(self):
        with self.assertRaisesRegex(ValueError, 'Install failure'):
            report.analyze(capture([], footer='# install_failed'))


if __name__ == '__main__':
    unittest.main()
