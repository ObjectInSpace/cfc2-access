"""Build the Power Stone 2 replay fixture from the retained live capture.

Input, recorded 2026-10-27 on ROM 11 pws2u (docs/port-tasks/pws2-match-fields.md):
build/pws2-live-20261027/trace.bin.gz -- Arcade, fighter 0 played by the
user against three CPU fighters (IDs 5, 3, 7): character select (Falcon, one
cell out and back three times, confirmed), the screens before the fight, one
fight of about three minutes with stones collected, a transformation (by a
CPU), a 3.5 s pause and the user's KO with 19 s left on the clock, then the
continue screen. The capture ended when the game was closed; samples are read
up to the last complete one. The raw trace (local) is kept out of git.

Output: tools/fixtures/pws2-frames-20261027.bin -- every trace sample where
an input of the selection or frame decoders changes: its time and root, the
byte ranges in RANGES, then the list-9 pool records (the arena's items,
props and stones), sparsely, as their index and the fields in POOL_FIELDS.
tools/test_pws2_frame.c and tools/test_pws2_select.c list the same.

Second output: tools/fixtures/pws2-stage-20261027.bin -- the Versus capture
build/pws2-versus-stage-20261027/trace.bin.gz (2026-10-27, Stage Selection:
Manual): the stage select, where the user's fighter landed, walked forward,
back, forward, left and right, and walked into stage spot 2 with 76 frames
of the 20 s countdown left. Steps 3 and 4 only, same layout with
STAGE_RANGES and the list-5 pool records (the five stage spots among them)
with STAGE_FIELDS, only those whose index byte +2 is 0..4: the spots and
same-numbered records of other kinds, which the decoder must not take.

Usage: python tools/make_pws2_fixtures.py [--check]
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
TRACE = HERE.parent / 'build' / 'pws2-live-20261027' / 'trace.bin.gz'
OUT = HERE / 'fixtures' / 'pws2-frames-20261027.bin'
STAGE_TRACE = HERE.parent / 'build' / 'pws2-versus-stage-20261027' / 'trace.bin.gz'
STAGE_OUT = HERE / 'fixtures' / 'pws2-stage-20261027.bin'
SIZE = 0x8CD40


def actor(p):
    return 0x40818 + p * 0x42B8


# Slot and highlighted ID, position, health, included, CPU flag, stones,
# carried pointer, shuffle -- per fighter; then the root fields.
RANGES = [r for p in range(4) for r in (
    (actor(p) + 1, 2), (actor(p) + 0x3C, 12), (actor(p) + 0x1DC, 4), (actor(p) + 0x401C, 1),
    (actor(p) + 0x4028, 1), (actor(p) + 0x4055, 1), (actor(p) + 0x4080, 8), (actor(p) + 0x4258, 4))]
RANGES += [(0x529D2 + p * 0x98, 1) for p in range(4)]
RANGES += [(0x52BD8, 8), (0x52D2B, 1), (0x52D2E, 1), (0x52D34, 4), (0x52E88, 1),
           (0x52EE0, 2), (0x52EE5, 1), (0x52FDC, 1), (0x54B49, 1), (0x51D90, 0x40)]
POOL, STRIDE, COUNT = 0x4B10, 0x4C8, 200
POOL_FIELDS = ((0, 1), (0x3C, 12), (0xB8, 8), (0x4B0, 2))
# The stage select also needs the countdown, the choice and the fight's
# stage, and per list-5 record its index byte and callback.
STAGE_RANGES = RANGES + [(0x52BE8, 2), (0x52BEA, 1), (0x53010, 1)]
STAGE_FIELDS = ((0, 1), (2, 1), (8, 8), (0x3C, 12), (0xB8, 8), (0x4B0, 2))


def samples(trace=TRACE):
    state = None
    try:
        for t, root, key, payload in read_trace(trace):
            if key:
                state = bytearray(payload)
            else:
                for offset, value in payload:
                    state[offset] = value
            if len(state) == SIZE:
                yield t, root, state
    except (EOFError, zlib.error, struct.error):
        return      # the interrupted tail


def record(s, ranges=RANGES, fields=POOL_FIELDS, lists=(9,), small_index=False):
    head = b''.join(bytes(s[o:o + n]) for o, n in ranges)
    pool = bytearray()
    used = 0
    for i in range(COUNT):
        o = POOL + i * STRIDE
        if s[o] not in lists or (small_index and s[o + 2] >= 5):
            continue
        used += 1
        pool.append(i)
        for f, n in fields:
            pool += s[o + f:o + f + n]
    return head + bytes([used]) + bytes(pool)


def build(trace=TRACE, magic=b'PW2F', steps=None, **layout):
    out, last, t0 = [], None, None
    for t, root, s in samples(trace):
        if t0 is None:
            t0 = t
        if steps and s[0x52BDD] not in steps:
            continue
        b = record(s, **layout)
        if (root, b) != last:
            out.append(struct.pack('<dQ', t - t0, root) + b)
            last = (root, b)
    return magic + struct.pack('<I', len(out)) + b''.join(out)


def outputs():
    yield OUT, build()
    yield STAGE_OUT, build(STAGE_TRACE, b'PW2S', steps=(3, 4), ranges=STAGE_RANGES, fields=STAGE_FIELDS, lists=(5,),
                           small_index=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help='compare with the committed fixture')
    args = ap.parse_args()
    bad = 0
    for out, data in outputs():
        if args.check:
            ok = out.exists() and out.read_bytes() == data
            print(f'{out.name}: {"ok" if ok else "DIFFERS"}')
            bad += not ok
            continue
        out.write_bytes(data)
        print(f'{out.name}: {struct.unpack_from("<I", data, 4)[0]} records, {len(data)} bytes')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
