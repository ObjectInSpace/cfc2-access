"""Sample a region of a recompiled game's context object at ~20 Hz, read-only.

The shared NAOMI renderer composes its transforms in the context object
(`*0x142C79B40` for MJUS): 0x140F33830 multiplies ctx+0x1F812C, ctx+0x1F816C
and the viewport at 0x141C00210 into ctx+0x1F8200. To find a fighter's
screen side in Project Justice's 3D arena, those matrices must be recorded
next to the fighters' positions, which tools/collection_state_capture.py
already traces. Times use time.monotonic(), the capture tool's clock, so the
two files pair by time.

Output: samples.bin.gz of records <d t><Q ctx><I n><n bytes>, and the
viewport's 64 bytes once in meta.json.

No hooks, no writes. Usage:
    python tools/context_sampler.py PID --context 0x142C79B40 --offset 0x1F8100 --size 0x200 --out DIR
"""
import argparse
import gzip
import json
import struct
import time
from pathlib import Path

from collection_metadata_capture import ProcessReader


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('pid', type=int)
    ap.add_argument('--context', type=lambda v: int(v, 0), required=True)
    ap.add_argument('--offset', type=lambda v: int(v, 0), required=True)
    ap.add_argument('--size', type=lambda v: int(v, 0), required=True)
    ap.add_argument('--static', type=lambda v: int(v, 0), default=0x141C00210,
                    help='A static 64-byte block to record once (the viewport)')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--minutes', type=float, default=40)
    ap.add_argument('--interval', type=float, default=0.05)
    args = ap.parse_args()
    if args.out.exists() and any(args.out.iterdir()):
        ap.error('Output directory is not empty')
    if not 0 < args.size <= 0x10000:
        ap.error('size must be 1..0x10000')
    args.out.mkdir(parents=True, exist_ok=True)
    reader = ProcessReader(args.pid)
    meta = dict(context=hex(args.context), offset=hex(args.offset), size=args.size,
                static=hex(args.static), clock='time.monotonic')
    try:
        meta['static_bytes'] = reader.read(args.static, 64).hex()
    except OSError as e:
        meta['static_error'] = str(e)
    (args.out / 'meta.json').write_text(json.dumps(meta, indent=1))
    n = errors = 0
    end = time.monotonic() + args.minutes * 60
    print('Sampling context region.', flush=True)
    with gzip.open(args.out / 'samples.bin.gz', 'wb', compresslevel=1) as f:
        try:
            while time.monotonic() < end:
                t = time.monotonic()
                try:
                    ctx = struct.unpack('<Q', reader.read(args.context, 8))[0]
                    raw = reader.read(ctx + args.offset, args.size) if ctx else b''
                except OSError:
                    errors += 1
                    ctx, raw = 0, b''
                f.write(struct.pack('<dQI', t, ctx, len(raw)) + raw)
                n += 1
                if n % 20 == 0:
                    f.flush()
                time.sleep(args.interval)
        except KeyboardInterrupt:
            pass
    reader.close()
    print(f'{n} samples, {errors} unreadable', flush=True)


if __name__ == '__main__':
    main()
