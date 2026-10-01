"""Retain bounded PWS2 four-record/list evidence; no gameplay field semantics."""
import argparse
import json
from pathlib import Path

from collection_lifecycle_audit import CODE_SHA256, DEFAULT_DUMP, Dump

SPANS = (
    ('native-update', 0x140A47290, 0x140A472C2),
    ('phase-dispatch', 0x140A48CC0, 0x140A48DD0),
    ('four-input-record-update', 0x140A490A0, 0x140A49132),
    ('four-input-record-reset', 0x140A49140, 0x140A49178),
    ('shared-input-mask-conversion', 0x140048BB0, 0x140048C81),
    ('gated-input-copy', 0x140A4E870, 0x140A4EB83),
    ('actor-input-and-processing-order', 0x140A7CE00, 0x140A7D790),
    ('actor-processing-caller', 0x140A4C710, 0x140A4C7D6),
    ('record-pool-rebuild', 0x140A4BF10, 0x140A4C3D2),
    ('list-dispatch', 0x140A4C3E0, 0x140A4C415),
    ('filtered-list-rebuild', 0x140A65A60, 0x140A65CC1),
    ('allocate-record-resource-a', 0x140A644A0, 0x140A64525),
    ('allocate-record-resource-b', 0x140A65330, 0x140A653B1),
)

# Review anchors distinguish these four fixed records from the separate
# 200-record general pool in the same function. They are not player labels.
ANCHORS = (
    (0x140A4C118, 0x140A4C11E, 'mov', 'r8d, 0x10ae0'),
    (0x140A4C172, 0x140A4C179, 'add', 'rcx, 0x40818'),
    (0x140A4C24B, 0x140A4C251, 'mov', 'byte ptr [rax + 0x40819], bl'),
    (0x140A4C258, 0x140A4C25F, 'mov', 'byte ptr [rax + 0x44ad1], 1'),
    (0x140A4C266, 0x140A4C26D, 'mov', 'byte ptr [rax + 0x48d89], 2'),
    (0x140A4C274, 0x140A4C27B, 'mov', 'byte ptr [rax + 0x4d041], 3'),
    (0x140A4C1E1, 0x140A4C1E6, 'mov', 'ebp, 4'),
    (0x140A4C3A7, 0x140A4C3AE, 'add', 'rdi, 0x42b8'),
    (0x140A4C403, 0x140A4C406, 'call', 'qword ptr [rbx + 8]'),
    (0x140A4C406, 0x140A4C40A, 'mov', 'rbx, qword ptr [rbx + 0x18]'),
)


def produce(directory=DEFAULT_DUMP):
    dump = Dump(directory)
    order_bytes = dump.read(0x1417308D0, 24)
    if order_bytes != bytes((0,1,2,3, 0,1,3,2, 0,2,1,3, 0,2,3,1, 0,3,1,2, 0,3,2,1)):
        raise ValueError('Actor processing-order table differs')
    for start, end, mnemonic, operands in ANCHORS:
        instructions = dump.instructions(start, end)
        if len(instructions) != 1 or (instructions[0].mnemonic, instructions[0].op_str) != (mnemonic, operands):
            raise ValueError(f'Record/list anchor differs at {start:#x}')
    return dict(
        schema=1, evidence='offline native record/list structure; no live player/HUD mapping',
        code_sha256=CODE_SHA256, root_slot='0x14262f0b0', root_bytes='0x8cd40',
        fixed_records=dict(base='0x40818', stride='0x42b8', count=4,
                           index_byte='0x1', filter_byte='0x401c',
                           previous_pointer='0x10', next_pointer='0x18', callback_pointer='0x8',
                           list_zero_head='0x40750'),
        input_records=dict(base='0x53114', stride='0x28', count=4,
                           current_word='0x10', previous_word='0x12',
                           rising_word='0x14', falling_word='0x16',
                           source='S+0x218+4*index, through 0x140048bb0 mask conversion',
                           gated_copy_base='0x531b4', actor_copy_offset='0x4b0',
                           actor_copy_scope='R+0x52ee5=1 with record+0x4028=0; mode 2 has an additional float gate'),
        processing_order=dict(address='0x1417308d0', bytes=order_bytes.hex(),
                              rows=[list(order_bytes[i:i+4]) for i in range(0,24,4)]),
        limitations=['Record indices are not yet mapped to controllers or HUD owners.',
                     'Nonzero filter byte establishes list inclusion, not alive/human/visible semantics.',
                     'No health, meter, position, camera, transformation or safe hook ABI is certified.',
                     'Root content and links can be rebuilt without changing root identity.'],
        spans=[dump.span(label, start, end) for label, start, end in SPANS])


def compact(report):
    result = dict(report)
    result['spans'] = [{k: v for k, v in span.items() if k != 'instructions'}
                       for span in report['spans']]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump', default=DEFAULT_DUMP)
    parser.add_argument('--out', type=Path, default=Path('build/pws2-actor-audit.json'))
    parser.add_argument('--manifest-out', type=Path)
    args = parser.parse_args()
    report = produce(args.dump)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if args.manifest_out:
        args.manifest_out.parent.mkdir(parents=True, exist_ok=True)
        args.manifest_out.write_text(json.dumps(compact(report), indent=2) + '\n', encoding='utf-8')
    print(f"Recorded {len(report['spans'])} bounded PWS2 spans. Offline only.")


if __name__ == '__main__':
    main()
