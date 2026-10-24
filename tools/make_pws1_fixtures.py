"""Build the Power Stone replay fixtures from the retained live capture.

Input, recorded 2026-10-22 on ROM 9 pws1u (docs/port-tasks/pws-selection.md):
- build/pws1-live-20261022-c/trace.bin.gz: Arcade, side 0 human against the
  CPU: character select with two shuffles, a first fight left early, then
  select again, a full round lost by KO with a pause, round two's start, and a
  third return to select. The capture was interrupted, so the gzip stream has
  no proper end; samples are read up to the last complete one.
- tools/fixtures/pws1-observations-20261022/camera-brackets.json: the shared
  renderer's context sampled beside the snapshots (tools/pws_camera_pair.py).
The raw trace (about 10 MB) is kept out of git; the outputs below are what
the tests replay.

Outputs:
- tools/fixtures/pws1-match-frames-20261022.bin: every trace sample where the
  frame decoder's inputs change, as the byte ranges in RANGES, with its time.
- tools/fixtures/pws1-select-live-20261022.bin: every trace sample where the
  selection decoder's inputs change, with its time and root.
- tools/fixtures/pws1-snapshot-frames-20261022.bin: the same ranges from the
  thirteen retained snapshots, in order, so the pans the offline pairing
  tool computed for them (camera-brackets.json) can be checked.
- tools/fixtures/pws1-pickup-frames-20261022.bin: every trace sample taken
  while a round was fought, with its time and root, as the ranges in PICKUPS
  (the frame ranges plus the human mask and carried pointers), then the
  object pool sparsely: the records in use, with header, position and item code.
- tools/fixtures/pws1-stage-live-20261024.bin: every sample of the Versus
  stage-select capture (build/pws1-stage-20261024/trace.bin.gz, 2026-10-24,
  one move from cursor 6 to 7, then chosen) where the stage inputs change.
- tools/fixtures/pws1-render-20261022.bin: the renderer's projection
  (ctx+0x1F816C), 16 floats, from the snapshots taken while fighting. They
  were identical in every one; the build refuses otherwise.

Usage: python tools/make_pws1_fixtures.py [--check]
"""
import argparse
import gzip
import json
import struct
import sys
import zlib
from pathlib import Path

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
TRACE = HERE.parent / 'build' / 'pws1-live-20261022-c' / 'trace.bin.gz'
BRACKETS = HERE / 'fixtures' / 'pws1-observations-20261022' / 'camera-brackets.json'
FRAMES_OUT = HERE / 'fixtures' / 'pws1-match-frames-20261022.bin'
SELECT_OUT = HERE / 'fixtures' / 'pws1-select-live-20261022.bin'
RENDER_OUT = HERE / 'fixtures' / 'pws1-render-20261022.bin'
SNAP_OUT = HERE / 'fixtures' / 'pws1-snapshot-frames-20261022.bin'
SNAPS = HERE / 'fixtures' / 'pws1-observations-20261022'
A0, A1 = 0x18428, 0x1FCB8
CTX_BASE, PROJ = 0x1F8100, 0x1F816C
# What pws1_frame_decode reads; tools/test_pws1_frame.c lists the same.
RANGES = [(A0 + 3, 1), (A0 + 0x30, 0xC), (A0 + 0x7790, 2), (A0 + 0x77EC, 1),
          (A1 + 3, 1), (A1 + 0x30, 0xC), (A1 + 0x7790, 2), (A1 + 0x77EC, 1),
          (0x27974, 0x40), (0x27E58, 1), (0x27E5D, 1), (0x27E7D, 1), (0x27E92, 1),
          (0x27F94, 4), (0x27FAC, 2), (0x27FCD, 1), (0x280C4, 1)]
# What pws_select_from_state reads for PWS1, plus the fighters' committed IDs
# (actor+4, written from the cursor at 0x1409A0F8C).
SELECT = [(0x27E58, 8), (0x27F8B, 1), (0x27F8E, 1), (0x27FF1, 1),
          (0x27DC2, 1), (0x27DCC, 1), (0x27DC2 + 0x78, 1), (0x27DCC + 0x78, 1),
          (A0 + 3, 2), (A1 + 3, 2), (A0 + 0x7874, 4), (A1 + 0x7874, 4)]
# What pws1_pickups_decode reads beyond RANGES: the human mask, the carried
# pointers, and per pool record its header, position and item code.
POOL_FIELDS = ((0, 16), (0x30, 12), (0x3C8, 1))
PICKUPS = RANGES + [(0x27FF2, 1), (A0 + 0x7780, 8), (A1 + 0x7780, 8)]
PICKUPS_OUT = HERE / 'fixtures' / 'pws1-pickup-frames-20261022.bin'


def pool(s):
    """Sparse: <B count>, then per record in use <B index><fields>. A record
    not in use (+0 = 0) is all zero in the test's reconstruction, as the
    decoder skips it on that byte alone."""
    used = [i for i in range(100) if s[i * 0x3E0]]
    return bytes([len(used)]) + b''.join(
        bytes([i]) + b''.join(bytes(s[i * 0x3E0 + o:i * 0x3E0 + o + n]) for o, n in POOL_FIELDS)
        for i in used)


STAGE_TRACE = HERE.parent / 'build' / 'pws1-stage-20261024' / 'trace.bin.gz'
STAGE_OUT = HERE / 'fixtures' / 'pws1-stage-live-20261024.bin'
# What the stage select reads: phase, sub-state, step, the two pause gates, the
# cursor word, and the game's own stage ID (R+0x27FD1) as the check.
STAGE = [(0x27E5D, 3), (0x27F8B, 1), (0x27F8E, 1), (0x27FC2, 2), (0x27FD1, 1)]


def stage():
    out, last = [], None
    for t, root, s in samples(STAGE_TRACE):
        b = pick(s, STAGE)
        if (root, b) != last:
            out.append(struct.pack('<dQ', t, root) + b)
            last = (root, b)
    return b'PWSG' + struct.pack('<I', len(out)) + b''.join(out)


def fought(s):
    """pws1_fighting: running, match phase, sub-state 3 step 0, single view."""
    return (s[0x27E58] == 1 and s[0x27E5D] == 4 and s[0x27FAC] == 3 and
            s[0x27FAD] == 0 and s[0x280C4] == 0)


def pickups():
    out, last = [], None
    for t, root, s in samples(TRACE):
        if not fought(s):
            continue
        b = pick(s, PICKUPS) + pool(s)
        if (root, b) != last:      # the paused samples repeat exactly
            out.append(struct.pack('<dQ', t, root) + b)
            last = (root, b)
    return b'PWSP' + struct.pack('<I', len(out)) + b''.join(out)


def samples(path):
    state = None
    try:
        for t, root, key, payload in read_trace(path):
            if key:
                state = bytearray(payload)
            else:
                for offset, value in payload:
                    state[offset] = value
            yield t, root, state
    except (EOFError, zlib.error, struct.error):
        return      # the interrupted tail


def pick(s, ranges):
    return b''.join(bytes(s[o:o + n]) for o, n in ranges)


def changes(ranges, with_root):
    out, last = [], None
    for t, root, s in samples(TRACE):
        b = pick(s, ranges)
        key = (root, b) if with_root else b
        if key != last:
            out.append(struct.pack('<d', t) + (struct.pack('<Q', root) if with_root else b'') + b)
            last = key
    return out


def render():
    shots = json.loads(BRACKETS.read_text())['snapshots']
    seen = set()
    # Snapshots 7..12 were taken while fighting (sub-state 3, decoded from the
    # snapshots themselves, never from their capture-plan labels).
    for s in shots[6:12]:
        for side in ('before', 'after'):
            raw = bytes.fromhex(s[side]['raw_hex'])
            seen.add(raw[PROJ - CTX_BASE:PROJ - CTX_BASE + 64])
    assert len(seen) == 1, f'projection changed while fighting ({len(seen)} distinct)'
    return seen.pop()


def snapshots():
    out = []
    for n in range(1, 14):
        s = gzip.decompress((SNAPS / f'state-{n:02d}.bin.gz').read_bytes())
        out.append(pick(s, RANGES))
    return b'PWSN' + struct.pack('<I', len(out)) + b''.join(out)


def build():
    frames = changes(RANGES, False)
    sel = changes(SELECT, True)
    return {FRAMES_OUT: b'PWSF' + struct.pack('<I', len(frames)) + b''.join(frames),
            SELECT_OUT: b'PWSS' + struct.pack('<I', len(sel)) + b''.join(sel),
            RENDER_OUT: render(),
            SNAP_OUT: snapshots(),
            PICKUPS_OUT: pickups(),
            STAGE_OUT: stage()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for source in (TRACE, BRACKETS, STAGE_TRACE):
        if not source.exists():
            sys.exit(f'Missing raw capture: {source}')
    outputs = build()
    if args.check:
        for path, data in outputs.items():
            if not path.exists() or path.read_bytes() != data:
                sys.exit(f'Stale: {path}')
        print('PWS1 fixtures are current.')
        return
    for path, data in outputs.items():
        path.write_bytes(data)
        print(f'Wrote {path} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
