import unittest
import collection_probe_report as report


def event(sequence, site=0, exiting=0, thread=123, readable=1, depth=1):
    return ','.join(str(v) for v in (sequence, sequence*100, thread, site, exiting,
        depth, readable, '100000', '200000', 1, 2, 100, 4, 0, 1, 'abc', -17, -1))


def capture(rows, footer='# complete,dropped,0'):
    return '\n'.join(['# boundary_probe,1,qpc_frequency,1000000,duration_ms,120000',
                      ','.join(report.FIELDS), *rows, *([footer] if footer else [])])


class ProbeReportTests(unittest.TestCase):
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
