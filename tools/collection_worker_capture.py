"""Bounded read-only worker/group inventory for the reviewed collection build.

Matching observations are not atomic and do not prove completion or lifetime.
Does not install hooks, call game functions, or read gameplay arenas.
"""
import argparse
from collections import deque
import hashlib
import json
from pathlib import Path
import struct
import time

from collection_metadata_capture import ProcessReader, exact, utc, verify_build

MANIFEST = Path(__file__).resolve().parents[1] / 'docs/collection-native-boundary-manifest.json'
APP = 0x1422C8B70
MANAGER = 0x1422C80B0
SESSION = 0x141C14F98
GROUP_VTABLE = 0x141069370
GROUP_UPDATE = 0x14056DFF0
GROUP_THUNK = 0x14056E480
MAX_NODES = 8192
MAX_EDGES = 16384
MAX_BYTES = 4 * 1024 * 1024
MAX_READS = 40000


def number(raw, offset=0, fmt='Q'):
    return struct.unpack_from('<' + fmt, raw, offset)[0]


def observe(read):
    """Inventory only reviewed list/group layouts; retain explicit coverage limits."""
    result = dict(slots={}, scheduler={}, identity={}, categories=[], nodes=[],
                  edges=[], ring=[], errors=[], limits=[], reads=0, bytes=0)

    def block(address, size):
        if result['reads'] >= MAX_READS or result['bytes'] + size > MAX_BYTES:
            if 'read_budget' not in result['limits']:
                result['limits'].append('read_budget')
            return None
        result['reads'] += 1
        result['bytes'] += size
        try:
            return exact(read, address, size)
        except OSError as exc:
            result['errors'].append(dict(address=hex(address), size=size, error=str(exc)))
            return None

    def qword(address):
        raw = block(address, 8)
        return number(raw) if raw is not None else None

    for label, slot in (('application', APP), ('unit_manager', MANAGER), ('session', SESSION)):
        value = qword(slot)
        result['slots'][label] = value

    session = result['slots']['session']
    if session:
        header = block(session, 0x48)
        modes = block(session + 0x7B4, 0x78)
        if header is not None:
            result['identity'].update(game=number(header, 8, 'I'), rom=number(header, 0x44, 'I'))
        if modes is not None:
            result['identity']['raw_session_fields'] = {
                hex(off): number(modes, off - 0x7B4, 'I')
                for off in (0x7B4, 0x80C, 0x828)}

    app = result['slots']['application']
    if app:
        pause = block(app + 0x78, 8)
        if pause is not None:
            result['scheduler'].update(pause_count=number(pause, fmt='I'),
                                       pause_enabled=pause[4], retirement_started=pause[7])
        header = block(app + 0x98, 0x30)
        owner = qword(app + 0x3FF78)
        result['scheduler']['application_unit_manager'] = owner
        if header is not None:
            result['scheduler'].update(
                batch_count=number(header, 0, 'I'), batch_claimed=number(header, 4, 'I'),
                batch_kind=number(header, 8, 'I'), batch_width=number(header, 12, 'I'),
                generic_workers=number(header, 0x14, 'I'),
                producer=number(header, 0x18), consumer=number(header, 0x20))
        # Include records even when producer==consumer: a popped callback can run.
        ring = block(app + 0x1FAC8, 4096 * 32)
        if ring is not None:
            for slot in range(4096):
                context, callback, argument, kind = struct.unpack_from('<QQQI', ring, slot * 32)
                if context or callback:
                    result['ring'].append(dict(slot=slot, context=context, callback=callback,
                                               argument=argument, kind=kind))

    queue, seen, vtables, nodes, expanded_next = deque(), set(), {}, {}, set()

    def follow_link(row):
        if row['address'] not in expanded_next:
            expanded_next.add(row['address'])
            edge(row['address'], row['next'], 'list_next')

    def edge(source, target, kind, index=None):
        if not target:
            return
        if len(result['edges']) >= MAX_EDGES:
            if 'edge_budget' not in result['limits']:
                result['limits'].append('edge_budget')
            return
        result['edges'].append(dict(source=source, target=target, kind=kind, index=index))
        if target not in seen:
            queue.append((target, kind != 'group_member'))

    manager = result['slots']['unit_manager']
    if manager:
        result['unit_mask'] = qword(manager + 0x1848)
        count = block(manager + 0xC38, 4)
        if count is not None:
            count = number(count, fmt='I')
            result['category_count'] = count
            if count > 64:
                result['limits'].append('category_count_above_reviewed_layout')
            else:
                for category in range(count):
                    raw = block(manager + 0x48 + category * 0x30, 0x18)
                    if raw is None:
                        continue
                    flags, head, tail = number(raw, fmt='I'), number(raw, 8), number(raw, 16)
                    result['categories'].append(dict(category=category, flags=flags,
                                                     head=head, tail=tail))
                    edge(f'category:{category}', head, 'category_head', category)

    # The known thunk is positive evidence that its context has group layout.
    # A null callback is NOT sufficient to identify an in-flight context type.
    for record in result['ring']:
        if record['callback'] == GROUP_THUNK and record['kind'] == 0:
            edge(f"ring:{record['slot']}", record['context'], 'ring_group')

    while queue:
        address, follow_next = queue.popleft()
        if address in seen:
            if follow_next and address in nodes:
                follow_link(nodes[address])
            continue
        if len(seen) >= MAX_NODES:
            result['limits'].append('node_budget')
            break
        seen.add(address)
        raw = block(address, 0x38)
        if raw is None:
            continue
        vtable = number(raw)
        row = dict(address=address, vtable=vtable, flags=number(raw, 0x10, 'I'),
                   next=number(raw, 0x18), previous=number(raw, 0x20))
        result['nodes'].append(row)
        nodes[address] = row
        if follow_next:
            follow_link(row)
        if vtable not in vtables:
            vtables[vtable] = qword(vtable + 0x40) if vtable else None
        row['update'] = vtables[vtable]
        row['known_group_layout'] = vtable == GROUP_VTABLE
        if vtable != GROUP_VTABLE:
            if row['update'] == GROUP_UPDATE:
                result['limits'].append(f'unreviewed_group_vtable:{vtable:#x}')
            continue
        group = block(address + 0x60, 0x58)
        if group is None:
            continue
        count, members = number(group, fmt='I'), number(group, 0x10)
        row['group'] = dict(count=count, members=members, saved_indices=number(group, 0x48),
                            parallel_enabled=group[0x50], threshold=number(group, 0x54, 'I'))
        if count > MAX_EDGES - len(result['edges']):
            result['limits'].append(f'group_members_truncated:{address:#x}')
            continue
        if count and not members:
            result['limits'].append(f'group_members_null:{address:#x}')
            continue
        if count:
            array = block(members, count * 8)
            if array is not None:
                for index in range(count):
                    edge(address, number(array, index * 8), 'group_member', index)

    result['coverage'] = 'Unit category lists and exact reviewed geometry groups only; other containers/callback descendants are not traversed.'
    result['lifetime_proven'] = False
    return result


def capture(read):
    started = time.perf_counter()
    before, after = observe(read), observe(read)
    limited = any(row['limits'] for row in (before, after))
    errors = any(row['errors'] for row in (before, after))
    return dict(created_utc=utc(), before=before, after=after,
                elapsed_ms=(time.perf_counter() - started) * 1000,
                status='unreadable' if errors else 'limited' if limited else
                       'matching' if before == after else 'changed',
                lifetime_proven=False,
                note='Matching reads do not prove atomicity, completion, absence of transient groups, or a global submission bound.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--mode', choices=('Arcade', 'Versus', 'Training'), required=True)
    parser.add_argument('--label', required=True, help='Observed screen/action')
    parser.add_argument('--count', type=int, default=1)
    parser.add_argument('--interval', type=float, default=0.25)
    args = parser.parse_args()
    if args.pid <= 0 or args.out.exists():
        parser.error('Require positive PID and a new output path')
    if not 1 <= args.count <= 60 or not 0.1 <= args.interval <= 1:
        parser.error('Require count 1..60 and interval 0.1..1 seconds')
    manifest_bytes = MANIFEST.read_bytes()
    reader = ProcessReader(args.pid)
    try:
        checked = verify_build(reader.read, json.loads(manifest_bytes))
        report = dict(schema=1, kind='collection-worker-inventory', pid=args.pid,
                      observed_mode=args.mode, label=args.label,
                      manifest_sha256=hashlib.sha256(manifest_bytes).hexdigest(),
                      tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      checked_code_spans=checked, samples=[], interval_seconds=args.interval)
        for index in range(args.count):
            if index:
                time.sleep(args.interval)
            report['samples'].append(capture(reader.read))
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.open('x', encoding='utf-8') as output:
            json.dump(report, output, indent=2)
            output.write('\n')
        print(f"Wrote {args.out}: {len(report['samples'])} samples; lifetime remains unproven.")
    finally:
        reader.close()


if __name__ == '__main__':
    main()
