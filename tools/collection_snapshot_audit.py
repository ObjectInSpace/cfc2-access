"""Pinned shared snapshot callbacks; offline evidence, not runtime hooks."""
import argparse
import json
from pathlib import Path

from collection_lifecycle_audit import Dump, DEFAULT_DUMP, CODE_SHA256, rip_targets
from collection_restore_audit import require, compact

SPANS = (
    ('join-match-type-registration', 0x1400282B0, 0x140028300),
    ('join-match-constructor', 0x14008F220, 0x14008F28F),
    ('join-match-type-getter', 0x14008F380, 0x14008F388),
    ('join-match-update', 0x14008F390, 0x14009027B),
    ('callback-request-type-registration', 0x140027010, 0x14002705E),
    ('callback-request-constructor', 0x14003C640, 0x14003C695),
    ('callback-request-type-getter', 0x14003C7E0, 0x14003C7E8),
    ('callback-enable-and-game-request', 0x14003C800, 0x14003C97A),
    ('shell-update-batch', 0x14003A290, 0x14003A79D),
    ('register-root', 0x1400B6570, 0x1400B65DE),
    ('load-scatter', 0x1400B65E0, 0x1400B6663),
    ('save-gather', 0x1400B6670, 0x1400B66F3),
    ('input-update-bridge', 0x1400B6700, 0x1400B682E),
    ('registration-A-prefix', 0x1400B6890, 0x1400B6931),
    ('registration-B', 0x1400B6AA0, 0x1400B6B72),
    ('advance-callback', 0x1400B6CB0, 0x1400B6D3D),
    ('load-callback', 0x1400B6D50, 0x1400B6DBA),
    ('save-callback', 0x1400B7130, 0x1400B718E),
    ('constructor-A-prefix', 0x1400B8650, 0x1400B87AC),
    ('constructor-B', 0x1400B9A00, 0x1400B9B5D),
    ('callback-copy-to-replay-prefix', 0x1400BA690, 0x1400BA720),
    ('restore-replay-explicit', 0x1400BA2C0, 0x1400BA3E0),
    ('restore-replay-selected', 0x1400BA3E0, 0x1400BA548),
    ('snapshot-ring-save', 0x1400BA840, 0x1400BA8C2),
    ('shell-callback-path', 0x14003A7A0, 0x14003ADB7),
    ('callback-normal-advance', 0x1400B6B80, 0x1400B6C82),
    ('callback-poll-wrapper', 0x1400B6880, 0x1400B688E),
    ('virtual-poll-wrapper', 0x1400B76C0, 0x1400B76D0),
    ('virtual-input-wrapper', 0x1400B7810, 0x1400B7820),
    ('virtual-finish-wrapper', 0x1400B7680, 0x1400B7690),
    ('primary-explicit-replay-caller', 0x1400B8F80, 0x1400B901B),
    ('primary-poll', 0x1400B9020, 0x1400B94C8),
    ('primary-finish', 0x1400B9520, 0x1400B9590),
    ('secondary-poll', 0x1400B9C10, 0x1400B9C34),
    ('secondary-finish', 0x1400B9C40, 0x1400B9C61),
    ('callback-mode-toggle', 0x1400520B0, 0x1400520F0),
    ('callback-object-close', 0x1400B6830, 0x1400B687D),
    ('virtual-close-wrapper', 0x1400B7690, 0x1400B76B2),
    ('shell-post-update-drain', 0x14006D530, 0x14006D579),
)

VIRTUAL_SLOTS = (
    (0x140FE9F70, 0x14008F220),
    (0x140FE9FE8, 0x14008F380),
    (0x140FEA008, 0x14008F390),
    (0x140FDE940, 0x14003C640),
    (0x140FDE978, 0x14003C7E0),
    (0x140FDE988, 0x14003C800),
    (0x140FF02D8, 0x1400B9020),
    (0x140FF02F0, 0x1400B99C0),
    (0x140FF02F8, 0x1400B9520),
    (0x140FF0398, 0x1400B9C10),
    (0x140FF03B0, 0x1400B9E80),
    (0x140FF03B8, 0x1400B9C40),
)

# Exact local contracts supplement span hashes and catch plausible misreadings.
OPERATIONS = (
    (0x140090079, 'test', 'dl, dl'),
    (0x1400900A9, 'call', '0x14004c810'),
    (0x1400900B0, 'jne', '0x14009026b'),
    (0x14009013E, 'jmp', '0x140090181'),
    (0x14009018B, 'call', 'qword ptr [rax + 0x50]'),
    (0x14003C925, 'mov', 'dl, 1'),
    (0x14003C93C, 'call', '0x1400520b0'),
    (0x14003C976, 'jmp', 'qword ptr [rax + 0x50]'),
    (0x1400520C2, 'jne', '0x1400520df'),
    (0x1400520DF, 'mov', 'byte ptr [rcx + 0x1cd], bl'),
    (0x14003A305, 'cmp', 'byte ptr [r8 + 0x1cd], r12b'),
    (0x14003A2A0, 'xor', 'r12d, r12d'),
    (0x14003A671, 'cmp', 'edx, 1'),
    (0x14003A674, 'setg', 'al'),
    (0x14003A677, 'mov', 'dword ptr [rcx + 0x770], eax'),
    (0x14003A714, 'call', 'qword ptr [rax + 0x10]'),
    (0x14003A73F, 'dec', 'dword ptr [rax + 0x798]'),
    (0x14003A74C, 'mov', 'dword ptr [rax + 0x770], r12d'),
    (0x14003A761, 'je', '0x14003a660'),
    (0x14003A30E, 'call', '0x14003a7a0'),
    (0x14003A31A, 'jmp', '0x14003a767'),
    (0x14003AAAE, 'call', '0x1400b6890'),
    (0x14003AAE3, 'call', '0x1400b6aa0'),
    (0x14003ABCF, 'call', '0x1400b6880'),
    (0x14003AC2A, 'call', '0x1400b6b80'),
    (0x1400B6BF1, 'call', '0x1400b6700'),
    (0x1400B6C37, 'call', '0x1400b7680'),
    (0x1400B76CC, 'jmp', 'qword ptr [rax + 8]'),
    (0x1400B781C, 'jmp', 'qword ptr [rax + 0x20]'),
    (0x1400B768C, 'jmp', 'qword ptr [rax + 0x28]'),
    (0x1400B8FDC, 'call', '0x1400ba2c0'),
    (0x1400B915A, 'call', '0x1400ba3e0'),
    (0x1400B9541, 'call', 'qword ptr [rax + 8]'),
    (0x1400B9C4E, 'call', 'qword ptr [rax + 8]'),
    (0x1400520C4, 'call', '0x1400b6830'),
    (0x1400B6868, 'call', '0x1400b7690'),
    (0x1400B76A4, 'mov', 'edx, 1'),
    (0x1400B76A9, 'call', 'qword ptr [rax]'),
    (0x1400B65A6, 'mov', 'rcx, qword ptr [rax + 0x1b0]'),
    (0x1400B65B2, 'mov', 'eax, dword ptr [rax + 0x1b8]'),
    (0x1400B65B0, 'je', '0x1400b65d9'),
    (0x1400B65BA, 'je', '0x1400b65d9'),
    (0x1400B6623, 'lea', 'rdx, [r15 + 4]'),
    (0x1400B6627, 'mov', 'rcx, qword ptr [rsi]'),
    (0x1400B662F, 'add', 'rdx, rax'),
    (0x1400B6632, 'call', '0x140eec5a0'),
    (0x1400B66B3, 'lea', 'rcx, [r15 + 4]'),
    (0x1400B66B7, 'mov', 'rdx, qword ptr [rsi]'),
    (0x1400B66BF, 'add', 'rcx, rax'),
    (0x1400B66C2, 'call', '0x140eec5a0'),
    (0x1400B6D5A, 'movsxd', 'rbx, edx'),
    (0x1400B6D7E, 'call', '0x140eec5a0'),
    (0x1400B6D8A, 'call', '0x1400b65e0'),
    (0x1400B6DAC, 'inc', 'dword ptr [rax + 0x76c]'),
    (0x1400B68AA, 'call', '0x1400b6570'),
    (0x1400B6AC8, 'call', '0x1400b6570'),
    (0x1400B68D9, 'lea', 'rdx, [rsp + 0x30]'),
    (0x1400B68ED, 'mov', 'qword ptr [rsp + 0x40], rax'),
    (0x1400B6903, 'mov', 'qword ptr [rsp + 0x38], rax'),
    (0x1400B68D4, 'mov', 'qword ptr [rsp + 0x58], rax'),
    (0x1400B692C, 'call', '0x1400b7700'),
    (0x1400B6AF6, 'lea', 'rdx, [rsp + 0x40]'),
    (0x1400B6B07, 'mov', 'qword ptr [rsp + 0x50], rax'),
    (0x1400B6B20, 'mov', 'qword ptr [rsp + 0x48], rax'),
    (0x1400B6AF1, 'mov', 'qword ptr [rsp + 0x68], rax'),
    (0x1400B6B54, 'call', '0x1400b7780'),
    (0x1400B6CEB, 'call', '0x1400b6700'),
    (0x1400B6700, 'inc', 'dword ptr [rcx]'),
    (0x1400B682A, 'jmp', 'qword ptr [rdx + 0x10]'),
    (0x1400BA6E9, 'movups', 'xmmword ptr [rcx + 8], xmm0'),
    (0x1400BA6F1, 'movups', 'xmmword ptr [rcx + 0x18], xmm1'),
    (0x1400BA6F9, 'movups', 'xmmword ptr [rcx + 0x28], xmm0'),
    (0x1400BA2F0, 'mov', 'dword ptr [rax + 0x770], 1'),
    (0x1400BA33E, 'call', 'qword ptr [rbx + 0x18]'),
    (0x1400BA3AA, 'call', 'qword ptr [rbx + 0x30]'),
    (0x1400BA3B1, 'jne', '0x1400ba3a8'),
    (0x1400BA3D0, 'mov', 'dword ptr [rax + 0x770], 0'),
    (0x1400BA459, 'mov', 'dword ptr [rax + 0x770], 1'),
    (0x1400BA4A2, 'call', 'qword ptr [rbx + 0x18]'),
    (0x1400BA512, 'call', 'qword ptr [rbx + 0x30]'),
    (0x1400BA519, 'jne', '0x1400ba510'),
    (0x1400BA52E, 'mov', 'dword ptr [rax + 0x770], 0'),
    (0x1400BA888, 'call', 'qword ptr [rbx + 0x10]'),
)

REFERENCES = (
    (0x1400282C1, 0x140FE9FB8),
    (0x1400282CC, 0x141D1C098),
    (0x1400282E2, 0x140FE9F68),
    (0x14008F25E, 0x140FE9FC8),
    (0x14008F380, 0x141D1C098),
    (0x140090137, 0x141C14BE8),
    (0x140090181, 0x1422C8358),
    (0x140027021, 0x140FDE918),
    (0x14002702C, 0x141C14BE8),
    (0x140027040, 0x140FDE938),
    (0x14002704E, 0x141C14BE8),
    (0x14003C67C, 0x140FDE958),
    (0x14003C7E0, 0x141C14BE8),
    (0x14003C935, 0x141C14F90),
    (0x14003C948, 0x141C049D0),
    (0x14003C967, 0x1422C8358),
    (0x14003A2FE, 0x141C14F90),
    (0x14003A64A, 0x141C14F98),
    (0x14003A70D, 0x141C14F98),
    (0x14003A745, 0x141C14F98),
    (0x1400B8673, 0x140FF02D0),
    (0x1400B9A1C, 0x140FF0390),
    (0x1400B685C, 0x14221CE58),
    (0x1400B686D, 0x14221CE58),
    (0x1400B6882, 0x14221CE58),
    (0x1400B658B, 0x141C14F98),
    (0x1400B65BC, 0x14211CA90),
    (0x1400B65C3, 0x14211CB90),
    (0x1400B65C9, 0x14211CC10),
    (0x1400B65D3, 0x14211CC14),
    (0x1400B6608, 0x14211CA90),
    (0x1400B6614, 0x14211CB90),
    (0x1400B6698, 0x14211CA90),
    (0x1400B66A4, 0x14211CB90),
    (0x1400B6D74, 0x14211CE50),
    (0x1400B6D83, 0x14211CE50),
    (0x1400B6DA0, 0x141C14F98),
    (0x1400B68DE, 0x1400B6D50),
    (0x1400B68F9, 0x1400B7130),
    (0x1400B68C7, 0x1400B6CB0),
    (0x1400B6AFB, 0x1400B6D50),
    (0x1400B6B13, 0x1400B7130),
    (0x1400B6AE5, 0x1400B6CB0),
    (0x1400B6823, 0x141C14F98),
    (0x1400BA2E6, 0x141C14F98),
    (0x1400BA3C4, 0x141C14F98),
    (0x1400BA445, 0x141C14F98),
    (0x1400BA527, 0x141C14F98),
)


def produce(directory):
    dump = Dump(directory)
    instructions = {i.address: i for _, start, end in SPANS
                    for i in dump.instructions(start, end)}
    for address, mnemonic, operands in OPERATIONS:
        ins = instructions[address]
        require((ins.mnemonic, ins.op_str) == (mnemonic, operands),
                f'{address:#x}: snapshot operation differs')
    for address, target in REFERENCES:
        require(rip_targets(instructions[address]) == [target],
                f'{address:#x}: snapshot reference differs')
    for address, target in VIRTUAL_SLOTS:
        require(dump.pointer(address) == target, f'{address:#x}: snapshot virtual slot differs')
    require(dump.read(0x140FDE918, 12) == b'aGame2Match\0',
            'snapshot request type name differs')
    require(dump.read(0x140FE9FB8, 13) == b'uUiJoinMatch\0',
            'snapshot join type name differs')
    require(int.from_bytes(dump.read(0x14009027C + 12*4, 4), 'little') == 0x90079,
            'snapshot join state target differs')
    return dict(schema=3, code_sha256=CODE_SHA256,
                evidence='offline instruction trace; no live callback ordering proof',
                join_match_request=dict(type_name='uUiJoinMatch',
                    name_address='0x140fe9fb8', descriptor='0x141d1c098',
                    object_vtable='0x140fe9fc8', update='0x14008f390',
                    state_table='0x14009027c', state=12, target='0x140090079',
                    requested_descriptor='0x141c14be8',
                    limitation='Match entry is a capture candidate; it does not guarantee snapshot load.'),
                callback_enable_request=dict(type_name='aGame2Match',
                    name_address='0x140fde918', descriptor='0x141c14be8',
                    constructor='0x14003c640', object_vtable='0x140fde958',
                    initialize_slot='0x30', initialize='0x14003c800',
                    shell_owner_slot='0x141c14f90', mode_offset='0x1cd',
                    requested_descriptor='0x141c049d0',
                    limitation='Internal type name is not a verified user-facing action.'),
                root_registration=dict(pointer_offset='0x1b0', size_offset='0x1b8',
                                       pointer_table='0x14211ca90', size_table='0x14211cb90',
                                       count='0x14211cc10', scratch='0x14211ce50'),
                callbacks=dict(load='0x1400b6d50', save='0x1400b7130', advance='0x1400b6cb0'),
                load_completion_counter_offset='0x76c',
                intermediate_update_flag_offset='0x770',
                virtual_slots=[dict(address=hex(a), target=hex(t)) for a,t in VIRTUAL_SLOTS],
                limitations=[
                    'Root registration retains raw pointers; lifetime is not proved.',
                    'Load replaces live state without changing the root pointer.',
                    'The completion counter is not a universal session epoch.',
                    'Replay invokes multiple native updates while session+0x770 is set.',
                    'Shell batching also sets session+0x770; it is not replay-exclusive.',
                    'A cleared replay flag alone does not establish presentation eligibility.',
                    'Normal callback advance calls finish, which can poll and replay again.',
                    'Callback registration alone does not establish UI or network semantics.',
                    'Span alignment is not a complete control-flow or write inventory.'],
                spans=[dump.span(*span) for span in SPANS])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump', default=DEFAULT_DUMP)
    parser.add_argument('--out', type=Path, default=Path('build/collection-snapshot-audit.json'))
    parser.add_argument('--manifest-out', type=Path)
    args = parser.parse_args()
    report = produce(args.dump)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    if args.manifest_out:
        args.manifest_out.write_text(json.dumps(compact(report), indent=2)+'\n', encoding='utf-8')
    print(f'Validated {len(SPANS)} shared snapshot spans.')


if __name__ == '__main__':
    main()
