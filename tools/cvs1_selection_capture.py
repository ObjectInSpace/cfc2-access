"""Read-only, bracketed CVS1 selection evidence; no hooks or speech decoding."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import time

import collection_metadata_capture as metadata

ACQUISITION = Path(__file__).resolve().parents[1] / 'docs/cvs1-acquisition-manifest.json'
ROOT_SLOT, PHASE_SLOT, CONTEXT_SLOT = 0x1422D2B90, 0x1422D2BA0, 0x142C79B40
MAX_ASSET_BYTES = 4096  # Operator capture cap, not a proven asset length.


def number(raw, offset=0, fmt='Q'):
    return struct.unpack_from('<' + fmt, raw, offset)[0]


def verify(read, lifecycle, acquisition):
    checked = metadata.verify_build(read, lifecycle) + metadata.verify_build(read, acquisition)
    # Data dispatch/layout bytes matter too; do not check mutable snapshot pointers.
    rows = list(acquisition['dispatch_edges']) + list(acquisition['selected_object_descriptors'])
    rows += list(acquisition['graphic_tables'])
    rows += [acquisition['render_table'], acquisition['mode_table']]
    for row in rows:
        address = int(row.get('slot', row.get('address')), 16)
        raw = bytes.fromhex(row['bytes'])
        if metadata.exact(read, address, len(raw)) != raw:
            raise ValueError(f'Reviewed dispatch/data differs: {address:#x}')
    return checked


def observe(read, lifecycle):
    result = dict(metadata=metadata.observe(read, lifecycle), records={}, eligible=False)
    m = result['metadata']
    d = m['decoded']
    if m['errors']:
        result['reason'] = 'unreadable identity'
        return result
    path = next(p for p in lifecycle['paths'] if p['tag'] == 'CVS1')
    callbacks = {offset: row['target'] for offset, row in path['callbacks'].items()}
    root = int(d.get('primary', '0x0'), 16)
    context = int(d['slots'].get(hex(CONTEXT_SLOT)) or '0x0', 16)
    identity = (d.get('game') == 0 and
                (d.get('rom_index'), d.get('rom_name')) in ((0, 'cvs1'), (1, 'cvs1u')) and
                d.get('callbacks') == callbacks and root != 0 and context != 0 and
                d.get('primary_size') == 0x32AB0 and
                d['slots'].get(hex(ROOT_SLOT)) == hex(root))
    if not identity:
        result['reason'] = 'CVS1 identity/publication gate not satisfied'
        return result

    def block(name, address, size):
        raw = metadata.exact(read, address, size)
        result['records'][name] = dict(address=hex(address), size=size, hex=raw.hex())
        return raw

    try:
        phase = number(block('phase_pointer', PHASE_SLOT, 8))
        if phase != root + 0x3E58:
            result['reason'] = 'phase pointer mismatch'
            return result
        p = block('phase', phase, 0x112)
        counts = block('navigation_counts', root + 0x327BC, 16)
        actors = []
        for side in range(2):
            actor = root + 0x3FB8 + side * 0x700
            header = block(f'actor_{side}_header', actor, 0x38)
            owner = number(block(f'actor_{side}_owner', actor + 0x688, 1), fmt='b')
            inputs = block(f'actor_{side}_inputs', actor + 0x5BA, 0x20)
            slot = number(p, 0xA0 + side, 'b')
            selected = number(p, 0x78 + 4 * side + slot, 'b') if 0 <= slot < 4 else None
            actors.append(dict(side=side, owner=owner, slot=slot, selected=selected,
                               actor_id=header[1], state=header[4], activity=header[5],
                               cooldown=number(header, 0x30, 'H'),
                               input_words={hex(offset): number(inputs, offset - 0x5BA, 'H')
                                            for offset in (0x5BA, 0x5BC, 0x5BE, 0x5C0,
                                                           0x5C2, 0x5C4, 0x5C6, 0x5D8)}))
        block('pending_refresh', root + 0x30BFB, 2)
        block('input_copy_flag', 0x1422D2B85, 1)
        buffer = number(block('context_buffer', context + 8, 8))
        block('resource_base', root + 0x52B0, 8)
        result.update(actors=actors, buffer=hex(buffer), mode=p[0x48])
        # Fixed side/group domains only. Never index reads using an untrusted owner/group.
        result['navigation'] = dict(enabled_mask=p[0x111], sides=[
            dict(side=side, group=number(p, 0x10C + side, 'b'),
                 delta=number(p, 0x10E + side, 'b'),
                 positions=[number(p, 0x104 + 4 * side + group, 'b') for group in range(4)],
                 counts=[number(counts, 2 * (4 * side + group), 'H') for group in range(4)])
            for side in range(2)])
        result['eligible'] = p[:4] == bytes((2, 1, 1, 2)) and p[0x48] in (1, 4, 5, 7)
        result['reason'] = 'branch-B selection gate' if result['eligible'] else 'outside reviewed branch-B phase'
    except OSError as exc:
        result.update(reason='unreadable state', error=str(exc))
    return result


def capture(read, lifecycle, asset_bytes=0):
    if not 0 <= asset_bytes <= MAX_ASSET_BYTES:
        raise ValueError('asset_bytes must be 0..4096')
    started_utc, started = metadata.utc(), time.perf_counter()
    before = observe(read, lifecycle)
    assets = []
    if asset_bytes and before['eligible']:
        for actor in before['actors']:
            row = dict(side=actor['side'], status='skipped')
            assets.append(row)
            ident = actor['actor_id']
            # Conservative collection domain only; not a claimed legal-ID map.
            if (actor['owner'] != actor['side'] or actor['selected'] != ident or
                    actor['cooldown'] != 0 or actor['state'] != 6 or actor['activity'] != 6 or
                    not 0 <= ident <= 0x22 or int(before['buffer'], 16) == 0):
                row['reason'] = 'owner/slot/actor/special-ID gate not satisfied'
                continue
            mapped = {9: 0x23, 0x12: 0x24, 0x19: 0x25}.get(ident, ident) if actor['side'] else ident
            address = int(before['buffer'], 16) + 0xAB0000 + (mapped << 15)
            row.update(native_id=ident, mapped_id=mapped, address=hex(address), size=asset_bytes)
            try:
                raw = metadata.exact(read, address, asset_bytes)
                row.update(status='read', hex=raw.hex(), sha256=hashlib.sha256(raw).hexdigest())
            except OSError as exc:
                row.update(status='unreadable', error=str(exc))
    after = observe(read, lifecycle)
    bad = any('error' in x or x['metadata']['errors'] for x in (before, after))
    bad |= any(a['status'] == 'unreadable' for a in assets)
    status = ('unreadable' if bad else 'changed' if before != after else
              'matching' if before['eligible'] else 'outside_scope')
    return dict(started_utc=started_utc, elapsed_ms=(time.perf_counter()-started)*1000,
                status=status, before=before, after=after, assets=assets,
                note='Non-atomic raw observations. Matching cannot exclude ABA, stale state, or asset mutation; no visibility/name claim.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--mode', required=True)
    parser.add_argument('--language', required=True)
    parser.add_argument('--count', type=int, default=1)
    parser.add_argument('--interval', type=float, default=0.25)
    parser.add_argument('--asset-bytes', type=int, default=0, help='Optional raw prefix per side, 0..4096; not an image export')
    args = parser.parse_args()
    if (args.pid <= 0 or not 1 <= args.count <= 240 or not math.isfinite(args.interval) or
            not 0.1 <= args.interval <= 5 or args.count * args.interval > 60 or
            not 0 <= args.asset_bytes <= MAX_ASSET_BYTES):
        parser.error('Require PID>0, count 1..240, interval 0.1..5, schedule<=60s, asset bytes 0..4096')
    if args.out.exists():
        parser.error('Output exists; choose a fresh path')
    manifests = [p.read_bytes() for p in (metadata.MANIFEST, ACQUISITION)]
    lifecycle, acquisition = map(json.loads, manifests)
    reader = metadata.ProcessReader(args.pid)
    try:
        checked = verify(reader.read, lifecycle, acquisition)
        provenance = dict(kind='cvs1-selection-capture', schema=2, pid=args.pid,
                          label=args.label, mode=args.mode, language=args.language,
                          created_utc=metadata.utc(), asset_bytes=args.asset_bytes,
                          count=args.count, interval=args.interval, checked_code_spans=checked,
                          manifest_sha256=[hashlib.sha256(m).hexdigest() for m in manifests],
                          tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                          transport_sha256=hashlib.sha256(Path(metadata.__file__).read_bytes()).hexdigest())
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.open('x', encoding='utf-8') as out:
            out.write(json.dumps(provenance) + '\n')
            for index in range(args.count):
                if index:
                    time.sleep(args.interval)
                row = capture(reader.read, lifecycle, args.asset_bytes)
                row['sample_index'] = index
                out.write(json.dumps(row) + '\n')
                out.flush()
                print(f"Sample {index+1}: {row['status']}", flush=True)
        print(f'Wrote {args.out}; raw evidence only.')
    finally:
        reader.close()


if __name__ == '__main__':
    main()
