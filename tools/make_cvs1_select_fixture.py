"""Build the CVS1 selection replay fixture from the retained live capture.

Input: tools/fixtures/cvs1-capture-20261013/ (trace.bin.gz + session.jsonl.gz),
recorded 2026-10-13 on ROM 1 cvs1u, branch-A Arcade character select, side 0.
The capture's 317 logged presses were dropped: all came after the game closed
at 51 s, so they are not game input.

For every selection row, the state at that moment is reconstructed from the
trace and the decoder's inputs are written out: P (0x112 bytes), both actor
headers (0x38 each) and owner bytes, with the side and the native ID the
capture saw. Output: tools/fixtures/cvs1-select-live-20261013.bin

Usage: python tools/make_cvs1_select_fixture.py [--check]
"""
import argparse
import bisect
import gzip
import json
from pathlib import Path
import struct
import sys

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'fixtures' / 'cvs1-capture-20261013'
OUT = HERE / 'fixtures' / 'cvs1-select-live-20261013.bin'
P, ACTOR, STEP, HEAD, OWNER = 0x3E58, 0x3FB8, 0x700, 0x38, 0x688


def build():
    rows = [json.loads(l) for l in gzip.open(SOURCE / 'session.jsonl.gz', 'rt', encoding='utf-8')]
    states, times, state = [], [], None
    for t, root, key, payload in read_trace(SOURCE / 'trace.bin.gz'):
        if key:
            state = bytearray(payload)
        else:
            for offset, value in payload:
                state[offset] = value
        times.append(t)
        states.append(bytes(state))
    out = bytearray(b'CV1S')
    picks = [r for r in rows if r['kind'] == 'selection']
    out += struct.pack('<I', len(picks))
    for r in picks:
        s = states[max(0, bisect.bisect_right(times, r['t']) - 1)]
        record = s[P:P + 0x112] + s[ACTOR:ACTOR + HEAD] + s[ACTOR + STEP:ACTOR + STEP + HEAD]
        record += bytes((s[ACTOR + OWNER], s[ACTOR + STEP + OWNER]))
        out += struct.pack('<BB', r['side'], r['native_id']) + record
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = build()
    if args.check:
        if not OUT.exists() or OUT.read_bytes() != data:
            sys.exit(f'Stale: {OUT}')
        print('CVS1 selection fixture is current.')
        return
    OUT.write_bytes(data)
    print(f'Wrote {OUT} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
