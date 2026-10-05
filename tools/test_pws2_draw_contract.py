"""Synthetic byte fixtures only; no attachment, native guards or live claims."""
import struct
import unittest

import pws2_draw_contract as c


class DrawContractTests(unittest.TestCase):
    def setUp(self):
        self.root, self.obj, self.context, self.packet = 0x100000, 0x200000, 0x300000, 0x600000
        self.resource = self.root + 0x55010
        obj = bytearray(0xE0)
        obj[0:3] = bytes((8, 0, 1))
        obj[0xC0] = 1
        struct.pack_into('<Q', obj, 8, c.CALLBACKS[0])
        struct.pack_into('<i', obj, 0x28, 2)
        struct.pack_into('<i', obj, 0xD8, 8)
        res = bytearray(0xA8)
        res[0] = 1
        struct.pack_into('<Q', res, 8, self.packet)
        slots = bytearray(0x108)
        struct.pack_into('<i', slots, 0, 1)
        self.memory = {
            c.ROOT_SLOT: struct.pack('<Q', self.root),
            c.CONTEXT_SLOT: struct.pack('<Q', self.context),
            self.obj: obj,
            self.obj + 0xE0: bytearray(struct.pack('<8Q', self.resource, *range(7))),
            self.resource: res,
            self.packet: bytearray(24),
            self.context + 0x1F80A4: slots,
            0x141C00210: bytearray(128),
        }
        self.calls = []

    def read(self, address, size):
        self.calls.append((address, size))
        if address not in self.memory:
            raise OSError('unreadable')
        return bytes(self.memory[address][:size])

    def capture(self, **overrides):
        args = dict(root=self.root, object_address=self.obj,
                    resource_address=self.resource, matrix_address=self.resource + 0x34,
                    caller=c.CALLER, invocation=1, revision=0)
        args.update(overrides)
        return c.capture_matrix_load(self.read, **args)

    def test_exact_budget_and_state_two(self):
        row = self.capture()
        self.assertEqual(row['status'], 'matching_brackets_only')
        self.assertEqual((row['read_attempts'], row['requested_bytes']), (10, 904))
        self.assertEqual(row['state_raw'], 2)
        self.assertFalse(row['audio_eligible'])

    def test_owner_does_not_follow_compacted_layout(self):
        for owner, layout in ((1, 0), (3, 1)):
            self.memory[self.obj][1:3] = bytes((layout, owner))
            row = self.capture()
            self.assertEqual((row['owner_raw'], row['layout_raw']), (owner, layout))

    def test_special_layout_and_invalid_owner(self):
        self.memory[self.obj][1:3] = bytes((255, 3))
        struct.pack_into('<Q', self.memory[self.obj], 8, c.CALLBACKS[1])
        self.assertEqual(self.capture()['status'], 'matching_brackets_only')
        self.memory[self.obj][2] = 255
        self.assertIn('invalid_owner', self.capture()['issues'])

    def test_bad_stage_and_pointer_arithmetic_do_no_reads(self):
        for change in (dict(caller=0), dict(invocation=0), dict(matrix_address=0),
                       dict(object_address=c.U64_MAX), dict(resource_address=self.resource + 1)):
            with self.subTest(change=change):
                self.calls.clear()
                self.assertEqual(self.capture(**change)['status'], 'rejected')
                self.assertEqual(self.calls, [])

    def test_counts_rejected_before_array_access(self):
        for count in (-1, 0, 9, 0x7FFFFFFF):
            struct.pack_into('<i', self.memory[self.obj], 0xD8, count)
            self.calls.clear()
            self.assertIn('resource_count', self.capture()['issues'])
            self.assertEqual(len(self.calls), 3)

    def test_missing_and_duplicate_association(self):
        for pointers in ((0,) * 8, (self.resource,) * 8):
            self.memory[self.obj + 0xE0] = struct.pack('<8Q', *pointers)
            self.assertIn('association_ambiguous', self.capture()['issues'])

    def test_each_read_failure_and_short_read_is_bounded(self):
        good = self.read
        for fail_at in range(1, 11):
            for short in (False, True):
                attempts = []
                def failing(address, size):
                    attempts.append((address, size))
                    if len(attempts) == fail_at:
                        if short:
                            return bytes(size - 1)
                        raise OSError('injected_fault')
                    return good(address, size)
                self.read = failing
                row = self.capture()
                self.assertEqual(row['status'], 'rejected')
                self.assertEqual(row['read_attempts'], fail_at)
                self.assertLessEqual(row['requested_bytes'], 904)
        self.read = good

    def test_identity_change_and_nonfinite_matrices(self):
        good = self.read
        hits = 0
        def changed(address, size):
            nonlocal hits
            if address == c.ROOT_SLOT:
                hits += 1
                if hits == 2:
                    return struct.pack('<Q', self.root + 8)
            return good(address, size)
        self.read = changed
        self.assertIn('identity_changed', self.capture()['issues'])
        self.read = good
        struct.pack_into('<f', self.memory[self.resource], 0x34, float('nan'))
        self.assertIn('nonfinite_matrix', self.capture()['issues'])

    def test_gate_disagreements_remain_explicit(self):
        self.memory[self.obj][0xC0] = 0
        self.memory[self.resource][0:2] = bytes((0, 1))
        struct.pack_into('<i', self.memory[self.packet], 0, -1)
        struct.pack_into('<i', self.memory[self.context + 0x1F80A4], 0, 2)
        self.assertEqual(set(self.capture()['issues']), {
            'disabled_object', 'disabled_resource', 'unreviewed_resource_type',
            'negative_packet_header', 'wrong_selected_slot'})

    def test_reuse_and_repeated_resources_are_separate_events(self):
        first = self.capture()
        self.memory[self.obj][2] = 3
        second = self.capture(invocation=2, revision=1)
        self.assertEqual((first['owner_raw'], second['owner_raw']), (1, 3))
        self.assertNotEqual(first['records']['object'], second['records']['object'])
        # An opaque native link cycle is never followed by this reader.
        struct.pack_into('<Q', self.memory[self.obj], 0x18, self.obj)
        self.assertEqual(self.capture()['read_attempts'], 10)

    def test_budget_reader_charges_faults_and_refuses_extra_work(self):
        reader = c.BudgetReader(self.read)
        for _ in range(10):
            reader.take(c.ROOT_SLOT, 8)
        with self.assertRaisesRegex(ValueError, 'read_budget'):
            reader.take(c.ROOT_SLOT, 8)
        reader = c.BudgetReader(self.read)
        with self.assertRaisesRegex(ValueError, 'read_budget'):
            reader.take(c.ROOT_SLOT, 905)
        self.assertEqual(reader.calls, 0)


def command(command_id, payload, items=1):
    return struct.pack('<6i', command_id, len(payload), items, -1, 2, 3) + payload


class CommandSnapshotTests(unittest.TestCase):
    def test_matrix_order_and_three_transposed_rows(self):
        # Distinct integer bit patterns expose order; identity matrices would not.
        payload = struct.pack('<16I', *range(16))
        blob = command(4, bytes(128)) + command(7, payload)
        rows = c.decode_commands(blob, 2)
        self.assertEqual(rows[1]['offset'], 152)
        self.assertEqual(struct.unpack('<12I', bytes.fromhex(rows[1]['uploaded_rows'])),
                         (0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14))

    def test_truncated_or_negative_lengths_and_trailing_bytes(self):
        for blob, count in ((b'', 1), (bytes(23), 1),
                            (struct.pack('<6i', 7, -1, 1, 0, 0, 0), 1),
                            (struct.pack('<6i', 7, 0x7FFFFFFF, 1, 0, 0, 0), 1),
                            (command(7, bytes(64))[:-1], 1), (b'x', 0)):
            with self.subTest(blob_length=len(blob)), self.assertRaises(ValueError):
                c.decode_commands(blob, count)

    def test_matrix_sizes_counts_and_hard_caps(self):
        for blob, count in ((command(4, bytes(64)), 1), (command(7, bytes(128)), 1),
                            (command(7, bytes(64), 2), 1), (b'', -1), (b'', 65),
                            (bytes(65537), 0)):
            with self.assertRaises(ValueError):
                c.decode_commands(blob, count)
        self.assertEqual(c.decode_commands(b'', 0), [])

    def test_unknown_command_is_raw_and_nan_bits_are_preserved(self):
        payload = bytes.fromhex('0100c07f') * 16
        rows = c.decode_commands(command(99, b'raw') + command(7, payload), 2)
        self.assertEqual(rows[0]['payload'], b'raw'.hex())
        self.assertEqual(rows[1]['uploaded_rows'], payload[:48].hex())


if __name__ == '__main__':
    unittest.main()
