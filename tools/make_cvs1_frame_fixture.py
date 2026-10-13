"""Build the CVS1 frame-decode replay fixture from the labelled match capture.

Input: tools/fixtures/cvs1-match-20261013/state-NN.bin.gz (ten whole primary
blocks, labels in that folder's README). Output keeps only what the decoder
reads: P (R+0x3E58, 0x112 bytes) and both actors (R+0x3FB8, 0xE00 bytes),
for each snapshot in label order: tools/fixtures/cvs1-match-frames-20261013.bin

Usage: python tools/make_cvs1_frame_fixture.py [--check]
"""
import argparse
import gzip
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'fixtures' / 'cvs1-match-20261013'
OUT = HERE / 'fixtures' / 'cvs1-match-frames-20261013.bin'
P, P_SIZE, ACTORS, ACTORS_SIZE = 0x3E58, 0x112, 0x3FB8, 0xE00


def build():
    out = bytearray(b'CV1F') + struct.pack('<I', 10)
    for i in range(1, 11):
        s = gzip.open(SOURCE / f'state-{i:02d}.bin.gz').read()
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
