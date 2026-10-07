"""Offline Quick Save / Quick Load evidence; no process or save-file access."""
import argparse
import json
from pathlib import Path

from collection_lifecycle_audit import Dump, DEFAULT_DUMP, CODE_SHA256
from collection_restore_audit import compact, require

SPANS = (
    ('quick-load-ui', 0x1400A70C0, 0x1400A73A9),
    ('quick-save-ui', 0x1400A8140, 0x1400A8621),
    ('quick-load-reinitialize', 0x14004FFD0, 0x14005007B),
    ('quick-save-package', 0x140051130, 0x1400511ED),
    ('save-record-to-buffer', 0x14003FD60, 0x14003FF31),
    ('buffer-to-save-record', 0x140041120, 0x1400412D3),
    ('buffer-to-secondary', 0x1400B5F90, 0x1400B60A8),
    ('secondary-to-buffer', 0x1400B60B0, 0x1400B618D),
    ('buffer-address', 0x1400B6360, 0x1400B637A),
    ('buffer-length-publication', 0x1400B6530, 0x1400B6543),
)

OPERATIONS = (
    (0x1400A736B, 'mov r8d, 0x10d'),
    (0x1400A7375, 'call 0x14007fc20'),
    (0x1400A72AF, 'cmp dword ptr [rax + 0x360], 0'),
    (0x1400A72B6, 'je 0x1400a737a'),
    (0x1400A717A, 'call 0x14004ffd0'),
    (0x1400A7238, 'mov r8d, 0x110'),
    (0x1400A8408, 'mov r8d, 0x10f'),
    (0x1400A841F, 'call 0x14007fc20'),
    (0x1400A838F, 'mov byte ptr [rdi + 9], 2'),
    (0x1400A82B7, 'call 0x140051130'),
    (0x14005002E, 'call 0x14003fd60'),
    (0x140050037, 'call 0x1400b6530'),
    (0x140050049, 'call qword ptr [rax + 0x20]'),
    (0x14005005B, 'call 0x14004c540'),
    (0x140050062, 'call 0x1400b5f90'),
    (0x14005118A, 'call 0x1400b60b0'),
    (0x140051191, 'call 0x1400b6360'),
    (0x1400511D8, 'call 0x140041120'),
    (0x1400B6156, 'mov esi, dword ptr [rdx + 0x1c8]'),
    (0x1400B615C, 'mov rdx, qword ptr [rdx + 0x1c0]'),
    (0x1400B6166, 'call 0x140eec5a0'),
    (0x1400B6048, 'add rdx, 0x18'),
    (0x1400B6066, 'mov rcx, qword ptr [rcx + 0x1c0]'),
    (0x1400B6070, 'call 0x140eec5a0'),
    (0x1400B6098, 'mov dword ptr [rax + 0x774], 1'),
    (0x14003FDBE, 'lea rdx, [rbx + 0x1c]'),
    (0x14003FDC2, 'mov rcx, r9'),
    (0x14003FDC5, 'call 0x140eec5a0'),
    (0x1400412C9, 'lea rcx, [r9 + 0x1c]'),
    (0x1400412CD, 'jmp 0x140eec5a0'),
)


def produce(directory=DEFAULT_DUMP):
    dump = Dump(directory)
    instructions = {i.address: i for _, start, end in SPANS
                    for i in dump.instructions(start, end)}
    for address, expected in OPERATIONS:
        ins = instructions[address]
        require(f'{ins.mnemonic} {ins.op_str}' == expected,
                f'{address:#x}: quick-save operation differs')
    return dict(schema=1, code_sha256=CODE_SHA256,
                evidence='offline command-path trace; no live Quick Load capture',
                message_indices=dict(load_confirmation=269, load_success=272,
                                     save_overwrite=271),
                quick_load=dict(ui='0x1400a70c0', operation='0x14004ffd0',
                                cleanup_call='0x140050049', initializer='0x14004c540',
                                restore_secondary='0x1400b5f90'),
                quick_save=dict(ui='0x1400a8140', operation='0x140051130',
                                collect_secondary='0x1400b60b0'),
                limitations=[
                    'Message indices use retained shell corpus evidence, not a new live label capture.',
                    'Secondary block checkpoint timing is not proved by the save command.',
                    'Load reinitializes before secondary restore; its return is not presentation readiness.',
                    'File persistence, all callers and concurrent native access are not certified.',
                    'These functions are not externally callable APIs or approved hooks.'],
                spans=[dump.span(*span) for span in SPANS])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest-out', type=Path)
    args = parser.parse_args()
    report = produce()
    output = Path('build/collection-quicksave-audit.json')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2)+'\n')
    if args.manifest_out:
        args.manifest_out.write_text(json.dumps(compact(report), indent=2)+'\n')
    print(f'Validated {len(SPANS)} Quick Save / Quick Load spans. Offline only.')


if __name__ == '__main__':
    main()
