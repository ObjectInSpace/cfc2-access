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

    def test_missing_range_is_not_empty_evidence(self):
        with self.assertRaisesRegex(ValueError, "Missing dump range"):
            self.dump.read(0x10, 8)

    def test_partial_instruction_rejected(self):
        # Real failure encountered while preparing the trace: this endpoint is
        # in the middle of the success-path restore, not the function end.
        with self.assertRaisesRegex(ValueError, "Incomplete instruction span"):
            self.dump.instructions(0x1400B6250, 0x1400B62B0)

    def test_missing_callback_is_not_partial_success(self):
        instructions = self.dump.instructions(0x1405A5A40, 0x1405A5C5C)
        instructions = [i for i in instructions if i.address != 0x1405A5B28]
        with self.assertRaisesRegex(ValueError, "four reviewed"):
            audit.callbacks(instructions)

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
