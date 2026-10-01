"""Offline evidence-tool checks using the installed dump; never attach to a game."""
import copy
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import collection_lifecycle_audit as audit


class LifecycleAuditTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dump = audit.Dump(audit.DEFAULT_DUMP)
        cls.report = audit.produce(audit.DEFAULT_DUMP)

    def test_reviewed_manifest_reproduces(self):
        manifest = json.loads((Path(__file__).resolve().parents[1] /
                               "docs/collection-lifecycle-manifest.json").read_text())
        compact = dict(self.report)
        compact["spans"] = [{k: v for k, v in s.items() if k != "instructions"}
                            for s in compact["spans"]]
        self.assertEqual(compact, manifest)
        self.assertEqual(len(compact["paths"]), 9)
        self.assertEqual(len(compact["roms"]), 18)
        # Explicit falsifiers for the tempting ROM/2 and universal-context rules.
        self.assertEqual(compact["roms"][0]["game_index"], 0)
        self.assertEqual(compact["roms"][16]["game_index"], 3)
        self.assertEqual(compact["roms"][17]["initializer"], "0x140dcb360")
        self.assertEqual(compact["paths"][2]["context_slot"], "0x142cf9d48")

    def test_changed_code_rejected(self):
        sections = [copy.copy(s) for s in self.dump.sections]
        code = next(s for s in sections if s.name == "sec_00001000_code.bin")
        code.data = bytes([code.data[0] ^ 1]) + code.data[1:]
        with patch.object(audit, "read_manifest", return_value=sections):
            with self.assertRaisesRegex(ValueError, "Code differs"):
                audit.Dump(audit.DEFAULT_DUMP)

    def test_initializer_entry_sites_are_instruction_aligned(self):
        expected = {0x140038B1C, 0x140038FD1, 0x14005005B, 0x14008C9E1,
                    0x1400A6C40, 0x1400B0FA3, 0x1400B49DD}
        sites = set()
        for label, start, end in audit.SHARED_SPANS:
            if label.startswith('initializer-entry-') or label == 'cleanup-before-reinitialize':
                for ins in self.dump.instructions(start, end):
                    if ins.mnemonic == 'call' and ins.op_str == '0x14004c540':
                        sites.add(ins.address)
        self.assertEqual(sites, expected)

    def test_dispatcher_has_no_direct_generation_counter_write(self):
        # A visible increment on selected callers is not a universal epoch.
        from capstone import CS_AC_WRITE
        from capstone.x86 import X86_OP_MEM
        for ins in self.dump.instructions(0x14004C540, 0x14004C674):
            for operand in ins.operands:
                self.assertFalse(operand.type == X86_OP_MEM and
                                 operand.access & CS_AC_WRITE and
                                 operand.mem.disp == 0x7dc)

    def test_missing_range_is_not_empty_evidence(self):
        with self.assertRaisesRegex(ValueError, "Missing dump range"):
            self.dump.read(0x10, 8)

    def test_destructor_vtable_target_mutation_rejected(self):
        original = audit.Dump.pointer
        def changed(dump, address):
            return 0 if address == 0x140FE12F0 else original(dump, address)
        with patch.object(audit.Dump, 'pointer', changed):
            with self.assertRaisesRegex(ValueError, 'destructor vtable slot differs'):
                audit.produce(audit.DEFAULT_DUMP)

    def test_partial_instruction_rejected(self):
        # Real failure encountered while preparing the trace: this endpoint is
        # in the middle of the success-path restore, not the function end.
        with self.assertRaisesRegex(ValueError, "Incomplete instruction span"):
            self.dump.instructions(0x1400B6250, 0x1400B62B0)

    def test_screen_release_slot_mutation_rejected(self):
        original = audit.Dump.pointer
        def changed(dump, address):
            return 0 if address == 0x140FDE730 else original(dump, address)
        with patch.object(audit.Dump, 'pointer', changed):
            with self.assertRaisesRegex(ValueError, 'Game-screen vtable slot differs'):
                audit.produce(audit.DEFAULT_DUMP)

    def test_screen_release_closes_callback_object_before_game_cleanup(self):
        calls = [(i.address, i.op_str) for i in self.dump.instructions(0x140037140, 0x140037292)
                 if i.mnemonic == 'call']
        self.assertIn((0x140037179, '0x1400520b0'), calls)
        self.assertIn((0x14003719D, '0x14004adc0'), calls)
        # DL=0 selects close; DL=1 would instead keep the callback path enabled.
        ins = self.dump.instructions(0x140037177, 0x140037179)[0]
        self.assertEqual((ins.mnemonic, ins.op_str), ('xor', 'edx, edx'))

    def test_missing_callback_is_not_partial_success(self):
        instructions = self.dump.instructions(0x1405A5A40, 0x1405A5C5C)
        instructions = [i for i in instructions if i.address != 0x1405A5B28]
        with self.assertRaisesRegex(ValueError, "four reviewed"):
                audit.callbacks(instructions)

    def test_manager_replacement_slot_mutation_rejected(self):
        original = audit.Dump.pointer
        def changed(dump, address):
            return 0 if address == 0x141018BB8 else original(dump, address)
        with patch.object(audit.Dump, 'pointer', changed):
            with self.assertRaisesRegex(ValueError, 'Screen-manager vtable slot differs'):
                audit.produce(audit.DEFAULT_DUMP)

    def test_factory_constructor_mutation_rejected(self):
        original = audit.Dump.pointer
        def changed(dump, address):
            return 0 if address == 0x140FDE6A0 else original(dump, address)
        with patch.object(audit.Dump, 'pointer', changed):
            with self.assertRaisesRegex(ValueError, 'Game-screen factory record differs'):
                audit.produce(audit.DEFAULT_DUMP)

    def test_duplicate_ROM_descriptor_rejected(self):
        pointer = audit.Dump.pointer

        def wrong_pointer(dump, address):
            # Replace the alternate JP descriptor with the ordinary JP name.
            if address == 0x1411F5FE8:
                address = 0x1411F5FD8
            return pointer(dump, address)

        with patch.object(audit.Dump, "pointer", wrong_pointer):
            with self.assertRaisesRegex(ValueError, "exactly 18 names"):
                audit.produce(audit.DEFAULT_DUMP)


if __name__ == "__main__":
    unittest.main()
