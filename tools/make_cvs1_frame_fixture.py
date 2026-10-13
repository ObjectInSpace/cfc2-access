"""Build the CVS1 frame-decode replay fixture from the labelled match capture.

Input: tools/fixtures/cvs1-match-20261013/state-NN.bin.gz (ten whole primary
blocks, labels in that folder's README). Output keeps only what the decoder
reads: P (R+0x3E58, 0x112 bytes) and both actors (R+0x3FB8, 0xE00 bytes),
for each snapshot in label order, then three Arcade moments reconstructed
from tools/fixtures/cvs1-arcade-20261013/trace.bin.gz: the last live sample
with P1's gauge at 64 before the super, the first at 16 after it, and the
round-intro sample where the gauge reads 0.
Output: tools/fixtures/cvs1-match-frames-20261013.bin

Usage: python tools/make_cvs1_frame_fixture.py [--check]
"""
import argparse
import gzip
from pathlib import Path
import struct
import sys

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'fixtures' / 'cvs1-match-20261013'
OUT = HERE / 'fixtures' / 'cvs1-match-frames-20261013.bin'
P, P_SIZE, ACTORS, ACTORS_SIZE = 0x3E58, 0x112, 0x3FB8, 0xE00
ARCADE = HERE / 'fixtures' / 'cvs1-arcade-20261013' / 'trace.bin.gz'
METER = ACTORS + 0x2D1


def arcade_moments():
    state, before, after, blip = None, None, None, None
    for t, root, key, payload in read_trace(ARCADE):
        if key:
            state = bytearray(payload)
        else:
            previous = state[METER]
            for offset, value in payload:
                state[offset] = value
            sub = state[P + 4]
            if sub == 5 and state[METER] == 64 and after is None:
                before = bytes(state)
            if sub == 5 and previous == 64 and state[METER] == 16 and after is None:
                after = bytes(state)
            if sub == 4 and state[METER] == 0 and blip is None:
                blip = bytes(state)
    assert before and after and blip
    return [before, after, blip]


def build():
    out = bytearray(b'CV1F') + struct.pack('<I', 13)
    states = [gzip.open(SOURCE / f'state-{i:02d}.bin.gz').read() for i in range(1, 11)]
    for s in states + arcade_moments():
        assert len(s) == 0x32AB0
        out += s[P:P + P_SIZE] + s[ACTORS:ACTORS + ACTORS_SIZE]
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = build()
    if args.check:
        if not OUT.exists() or OUT.read_bytes() != data:
            sys.exit(f'Stale: {OUT}')
        print('CVS1 frame fixture is current.')
        return
    OUT.write_bytes(data)
    print(f'Wrote {OUT} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
