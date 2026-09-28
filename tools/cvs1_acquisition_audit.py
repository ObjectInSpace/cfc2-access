"""Pinned, offline CVS1 dispatch/restore and rendered-prompt acquisition evidence.

These bounded traces are not a gameplay decoder, full CFG, or hook ABI approval.
See docs/cvs1-acquisition-trace.md. Reuses the lifecycle audit's code hash gate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from collection_lifecycle_audit import Dump, DEFAULT_DUMP

SPANS = (
    ('phase-publication', 0x1405A6522, 0x1405A6536),
    ('native-update', 0x1405A65F0, 0x1405A665D),
    ('phase-dispatch', 0x1405AE5F0, 0x1405AE675),
    ('phase-2-subdispatch', 0x1405C5770, 0x1405C57A7),
    ('phase-2-substate-1-dispatch', 0x1405C5D30, 0x1405C5D47),
    ('restore-preparation', 0x1405C5D50, 0x1405C5E4B),
    ('copy-restore', 0x1405A6840, 0x1405A69B1),
    ('post-restore-pool-reset', 0x1405ADEE0, 0x1405AE162),
    ('mode-dispatch', 0x1405C5E50, 0x1405C5E7F),
    ('mode-branch-A-dispatch', 0x1405D8B10, 0x1405D8B27),
    ('mode-branch-B-dispatch', 0x140602440, 0x14060252D),
    ('mode-branch-B-initializer', 0x140602530, 0x140602AE8),
    ('mode-branch-B-actor-dispatch', 0x140602B70, 0x140602CA3),
    ('mode-branch-B-choice-update', 0x140606290, 0x14060690A),
    ('mode-branch-B-object-dispatch', 0x140602EC0, 0x140602ECF),
    ('mode-branch-B-choice-coordinates', 0x140603220, 0x140603468),
    ('selection-actor-owner-initialization', 0x1405C4070, 0x1405C41F4),
    ('phase-2-substate-0-initialization', 0x1405C5890, 0x1405C5D22),
    ('object-pool-acquisition', 0x1405ADA90, 0x1405ADB5F),
    ('object-list-prepend', 0x1405ADC60, 0x1405ADCD7),
    ('object-callback-scheduler', 0x1405AE460, 0x1405AE495),
    ('mode-branch-B-input-update', 0x1406079E0, 0x140607A72),
    ('object-resource-lookup', 0x1405C3170, 0x1405C318E),
    ('render-callback-enqueue', 0x1405AF7E0, 0x1405AF810),
    ('phase-1-subdispatch', 0x1405C4200, 0x1405C427C),
    ('phase-1-substate-6', 0x1405C5590, 0x1405C55E7),
    ('blinking-prompt-producer', 0x1405C4560, 0x1405C468C),
    ('text-record-producer', 0x1405AF6B0, 0x1405AF722),
    ('text-format-wrapper', 0x1405AF8A0, 0x1405AF8FC),
    ('text-queue-reset', 0x1405AF7C0, 0x1405AF7DF),
    ('text-and-callback-drain', 0x1405AF730, 0x1405AF7B1),
    ('text-record-renderer', 0x1405C8DB0, 0x1405C9071),
    ('style-7-text-renderer', 0x140F31050, 0x140F311D0),
)

# Explicit individual edges, NOT inferred table lengths. Adjacent dispatch
# tables share storage boundaries, and some callable entries have two routes.
TABLE_EDGES = (
    ('phase', 'P+0 signed byte', 0x141217568, 1, 0x1405C4200),
    ('phase', 'P+0 signed byte', 0x141217568, 2, 0x1405C5770),
    ('phase-2', 'P+1 signed byte', 0x141222420, 0, 0x1405C5890),
    ('phase-2', 'P+1 signed byte', 0x141222420, 1, 0x1405C5D30),
    ('phase-2-substate-1', 'P+2 signed byte', 0x141222430, 0, 0x1405C5D50),
    ('phase-2-substate-1', 'P+2 signed byte', 0x141222430, 1, 0x1405C5E50),
    ('phase-1', 'P+1 signed byte', 0x141222398, 6, 0x1405C5590),
    ('mode-branch-B', 'P+3 signed byte', 0x141240D98, 0, 0x140602530),
    ('mode-branch-B', 'P+3 signed byte', 0x141240D98, 2, 0x140602B70),
    ('mode-branch-B-actor', 'actor+4 unsigned byte', 0x141240DC0, 6, 0x140606290),
    ('mode-branch-B-object', 'object+0x34 unsigned byte', 0x141240E10, 2, 0x140603220),
    ('object-pool-insertion', 'R8B unsigned byte at pool entry', 0x141217548, 0, 0x1405ADC60),
)

PROMPTS = (
    (0x14106E608, 'FREE PLAY'),
    (0x14106E618, 'PRESS START BUTTON'),
    (0x14106E630, 'INSERT MORE COIN(S)'),
    (0x14106E648, '1 CREDITS TO START'),
    (0x14106E660, 'INSERT COIN(S)'),
)

# Four selected rows from the initializer's 215-row, four-byte descriptor loop.
# Resource byte, subtype, position index, mode mask; not character names.
OBJECT_DESCRIPTORS = (
    (211, '2c020007'), (212, '2e020007'),
    (213, '2d020007'), (214, '2f020007'),
)


def produce(directory):
    dump = Dump(directory)
    spans = [dump.span(*spec) for spec in SPANS]
    edges = []
    for owner, selector, table, index, expected in TABLE_EDGES:
        raw = dump.read(table + index * 8, 8)
        target = struct.unpack('<Q', raw)[0]
        if target != expected:
            raise ValueError(f'Dispatch entry differs: {owner}[{index}]')
        edges.append(dict(owner=owner, selector=selector, table=hex(table), index=index,
                          slot=hex(table + index * 8), bytes=raw.hex(), target=hex(target)))
    prompts = []
    for address, expected in PROMPTS:
        raw = dump.read(address, len(expected) + 1)
        if raw != expected.encode('ascii') + b'\0':
            raise ValueError(f'Prompt literal differs: {address:#x}')
        prompts.append(dict(address=hex(address), bytes=raw.hex(), text=expected))
    mode_raw = dump.read(0x1405C5E80, 40)
    mode_targets = [0x140000000 + offset for offset in struct.unpack('<10I', mode_raw)]
    if any(target not in (0x1405C5E74, 0x1405C5E79, 0x1405C5E7E) for target in mode_targets):
        raise ValueError('Mode jump-table target outside reviewed branches')
    descriptors = []
    for index, expected in OBJECT_DESCRIPTORS:
        address = 0x1412403E0 + index * 4
        raw = dump.read(address, 4)
        if raw.hex() != expected:
            raise ValueError(f'Object descriptor differs: {index}')
        descriptors.append(dict(index=index, address=hex(address), bytes=raw.hex()))
    return dict(schema=1, evidence='offline instruction/data evidence; no live validation',
                limitations=['Individual table edges do not prove selector bounds or screen meanings.',
                             'Text enqueue is not visibility; renderer execution needs live checks.',
                             'No character-ID/name map or safe hook ABI established.'],
                sections=[dict(name=s.name, live=hex(s.live), size=len(s.data),
                               sha256=hashlib.sha256(s.data).hexdigest()) for s in dump.sections],
                spans=spans, dispatch_edges=edges, prompt_literals=prompts,
                selected_object_descriptors=descriptors,
                mode_table=dict(address='0x1405c5e80', selector='P+0x48 signed byte; unsigned <=9 guard',
                                bytes=mode_raw.hex(), targets=[hex(t) for t in mode_targets]))


def compact(report):
    result = dict(report)
    result['spans'] = [{k: v for k, v in span.items() if k != 'instructions'}
                       for span in report['spans']]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump', default=DEFAULT_DUMP)
    parser.add_argument('--out', type=Path, default=Path('build/cvs1-acquisition-audit.json'))
    parser.add_argument('--manifest-out', type=Path)
    args = parser.parse_args()
    result = produce(args.dump)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    if args.manifest_out:
        args.manifest_out.parent.mkdir(parents=True, exist_ok=True)
        args.manifest_out.write_text(json.dumps(compact(result), indent=2) + '\n', encoding='utf-8')
    print(f"Recorded {len(result['spans'])} spans, {len(result['dispatch_edges'])} explicit table edges, "
          f"{len(result['prompt_literals'])} prompt literals. Offline only.")


if __name__ == '__main__':
    main()
