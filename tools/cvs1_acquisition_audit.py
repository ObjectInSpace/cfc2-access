"""Pinned, offline CVS1 dispatch/restore and rendered-prompt acquisition evidence.

These bounded traces are not a gameplay decoder, full CFG, or hook ABI approval.
See docs/cvs1-acquisition-trace.md. Reuses the lifecycle audit's code hash gate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from capstone import CS_AC_WRITE
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

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
    ('mode-branch-A-substate-6', 0x1405DBC30, 0x1405DBFCD),
    ('mode-branch-A-actor-4-entry', 0x1405D9E70, 0x1405D9F30),
    ('mode-branch-A-navigation-excerpt', 0x1405D9F30, 0x1405DA306),
    ('mode-branch-A-choice-actor-consumer', 0x1405DA306, 0x1405DA3BC),
    ('mode-branch-A-object-state-dispatch', 0x14060B500, 0x14060B50F),
    ('mode-branch-A-object-substate-dispatch', 0x14060A730, 0x14060A73F),
    ('mode-branch-A-object-choice-writer', 0x14060A800, 0x14060B44E),
    ('mode-branch-A-choice-object-creation', 0x140610CF0, 0x140611189),
    ('mode-branch-B-dispatch', 0x140602440, 0x14060252D),
    ('mode-branch-B-initializer', 0x140602530, 0x140602AE8),
    ('mode-branch-B-actor-dispatch', 0x140602B70, 0x140602CA3),
    ('mode-branch-B-choice-update-excerpt', 0x140606290, 0x14060690A),
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
    ('selection-render-callback', 0x1405DDCD0, 0x1405DDDAF),
    ('selection-list-renderer', 0x1405E1980, 0x1405E1EAD),
    ('selection-render-type-10', 0x1405DF8E0, 0x1405DFB62),
    ('shared-resource-draw-wrapper', 0x140F36200, 0x140F36215),
    ('input-copy-flag-initialization', 0x1405FEC40, 0x1405FED2E),
    ('resource-selector-dispatch-prefix', 0x1405ABD30, 0x1405ABD6B),
    ('selection-resource-branch-prefix', 0x1405AC282, 0x1405AC2AD),
    ('resource-publication', 0x1405AABA0, 0x1405AACE2),
    ('shared-resource-command-submission', 0x140F34CF0, 0x140F34E34),
    ('resource-offset-fixup', 0x1405AD9B0, 0x1405ADA33),
    ('shared-command-queue', 0x140F2F390, 0x140F2FB39),
    ('mode-branch-B-graphic-change-object', 0x1406048D0, 0x140604C51),
    ('side-graphic-refresh', 0x1405C8AF0, 0x1405C8BAD),
    ('graphic-refresh-dispatch', 0x1405C8BE0, 0x1405C8DA8),
    ('graphic-refresh-wrapper', 0x140F31BE0, 0x140F31C44),
    ('native-post-phase-update', 0x1405AF690, 0x1405AF6AA),
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
    ('mode-branch-A', 'P+3 signed byte', 0x141237D80, 6, 0x1405DBC30),
    ('mode-branch-A-actor', 'actor+4 unsigned byte', 0x141237DC0, 4, 0x1405D9E70),
    ('mode-branch-A-object', 'object+4 unsigned byte', 0x141242468, 1, 0x14060A730),
    ('mode-branch-A-object-substate', 'object+5 unsigned byte', 0x141242450, 1, 0x14060A800),
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
    ('mode-branch-B-object', 'object+0x34 unsigned byte', 0x141240E10, 10, 0x1406048D0),
)

PROMPTS = (
    (0x14106E608, 'FREE PLAY'),
    (0x14106E618, 'PRESS START BUTTON'),
    (0x14106E630, 'INSERT MORE COIN(S)'),
    (0x14106E648, '1 CREDITS TO START'),
    (0x14106E660, 'INSERT COIN(S)'),
)

# Selected rows from the initializer's 215-row, four-byte descriptor loop.
# Resource byte, subtype, position index, mode mask; not character names.
OBJECT_DESCRIPTORS = (
    (60, '3b0a0007'), (61, '3c0a0007'),
    (211, '2c020007'), (212, '2e020007'),
    (213, '2d020007'), (214, '2f020007'),
)

RENDER_TARGETS = (
    0x1405E1A20, 0x1405E1BA2, 0x1405E1BAF, 0x1405E1BBC,
    0x1405E1BC9, 0x1405E1CD9, 0x1405E1DB1, 0x1405E1DBE,
    0x1405E1DCB, 0x1405E1DD8, 0x1405E1DE2, 0x1405E1DEC,
)

CHECKED_RVA_TABLES = (
    ('branch-A-P4-6-entry-only', 0x1405DBFE8, (0x1405DBE21,)),
    ('side-graphic-mode', 0x1405C8BB0,
     (0x1405C8B2F, 0x1405C8B2F, 0x1405C8B2F, 0x1405C8B21, 0x1405C8B21,
      0x1405C8B21, 0x1405C8B2F, 0x1405C8B28, 0x1405C8B2F, 0x1405C8B2F)),
    ('graphic-object-resource-subtype', 0x140604C54,
     (0x14060490C, 0x14060490C, 0x140604912, 0x140604912, 0x140604923, 0x140604923)),
    ('command-type-2-entry-only', 0x140F2FB44, (0x140F2F69B,)),
)


def overlapping_rip_writes(dump, start, end, address):
    """Writes covering a byte in one bounded span, not all possible writers."""
    result = []
    for ins in dump.instructions(start, end):
        for op in ins.operands:
            if op.type != X86_OP_MEM or op.mem.base != X86_REG_RIP or not op.access & CS_AC_WRITE:
                continue
            target = ins.address + ins.size + op.mem.disp
            if target <= address < target + op.size:
                result.append(dict(instruction=hex(ins.address), target=hex(target),
                                   width=op.size, bytes=ins.bytes.hex()))
    return result


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
    render_raw = dump.read(0x1405E1EB0, 48)
    render_targets = tuple(0x140000000 + rva for rva in struct.unpack('<12I', render_raw))
    if render_targets != RENDER_TARGETS:
        raise ValueError('Render jump-table differs from reviewed dispatch')
    flag_writes = overlapping_rip_writes(dump, 0x1405FEC40, 0x1405FED2E, 0x1422D2B85)
    resources = []
    for selector in (0x29, 0x2A, 0x35):
        index = dump.read(0x1405AC588 + selector, 1)[0]
        target = 0x140000000 + struct.unpack('<I', dump.read(0x1405AC540 + index * 4, 4))[0]
        layout = dump.read(0x141217070 + selector * 16, 16)
        if index != 12 or target != 0x1405AC282 or layout.hex() != '00d0a2000068a4000000900000009e00':
            raise ValueError(f'Selection resource route differs: {selector:#x}')
        resources.append(dict(selector=selector, compressed_index=index, target=hex(target),
                              layout_address=hex(0x141217070 + selector * 16), bytes=layout.hex()))
    graphic_tables = []
    for label, address, expected in CHECKED_RVA_TABLES:
        raw = dump.read(address, len(expected) * 4)
        targets = tuple(0x140000000 + rva for rva in struct.unpack('<' + 'I' * len(expected), raw))
        if targets != expected:
            raise ValueError(f'Graphic route differs: {label}')
        graphic_tables.append(dict(label=label, address=hex(address), bytes=raw.hex(),
                                   targets=[hex(t) for t in targets]))
    return dict(schema=1, evidence='offline instruction/data evidence; no live validation',
                limitations=['Individual table edges do not prove selector bounds or screen meanings.',
                             'Text enqueue is not visibility; renderer execution needs live checks.',
                             'No character-ID/name map or safe hook ABI established.'],
                sections=[dict(name=s.name, live=hex(s.live), size=len(s.data),
                               sha256=hashlib.sha256(s.data).hexdigest()) for s in dump.sections],
                spans=spans, dispatch_edges=edges, prompt_literals=prompts,
                selected_object_descriptors=descriptors,
                render_table=dict(address='0x1405e1eb0',
                                  selector='object+0x9c dword & 0xf; unsigned <=11 guard',
                                  bytes=render_raw.hex(), targets=[hex(t) for t in render_targets]),
                input_copy_flag_writes=dict(address='0x1422d2b85',
                                           scope='0x1405fec40..0x1405fed2e only', writes=flag_writes),
                selected_resource_routes=resources,
                graphic_tables=graphic_tables,
                dump_pointer_snapshot={hex(a): hex(dump.pointer(a))
                                       for a in (0x1422D2B90, 0x142C79B40)},
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
