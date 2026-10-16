"""Build the CVS1 branch-B stage-menu replay fixture from the 2026-10-15 capture.

Input: the same capture as make_cvs1_versus_fixture.py
(tools/fixtures/cvs1-select-versus-20261015/). The stage menu is the first
select menu in Versus and Training (actor state 2, handler 0x140606E90); the
user's first F9 was taken on it.

Its cursor is the image global 0x1422D3E82, outside the captured block, but
every press writes STAGE_TABLE[cursor] to R+0x529C, which the trace holds.
STAGE_TABLE (0x141240D60) is a permutation of 0..15, so each traced stage
byte gives the cursor back exactly. Every expectation below was read by hand
off the trace's R+0x529C and actor states, not produced by the decoder.
Expected 0xFF = the menu is not open.

Each record: the time (f64), the expected cursor, the cursor recovered from
R+0x529C, then P (0x118), both actor headers (0x38) and both owner bytes.
Output: tools/fixtures/cvs1-stage-select-20261015.bin

Usage: python tools/make_cvs1_stage_fixture.py [--check]
"""
import argparse
import bisect
from pathlib import Path
import struct
import sys

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'fixtures' / 'cvs1-select-versus-20261015'
OUT = HERE / 'fixtures' / 'cvs1-stage-select-20261015.bin'
P, ACTOR, STEP, HEAD, OWNER, STAGE = 0x3E58, 0x3FB8, 0x700, 0x38, 0x688, 0x529C
STAGE_TABLE = [15, 0, 1, 2, 3, 5, 6, 7, 8, 10, 4, 9, 12, 11, 13, 14]
T0 = 106398.0090658
NONE = 0xFF

# Versus: R+0x529C read F, 0, F, E, F, 0, 1, 2 (cursor 0, 1, 0, 15, 0, 1, 2,
# 3); Training: F, E, F (cursor 0, 15, 0), then Random was confirmed.
MOMENTS = [
    (5.00, 0), (12.50, 1), (13.40, 0), (15.00, 15), (18.80, 0), (19.50, 1),
    (20.40, 2), (22.00, 3),
    (26.70, NONE),         # state 3: the menu was confirmed
    (30.00, NONE),         # total ratio
    (114.38, 0), (114.60, 15), (116.00, 0),
    (117.92, NONE),        # confirmed
]


def build():
    assert sorted(STAGE_TABLE) == list(range(16))
    times, states, state = [], [], None
    for t, _root, key, payload in read_trace(SOURCE / 'trace.bin.gz'):
        if key:
            state = bytearray(payload)
        else:
            for offset, value in payload:
                state[offset] = value
        times.append(t)
        states.append(bytes(state))
    out = bytearray(b'CV1T') + struct.pack('<I', len(MOMENTS))
    for rel, want in MOMENTS:
        s = states[max(0, bisect.bisect_right(times, T0 + rel) - 1)]
        cursor = STAGE_TABLE.index(s[STAGE])
        a0, a1 = ACTOR, ACTOR + STEP
        out += struct.pack('<dBB', rel, want, cursor)
        out += s[P:P + 0x118] + s[a0:a0 + HEAD] + s[a1:a1 + HEAD]
        out += bytes((s[a0 + OWNER], s[a1 + OWNER]))
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = build()
    if args.check:
        if not OUT.exists() or OUT.read_bytes() != data:
            sys.exit(f'Stale: {OUT}')
        print('CVS1 stage-menu fixture is current.')
        return
    OUT.write_bytes(data)
    print(f'Wrote {OUT} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
