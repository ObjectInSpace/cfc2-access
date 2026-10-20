"""Build the Plasma Sword replay fixtures from the retained live captures.

Inputs, both recorded 2026-10-20 on ROM 15 pswd (docs/port-tasks/stg2-capture.md):
- tools/fixtures/stg2-arcade-20261020/trace.bin.gz: Arcade, side 0 human:
  character select, a two-round fight with a pause, the continue screen.
- tools/fixtures/stg2-training-20261020/: Training, walking, side steps,
  jumps over the opponent and a corner (trace.bin.gz), with the shared
  renderer's context sampled beside it (context.bin.gz, from
  tools/context_sampler.py: ctx = *0x142C79B40, offset 0x1F8100, 0x200 bytes).
The raw traces (about 37 MB each) are kept out of git; the outputs below are
what the tests replay.

Outputs:
- tools/fixtures/stg2-select-live-20261020.bin: every Arcade trace sample
  where the selection decoder's inputs change, with its time: the phase byte
  R+0, the side states and select numbers R+0x4E470..+4, the human mask
  R+0x4E4B9, the mode and sub-state words R+0x4E5EC and R+0x4E5F4, and both
  sides' shuffle flags R+side*0xA40+0xAB4. The trailer is the game's own
  commit, read after the pick: the native IDs R+0x4DF30/31 and row flags
  R+0x4E476/77.
- tools/fixtures/stg2-match-frames-20261020.bin and
  tools/fixtures/stg2-training-frames-20261020.bin: every trace sample where
  the frame decoder's inputs change, as the byte ranges in RANGES, with its
  time.
- tools/fixtures/stg2-render-20261020.bin: the renderer's view (ctx+0x1F812C)
  and projection (ctx+0x1F816C), 32 floats. They were identical in every one
  of the context samples; the build refuses otherwise.

Usage: python tools/make_stg2_fixtures.py [--check]
"""
import argparse
import gzip
from pathlib import Path
import struct
import sys

from collection_state_capture import read_trace

HERE = Path(__file__).resolve().parent
ARCADE = HERE / 'fixtures' / 'stg2-arcade-20261020' / 'trace.bin.gz'
TRAINING = HERE / 'fixtures' / 'stg2-training-20261020'
SELECT_OUT = HERE / 'fixtures' / 'stg2-select-live-20261020.bin'
FRAMES_OUT = HERE / 'fixtures' / 'stg2-match-frames-20261020.bin'
TRAINING_OUT = HERE / 'fixtures' / 'stg2-training-frames-20261020.bin'
RENDER_OUT = HERE / 'fixtures' / 'stg2-render-20261020.bin'
WINDOW, STEP, ROULETTE = 0x4E470, 0xA40, 0xAB4
CTX_BASE, VIEW, PROJ = 0x1F8100, 0x1F812C, 0x1F816C
# What stg2_frame_decode reads; tools/test_stg2_frame.c lists the same.
RANGES = [(0x0, 2), (0x94, 2),
          (0x220, 0xC), (0x428, 4), (0x57C, 2),
          (0xA40 + 0x220, 0xC), (0xA40 + 0x428, 4), (0xA40 + 0x57C, 2),
          (0x4D4B4, 0x40), (0x4DF33, 1), (0x4DFF0, 0xA), (0x4E598, 1), (0x4E5EC, 2)]


def samples(path):
    state = None
    for t, root, key, payload in read_trace(path):
        if key:
            state = bytearray(payload)
        else:
            for offset, value in payload:
                state[offset] = value
        yield t, root, state


def select_inputs(s):
    return (bytes(s[0:1]) + bytes(s[WINDOW:WINDOW + 4]) + bytes(s[0x4E4B9:0x4E4BA]) +
            bytes(s[0x4E5EC:0x4E5EE]) + bytes(s[0x4E5F4:0x4E5F6]) +
            bytes(s[ROULETTE:ROULETTE + 4]) + bytes(s[STEP + ROULETTE:STEP + ROULETTE + 4]))


def frame_inputs(s):
    return b''.join(bytes(s[o:o + n]) for o, n in RANGES)


def frames(path):
    out, last = [], None
    for t, _root, s in samples(path):
        b = frame_inputs(s)
        if b != last:
            out.append(struct.pack('<d', t) + b)
            last = b
    return b'STGF' + struct.pack('<I', len(out)) + b''.join(out)


def render():
    """The context samples: <d t><Q ctx><I n><n bytes>; a killed sampler
    leaves a partial last record, which is dropped."""
    seen = set()
    with gzip.open(TRAINING / 'context.bin.gz', 'rb') as f:
        while True:
            try:
                head = f.read(20)
                if len(head) < 20:
                    break
                _t, ctx, n = struct.unpack('<dQI', head)
                block = f.read(n)
            except EOFError:
                break
            if len(block) < n:
                break
            if n == 0:          # the sampler's read failed (the game left)
                continue
            assert ctx and n == 0x200
            seen.add(block[VIEW - CTX_BASE:VIEW - CTX_BASE + 64] + block[PROJ - CTX_BASE:PROJ - CTX_BASE + 64])
    assert len(seen) == 1, f'renderer matrices changed during the capture ({len(seen)} distinct)'
    return seen.pop()


def build():
    sel, last_sel, committed = [], None, None
    for t, _root, s in samples(ARCADE):
        a = select_inputs(s)
        if a != last_sel:
            sel.append(struct.pack('<d', t) + a)
            last_sel = a
        # The pick is committed when the mode leaves select (1 -> 2).
        if committed is None and struct.unpack_from('<H', s, 0x4E5EC)[0] == 2:
            committed = bytes((s[0x4DF30], s[0x4DF31], s[0x4E476], s[0x4E477]))
    assert committed
    return {SELECT_OUT: b'STGS' + struct.pack('<I', len(sel)) + b''.join(sel) + committed,
            FRAMES_OUT: frames(ARCADE),
            TRAINING_OUT: frames(TRAINING / 'trace.bin.gz'),
            RENDER_OUT: render()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for source in (ARCADE, TRAINING / 'trace.bin.gz', TRAINING / 'context.bin.gz'):
        if not source.exists():
            sys.exit(f'Missing raw capture: {source}')
    outputs = build()
    if args.check:
        for path, data in outputs.items():
            if not path.exists() or path.read_bytes() != data:
                sys.exit(f'Stale: {path}')
        print('STG2 fixtures are current.')
        return
    for path, data in outputs.items():
        path.write_bytes(data)
        print(f'Wrote {path} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
