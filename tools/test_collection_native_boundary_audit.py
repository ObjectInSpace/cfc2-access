"""Regression controls for static evidence, not native concurrency tests."""
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import collection_native_boundary_audit as audit


class NativeBoundaryAuditTests(unittest.TestCase):
    def test_manifest_reproduces(self):
        path = Path(__file__).resolve().parents[1] / 'docs/collection-native-boundary-manifest.json'
        self.assertEqual(audit.compact(audit.produce()), json.loads(path.read_bytes()))

    def test_changed_boundary_evidence_is_refused(self):
        # Change operands without changing instruction lengths. Each site must
        # fail its explicit operation/reference/slot assertion, not just a hash.
        sites = (0x140057CCD, 0x140057CD6, 0x141018B90,
                 0x1406A98DA + 3, 0x1406A9A3E + 2,
                 0x140039254 + 1, 0x1406AA99D + 3,
                 0x1401A5C74 + 3, 0x1400485F6 + 2,
                 0x14020BD77 + 2, 0x14020BDD4 + 2,
                 0x140057CEE + 2, 0x14017AE78 + 4,
                 0x1401F80A7 + 2, 0x140FED3A0,
                 0x1400A6F50 + 7*4, 0x1400A6EF8 + 14*4,
                 0x141207D30 + 8*4, 0x1400A760A + 1,
                 0x140038C6E + 3, 0x140179E8B + 4,
                 0x14056E0B4 + 1, 0x1401F7E9C + 2,
                 0x1400A3297 + 3, 0x1411F7C70,
                 0x140038E8E + 2, 0x140038EC5 + 3,
                 0x140FEFAC8, 0x1400B499E + 2,
                 0x1400B49D1 + 1, 0x1400B5784 + 4*4,
                 0x1400B5727 + 2, 0x14056DCC6 + 6,
                 0x140038843 + 2, 0x1406D66BF + 2, 0x14070005D + 2,
                 0x1401F6CFA + 1, 0x1401F7E33 + 3,
                 0x1401F7E8B + 3, 0x14056E0AB + 2,
                 0x14056E485 + 3, 0x141069370,
                 0x14056DE4C + 3, 0x14056DE79 + 3,
                 0x1401F6731 + 1, 0x1401F673E + 2,
                 0x1401F65EA + 2, 0x1401F6654 + 7,
                 0x1401F6779 + 3, 0x1401F84FB + 2,
                 0x1401F8698 + 3, 0x1401F869C + 2,
                 0x1401F86CC, 0x140FE21B8,
                 0x14017B7DC + 2, 0x14056E177 + 2,
                 0x1401798F2 + 3, 0x14017AD10 + 3)
        original = audit.Dump.read
        for address in sites:
            with self.subTest(address=hex(address)):
                def changed(dump, start, size):
                    raw = bytearray(original(dump, start, size))
                    if start <= address < start + size:
                        raw[address - start] ^= 1
                    return bytes(raw)
                with patch.object(audit.Dump, 'read', changed):
                    with self.assertRaisesRegex(ValueError, 'boundary .* differs'):
                        audit.produce()

    def test_local_ordering_does_not_claim_lifetime(self):
        report = audit.produce()
        self.assertTrue(report['boundary']['application_caller_resolved'])
        for key in ('application_exclusive_thread_proven',
                    'shared_lifetime_guard_proven', 'lifetime_protection_proven',
                    'worker_native_read_allowed'):
            self.assertFalse(report['boundary'][key])
        self.assertTrue(report['training']['ordinary_update_skipped_on_this_path'])
        self.assertFalse(report['training']['complete_mutation_inventory'])
        self.assertFalse(report['training']['live_UI_reset_mapping'])

    def test_scheduler_and_mode_evidence_limits(self):
        report = audit.produce()
        self.assertTrue(report['scheduler']['same_path_update_before_delete'])
        self.assertFalse(report['scheduler']['wait_return_checked'])
        self.assertFalse(report['scheduler']['all_async_tasks_covered'])
        self.assertEqual(report['character_change']['modes'], ['Versus', 'Training'])
        self.assertTrue(report['character_change']['requires_new_generation'])
        self.assertFalse(report['character_change']['live_verified'])

    def test_worker_and_coin_route_limits(self):
        report = audit.produce()
        self.assertTrue(report['scheduler']['pause_task_registration_resolved'])
        self.assertEqual(report['scheduler']['pause_task_category'], 3)
        self.assertTrue(report['scheduler']['generic_caller_polls_completion'])
        self.assertFalse(report['scheduler']['ring_reuse_safety_proven'])
        self.assertTrue(report['training_input']['configured_coin_route_resolved'])
        self.assertFalse(report['training_input']['live_verified'])
        self.assertFalse(report['training']['complete_mutation_inventory'])

    def test_ring_completion_evidence_limits(self):
        scheduler = audit.produce()['scheduler']
        self.assertEqual(scheduler['completion_distance_limit_exclusive'], 4096)
        self.assertFalse(scheduler['submitter_capacity_check_present'])
        self.assertTrue(scheduler['consume_index_advances_before_callback'])
        self.assertTrue(scheduler['callback_pointer_cleared_before_call'])
        self.assertFalse(scheduler['poll_retry_rechecks_submission_distance'])
        self.assertTrue(scheduler['group_destructor_deletes_members'])
        self.assertFalse(scheduler['live_geometry_arena_access_proven'])

    def test_equality_bypass_schedule_model(self):
        # Conditional schedule witness, not a live game/concurrency test:
        # worker A has claimed job zero and is paused in its callback;
        # worker B completes all later jobs. No slot has been overwritten.
        # The modeled unsigned subtraction/branch is pinned by the audit.
        scheduler = audit.produce()['scheduler']
        limit = scheduler['completion_distance_limit_exclusive']
        first = scheduler['generic_initial_ticket']
        for count in (limit - 1, limit):
            with self.subTest(submissions=count):
                contexts = [True] + [False] * (count - 1)
                producer = consumer = first + count
                outstanding_callbacks = {first}
                blocked = []
                bypassed = []
                for saved in range(first, first + count):
                    distance = (producer - saved) & ((1 << 64) - 1)
                    if distance >= limit:
                        bypassed.append(saved)
                    elif contexts[saved - first]:
                        blocked.append(saved)
                self.assertEqual(producer, consumer)  # Queue empty, job still running.
                self.assertEqual(outstanding_callbacks, {first})
                self.assertEqual(blocked, [first] if count < limit else [])
                self.assertEqual(bypassed, [] if count < limit else [first])

    def test_outer_batch_and_later_phases_do_not_certify_generic_completion(self):
        row = audit.produce()['scheduler']
        self.assertEqual(row['generic_startup_worker_limit'], 8)
        self.assertEqual(row['generic_startup_wake_limit'], 4096)
        self.assertTrue(row['batch_kind_zero_signals_after_own_callbacks'])
        self.assertFalse(row['batch_kind_zero_drains_generic_ring'])
        self.assertTrue(row['later_unit_phases_resolved'])
        self.assertFalse(row['later_group_phases_explicit_generic_barrier'])
        self.assertFalse(row['all_async_tasks_covered'])
    def test_versus_rematch_and_geometry_evidence_limits(self):
        report = audit.produce()
        rematch = report['versus_rematch']
        self.assertEqual((rematch['cycle_category'], rematch['dialog_category']), (4, 25))
        self.assertNotEqual(rematch['yes_flag'], rematch['no_flag'])
        self.assertTrue(rematch['requires_new_generation'])
        self.assertTrue(rematch['result_signal_producer_resolved'])
        self.assertFalse(rematch['complete_outcome_coverage'])
        self.assertFalse(rematch['live_verified'])
        self.assertEqual(report['scheduler']['generic_caller_type'], 'uGeometry2Group')
        self.assertFalse(report['scheduler']['geometry_threshold_is_capacity'])
        self.assertFalse(report['scheduler']['ring_reuse_safety_proven'])


if __name__ == '__main__':
    unittest.main()
