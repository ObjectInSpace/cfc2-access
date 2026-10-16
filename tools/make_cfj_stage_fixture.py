"""Build the CFE stage-select replay fixture from the 2026-10-15 capture.

Input: tools/fixtures/cfj-stage-select-20261015/ (trace.bin.gz +
session.jsonl.gz), recorded on ROM 5 cfe. The user picked characters in
Versus, then in Training, and on the stage menu that follows both picks
(select sub-state 4) moved the cursor: Versus 0 <-> 1, Training 0 <-> 10.

Every expectation below was read by hand off the trace (phase R+0x21960,
sub-state R+0x21961, stage word R+0x26E00), not produced by the decoder.
Expected value 0xFF = the stage menu is not open.

Each record: the time (f64), the expected stage entry, then the decoder's
inputs: phase (2), the human mask R+0x2197C, the side records (0x20).
Output: tools/fixtures/cfj-stage-select-20261015.bin

Usage: python tools/make_cfj_stage_fixture.py [--check]
"""
import argparse
import bisect
from pathlib import Path
import struct
import sys

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'fixtures' / 'cfj-stage-select-20261015'
OUT = HERE / 'fixtures' / 'cfj-stage-select-20261015.bin'
PHASE, HUMANS, SIDES = 0x21960, 0x2197C, 0x26E00
T0 = 108857.3937106        # the trace's first record
NONE = 0xFF

MOMENTS = [
    # Versus
    (7.00, NONE),          # picking (sub-state 0)
    (13.58, NONE),         # both picked, sub-state 0 -> 1
    (13.80, NONE),         # sub-state 1
    (14.50, 0),            # stage menu open on Random
    (15.30, 1),
    (16.00, 0),
    (18.00, 1),
    (18.30, 0),
    (18.70, NONE),         # confirmed: sub-state 6
    (19.20, NONE),         # sub-state 3
    (19.60, NONE),         # loading the match (phase 4)
    # Training
    (36.00, NONE),         # picking
    (40.60, NONE),         # sub-state 1
    (41.20, 10),           # left from Random wraps to 10
    (41.60, 0),
    (41.80, 10),
    (41.95, 0),
    (42.10, 10),
    (42.50, NONE),         # confirmed
    (43.30, NONE),
]


def build():
    times, states, state = [], [], None
    for t, _root, key, payload in read_trace(SOURCE / 'trace.bin.gz'):
        if key:
            state = bytearray(payload)
        else:
            for offset, value in payload:
                state[offset] = value
        times.append(t)
        states.append(bytes(state))
    out = bytearray(b'CFJT') + struct.pack('<I', len(MOMENTS))
    for rel, want in MOMENTS:
        s = states[max(0, bisect.bisect_right(times, T0 + rel) - 1)]
        out += struct.pack('<dB', rel, want)
        out += s[PHASE:PHASE + 2] + bytes((s[HUMANS],)) + s[SIDES:SIDES + 0x20]
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = build()
    if args.check:
        if not OUT.exists() or OUT.read_bytes() != data:
            sys.exit(f'Stale: {OUT}')
        print('CFE stage-select fixture is current.')
        return
    OUT.write_bytes(data)
    print(f'Wrote {OUT} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
