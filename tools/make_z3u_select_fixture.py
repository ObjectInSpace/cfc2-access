"""Build the Alpha 3 selection replay fixture from the retained live capture.

Input: tools/fixtures/z3u-capture-20261014/ (trace.bin.gz + session.jsonl),
recorded 2026-10-14 on ROM 7 a3u, Arcade, side 0 human: character select,
the ism menu, then the speed menu, into the first match.

Every trace sample where the decoder's inputs change is written out: the mode
words R+0x28..0x2D, the speed byte R+0x3C, the masks R+0x59 and R+0x69, then
per side (objects R+0x1B0, stride 0x550) the state +4, sub-state +5, side
+0x1D1, character +0x1D2, random marker +0x59 and ism +0x202.
Output: tools/fixtures/z3u-select-live-20261014.bin

Usage: python tools/make_z3u_select_fixture.py [--check]
"""
import argparse
from pathlib import Path
import struct
import sys

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'fixtures' / 'z3u-capture-20261014'
OUT = HERE / 'fixtures' / 'z3u-select-live-20261014.bin'
OBJ, STEP = 0x1B0, 0x550
FIELDS = (0x04, 0x05, 0x1D1, 0x1D2, 0x59, 0x202)


def inputs(state):
    head = bytes(state[0x28:0x2E]) + bytes((state[0x3C], state[0x59], state[0x69]))
    return head + bytes(state[OBJ + side * STEP + f] for side in (0, 1) for f in FIELDS)


def build():
    records, state, last = [], None, None
    for _t, _root, key, payload in read_trace(SOURCE / 'trace.bin.gz'):
        if key:
            state = bytearray(payload)
        else:
            for offset, value in payload:
                state[offset] = value
        record = inputs(state)
        if record != last:
            records.append(record)
            last = record
    return b'Z3US' + struct.pack('<I', len(records)) + b''.join(records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = build()
    if args.check:
        if not OUT.exists() or OUT.read_bytes() != data:
            sys.exit(f'Stale: {OUT}')
        print('Alpha 3 selection fixture is current.')
        return
    OUT.write_bytes(data)
    print(f'Wrote {OUT} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
