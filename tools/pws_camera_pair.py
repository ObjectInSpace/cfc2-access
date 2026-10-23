"""Compare PWS root snapshots with asynchronous renderer observations.

Numeric diagnostics only: timestamp proximity is not a synchronized draw,
and neither finite coordinates nor an on-screen range prove visible geometry.
"""
import argparse
from bisect import bisect_left
import gzip
import hashlib
import json
import math
from pathlib import Path
import struct

from pws_camera_decode import decode


def context_records(path):
    """Keep complete records from an interrupted sampler, including errors."""
    with gzip.open(path, 'rb') as stream:
        try:
            while True:
                header = stream.read(20)
                if not header:
                    return
                if len(header) != 20:
                    return
                t, pointer, size = struct.unpack('<dQI', header)
                if size not in (0, 512):
                    raise ValueError('Expected the captured 512-byte context window')
                raw = stream.read(size)
                if len(raw) != size:
                    return
                yield dict(t=t, pointer=hex(pointer), raw_hex=raw.hex())
        except EOFError:
            return


def project(view, projection, position):
    if len(view) != 16 or len(projection) != 16 or len(position) != 3:
        return None
    if not all(math.isfinite(v) for v in (*view, *projection, *position)):
        return None
    if tuple(view[i] for i in (3, 7, 11, 15)) != (0, 0, 0, 1):
        return None
    if tuple(projection[i] for i in (3, 7, 11, 15)) != (0, 0, -1, 0) or projection[0] <= 0:
        return None
    eye = [sum(position[r] * view[4*r+c] for r in range(3)) + view[12+c]
           for c in range(4)]
    clip = [sum(eye[r] * projection[4*r+c] for r in range(4)) for c in range(4)]
    if not all(math.isfinite(v) for v in clip) or clip[3] <= 1e-3:
        return None
    ndc = [clip[i] / clip[3] for i in range(3)]
    return dict(ndc=ndc, clip_w=clip[3]) if all(math.isfinite(v) for v in ndc) else None


def diagnose(game, state, t, before, after, max_gap=0.1):
    root = decode(game, state)
    result = dict(audio_eligible=False, bracket_usable=False,
                  ordinary_selected_index=root['ordinary_selected_index'], observations=[])
    bracket = [before, after]
    if any(r is None or len(bytes.fromhex(r['raw_hex'])) != 512 or int(r['pointer'], 16) == 0
           for r in bracket):
        return result
    if not (0 <= t-before['t'] <= max_gap and 0 <= after['t']-t <= max_gap):
        return result
    if before['pointer'] != after['pointer']:
        return result
    result['bracket_usable'] = True
    cams = [bytes.fromhex(r['raw_hex'])[0x2c:0xac] for r in bracket]
    result['bracket_camera_bytes_equal'] = cams[0] == cams[1]
    index = root['ordinary_selected_index']
    if index is None:
        return result
    selected = root['candidates'][index]
    if not selected['affine']:
        return result
    for row, raw in zip(bracket, cams):
        view, projection = struct.unpack('<16f', raw[:64]), struct.unpack('<16f', raw[64:])
        actors = []
        for actor in root['actor_candidates']:
            pos = actor['position']
            actors.append(dict(fixed_index=actor['fixed_index'], raw_slot=actor['raw_slot'],
                               stored_view=project(selected['matrix'], projection, pos) if pos else None,
                               context_view=project(view, projection, pos) if pos else None))
        result['observations'].append(dict(dt=row['t']-t,
            view_bytes_equal=raw[:64].hex() == selected['raw_hex'], actors=actors))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--context', type=Path, required=True)
    p.add_argument('--snapshots', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    meta = json.loads((args.context / 'meta.json').read_text())
    if int(meta['offset'], 16) != 0x1f8100 or meta['size'] != 512:
        raise ValueError('Wrong context layout')
    records = list(context_records(args.context / 'samples.bin.gz'))
    times = [r['t'] for r in records]
    if any(a >= b for a, b in zip(times, times[1:])):
        raise ValueError('Context timestamps must increase')
    manifest = json.loads((args.snapshots / 'manifest.json').read_text())
    retained = []
    for snapshot in manifest['snapshots']:
        raw = gzip.decompress((args.snapshots / (snapshot['block'] + '.gz')).read_bytes())
        if hashlib.sha256(raw).hexdigest() != snapshot['sha256']:
            raise ValueError('Snapshot hash mismatch')
        t = snapshot['t']
        index = bisect_left(times, t)
        before = records[index-1] if index else None
        after = records[index] if index < len(records) else None
        retained.append(dict(block=snapshot['block'], sha256=snapshot['sha256'], t=t,
            before=before, after=after, diagnostics=diagnose(manifest['provenance']['game'], raw, t, before, after)))
    result = dict(schema=1, audio_eligible=False, visual_validation=False,
                  limitation=__doc__.strip(), context_metadata=meta,
                  source_sha256=hashlib.sha256((args.context / 'samples.bin.gz').read_bytes()).hexdigest(),
                  complete_context_records=len(records), snapshots=retained)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
    print(f'Retained {len(retained)} asynchronous snapshot brackets; no audio eligibility.')


if __name__ == '__main__':
    main()
