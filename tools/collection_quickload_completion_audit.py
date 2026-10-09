"""CVS2 Quick Load consumers and exclusion gates; offline evidence only."""
import argparse
import json
from pathlib import Path

from collection_lifecycle_audit import Dump, DEFAULT_DUMP, CODE_SHA256
from collection_restore_audit import compact, require


SPANS = (
    ('buffer-to-secondary', 0x1400B5F90, 0x1400B60A8),
    ('CVS2-root-and-phase-publication', 0x1406AA320, 0x1406AA794),
    ('CVS2-update-wrapper', 0x1406A9A10, 0x1406A9A27),
    ('CVS2-native-update', 0x1406AA7A0, 0x1406AA957),
    ('CVS2-phase-dispatch', 0x1406B1CA0, 0x1406B1D03),
    ('CVS2-phase-2-dispatch', 0x1406BA870, 0x1406BA899),
    ('CVS2-preparation-dispatch', 0x1406BABA0, 0x1406BABB7),
    ('CVS2-preparation', 0x1406BABC0, 0x1406BAD46),
    ('CVS2-save-restore', 0x1406AAB40, 0x1406AAC2C),
    ('CVS2-list-repair', 0x1406ACE60, 0x1406AD0E2),
    ('CVS2-conditional-resource-repair', 0x1406B0520, 0x1406B057F),
    ('CVS2-resource-list', 0x1406B0690, 0x1406B088D),
    ('CVS2-resource-copy', 0x1406A9BF0, 0x1406A9C3B),
    ('CVS2-post-update', 0x1406A9DE0, 0x1406AA318),
    ('CVS2-clear-secondary-status', 0x1406A9D20, 0x1406A9DD8),
    ('CVS2-clear-checkpoint', 0x1406AAB20, 0x1406AAB39),
    ('CVS2-explicit-checkpoint', 0x1406AAC30, 0x1406AACC6),
    ('CVS2-secondary-header-consumer', 0x1406B20D0, 0x1406B225B),
    ('CVS2-start-phase', 0x1406B9A60, 0x1406B9B9B),
    ('outer-update', 0x14003A290, 0x14003A79D),
    ('callback-mode-setter', 0x1400520B0, 0x1400520F0),
    ('command-recording-gate', 0x1400487C0, 0x14004880D),
    ('command-recording-reset', 0x140048E90, 0x140048ECB),
)

# Reviewed exact slots, not inferred table lengths.
SLOTS = (
    (0x141282A30, 0x1406BA870),  # phase[0] == 2
    (0x14128CD70, 0x1406BABA0),  # phase[1] == 1
    (0x14128CD78, 0x1406BABC0),  # phase[2] == 0
)

OPERATIONS = (
    (0x1400B5FA0, 'cmp qword ptr [rcx + 0x1a8], 0'),
    (0x1400B5FA8, 'jne 0x1400b60a2'),
    (0x1400B6066, 'mov rcx, qword ptr [rcx + 0x1c0]'),
    (0x1400B6070, 'call 0x140eec5a0'),
    (0x1400B6098, 'mov dword ptr [rax + 0x774], 1'),
    (0x1406AA3C9, 'add rax, 0x78'),
    (0x1406AA8B0, 'call 0x1406b1ca0'),
    (0x1406B1CAB, 'cmp byte ptr [rax + 0x24], 0'),
    (0x1406B1CAF, 'jne 0x1406b1cdc'),
    (0x1406B1CC8, 'call qword ptr [rax + rcx*8]'),
    (0x1406B1CCB, 'call 0x1406bcf60'),
    (0x1406BA885, 'movsx rcx, byte ptr [rax + 1]'),
    (0x1406BA895, 'jmp qword ptr [rax + rcx*8]'),
    (0x1406BABA7, 'movsx rcx, byte ptr [rax + 2]'),
    (0x1406BABCD, 'mov byte ptr [rax + 2], 1'),
    (0x1406BAD41, 'jmp 0x1406aab40'),
    (0x1406AAB52, 'jne 0x1406aac27'),
    (0x1406AAB5F, 'jne 0x1406aac27'),
    (0x1406AAB6C, 'jne 0x1406aac27'),
    (0x1406AAB82, 'jg 0x1406aabe6'),
    (0x1406AABED, 'add rdx, 8'),
    (0x1406AABF1, 'call 0x140eec5a0'),
    (0x1406AABF6, 'call 0x1406ace60'),
    (0x1406AAC04, 'add ecx, -2'),
    (0x1406AAC07, 'cmp ecx, 2'),
    (0x1406AAC0A, 'ja 0x1406aac16'),
    (0x1406AAC11, 'call 0x1406b0520'),
    (0x1406AAC1D, 'mov dword ptr [rax + 0x528], 1'),
    (0x1406ACE89, 'mov r14d, 0x12c'),
    (0x1406ACED0, 'mov r8d, 0x168'),
    (0x1406ACED9, 'call 0x140eeca00'),
    (0x1406ACEF7, 'mov qword ptr [rcx + rsi + 0xbe0], rax'),
    (0x1406AD0D3, 'mov word ptr [rax + 0x1b27c], r14w'),
    (0x1406B055F, 'call 0x1406a9bf0'),
    (0x1406A9C27, 'call 0x140eec5a0'),
    (0x1406A9A22, 'jmp 0x1406a9de0'),
    (0x1406A9E60, 'jne 0x1406a9f5e'),
    (0x1406A9E7A, 'call 0x140eec5a0'),
    (0x1406AA057, 'jmp 0x1406a9d20'),
    (0x1406A9D29, 'mov dword ptr [rax], ecx'),
    (0x1406A9D32, 'mov dword ptr [rax + 0x528], ecx'),
    (0x1406AAC7E, 'mov dword ptr [rax], ebx'),
    (0x14003A305, 'cmp byte ptr [r8 + 0x1cd], r12b'),
    (0x14003A30C, 'je 0x14003a31f'),
    (0x14003A30E, 'call 0x14003a7a0'),
    (0x14003A714, 'call qword ptr [rax + 0x10]'),
    (0x14003A761, 'je 0x14003a660'),
    (0x14003A78A, 'call qword ptr [rax + 0x18]'),
    (0x14003A798, 'jmp 0x14006d530'),
    (0x1400520C4, 'call 0x1400b6830'),
    (0x1400520CE, 'mov byte ptr [rdi + 0x1cd], bl'),
    (0x1400520DF, 'mov byte ptr [rcx + 0x1cd], bl'),
    (0x1400487CB, 'cmp dword ptr [rax + 0x774], 0'),
    (0x140048803, 'call 0x14004ca10'),
    (0x140048EC4, 'mov dword ptr [rax + 0x774], ecx'),
)


def produce(directory=DEFAULT_DUMP):
    dump = Dump(directory)
    instructions = {i.address: i for _, start, end in SPANS
                    for i in dump.instructions(start, end)}
    for address, expected in OPERATIONS:
        ins = instructions.get(address)
        require(ins is not None and f'{ins.mnemonic} {ins.op_str}' == expected,
                f'{address:#x}: completion operation differs')
    for address, target in SLOTS:
        require(dump.pointer(address) == target,
                f'{address:#x}: completion dispatch slot differs')
    return dict(
        schema=1, code_sha256=CODE_SHA256,
        evidence='offline CVS2 consumer trace; no restore-completion live capture',
        slots=[dict(address=hex(a), target=hex(t)) for a, t in SLOTS],
        restore=dict(entry='0x1406aab40', positive_branch='0x1406aabe6',
                     local_return='0x1406aac2b', payload_bytes=0x216b0,
                     phase_prefix=[2, 1, 0], conditional_repair_controls=[2, 3, 4]),
        exclusion=dict(shell_owner_slot='0x141c14f90', callback_byte=0x1cd,
                       session_slot='0x141c14f98', restore_bypass_dwords=[0x7b4, 0x828, 0x80c],
                       secondary_import_bypass_qword=0x1a8,
                       zero_values_prove_supported_offline_mode=False),
        limitations=[
            'Local restore return includes repairs but not enclosing update completion.',
            'S+0x528, S+0x774 and secondary control are not completion certificates.',
            'The captured load has no mode, phase or secondary-header observations.',
            'Callback exclusion is necessary, not a complete offline UI-mode allowlist.',
            'No thread, lifetime, hook ABI or concurrent output protocol is certified.',
        ],
        spans=[dump.span(*span) for span in SPANS])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump', default=DEFAULT_DUMP)
    parser.add_argument('--manifest-out', type=Path)
    args = parser.parse_args()
    report = produce(args.dump)
    output = Path('build/collection-quickload-completion-audit.json')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if args.manifest_out:
        args.manifest_out.write_text(json.dumps(compact(report), indent=2) + '\n', encoding='utf-8')
    print(f'Validated {len(SPANS)} completion/gate spans. Offline only.')


if __name__ == '__main__':
    main()
