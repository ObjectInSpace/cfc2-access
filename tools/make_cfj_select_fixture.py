"""Build the CFJ selection replay fixture from the retained live capture.

Input: tools/fixtures/cfj-capture-20261014/ (trace.bin.gz + session.jsonl.gz),
recorded 2026-10-14 on ROM 5 cfe, Arcade character select, side 0 human. The
player picked two characters; the CPU's pair was committed with them.

Every trace sample where the decoder's inputs change is written out: the phase
and select sub-state (R+0x21960, 2 bytes), the human mask (R+0x2197C) and both
side records (R+0x26E00, 0x20 bytes). The trailer holds the fighter records'
committed native IDs (R+0x1EC18/+0x1EC19 per side, stride 0x660) from the last
sample: the game's own result, independent of the decoder.
Output: tools/fixtures/cfj-select-live-20261014.bin

Usage: python tools/make_cfj_select_fixture.py [--check]
"""
import argparse
from pathlib import Path
import struct
import sys

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'fixtures' / 'cfj-capture-20261014'
OUT = HERE / 'fixtures' / 'cfj-select-live-20261014.bin'
PHASE, HUMANS, SIDES, SIDES_SIZE = 0x21960, 0x2197C, 0x26E00, 0x20
COMMITTED, FIGHTER_STEP = 0x1EC18, 0x660


def inputs(state):
    return bytes(state[PHASE:PHASE + 2]) + bytes((state[HUMANS],)) + bytes(state[SIDES:SIDES + SIDES_SIZE])


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
    committed = bytes(state[COMMITTED + side * FIGHTER_STEP + slot] for side in (0, 1) for slot in (0, 1))
    return b'CFJS' + struct.pack('<I', len(records)) + b''.join(records) + committed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = build()
    if args.check:
        if not OUT.exists() or OUT.read_bytes() != data:
            sys.exit(f'Stale: {OUT}')
        print('CFJ selection fixture is current.')
        return
    OUT.write_bytes(data)
    print(f'Wrote {OUT} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
