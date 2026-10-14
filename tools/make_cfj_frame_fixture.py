"""Build the CFJ frame-decode replay fixture from the labelled match capture.

Input: tools/fixtures/cfj-match-20261014/state-NN.bin.gz (twelve whole primary
blocks, labels in that folder's README) and its trace.bin.gz, plus the select
capture tools/fixtures/cfj-capture-20261014/trace.bin.gz. Output keeps only
what the decoder reads: the phase chain to the clock (R+0xC8..R+0x1D7) and
both fighter records (R+0x1E678, 2 * 0x660), for each snapshot in label
order, then three moments from the traces:
- the first round-setup sample of round 2 where P1's gauge blips to 0;
- the first sample where P1's health holds the KO sentinel -1;
- a character-select sample (phase 02 01 01 01).
Output: tools/fixtures/cfj-match-frames-20261014.bin

Usage: python tools/make_cfj_frame_fixture.py [--check]
"""
import argparse
import gzip
from pathlib import Path
import struct
import sys

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'fixtures' / 'cfj-match-20261014'
SELECT = HERE / 'fixtures' / 'cfj-capture-20261014' / 'trace.bin.gz'
OUT = HERE / 'fixtures' / 'cfj-match-frames-20261014.bin'
LOW, LOW_SIZE, FIGHTERS, FIGHTERS_SIZE = 0xC8, 0x110, 0x1E678, 0xCC0
SIZE = 0x26E80
STAGE, METER, HP = 0xCC, FIGHTERS + 0x294, FIGHTERS + 0x16C


def states(path):
    state = None
    for t, _root, key, payload in read_trace(path):
        if key:
            state = bytearray(payload)
        else:
            for offset, value in payload:
                state[offset] = value
        yield t, state


def i16(s, off):
    return struct.unpack_from('<h', s, off)[0]


def trace_moments():
    blip = ko = None
    seen_full = False
    for _t, s in states(SOURCE / 'trace.bin.gz'):
        if i16(s, METER) >= 100:
            seen_full = True
        if blip is None and seen_full and s[STAGE] < 4 and i16(s, METER) == 0:
            blip = bytes(s)
        if ko is None and i16(s, HP) == -1:
            ko = bytes(s)
    select = next(bytes(s) for _t, s in states(SELECT) if s[LOW:LOW + 4] == b'\x02\x01\x01\x01')
    assert blip and ko and select
    return [blip, ko, select]


def build():
    snaps = [gzip.open(SOURCE / f'state-{i:02d}.bin.gz').read() for i in range(1, 13)]
    records = snaps + trace_moments()
    out = bytearray(b'CFJF') + struct.pack('<I', len(records))
    for s in records:
        assert len(s) == SIZE
        out += s[LOW:LOW + LOW_SIZE] + s[FIGHTERS:FIGHTERS + FIGHTERS_SIZE]
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = build()
    if args.check:
        if not OUT.exists() or OUT.read_bytes() != data:
            sys.exit(f'Stale: {OUT}')
        print('CFJ frame fixture is current.')
        return
    OUT.write_bytes(data)
    print(f'Wrote {OUT} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
