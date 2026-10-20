"""Build the Project Justice replay fixtures from the 2026-10-19 live captures.

Sources (build/captures, not retained in the repository: 32 MB and 165 MB):
- mjus-3: ROM 13 pjus, English shell. Session 1: Free mode, then three free
  picks (Wild Daigo, Edge, Batsu, committed at R+0x255C..0x255E). Session 2
  (republished root): Story mode, the school list, Taiyo.
- mjus-4: Story mode, Taiyo, the chapter teammate choice (Batsu, then
  Hinata; Kyosuke filled in), two fights (P1 KO'd in each), results and the
  continue decision.
- mjus-1: Story mode, school 5, whose teammate choice offered Akira, Zaki and
  Yurika (Seijyun); the capture ends before a confirm.
- mjus-6 (2026-10-20): Versus select, then Training select, both sides
  picking three; side record +0x2A is 0xFF there (see build_versus).

Outputs (tools/fixtures):
- mjus-select-live-20261019.bin: 'MJSL', count, then one record per change
  of the decoder's inputs: bytes +0, +1, +5, +7, +8 of each of the 16 task
  records at R+0x93F30 (80 bytes),
  R+0x22F0..0x22F2, R+0x2029, and both side records' first 0x30 bytes
  (R+0x2500, R+0x2580), then R+0x21C0, R+0x225E..0x2269 and
  R+0x230B..0x2316 (the story teammate choice). Captures in order mjus-3,
  mjus-4 to 186 s, mjus-1. Trailer, the game's own commits, independent of
  the decoder: R+0x255C..0x255E at the end of mjus-3's first session, then
  the story team R+0x21CC..0x21CE in mjus-3 at 140 s and mjus-4 at 170 s.
- mjus-match-frames-20261019.bin: 'MJMF', count, then records of t (float,
  seconds into mjus-4), R+0x21C0, and per fighter (R+0x3B48 + side*0x608)
  +0xEC..+0xF8 (12 bytes), +0x2DE (2), +0x2FF (1), +0x3CC..+0x3CD (2). One
  record whenever any of those change, from 185 s to 382 s.
- mjus-camera-20261019.bin: 'MJCM', the projection (64 bytes, constant in
  the run), count, then records of t (float, seconds into mjus-5), R+0x21C0,
  per fighter +0xD0..+0xDC world position (12), +0xEC..+0xF8 (12, height at
  +0xF0) and +0x2FF facing (1), then
  the view matrix (64). From mjus-5 (five fights) paired by time with
  tools/context_sampler.py's samples of ctx+0x1F8000 (mjus-5-ctx), within
  30 ms; every third fighting sample.

Usage: python tools/make_mjus_fixtures.py [--check]
"""
import argparse
from pathlib import Path
import struct
import sys

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
CAPS = HERE.parent / 'build' / 'captures'
SELECT_OUT = HERE / 'fixtures' / 'mjus-select-live-20261019.bin'
MATCH_OUT = HERE / 'fixtures' / 'mjus-match-frames-20261019.bin'
CAMERA_OUT = HERE / 'fixtures' / 'mjus-camera-20261019.bin'
VERSUS_OUT = HERE / 'fixtures' / 'mjus-versus-20261020.bin'
TASKS, MENU, HUMANS, SIDE, SIDE_STRIDE = 0x93F30, 0x22F0, 0x2029, 0x2500, 0x80
F, FSTEP = 0x3B48, 0x608
TASK_BYTES = (0, 1, 5, 7, 8)   # state word, routine, screen state, step


def replay(cap):
    state, root, t0 = None, None, None
    for t, r, key, payload in read_trace(cap / 'trace.bin.gz'):
        if t0 is None:
            t0 = t
        if key:
            state, root = bytearray(payload), r
        else:
            for off, v in payload:
                state[off] = v
        yield t - t0, root, state


def select_inputs(s):
    tasks = b''.join(bytes(s[TASKS + i * 0x18 + k] for k in TASK_BYTES) for i in range(16))
    return (tasks + bytes(s[MENU:MENU + 3]) + bytes((s[HUMANS],)) +
            bytes(s[SIDE:SIDE + 0x30]) + bytes(s[SIDE + SIDE_STRIDE:SIDE + SIDE_STRIDE + 0x30]) +
            bytes((s[0x21C0],)) + bytes(s[0x225E:0x226A]) + bytes(s[0x230B:0x2317]))


def build_select():
    records, last, committed, first_root = [], None, None, None
    teams = {}
    for cap in ('mjus-3', 'mjus-4', 'mjus-1'):
        for rt, root, s in replay(CAPS / cap):
            if cap == 'mjus-3':
                if first_root is None:
                    first_root = root
                if root == first_root:
                    committed = bytes(s[0x255C:0x255F])
            if cap not in teams and rt >= {'mjus-3': 140, 'mjus-4': 170}.get(cap, 1e9):
                teams[cap] = bytes(s[0x21CC:0x21CF])
            if cap == 'mjus-4' and rt > 186:
                break
            rec = select_inputs(s)
            if rec != last:
                records.append(rec)
                last = rec
    return (b'MJSL' + struct.pack('<I', len(records)) + b''.join(records) + committed +
            teams['mjus-3'] + teams['mjus-4'])


def build_versus():
    """mjus-6 (2026-10-20): Versus select, then Training select, both sides
    human, each side picking three. Records: session (u8: 0 Versus, 1
    Training; a new root is a new session) + select_inputs. Trailer per
    session: the game's commits, side 1 R+0x255C..E then side 2 R+0x25DC..E,
    from the session's last sample."""
    records, last, roots, commits = [], None, [], {}
    for _rt, root, s in replay(CAPS / 'mjus-6'):
        if root not in roots:
            roots.append(root)
        session = roots.index(root)
        commits[session] = bytes(s[0x255C:0x255F]) + bytes(s[0x25DC:0x25DF])
        rec = bytes((session,)) + select_inputs(s)
        if rec != last:
            records.append(rec)
            last = rec
    if len(roots) != 2:
        sys.exit('mjus-6: expected two sessions, found %d' % len(roots))
    return b'MJSV' + struct.pack('<I', len(records)) + b''.join(records) + commits[0] + commits[1]


def match_inputs(s):
    out = bytes((s[0x21C0],))
    for p in (0, 1):
        a = F + p * FSTEP
        out += bytes(s[a + 0xEC:a + 0xF8]) + bytes(s[a + 0x2DE:a + 0x2E0]) + bytes((s[a + 0x2FF],)) + \
            bytes(s[a + 0x3CC:a + 0x3CE])
    return out


def build_match():
    records, last = [], None
    for rt, _root, s in replay(CAPS / 'mjus-4'):
        if rt < 185:
            continue
        if rt > 382:
            break
        rec = match_inputs(s)
        if rec != last:
            records.append(struct.pack('<f', rt) + rec)
            last = rec
    return b'MJMF' + struct.pack('<I', len(records)) + b''.join(records)


def context_samples(path):
    """[(t, bytes)] from a context_sampler run; a killed run keeps every flushed record."""
    import gzip
    raw = b''
    with gzip.open(path) as f:
        try:
            while True:
                c = f.read(1 << 16)
                if not c:
                    break
                raw += c
        except EOFError:
            pass
    out, i = [], 0
    while i + 20 <= len(raw):
        t, _ctx, n = struct.unpack_from('<dQI', raw, i)
        i += 20
        if i + n > len(raw):
            break
        out.append((t, raw[i:i + n]))
        i += n
    return out


def build_camera():
    import bisect
    cs = context_samples(CAPS / 'mjus-5-ctx' / 'samples.bin.gz')
    ct = [c[0] for c in cs]
    records, proj, k = [], None, 0
    t0 = None
    for t, _root, s in replay_abs(CAPS / 'mjus-5'):
        if t0 is None:
            t0 = t
        if s[0x21C0] != 2:
            continue
        j = bisect.bisect_left(ct, t)
        if j >= len(cs) or abs(ct[j] - t) > 0.03 or len(cs[j][1]) < 0x1AC:
            continue
        k += 1
        if k % 3:
            continue
        b = cs[j][1]
        if proj is None:
            proj = b[0x16C:0x1AC]
        assert b[0x16C:0x1AC] == proj, 'projection changed'
        rec = struct.pack('<f', t - t0) + bytes((s[0x21C0],))
        for p in (0, 1):
            a = F + p * FSTEP
            rec += bytes(s[a + 0xD0:a + 0xDC]) + bytes(s[a + 0xEC:a + 0xF8]) + bytes((s[a + 0x2FF],))
        records.append(rec + b[0x12C:0x16C])
    return b'MJCM' + proj + struct.pack('<I', len(records)) + b''.join(records)


def replay_abs(cap):
    state = None
    for t, r, key, payload in read_trace(cap / 'trace.bin.gz'):
        if key:
            state = bytearray(payload)
        else:
            for off, v in payload:
                state[off] = v
        yield t, r, state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if not (CAPS / 'mjus-3' / 'trace.bin.gz').exists():
        if args.check:
            print('Project Justice captures are not present; fixtures not re-derived.')
            return
        sys.exit('The source captures are not present under build/captures.')
    outs = ((SELECT_OUT, build_select()), (MATCH_OUT, build_match()), (CAMERA_OUT, build_camera()),
            (VERSUS_OUT, build_versus()))
    if args.check:
        stale = [str(p) for p, d in outs if not p.exists() or p.read_bytes() != d]
        if stale:
            sys.exit('Stale: ' + ', '.join(stale))
        print('Project Justice fixtures are current.')
        return
    for p, d in outs:
        p.write_bytes(d)
        print(f'{p.name}: {len(d)} bytes')


if __name__ == '__main__':
    main()
