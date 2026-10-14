"""Build the Alpha 3 frame-decode replay fixture from the labelled match capture.

Input: tools/fixtures/z3u-match-20261014/state-NN.bin.gz (thirteen whole
primary blocks, labels in that folder's README) and its trace.bin.gz, plus the
select capture tools/fixtures/z3u-capture-20261014/trace.bin.gz. Output keeps
only what the decoder reads: the low root R+0x00..R+0xFF (mode words, round
stage, clock), both player objects (R+0x1B0, 2 * 0x550) and the play layer's
scroll object (R+0xC548..R+0xC55F, camera at +0x12), for each snapshot in
label order, then three moments from the traces:
- the first intro sample (stage 2) of the new match where the gauges are
  mid-fill (0 < gauge < 144);
- the first sample where P1's health holds the KO sentinel -1;
- a character-select sample (mode words 1, 1, 2).
Output: tools/fixtures/z3u-match-frames-20261014.bin

Usage: python tools/make_z3u_frame_fixture.py [--check]
"""
import argparse
import gzip
from pathlib import Path
import struct
import sys

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'fixtures' / 'z3u-match-20261014'
SELECT = HERE / 'fixtures' / 'z3u-capture-20261014' / 'trace.bin.gz'
OUT = HERE / 'fixtures' / 'z3u-match-frames-20261014.bin'
REGIONS = ((0x0, 0x100), (0x1B0, 0xAA0), (0xC548, 0x18))
SIZE = 0x12F98
OBJ, GAUGE, HP = 0x1B0, 0x1EE, 0x88


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
    fill = ko = None
    for _t, s in states(SOURCE / 'trace.bin.gz'):
        mode = (i16(s, 0x28), i16(s, 0x2A), i16(s, 0x2C))
        if (fill is None and mode == (1, 2, 3) and i16(s, 0x2E) == 2 and
                0 < i16(s, OBJ + GAUGE) < 144):
            fill = bytes(s)
        if ko is None and i16(s, OBJ + HP) == -1:
            ko = bytes(s)
    select = next(bytes(s) for _t, s in states(SELECT)
                  if (i16(s, 0x28), i16(s, 0x2A), i16(s, 0x2C)) == (1, 1, 2))
    assert fill and ko and select
    return [fill, ko, select]


def build():
    snaps = [gzip.open(SOURCE / f'state-{i:02d}.bin.gz').read() for i in range(1, 14)]
    records = snaps + trace_moments()
    out = bytearray(b'Z3UF') + struct.pack('<I', len(records))
    for s in records:
        assert len(s) == SIZE
        for off, n in REGIONS:
            out += s[off:off + n]
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = build()
    if args.check:
        if not OUT.exists() or OUT.read_bytes() != data:
            sys.exit(f'Stale: {OUT}')
        print('Alpha 3 frame fixture is current.')
        return
    OUT.write_bytes(data)
    print(f'Wrote {OUT} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
