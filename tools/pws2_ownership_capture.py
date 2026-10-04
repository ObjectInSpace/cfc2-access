"""Read-only PWS2 ownership research samples, NOT draw-stage camera samples.

Raw records support labelled comparisons. No player, gauge, visible-camera,
atomic-frame or production-audio interpretation is made.
"""
import argparse
import hashlib
import json
from pathlib import Path
import time

import collection_metadata_capture as metadata

ACTOR_MANIFEST = Path(__file__).resolve().parents[1] / 'docs/pws2-actor-manifest.json'
HUD_MANIFEST = Path(__file__).resolve().parents[1] / 'docs/pws2-hud-manifest.json'
ROOT_SLOT = '0x14262f0b0'
RANGES = (('actors', 0x40818, 4 * 0x42b8),
          ('transforms', 0x51d58, 4 * 0x308),
          ('inputs', 0x53114, 8 * 0x28),
          ('display_layout_gate', 0x5300e, 1),
          ('display_gauge_state', 0x5327c, 7 * 4 * 4))


def eligible(observation, manifest):
    """Identity gate only; never a match/readiness or lifetime certificate."""
    if observation['errors']:
        return False
    d = observation['decoded']
    path = next(p for p in manifest['paths'] if p['tag'] == 'PWS2')
    root = d['slots'].get(ROOT_SLOT)
    return (d.get('session') not in (None, '0x0') and root not in (None, '0x0')
            and d.get('game') == 5
            and (d.get('rom_index'), d.get('rom_name'), d.get('region'))
            in ((10, 'pws2', 0), (11, 'pws2u', 1))
            and d.get('primary') == root
            and d.get('primary_size') == int(path['published_size'], 16)
            and d.get('callbacks') == {off: value['target']
                                      for off, value in path['callbacks'].items()})


def capture(read, manifest):
    start = time.perf_counter()
    before = metadata.observe(read, manifest)
    result = dict(kind='pws2-ownership-sample', evidence_kind='unverified_capture',
                  started_utc=metadata.utc(), before=before, records={},
                  claim_scope='raw_ownership_research_only', audio_eligible=False,
                  note='Not draw-stage synchronized; matching brackets cannot exclude ABA reuse or torn records.')
    if not eligible(before, manifest):
        result['status'] = 'identity_rejected'
    else:
        root = int(before['decoded']['primary'], 16)
        errors = []
        for name, offset, size in RANGES:
            record = dict(address=hex(root + offset), size=size)
            result['records'][name] = record
            try:
                record.update(status='read', hex=metadata.exact(read, root + offset, size).hex())
            except OSError as exc:
                record.update(status='unreadable', error=str(exc))
                errors.append(name)
        after = metadata.observe(read, manifest)
        result['after'] = after
        result['status'] = ('unreadable' if errors or after['errors'] else
                            'identity_changed' if not eligible(after, manifest)
                            or before['records'] != after['records'] else
                            'matching_metadata_only')
    result['elapsed_ms'] = (time.perf_counter() - start) * 1000
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', required=True, help='Observed action, participant and HUD description')
    parser.add_argument('--mode', required=True)
    parser.add_argument('--language', required=True)
    parser.add_argument('--count', type=int, default=1)
    parser.add_argument('--interval', type=float, default=0.25)
    args = parser.parse_args()
    if args.pid <= 0 or not 1 <= args.count <= 120 or not 0.1 <= args.interval <= 5:
        parser.error('Require PID > 0, count 1..120 and interval 0.1..5 seconds')
    if args.count * args.interval > 30:
        parser.error('Sampling schedule exceeds 30 seconds')
    if not all(s.strip() for s in (args.label, args.mode, args.language)):
        parser.error('Labels must not be blank')
    if args.out.exists():
        parser.error('Output exists; choose a new path')
    files = [metadata.MANIFEST, ACTOR_MANIFEST, HUD_MANIFEST,
             Path(__file__), Path(metadata.__file__)]
    contents = [p.read_bytes() for p in files]
    lifecycle, actors, hud = (json.loads(raw) for raw in contents[:3])
    reader = metadata.ProcessReader(args.pid)
    try:
        checks = {name: metadata.verify_build(reader.read, manifest)
                  for name, manifest in (('lifecycle', lifecycle), ('actors', actors), ('hud', hud))}
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.open('x', encoding='utf-8') as output:
            header = dict(schema=2, kind='pws2-ownership-capture', pid=args.pid,
                          created_utc=metadata.utc(), label=args.label, mode=args.mode,
                          language=args.language, requested_count=args.count,
                          interval_seconds=args.interval, checked_code_spans=checks,
                          provenance={p.name: hashlib.sha256(raw).hexdigest()
                                      for p, raw in zip(files, contents)},
                          evidence_kind='unverified_capture',
                          build_check='Bounded code spans only; not full live image or DLL validation.')
            output.write(json.dumps(header) + '\n')
            for index in range(args.count):
                if index:
                    time.sleep(args.interval)
                row = capture(reader.read, lifecycle)
                row['sample_index'] = index
                output.write(json.dumps(row) + '\n')
                output.flush()
                print(f"Sample {index + 1}: {row['status']}", flush=True)
            output.write(json.dumps(dict(kind='capture_complete', samples=args.count)) + '\n')
        print('Capture complete; no camera, HUD ownership or audio validation claimed.')
    finally:
        reader.close()


if __name__ == '__main__':
    main()
