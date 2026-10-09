"""Reproduce offline entry/flag provenance and the candidate copy boundary."""
import argparse
import json
import struct
from pathlib import Path

from collection_lifecycle_audit import Dump, DEFAULT_DUMP, CODE_SHA256
from collection_restore_audit import compact, require
from name_screens import load_corpus


SPANS = (
    ('main-menu-selection-prefix', 0x1400973D0, 0x140097C33),
    ('offline-parent-update', 0x14003DBE0, 0x14003E7E4),
    ('game-select-constructor', 0x1400B13F0, 0x1400B1441),
    ('game-select-update', 0x1400B1510, 0x1400B1AE3),
    ('game-select-confirm', 0x1400B1B10, 0x1400B1DB0),
    ('mode-select-constructor', 0x1400B2530, 0x1400B25D7),
    ('mode-select-update', 0x1400B26A0, 0x1400B28D5),
    ('mode-select-label', 0x1400B2950, 0x1400B29FD),
    ('session-mode-writer', 0x140037A60, 0x140037C42),
    ('session-option-writers', 0x140038280, 0x140038931),
    ('CVS2-import-gate-initialization', 0x1406A97D0, 0x1406A9A0F),
    ('screen-update', 0x140038F90, 0x140039778),
    ('manager-update', 0x1401A6200, 0x1401A62A6),
    ('manager-replacement', 0x1401A6420, 0x1401A65E7),
)

OPERATIONS = (
    (0x140097A3B, 'lea rdx, [rdi + 0x330]'),
    (0x140097A59, 'call 0x14007cdc0'),
    (0x140097B38, 'movsxd rax, dword ptr [rdi + 0x330]'),
    (0x140097B3F, 'cmp eax, 5'),
    (0x140097B48, 'mov ecx, dword ptr [rbx + rax*4 + 0x98098]'),
    (0x140097B5B, 'xor ebx, ebx'),
    (0x140097B5D, 'mov dword ptr [rax + 0xd0320], ebx'),
    (0x140097B6A, 'mov dword ptr [rax + 0xd0240], 4'),
    (0x14003DFDC, 'mov word ptr [rbx + 8], 4'),
    (0x14003E02C, 'call 0x1400b13f0'),
    (0x14003E11D, 'mov word ptr [rbx + 8], 0xd'),
    (0x14003E7D1, 'call qword ptr [rax + 0x50]'),
    (0x1400B1595, 'call 0x1400b1b10'),
    (0x1400B175B, 'call 0x1400b2530'),
    (0x1400B1D4F, 'mov byte ptr [rax + 0xd0330], bpl'),
    (0x1400B1D5D, 'mov byte ptr [rax + 0xd0338], bpl'),
    (0x1400B27AB, 'movsxd rcx, dword ptr [rbx + 0x338]'),
    (0x1400B27B2, 'mov edx, dword ptr [rbx + rcx*4 + 0x340]'),
    (0x1400B27B9, 'sub edx, 1'),
    (0x1400B27BC, 'je 0x1400b2805'),
    (0x1400B27C1, 'je 0x1400b27e4'),
    (0x1400B27DC, 'mov dword ptr [rax + 0xd0334], edx'),
    (0x1400B27F9, 'mov dword ptr [rax + 0xd0334], 0'),
    (0x1400B280C, 'mov byte ptr [rax + 0xd0338], 1'),
    (0x1400B29B4, 'movsxd rcx, dword ptr [rdi + rax*4 + 0x340]'),
    (0x1400B29BC, 'mov r9d, dword ptr [rsi + rcx*4 + 0x12084d8]'),
    (0x140037AD8, 'cmp dword ptr [rax + 0xd0320], 1'),
    (0x140037AE6, 'sete cl'),
    (0x140037AE9, 'mov dword ptr [rax + 0x7b4], ecx'),
    (0x140037B8A, 'mov dword ptr [rax + 0x7b4], r14d'),
    (0x140037BC2, 'mov dword ptr [rcx + 0x7b4], r14d'),
    (0x14003851B, 'cmp byte ptr [rax + 0xd0330], dil'),
    (0x140038531, 'inc ecx'),
    (0x140038533, 'mov dword ptr [rax + 0x828], ecx'),
    (0x14003863E, 'cmp byte ptr [rax + 0xd0338], dil'),
    (0x140038654, 'mov dword ptr [rax + 0x80c], 1'),
    (0x1406A9839, 'xor edi, edi'),
    (0x1406A9900, 'mov qword ptr [rax + 0x1a8], rdi'),
    (0x140039430, 'call 0x14003a290'),
    (0x140039438, 'call 0x140039ed0'),
    (0x1401A624E, 'call qword ptr [rax + 0x38]'),
    (0x1401A6288, 'call qword ptr [rax + 0x58]'),
    (0x1401A628B, 'mov qword ptr [rsi + 0x7040], 0'),
    (0x1401A62A5, 'ret '),
    (0x1401A6566, 'call qword ptr [rax + 0x40]'),
    (0x1401A6579, 'call qword ptr [rax]'),
)

# Exact reviewed entries; no inferred lengths for neighboring tables.
RVA_SLOTS = (
    (0x140098098, 0x140097B54),
    (0x14003E81C, 0x14003DFD7),  # request 4 minus 4
    (0x14003E7E4 + 4 * 4, 0x14003E010),  # state 4
    (0x14003E7E4 + 13 * 4, 0x14003E7B4),
)
POINTER_SLOTS = (
    (0x140FEEDA0, 0x1400B1490),
    (0x140FEEDE0, 0x1400B1510),
    (0x140FEF018, 0x1400B26A0),
    (0x141018B90, 0x1401A6200),
    (0x141018BB8, 0x1401A6420),
)


def produce(directory=DEFAULT_DUMP):
    dump = Dump(directory)
    instructions = {i.address: i for _, start, end in SPANS
                    for i in dump.instructions(start, end)}
    for address, expected in OPERATIONS:
        i = instructions.get(address)
        require(i is not None and f'{i.mnemonic} {i.op_str}' == expected,
                f'{address:#x}: offline operation differs')
    slots = []
    for address, target in RVA_SLOTS:
        require(0x140000000 + struct.unpack('<I', dump.read(address, 4))[0] == target,
                f'{address:#x}: offline dispatch differs')
        slots.append(dict(address=hex(address), target=hex(target), encoding='RVA32'))
    for address, target in POINTER_SLOTS:
        require(dump.pointer(address) == target, f'{address:#x}: offline pointer differs')
        slots.append(dict(address=hex(address), target=hex(target), encoding='VA64'))
    indices = struct.unpack('<4I', dump.read(0x1412084D8, 16))
    require(indices == (4105, 4106, 4107, 4108), 'offline label table differs')
    corpus = list(load_corpus().items())
    labels = [(37, 'OFFLINE_PLAY', 'Offline Play'),
              (4105, 'MODE_ARCADE', 'Arcade Mode'),
              (4106, 'MODE_VERSUS', 'Versus Mode'),
              (4107, 'MODE_TRAINING', 'Training Mode'),
              (4108, 'MODE_TRAINING2', 'Training Mode (Boss)')]
    for index, key, value in labels:
        require(corpus[index] == (key, value), 'offline corpus label differs')
    return dict(schema=1, code_sha256=CODE_SHA256,
                evidence='static entry and writer trace; no protected runtime copy certified',
                slots=slots, labels=[list(row) for row in labels],
                mode_labels=dict(table='0x1412084d8', indices=list(indices),
                                 source='local msg.arc/menu_eng via gmd.parse_gmd'),
                pilot=dict(route='Offline Play / Arcade Mode', shell_mode=0,
                           shell_option_bytes={'0xd0330': 0, '0xd0338': 0},
                           requires_positive_entry_record=True,
                           runtime_support_implemented=False),
                boundary=dict(candidate='0x1401a6200 return', return_instruction='0x1401a62a5',
                              after_deferred_replacement=True,
                              lifetime_protection_proven=False,
                              worker_native_read_allowed=False),
                spans=[dump.span(*s) for s in SPANS])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump', default=DEFAULT_DUMP)
    parser.add_argument('--manifest-out', type=Path)
    args = parser.parse_args()
    report = produce(args.dump)
    output = Path('build/collection-offline-gate-audit.json')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if args.manifest_out:
        args.manifest_out.write_text(json.dumps(compact(report), indent=2) + '\n', encoding='utf-8')
    print(f'Validated {len(SPANS)} offline-mode/boundary spans. Static evidence only.')


if __name__ == '__main__':
    main()
