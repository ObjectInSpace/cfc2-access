"""Build the CVS1 branch-B (Versus, Training) select replay fixture.

Input: tools/fixtures/cvs1-select-versus-20261015/ (trace.bin.gz +
session.jsonl.gz), recorded on ROM 1 cvs1u. The user played Versus with both
sides human (P+0x48 = 1): P1 total ratio, groove, two characters with a ratio
menu after the first; then P2 total ratio, groove, one character and its
ratio. Then Training (P+0x48 = 4): P1 groove and one character, then P2 groove
and one character; Training skips the total-ratio and ratio menus.

Every expected value below was read by hand off the trace timeline (actor
state +4, live marker +5, P+0x76/0x77 groove, P+0x78.. highlight, P+0x98..
ratio), not produced by the decoder. Kinds: 0 character (value = native ID),
1 groove, 4 total ratio, 5 ratio (value = cursor, ratio - 1). The total-ratio
cursor and the points left are image globals outside the captured block
(0x1422D3E70); the trace cannot hold them, so a total-ratio expectation's
value is TOTAL_FROM_TEST: the test injects the globals and checks those.

Record format: the CV1M record of make_cvs1_menu_fixture.py, magic CV1V.
Output: tools/fixtures/cvs1-select-versus-20261015.bin

Usage: python tools/make_cvs1_versus_fixture.py [--check]
"""
import argparse
import bisect
from pathlib import Path
import struct
import sys

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'fixtures' / 'cvs1-select-versus-20261015'
OUT = HERE / 'fixtures' / 'cvs1-select-versus-20261015.bin'
P, ACTOR, STEP, HEAD, OWNER = 0x3E58, 0x3FB8, 0x700, 0x38, 0x688
SPEED, TEAM, ORDER = 0x58E, 0x618, 0x31AD0
T0 = 106398.0090658        # the trace's first record
NONE = (0xFF, 0)
TOTAL_FROM_TEST = 0xFE
TOTAL = (4, TOTAL_FROM_TEST)

# (seconds after T0, side 0 expectation, side 1 expectation)
MOMENTS = [
    # Versus, mode 1
    (10.00, NONE, NONE),               # state 2, before the menus
    (27.00, NONE, NONE),               # state 4, entry countdown: +5 still 2
    (30.00, TOTAL, TOTAL),             # both sides on total ratio
    (34.50, NONE, TOTAL),              # P1 state 5 countdown
    (36.00, (1, 0), TOTAL),            # P1 groove Capcom
    (36.80, (1, 1), TOTAL),            # after down: SNK
    (39.00, (1, 1), TOTAL),
    (42.00, (1, 0), TOTAL),
    (42.60, NONE, TOTAL),              # P1 grid countdown
    (44.00, (0, 0x00), TOTAL),         # grid: Ryu
    (44.40, (0, 0x0C), TOTAL),
    (45.20, (0, 0x10), TOTAL),
    (47.00, (0, 0x1E), TOTAL),
    (52.50, NONE, TOTAL),              # P1 ratio countdown
    (53.00, (5, 1), TOTAL),            # ratio menu opens on the default, 1
    (54.20, (5, 0), TOTAL),
    (55.50, (5, 1), TOTAL),
    (57.65, (5, 0), TOTAL),
    (58.70, NONE, TOTAL),              # second slot, grid countdown (+5 = 7)
    (61.00, (0, 0x03), TOTAL),         # second slot: P+0x79
    (62.50, (0, 0x1D), TOTAL),
    (64.30, (0, 0x1A), TOTAL),
    (66.00, NONE, TOTAL),              # P1 done (state 8)
    (75.00, NONE, (1, 0)),             # P2 groove
    (76.20, NONE, (1, 1)),
    (77.00, NONE, (1, 0)),
    (81.00, NONE, (0, 0x10)),          # P2 grid, P+0x7C
    (81.80, NONE, (0, 0x1C)),
    (83.00, NONE, (0, 0x01)),
    (85.00, NONE, (0, 0x05)),
    (91.00, NONE, NONE),               # P2 ratio countdown
    (91.50, NONE, (5, 0)),
    (92.20, NONE, (5, 2)),
    (92.50, NONE, (5, 0)),
    (92.70, NONE, (5, 1)),
    (93.00, NONE, (5, 2)),
    (95.00, NONE, NONE),               # both done
    (100.00, NONE, NONE),              # in the match
    # Training, mode 4
    (115.00, NONE, NONE),
    (117.92, NONE, NONE),              # state 4, completed by the handler itself
    (118.20, NONE, NONE),              # groove countdown
    (119.00, (1, 0), NONE),
    (120.40, (1, 1), NONE),
    (121.00, (1, 0), NONE),
    (123.50, NONE, NONE),
    (124.00, (0, 0x00), NONE),
    (124.60, (0, 0x0B), NONE),
    (125.10, (0, 0x10), NONE),
    (126.00, (0, 0x00), NONE),
    (128.30, NONE, NONE),              # P1 done, no ratio menu in Training
    (129.50, NONE, NONE),              # P2 groove countdown
    (130.00, NONE, (1, 0)),
    (130.50, NONE, (1, 1)),
    (132.00, NONE, (1, 1)),
    (133.00, NONE, (0, 0x10)),
    (133.40, NONE, (0, 0x1C)),
    (133.70, NONE, (0, 0x1A)),
    (135.00, NONE, (0, 0x1C)),
    (136.00, NONE, NONE),
    (138.00, NONE, NONE),              # in the match
]


def build():
    times, states, state = [], [], None
    for t, root, key, payload in read_trace(SOURCE / 'trace.bin.gz'):
        if key:
            state = bytearray(payload)
        else:
            for offset, value in payload:
                state[offset] = value
        times.append(t)
        states.append(bytes(state))
    out = bytearray(b'CV1V') + struct.pack('<I', len(MOMENTS))
    for rel, e0, e1 in MOMENTS:
        t = T0 + rel
        s = states[max(0, bisect.bisect_right(times, t) - 1)]
        a0, a1 = ACTOR, ACTOR + STEP
        out += struct.pack('<dBBBB', rel, e0[0], e0[1], e1[0], e1[1])
        out += s[P:P + 0x118] + s[a0:a0 + HEAD] + s[a1:a1 + HEAD]
        out += bytes((s[a0 + OWNER], s[a1 + OWNER], s[a0 + SPEED], s[a1 + SPEED]))
        out += s[a0 + TEAM:a0 + TEAM + 4] + s[a1 + TEAM:a1 + TEAM + 4]
        out += s[ORDER:ORDER + 32]
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = build()
    if args.check:
        if not OUT.exists() or OUT.read_bytes() != data:
            sys.exit(f'Stale: {OUT}')
        print('CVS1 versus/training select fixture is current.')
        return
    OUT.write_bytes(data)
    print(f'Wrote {OUT} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
