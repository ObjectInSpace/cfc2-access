"""Offline reproduction and falsifying controls, never live HUD acceptance."""
import copy
import json
import struct
from pathlib import Path
import unittest
from unittest.mock import patch

import collection_lifecycle_audit as lifecycle
import pws2_hud_audit as audit


class HudAuditTests(unittest.TestCase):
    def test_rectangle_halving_selection_and_original_reciprocals(self):
        body = self.instructions('command-four-rectangle-helper')
        for address, instruction in (
            (0x1402E0AD6, 'je 0x1402e0e3e'),
            (0x1402E0BB6, 'cdq'),
            (0x1402E0BB7, 'sub eax, edx'),
            (0x1402E0BBF, 'sar eax, 1'),
            (0x1402E0C59, 'bt ecx, 9'),
            (0x1402E0C5D, 'jae 0x1402e0c7b'),
            (0x1402E0DAB, 'mov qword ptr [rbx + 0x1a08], rdi'),
            (0x1402E0DEF, 'mov eax, dword ptr [rsi + 8]'),
            (0x1402E0DF2, 'sub eax, dword ptr [rsi]'),
            (0x1402E0E0C, 'movss dword ptr [rdi + 0x20], xmm1'),
            (0x1402E0E15, 'movss dword ptr [rdi + 0x24], xmm6'),
        ):
            self.assertEqual(body[address].strip(), instruction)

    def test_strip_expansion_alternates_order_and_replicates_attributes(self):
        body = self.instructions('queued-render-vertex-complete')
        for address, instruction in (
            (0x140F345FD, 'test bpl, 0x40'),
            (0x140F34607, 'test r14d, r14d'),
            (0x140F34610, 'lea r9d, [rcx - 2]'),
            (0x140F34636, 'test r8b, 1'),
            (0x140F346D4, 'movsd xmm0, qword ptr [rdx + 0xc]'),
            (0x140F34701, 'add rdx, 0x28'),
            (0x140F34708, 'add rcx, 0x78'),
            (0x140F34717, 'lea ecx, [r9 + r9*2]'),
            (0x140F34744, 'cmp r10d, -3'),
            (0x140F34762, 'test byte ptr [rax + 0x44f4], 0x10'),
            (0x140F34847, 'test byte ptr [rax + 0x44f4], 0x20'),
        ):
            self.assertEqual(body[address], instruction)

    def test_derived_position_consumer_clamps_w_without_rectangle_origin(self):
        body = self.instructions('renderer-derived-position-consumer')
        self.assertEqual(body[0x1402DE454], 'mov rax, qword ptr [rcx + 0x19f8]')
        self.assertEqual(body[0x1402DE461], 'and rax, 0xfffffffffffffff0')
        self.assertEqual(body[0x1402DE4AD], 'jae 0x1402de4b2')
        self.assertEqual(body[0x1402DE4AF], 'movaps xmm2, xmm0')
        self.assertEqual(body[0x1402DE4C9], 'sub eax, dword ptr [rcx + 0x134]')
        self.assertEqual(body[0x1402DE4DD], 'sub eax, dword ptr [rcx + 0x138]')
        self.assertEqual(body[0x1402DE4F8], 'movss dword ptr [rdx + 0xc], xmm2')

    @classmethod
    def setUpClass(cls):
        cls.evidence = audit.produce()

    def instructions(self, label):
        span = next(s for s in self.evidence['spans'] if s['label'] == label)
        return {int(i['address'], 16): i['instruction'] for i in span['instructions']}

    def test_publisher_is_outer_two_inner_two_with_exclusion(self):
        data = {d['label']: bytes.fromhex(d['bytes']) for d in self.evidence['data']}
        outer = struct.unpack('<11I', data['publisher-outer-state-targets'])
        inner = struct.unpack('<6I', data['publisher-state-targets'])
        self.assertEqual(0x140000000 + outer[2], 0x140A6EE6C)
        self.assertEqual(0x140000000 + inner[2], 0x140A6E555)
        dispatch = self.instructions('display-outer-state-dispatch-prefix')
        self.assertEqual(dispatch[0x140A6EE6C], 'call 0x140a6e250')
        body = self.instructions('display-publisher-state-machine')
        self.assertEqual(body[0x140A6E555], 'inc cl')
        self.assertEqual(body[0x140A6E55C], 'mov byte ptr [rdx + 0x52eaa], cl')
        self.assertEqual(body[0x140A6E5F7], 'cmp byte ptr [rcx + 0x52bd9], 1')
        self.assertEqual(body[0x140A6E608], 'je 0x140a6e6a1')
        self.assertEqual(body[0x140A6E60E], 'call 0x140afa010')

    def test_matrix_build_follows_callbacks_and_keeps_override(self):
        update = self.instructions('list-eight-update-before-matrix-build')
        self.assertEqual(update[0x140A4C7C3], 'call 0x140a4c3e0')
        self.assertEqual(update[0x140A4C7C8], 'mov ecx, 8')
        self.assertEqual(update[0x140A4C7D1], 'jmp 0x140a7c600')
        build = self.instructions('list-resource-matrix-build')
        self.assertEqual(build[0x140A7C668], 'test word ptr [rsi + 0xc4], r12w')
        self.assertEqual(build[0x140A7C79D], 'mov rcx, qword ptr [rbx + 0x78]')
        self.assertEqual(build[0x140A7C7A6], 'lea rcx, [rsi + 0x78]')
        self.assertEqual(build[0x140A7C8B7], 'lea rcx, [rbx + 0x34]')
        self.assertEqual(build[0x140A7C8BB], 'call 0x140f31f40')

    def test_draw_filters_precede_exact_matrix_consumer(self):
        draw = self.instructions('list-eight-draw')
        for address, expected in (
            (0x140A5DED0, 'cmp byte ptr [rdi + 0xc0], 0'),
            (0x140A5DF4E, 'cmp byte ptr [rbx], 0'),
            (0x140A5DF62, 'mov rax, qword ptr [rbx + 8]'),
            (0x140A5DF66, 'test rax, rax'),
            (0x140A5DF6F, 'cmp dword ptr [rax], 0'),
            (0x140A5DF72, 'jl 0x140a5e013'),
            (0x140A5DF78, 'lea rcx, [rbx + 0x34]'),
            (0x140A5DF7C, 'call 0x140f32440'),
        ):
            self.assertEqual(draw[address], expected)

    def test_queue_types_select_different_middle_matrices(self):
        data = {d['label']: bytes.fromhex(d['bytes']) for d in self.evidence['data']}
        queue = struct.unpack('<25I', data['render-queue-type-targets'])
        drain = struct.unpack('<25I', data['render-queue-drain-targets'])
        self.assertEqual(queue[:4], (0xf2f406, 0xf2f406, 0xf2f69b, 0xf2f69b))
        self.assertEqual(drain[:4], (0xf2eb29,) * 4)
        build = self.instructions('render-queue-build')
        # Type 0/1 uses the shared template; type 2/3 reads slot 2.
        span = next(s for s in self.evidence['spans'] if s['label'] == 'render-queue-build')
        load = next(i for i in span['instructions'] if i['address'] == '0x140f2f508')
        self.assertEqual(load['rip_targets'], ['0x141c00250'])
        self.assertEqual(build[0x140F2F767], 'movups xmm0, xmmword ptr [rax + 0x1f812c]')
        self.assertEqual(build[0x140F2F79A], 'movups xmm0, xmmword ptr [rax + 0x1f80ec]')

    def test_matrix_load_does_not_bypass_submission_capacity_or_scalar(self):
        submit = self.instructions('render-packet-segment-submission')
        self.assertEqual(submit[0x140F34D04], 'jae 0x140f34e2b')
        self.assertEqual(submit[0x140F34D6F], 'cmp dword ptr [rcx + 0x18], 0')
        self.assertEqual(submit[0x140F34D77], 'jge 0x140f34e19')
        queue = self.instructions('render-queue-build')
        self.assertEqual(queue[0x140F2F3B6], 'cmp r9d, 0xe0000')
        self.assertEqual(queue[0x140F2F3BD], 'ja 0x140f2fb2f')
        self.assertEqual(queue[0x140F2FA70], 'cmp ecx, 0x800')
        self.assertEqual(queue[0x140F2FA76], 'jae 0x140f2fb10')

    def test_subtraction_is_actor_joined_and_has_conditional_one_floor(self):
        caller = self.instructions('indexed-actor-subtraction-caller')
        self.assertEqual(caller[0x140AD12A9], 'imul rcx, rax, 0x42b8')
        self.assertEqual(caller[0x140AD12B7], 'add rax, 0x40818')
        self.assertEqual(caller[0x140AD12D4], 'call 0x140ad17b0')
        writer = self.instructions('descriptor-driven-actor-subtraction')
        self.assertEqual(writer[0x140AD1906], 'subss xmm0, xmm6')
        self.assertEqual(writer[0x140AD190A], 'movss dword ptr [rbx + 0x1dc], xmm0')
        self.assertEqual(writer[0x140AD1ABE], 'mov dword ptr [rbx + 0x1dc], edi')
        self.assertEqual(writer[0x140AD1B8F], 'mov dword ptr [rbx + 0x1dc], 0x3f800000')

    def test_queue_sort_uses_segment_fields_and_returns_undivided_w(self):
        queue = self.instructions('render-queue-build')
        self.assertEqual(queue[0x140F2F711], 'movss xmm3, dword ptr [rcx + 0x1c]')
        self.assertEqual(queue[0x140F2F720], 'movss xmm0, dword ptr [rcx + 0x10]')
        self.assertEqual(queue[0x140F2F725], 'call 0x140f2ee80')
        sort = self.instructions('render-queue-sort-value')
        self.assertEqual(sort[0x140F2EE8E], 'cmp dword ptr [rax + 0x48], 8')
        self.assertEqual(sort[0x140F2EE9B], 'jbe 0x140f2eeaa')
        self.assertEqual(sort[0x140F2EEB0], 'jbe 0x140f2eec1')
        self.assertEqual(sort[0x140F2EED5], 'call 0x140f331c0')
        self.assertEqual(sort[0x140F2EEF4], 'add rcx, 0x1f816c')
        self.assertEqual(sort[0x140F2EF16], 'call 0x140f31d10')
        self.assertEqual(sort[0x140F2EF1D], 'call 0x140f33130')
        self.assertEqual(sort[0x140F2EF22], 'movss xmm0, dword ptr [rsp + 0x3c]')
        self.assertFalse(any(i.startswith(('divss ', 'divps ')) for i in sort.values()))

    def test_vertex_submission_is_gated_buffer_copy_with_header_advance(self):
        gate = self.instructions('vertex-command-session-gate')
        self.assertEqual(gate[0x1400487CB], 'cmp dword ptr [rax + 0x774], 0')
        self.assertEqual(gate[0x1400487D2], 'je 0x140048808')
        self.assertEqual(gate[0x140048803], 'call 0x14004ca10')
        copy = self.instructions('vertex-command-buffer-copy')
        self.assertEqual(copy[0x14004CA2C], 'imul edi, r9d')
        self.assertEqual(copy[0x14004CA33], 'add r10d, edi')
        self.assertEqual(copy[0x14004CA36], 'cmp r10d, 0x200000')
        self.assertEqual(copy[0x14004CA3D], 'jg 0x14004caa8')
        self.assertEqual(copy[0x14004CA41], 'je 0x14004caa8')
        self.assertEqual(copy[0x14004CA80], 'add rcx, 0x18')
        self.assertEqual(copy[0x14004CA84], 'call 0x140eec5a0')
        self.assertEqual(copy[0x14004CA99], 'lea eax, [rdi + 0x18]')
        self.assertEqual(copy[0x14004CA9C], 'add dword ptr [rsi + 0xded38], eax')

    def test_manifest_reproduces(self):
        path = Path(__file__).resolve().parents[1] / 'docs/pws2-hud-manifest.json'
        self.assertEqual(audit.produce(), json.loads(path.read_bytes()))

    def test_command_walk_uses_count_and_signed_header_length(self):
        reset = self.instructions('command-buffer-reset')
        self.assertEqual(reset[0x140048E9E], 'mov qword ptr [rdx + 0xded30], rax')
        self.assertEqual(reset[0x140048EAE], 'mov qword ptr [rdx + 0xded38], rax')
        walk = self.instructions('command-consumer-walk')
        self.assertEqual(walk[0x140074224], 'mov eax, dword ptr [rcx + 0xded3c]')
        self.assertEqual(walk[0x14007422A], 'mov rdx, qword ptr [rcx + 0xded18]')
        self.assertEqual(walk[0x1400742C0], 'movsxd rax, dword ptr [rdx + 4]')
        self.assertEqual(walk[0x1400742C7], 'add rdx, 0x18')
        self.assertEqual(walk[0x1400742CB], 'add rdx, rax')
        loop = self.instructions('command-consumer-loop-back')
        self.assertEqual(loop[0x14007436D], 'cmp ecx, dword ptr [rbp + 0x128]')
        self.assertEqual(loop[0x140074373], 'jl 0x1400742c0')

    def test_command_four_argument_order_and_derived_storage(self):
        dispatch = self.instructions('command-consumer-dispatch-through-four')
        self.assertEqual(dispatch[0x140074906], 'cmp eax, 4')
        self.assertEqual(dispatch[0x14007490F], 'lea rdx, [rbx + 0x58]')
        self.assertEqual(dispatch[0x140074926], 'lea r8, [rsp + 0x40]')
        self.assertEqual(dispatch[0x140074946], 'call 0x1402e1b90')
        setup = self.instructions('command-four-setup-helper')
        self.assertEqual(setup[0x1402E353E], 'mov qword ptr [rdi + 0x1a28], rbx')
        self.assertEqual(setup[0x1402E3934], 'mov qword ptr [rdi + 0x19f8], rbx')

    def test_command_seven_transposes_but_uploads_only_twelve_words(self):
        instructions = self.instructions('command-seven-transpose-upload')
        self.assertEqual(instructions[0x140074C1C], 'cmp eax, 7')
        self.assertEqual(instructions[0x140074CD3], 'mov qword ptr [r14 + 0x1a38], r9')
        self.assertEqual(instructions[0x140074CE5], 'je 0x14007433d')
        # Recover the source->scratch mapping from the actual scalar moves.
        registers, scratch = {}, {}
        import re
        for address, instruction in instructions.items():
            if not 0x140074CEB <= address < 0x140074DA5:
                continue
            load = re.fullmatch(r'movss (xmm\d), dword ptr \[rbx \+ (0x[0-9a-f]+)\]', instruction)
            store = re.fullmatch(r'movss dword ptr \[rsp \+ (0x[0-9a-f]+)\], (xmm\d)', instruction)
            if load:
                registers[load[1]] = (int(load[2], 16) - 0x18) // 4
            if store:
                scratch[(int(store[1], 16) - 0x40) // 4] = registers[store[2]]
        self.assertEqual([scratch[i] for i in range(16)],
                         [0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15])
        self.assertEqual(instructions[0x140074DE7], 'cmp r10d, 2')
        self.assertEqual(instructions[0x140074DEB], 'jbe 0x140074db0')

    def test_changed_layout_and_gauge_operands_rejected(self):
        original = audit.Dump.read
        for _, target, _ in audit.DATA:
            with self.subTest(address=hex(target)):
                def changed(dump, address, size):
                    raw = original(dump, address, size)
                    return bytes([raw[0] ^ 1]) + raw[1:] if address == target else raw
                with patch.object(audit.Dump, 'read', changed):
                    with self.assertRaisesRegex(ValueError, 'HUD operand differs'):
                        audit.produce()

    def test_swapped_owner_and_layout_store_rejected(self):
        sections = [copy.copy(s) for s in lifecycle.read_manifest(audit.DEFAULT_DUMP)]
        code = next(s for s in sections if s.name == 'sec_00001000_code.bin')
        raw = bytearray(code.data)
        # Change object +2 (actor owner) to +1 (layout) at publication.
        raw[0x140AFB458 - code.live] = 1
        code.data = bytes(raw)
        with patch.object(lifecycle, 'read_manifest', return_value=sections):
            with self.assertRaisesRegex(ValueError, 'Code differs'):
                audit.produce()

    def test_incomplete_publication_instruction_rejected(self):
        with patch.object(audit, 'SPANS', (('partial', 0x140AFB455, 0x140AFB458),)):
            with self.assertRaisesRegex(ValueError, 'Incomplete instruction span'):
                audit.produce()


if __name__ == '__main__':
    unittest.main()
