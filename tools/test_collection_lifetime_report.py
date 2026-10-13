"""Synthetic outcome controls, never a claim of a live successful wait."""
import unittest

import collection_probe_report as report


def row(sequence, site=11, exiting=0, **changes):
    values = dict.fromkeys(report.FIELDS + report.LIFETIME_FIELDS, 0)
    values.update(sequence=sequence, ticks=sequence*10, thread=12, site=site,
                  exit=exiting, readable=1, invocation=1, application_invocation=7,
                  return_site='1401f80ad', wait_count=2, wait_all=1,
                  timeout=0xffffffff, handles_readable=1, handles='111;222', result=-1)
    values.update(changes)
    return ','.join(str(values[k]) for k in report.FIELDS + report.LIFETIME_FIELDS)


def capture(*rows, footer='# complete,dropped,0'):
    return '\n'.join(['# boundary_probe,3,qpc_frequency,1000000,duration_ms,120000',
                      ','.join(report.FIELDS + report.LIFETIME_FIELDS), *rows,
                      *([footer] if footer else [])])


class LifetimeReportTests(unittest.TestCase):
    def test_api_results_are_not_lifetime_proof(self):
        for raw, name in ((0,'signaled'), (1,'signaled'), (-1,'failed'),
                          (258,'timeout'), (128,'abandoned'), (129,'abandoned'),
                          (130,'unknown'), (1234567,'unknown')):
            with self.subTest(raw=raw):
                result = report.analyze(capture(row(1), row(2, exiting=1, result=raw, last_error=6)))
                self.assertTrue(result['pairing_reliable'])
                self.assertFalse(result['lifetime_proven'])
                wait = result['wait_outcomes'][0]
                self.assertEqual(wait['outcome'], name)
                self.assertEqual(wait['raw_result'], raw & 0xffffffff)
                self.assertEqual(wait['last_error_meaningful'], raw == -1)
                self.assertEqual(wait['handles'], [0x111,0x222])

    def test_exact_call_pairing(self):
        for key, value in (('invocation',2), ('application_invocation',8),
                           ('handles','111;333'), ('wait_all',0), ('timeout',0)):
            with self.subTest(key=key):
                result = report.analyze(capture(row(1), row(2, exiting=1, **{key:value})))
                self.assertFalse(result['pairing_reliable'])
                self.assertEqual(result['wait_outcomes'], [])
                self.assertEqual(result['unmatched_entries'], [1])

    def test_no_wait_is_not_a_success(self):
        result = report.analyze(capture())
        self.assertEqual(result['wait_outcomes'], [])
        self.assertFalse(result['lifetime_proven'])

    def test_loss_and_window_edges(self):
        for text in (capture(row(1)), capture(row(1), row(2, exiting=1), footer=''),
                     capture(row(1), row(2, exiting=1), footer='# complete,dropped,1')):
            self.assertFalse(report.analyze(text)['pairing_reliable'])

    def test_wait_caller_and_bounds_refused(self):
        for changes in ({'return_site':'1234'}, {'handles':'111'}, {'invocation':0},
                        {'wait_count':65}, {'handles_readable':0}, {'last_error':-1}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                report.analyze(capture(row(1, **changes)))

    def test_unreadable_handle_copy_does_not_erase_outcome(self):
        result = report.analyze(capture(
            row(1, handles_readable=0, handles=''),
            row(2, exiting=1, result=-1, handles_readable=0, handles='', last_error=6)))
        wait = result['wait_outcomes'][0]
        self.assertFalse(wait['handles_readable'])
        self.assertEqual(wait['outcome'], 'failed')

    def test_invocation_reuse_refused(self):
        with self.assertRaisesRegex(ValueError, 'invocation identity'):
            report.analyze(capture(row(1), row(2, exiting=1), row(3)))

    def test_worker_wait_has_no_invented_application_owner(self):
        args = dict(site=10, return_site='1401f847f', application_invocation=0,
                    wait_count=1, wait_all=0, handles='111')
        result = report.analyze(capture(row(1, **args), row(2, exiting=1, result=0, **args)))
        wait = result['wait_outcomes'][0]
        self.assertEqual(wait['application_invocation'], 0)
        self.assertEqual(wait['purpose'], 'batch_wake')


if __name__ == '__main__':
    unittest.main()
