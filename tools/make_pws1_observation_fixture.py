"""Retain memory observations from the interrupted 2026-10-22 PWS1 capture.

Screenshots were stale desktop images. Plan labels are prompts, NOT verified
screen labels. This fixture cannot certify names, mode, ownership or camera.
"""
import gzip
import hashlib
import json
from pathlib import Path

SOURCE = Path('build/pws1-live-20261022-c')
TARGET = Path('tools/fixtures/pws1-observations-20261022')


def main():
    rows = [json.loads(line) for line in (SOURCE / 'session.jsonl').read_text().splitlines()]
    snapshots = [r for r in rows if r['kind'] == 'snapshot' and r['status'] == 'read'
                 and r['index'] < 13]
    if len(snapshots) != 13:
        raise ValueError('Expected the reviewed 13 snapshots')
    TARGET.mkdir(parents=True, exist_ok=True)
    for row in snapshots:
        raw = (SOURCE / row['block']).read_bytes()
        if hashlib.sha256(raw).hexdigest() != row['sha256']:
            raise ValueError('Snapshot hash mismatch')
        (TARGET / (row['block'] + '.gz')).write_bytes(gzip.compress(raw, mtime=0))
    report = dict(schema=1, source=str(SOURCE), provenance=rows[0],
                  visual_validation=False,
                  limitation=__doc__.strip(),
                  snapshots=snapshots,
                  expected_slot_zero=[-1, 8, 6, -2, -2, -1, -1, -1, -1, -1, -1, -1, -1])
    (TARGET / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
