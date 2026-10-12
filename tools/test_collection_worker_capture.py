"""Synthetic bounds/failure controls, not live worker completion evidence."""
import json
import gzip
import hashlib
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

import collection_worker_capture as capture
from collection_lifecycle_audit import Dump, DEFAULT_DUMP


class Memory:
    def __init__(self):
        self.blocks = {}
        self.calls = []
        for address, value in ((capture.APP, 0x100000), (capture.MANAGER, 0x200000),
                               (capture.SESSION, 0)):
            self.blocks[address] = struct.pack('<Q', value)
        header = bytearray(0x30)
        struct.pack_into('<IQQ', header, 0x14, 2, 4097, 4097)
        self.blocks[0x100098] = header
        self.blocks[0x100078] = bytes(8)
        self.blocks[0x13FF78] = struct.pack('<Q', 0x200000)
        self.blocks[0x11FAC8] = bytearray(4096 * 32)
        self.blocks[0x200C38] = struct.pack('<I', 1)
        self.blocks[0x201848] = struct.pack('<Q', (1 << 64) - 1)
        self.blocks[0x200048] = struct.pack('<IIQQ', 1, 0, 0x300000, 0x300000)
        self.unit(0x300000, capture.GROUP_VTABLE, capture.GROUP_UPDATE)
        group = bytearray(0x58)
        struct.pack_into('<I', group, 0, 1)
        struct.pack_into('<Q', group, 0x10, 0x400000)
        group[0x50] = 1
        struct.pack_into('<I', group, 0x54, 10)
        self.blocks[0x300060] = group
        self.blocks[0x400000] = struct.pack('<Q', 0x500000)
        self.unit(0x500000, 0x600000, 0x140123456)

    def unit(self, address, vtable, update, next_address=0):
        raw = bytearray(0x38)
        struct.pack_into('<Q', raw, 0, vtable)
        struct.pack_into('<I', raw, 0x10, 0x402)
        struct.pack_into('<Q', raw, 0x18, next_address)
        self.blocks[address] = raw
        self.blocks[vtable + 0x40] = struct.pack('<Q', update)

    def read(self, address, size):
        self.calls.append((address, size))
        if address not in self.blocks:
            raise OSError('Synthetic unreadable memory')
        return bytes(self.blocks[address][:size])


class WorkerCaptureTests(unittest.TestCase):
    def test_recorded_training_inventory_retains_mode_and_training_units(self):
        path = Path(__file__).parent / 'fixtures/collection-worker-training-20261012.json.gz'
        raw = path.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         '347229ce3a273955a1d6db73cd6841556c77537e659588790e3d73b93261b646')
        report = json.loads(gzip.decompress(raw))['report']
        self.assertEqual(report['observed_mode'], 'Training')
        self.assertEqual(len(report['samples']), 12)
        self.assertEqual(sum(s['status'] == 'changed' for s in report['samples']), 3)
        for sample in report['samples']:
            self.assertFalse(sample['lifetime_proven'])
            for side in ('before', 'after'):
                row = sample[side]
                self.assertEqual((row['identity']['game'], row['identity']['rom']), (1, 3))
                self.assertEqual(row['identity']['raw_session_fields']['0x80c'], 0)
                self.assertEqual(row['identity']['raw_session_fields']['0x828'], 1)
                self.assertEqual((row['scheduler']['producer'], row['scheduler']['consumer']), (1, 1))
                self.assertEqual(row['scheduler']['generic_workers'], 8)
                self.assertEqual(row['ring'], [])
                self.assertEqual(row['errors'], [])
                self.assertEqual(row['limits'], [])
                self.assertEqual(len(row['nodes']), 8)
                self.assertFalse(any(n['known_group_layout'] for n in row['nodes']))
                vtables = {n['vtable'] for n in row['nodes']}
                self.assertTrue({0x140FE5818, 0x140FEA3E8, 0x140FEA618} <= vtables)
                self.assertNotIn(0x140FEFA88, vtables)
                self.assertEqual([c['category'] for c in row['categories'] if c['head']],
                                 [0, 1, 2, 3, 4, 5])
                self.assertTrue(all(c['flags'] == 0x3FD for c in row['categories']))

    def test_recorded_versus_inventory_retains_mode_and_cycle_unit(self):
        path = Path(__file__).parent / 'fixtures/collection-worker-versus-20261012.json.gz'
        raw = path.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         '6840974e1f9c5968052269498e0d29005aafdacc5201841fa4781b989adc3dd2')
        report = json.loads(gzip.decompress(raw))['report']
        self.assertEqual(report['observed_mode'], 'Versus')
        self.assertEqual(sum(s['status'] == 'changed' for s in report['samples']), 2)
        for sample in report['samples']:
            for side in ('before', 'after'):
                row = sample[side]
                self.assertEqual(row['identity']['raw_session_fields']['0x80c'], 1)
                self.assertEqual(row['identity']['raw_session_fields']['0x828'], 0)
                self.assertEqual((row['scheduler']['producer'], row['scheduler']['consumer']), (1, 1))
                self.assertEqual(row['ring'], [])
                self.assertFalse(any(n['known_group_layout'] for n in row['nodes']))
                self.assertTrue(any(n['vtable'] == 0x140FEFA88 for n in row['nodes']))
                self.assertFalse(row['lifetime_proven'])

    def test_recorded_arcade_inventory_is_bounded_observation(self):
        path = Path(__file__).parent / 'fixtures/collection-worker-arcade-20261012.json.gz'
        raw = path.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         'b2c4205cd5f2ab9b067c66aed600f495fc641cae60d68e064870bc0c28aac3e6')
        fixture = json.loads(gzip.decompress(raw))
        samples = fixture['sources']['series']['report']['samples']
        self.assertEqual(len(samples), 12)
        self.assertEqual(sum(s['status'] == 'changed' for s in samples), 1)
        for sample in samples:
            self.assertFalse(sample['lifetime_proven'])
            for side in ('before', 'after'):
                row = sample[side]
                self.assertEqual((row['identity']['game'], row['identity']['rom']), (1, 3))
                self.assertEqual((row['scheduler']['producer'], row['scheduler']['consumer']), (1, 1))
                self.assertEqual(row['scheduler']['generic_workers'], 8)
                self.assertEqual(row['ring'], [])
                self.assertEqual(row['errors'], [])
                self.assertEqual(row['limits'], [])
                self.assertFalse(any(n['known_group_layout'] for n in row['nodes']))
                self.assertEqual(row['categories'][3]['flags'], 0x3FD)
        imports = fixture['sources']['imports']['report']
        self.assertFalse(imports['wait_outcomes_observed'])
        expected = {'0x140fdc0c8': 'WaitForMultipleObjects',
                    '0x140fdc350': 'WaitForSingleObject', '0x140fdc3a0': 'SetEvent',
                    '0x140fdc3a8': 'ResetEvent', '0x140fdc3b0': 'ReleaseSemaphore',
                    '0x140fdc3e0': 'CreateSemaphoreA', '0x140fdc3c0': 'CreateThread',
                    '0x140fdc3d0': 'ResumeThread', '0x140fdc1d0': 'CloseHandle'}
        self.assertEqual(len(imports['imports']), len(expected))
        for row in imports['imports']:
            self.assertTrue(row['resolved'])
            self.assertEqual(row['exports'], [expected[row['slot']]])
            self.assertEqual(row['live_prefix'], row['file_prefix'])

    def test_signatures_match_reviewed_dump(self):
        manifest = json.loads(capture.MANIFEST.read_bytes())
        dump = Dump(DEFAULT_DUMP)
        self.assertEqual(len(capture.verify_build(dump.read, manifest)), len(manifest['spans']))

    def test_inventory_links_categories_members_and_callbacks(self):
        memory = Memory()
        report = capture.capture(memory.read)
        self.assertEqual(report['status'], 'matching')
        row = report['before']
        self.assertEqual(row['categories'][0]['flags'], 1)
        self.assertEqual(row['nodes'][0]['group']['count'], 1)
        self.assertEqual(row['nodes'][1]['update'], 0x140123456)
        self.assertFalse(report['lifetime_proven'])
        self.assertLess(row['bytes'], capture.MAX_BYTES)

    def test_empty_queue_retains_context_with_cleared_callback(self):
        memory = Memory()
        struct.pack_into('<QQQI', memory.blocks[0x11FAC8], 32, 0x300000, 0, 0, 0)
        row = capture.observe(memory.read)
        self.assertEqual(row['scheduler']['producer'], row['scheduler']['consumer'])
        self.assertEqual(row['ring'][0]['context'], 0x300000)
        self.assertEqual(row['ring'][0]['callback'], 0)
        self.assertFalse(row['lifetime_proven'])

    def test_cycle_terminates_and_preserves_edge(self):
        memory = Memory()
        struct.pack_into('<Q', memory.blocks[0x300000], 0x18, 0x300000)
        row = capture.observe(memory.read)
        self.assertEqual(len(row['nodes']), 2)
        self.assertTrue(any(e['source'] == e['target'] for e in row['edges']))

    def test_huge_group_count_never_becomes_unbounded_read(self):
        memory = Memory()
        struct.pack_into('<I', memory.blocks[0x300060], 0, 0xFFFFFFFF)
        row = capture.capture(memory.read)
        self.assertEqual(row['status'], 'limited')
        self.assertNotIn((0x400000, 0xFFFFFFFF * 8), memory.calls)

    def test_node_budget_and_unknown_group_vtable_are_explicit(self):
        memory = Memory()
        with patch.object(capture, 'MAX_NODES', 1):
            self.assertIn('node_budget', capture.observe(memory.read)['limits'])
        memory.unit(0x300000, 0x600000, capture.GROUP_UPDATE)
        row = capture.observe(memory.read)
        self.assertTrue(row['limits'])
        self.assertNotIn('group', row['nodes'][0])

    def test_unreadable_and_short_reads_are_not_matching(self):
        for bad in (None, b'\0'):
            memory = Memory()
            if bad is None:
                del memory.blocks[0x300000]
            else:
                memory.blocks[0x300000] = bad
            self.assertEqual(capture.capture(memory.read)['status'], 'unreadable')

    def test_changed_producer_is_retained(self):
        memory = Memory()
        reads = 0
        def read(address, size):
            nonlocal reads
            if address == 0x100098:
                reads += 1
                struct.pack_into('<Q', memory.blocks[address], 0x18, 4096 + reads)
            return memory.read(address, size)
        report = capture.capture(read)
        self.assertEqual(report['status'], 'changed')
        self.assertEqual(report['after']['scheduler']['producer'], 4098)

    def test_invalid_category_count_and_read_budget_stop_reads(self):
        memory = Memory()
        memory.blocks[0x200C38] = struct.pack('<I', 65)
        row = capture.observe(memory.read)
        self.assertIn('category_count_above_reviewed_layout', row['limits'])
        self.assertEqual(row['categories'], [])
        with patch.object(capture, 'MAX_READS', 2):
            row = capture.observe(Memory().read)
            self.assertEqual(row['reads'], 2)
            self.assertIn('read_budget', row['limits'])


if __name__ == '__main__':
    unittest.main()
