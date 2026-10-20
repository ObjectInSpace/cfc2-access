"""Build the Project Justice story-dialogue replay fixture from live captures.

Sources (build/captures, not retained in the repository):
- mjus-1: ROM 13 pjus, English pictures (R+0x206C = 1). Story mode, school 5
  (Seijyun), scene 38: Akira and Yurika ask Zaki for help, messages 0..4.
- mjus-3: Story mode, Taiyo, scene 67: Batsu and Kyosuke meet Kurow and the
  controlled Daigo, messages 0..7.

Output tools/fixtures/mjus-story-20261019.bin: 'MJST', count (u32), then one
record per change of the reader's inputs: t (float, seconds into its
capture), capture number (u8: 1 or 3), R+0x206C, R+0x2259, R+0x2238 (school),
bytes +0, +1, +5, +7, +8 of each of the 16 task records at R+0x93F30 (80),
and R+0x2600..R+0x26B8 (both 0x5C message records). Captures in order
mjus-1 (school 5 intro), mjus-3 (school 0 intro).

Usage: python tools/make_mjus_story_fixture.py [--check]
"""
import argparse
from pathlib import Path
import struct
import sys

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
CAPS = HERE.parent / 'build' / 'captures'
OUT = HERE / 'fixtures' / 'mjus-story-20261019.bin'
REC, REC_LEN = 0x2600, 2 * 0x5C
TASKS, TASK_BYTES = 0x93F30, (0, 1, 5, 7, 8)


def replay(cap):
    state, t0 = None, None
    for t, r, key, payload in read_trace(cap / 'trace.bin.gz'):
        if t0 is None:
            t0 = t
        if key:
            state = bytearray(payload)
        else:
            for off, v in payload:
                state[off] = v
        yield t - t0, state


def build():
    recs, last = [], None
    for num in (1, 3):
        for t, s in replay(CAPS / ('mjus-%d' % num)):
            tasks = bytes(s[TASKS + i * 0x18 + k] for i in range(16) for k in TASK_BYTES)
            body = bytes([s[0x206C], s[0x2259], s[0x2238]]) + tasks + bytes(s[REC:REC + REC_LEN])
            if body != last:
                recs.append(struct.pack('<fB', t, num) + body)
                last = body
    return b'MJST' + struct.pack('<I', len(recs)) + b''.join(recs)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    data = build()
    if a.check:
        if OUT.read_bytes() != data:
            sys.exit('fixture differs from the captures')
        print('fixture matches the captures')
    else:
        OUT.write_bytes(data)
        print('wrote %s (%d bytes, %d records)' % (OUT, len(data), struct.unpack_from('<I', data, 4)[0]))
