"""Build the Alpha 3 EX Mode Select replay fixture from the 2026-10-15 capture.

Input: tools/fixtures/z3u-mode-select-20261015/ (trace.bin.gz +
session.jsonl.gz), recorded on ROM 17 a3uu (alternate copy, Additional
Characters on) with EX Mode Select on, Versus, both sides human. P1 stepped
the mode menu through every value and confirmed 3 (the ism menu was then
skipped, speed followed); P2 hovered Ken meanwhile, then stepped the mode menu
and confirmed 0, then took the ism menu. After about 62 s the user switched to
the ordinary copy, which this capture could not follow: the ordinary copy's
mode cursor (+0x237) is code-traced only.

Every expectation below was read by hand off the trace (object state +4,
sub +5, mode cursor +0x242, ism +0x20A, speed R+0x3C), not produced by the
decoder. Kinds: 0 character (value = native ID), 1 ism (value = ism + 1),
2 speed, 3 mode. 0xFF = inactive.

Each record: the time, the expected (kind, value) per side, the mode words
R+0x28..0x2D, the speed byte R+0x3C, the masks R+0x59 and R+0x69, then per
side (objects R+0x1F8, stride 0x580) state +4, sub +5, side +0x1D9, ID
+0x1DA, random +0x61, ism +0x20A and mode +0x242.
Output: tools/fixtures/z3u-mode-select-20261015.bin

Usage: python tools/make_z3u_mode_fixture.py [--check]
"""
import argparse
import bisect
from pathlib import Path
import struct
import sys

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'fixtures' / 'z3u-mode-select-20261015'
OUT = HERE / 'fixtures' / 'z3u-mode-select-20261015.bin'
OBJ, STEP = 0x1F8, 0x580
FIELDS = (0x04, 0x05, 0x1D9, 0x1DA, 0x61, 0x20A, 0x242)
T0 = 107730.0194657        # the trace's first record
NONE = (0xFF, 0)
KEN = (0, 1)

# (seconds after T0, side 0 expectation, side 1 expectation)
MOMENTS = [
    (3.00, (3, 0), KEN),          # P1 mode menu opens on 0
    (8.00, (3, 1), KEN),
    (9.00, (3, 0), KEN),
    (10.00, (3, 3), KEN),         # wraps 0 -> 3
    (11.00, (3, 2), KEN),
    (11.50, (3, 1), KEN),
    (12.50, (3, 0), KEN),
    (22.80, NONE, KEN),           # P1 confirmed 3: sub 2, ism now 0xFF
    (23.50, (2, 1), KEN),         # P1 speed menu, Turbo; no ism menu
    (24.40, NONE, KEN),           # speed confirmed
    (27.00, NONE, NONE),          # P1 done; P2 grid confirmed (sub 2)
    (27.50, NONE, (3, 0)),        # P2 mode menu opens on 0
    (27.80, NONE, (3, 1)),
    (28.30, NONE, (3, 0)),
    (28.80, NONE, (3, 3)),
    (29.30, NONE, (3, 2)),
    (29.70, NONE, (3, 1)),
    (48.05, NONE, (3, 1)),
    (48.15, NONE, (3, 2)),
    (50.50, NONE, (3, 0)),
    (52.50, NONE, NONE),          # P2 confirmed 0
    (53.50, NONE, (1, 1)),        # P2 ism menu: ism 0 -> value 1
    (54.30, NONE, NONE),
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
    out = bytearray(b'Z3UM') + struct.pack('<I', len(MOMENTS))
    for rel, e0, e1 in MOMENTS:
        s = states[max(0, bisect.bisect_right(times, T0 + rel) - 1)]
        out += struct.pack('<dBBBB', rel, e0[0], e0[1], e1[0], e1[1])
        out += bytes(s[0x28:0x2E]) + bytes((s[0x3C], s[0x59], s[0x69]))
        out += bytes(s[OBJ + side * STEP + f] for side in (0, 1) for f in FIELDS)
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = build()
    if args.check:
        if not OUT.exists() or OUT.read_bytes() != data:
            sys.exit(f'Stale: {OUT}')
        print('Alpha 3 mode-select fixture is current.')
        return
    OUT.write_bytes(data)
    print(f'Wrote {OUT} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
