"""Build the CVS1 select-menu replay fixture from the 2026-10-14 capture.

Input: tools/fixtures/cvs1-select-menus-20261014/ (trace.bin.gz +
session.jsonl.gz), recorded on ROM 1 cvs1u, Arcade, side 0, Speed Select set
to Free Select. The user walked the groove screen, the character grid, the
speed menu and the order screen. The capture's own arrow log missed the menu
presses; the game's input record (actor+0x5BE) has them, and every expected
value below was read off the trace by hand against those presses
(docs/port-tasks/cvs1-select-menus.md), not produced by the decoder.

Each record: the time, the expected (kind, value) per side, then the decoder's
inputs: P (0x118), both actor headers (0x38), owner, speed (actor+0x58E) and
team (actor+0x618, 4) bytes, and the order lists R+0x31AD0 (32 bytes).
Kind 0xFF = inactive. Output: tools/fixtures/cvs1-select-menus-20261014.bin

Usage: python tools/make_cvs1_menu_fixture.py [--check]
"""
import argparse
import bisect
from pathlib import Path
import struct
import sys

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'fixtures' / 'cvs1-select-menus-20261014'
OUT = HERE / 'fixtures' / 'cvs1-select-menus-20261014.bin'
P, ACTOR, STEP, HEAD, OWNER = 0x3E58, 0x3FB8, 0x700, 0x38, 0x688
SPEED, TEAM, ORDER = 0x58E, 0x618, 0x31AD0
NONE = (0xFF, 0)

# (time, side 0 expectation, side 1 expectation); kinds 1 groove, 2 speed,
# 3 order (value = native ID: 0 Ryu, 1 Ken).
MOMENTS = [
    (25346.50, NONE, NONE),        # groove screen intro, before step 4
    (25348.00, (1, 0), NONE),      # groove step 5: Capcom, P+0x113 bit 0 set
    (25351.30, (1, 0), NONE),      # after the right press: groove unchanged
    (25352.50, NONE, NONE),        # confirmed: P+0x113 cleared
    (25367.00, (2, 1), NONE),      # speed menu, default Turbo
    (25369.66, (2, 0), NONE),      # after down: Normal
    (25370.50, (2, 1), NONE),      # after up: Turbo
    (25373.60, NONE, NONE),        # order screen loading, step 02 01 02 01 02 00
    (25375.00, (3, 0), NONE),      # order cursor 0: Ryu
    (25376.10, (3, 1), NONE),      # after a press: Ken
    (25376.60, (3, 0), NONE),      # and back: Ryu
    (25381.30, (3, 1), NONE),      # Ken, just before confirm
    (25382.00, NONE, NONE),        # Ken placed; one left, nothing to choose
    (25386.00, NONE, NONE),        # versus screen
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
    out = bytearray(b'CV1M') + struct.pack('<I', len(MOMENTS))
    for t, e0, e1 in MOMENTS:
        s = states[max(0, bisect.bisect_right(times, t) - 1)]
        a0, a1 = ACTOR, ACTOR + STEP
        out += struct.pack('<dBBBB', t, e0[0], e0[1], e1[0], e1[1])
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
        print('CVS1 select-menu fixture is current.')
        return
    OUT.write_bytes(data)
    print(f'Wrote {OUT} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
