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
    ('draw-pass-dispatch', 0x140A5D5A0, 0x140A5D8A6),
    ('draw-pass-transform-setup', 0x140A5D8B0, 0x140A5D94E),
    ('draw-four-records-prefix', 0x140A5D950, 0x140A5DA1F),
    ('shared-transform-block-copy', 0x140F32440, 0x140F32487),
    ('actor-vector-draw-consumer', 0x140A548D0, 0x140A54A0D),
    ('actor-resource-extents-prefix', 0x140A5E060, 0x140A5E2B2),
    ('shared-affine-vector-transform', 0x140F31C60, 0x140F31D04),
    ('shared-transform-slot-selection', 0x140F326E0, 0x140F32724),
    ('resource-transform-translation-helper', 0x1409BAD20, 0x1409BAD45),
    ('shared-current-transform-translation', 0x140F31F80, 0x140F31F99),
    ('shared-transform-stack-push', 0x140F331C0, 0x140F3324E),
    ('shared-transform-stack-pop', 0x140F33130, 0x140F331BD),
    ('actor-two-point-primitive-wrapper', 0x140F380A0, 0x140F380B6),
    ('two-point-projection-and-rejection', 0x140F37CA0, 0x140F37F34),
    ('shared-homogeneous-vector-transform', 0x140F31D10, 0x140F31DEB),
    ('shared-composite-transform', 0x140F33830, 0x140F3388B),
    ('shared-current-transform-copy-out', 0x140F31F40, 0x140F31F72),
    ('shared-transform-postmultiply', 0x140F32C20, 0x140F3310F),
    ('shared-transform-premultiply', 0x140F32730, 0x140F32C17),
    ('viewport-scale-writer-prefix', 0x140F30830, 0x140F30A1C),
    ('shared-template-matrix-copy', 0x140F33610, 0x140F33643),
    ('shared-translation-matrix-construction', 0x140F334D0, 0x140F33520),
    ('shared-projective-matrix-construction', 0x140F338F0, 0x140F33A40),
    ('draw-direction-state-setter', 0x140F36390, 0x140F363D0),
    ('draw-direction-vector-rescale', 0x140F33F90, 0x140F34010),
    ('draw-state-flag-setter', 0x140F364E0, 0x140F364F2),
    ('record-pool-rebuild', 0x140A4BF10, 0x140A4C3D2),
    ('list-dispatch', 0x140A4C3E0, 0x140A4C415),
    ('filtered-list-rebuild', 0x140A65A60, 0x140A65CC1),
    ('allocate-record-resource-a', 0x140A644A0, 0x140A64525),
    ('allocate-record-resource-b', 0x140A65330, 0x140A653B1),
)

PROJECTION_CONSTANTS = (
    (0x14106E298, '00002044', 640.0),
    (0x14106E32C, '0000f043', 480.0),
    (0x140FDE828, '0000803f', 1.0),
    (0x140FE4244, '0000003f', 0.5),
    (0x140FE428C, '000000bf', -0.5),
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
    for address, expected, value in PROJECTION_CONSTANTS:
        if dump.read(address, 4).hex() != expected:
            raise ValueError(f'Projection constant differs at {address:#x}')
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
        draw_leads=dict(transform_base='0x51d90', transform_stride='0x308',
                        copied_bytes='0x40', index_global='0x14297a9e0',
                        pass_global='0x14297a9dc', current_matrix_slot='0x142c79b48',
                        matrix_slots_offset='0x1f80ac', matrix_slot_count=4,
                        affine_helper='0x140f31c60',
                        scope='affine and two-point homogeneous projection established; pass identity unresolved'),
        projection=dict(primitive='0x140f37ca0', homogeneous_helper='0x140f31d10',
                        composite_offset='0x1f8200', viewport_matrix='0x141c00210',
                        composition='current * slot2 * slot3 * mutable viewport matrix',
                        constants=[dict(address=hex(a), bytes=b, value=v)
                                   for a, b, v in PROJECTION_CONSTANTS],
                        scope='actor-linked two-point primitive only; no HUD owner or live camera certification'),
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
