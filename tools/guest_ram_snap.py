"""Save an emulated game's main RAM on each F9, read-only, for offline text search.

The recompiled NAOMI games address main RAM (guest 0x0C000000) through their
context object: host = *(context + 8) + (guest - 0x0C000000). Seen in MJUS at
0x140B95953..0x140B95963 (context slot 0x142C79B40), and in Alpha 3's story
reader ([context+8] + constant). This tool reads `--bytes` from that base on
each F9 press and writes ram-NN.bin.gz plus a JSON line with the time.

Run it beside tools/collection_state_capture.py: both watch F9, so one press
gives a state snapshot and a RAM snapshot with matching times.

No hooks, no writes, no DLL changes.

Usage: python tools/guest_ram_snap.py PID --context 0x142C79B40 --out DIR
"""
import argparse
import ctypes as C
import gzip
import json
import struct
import time
from pathlib import Path

from collection_metadata_capture import ProcessReader

F9 = 0x78
CHUNK = 1 << 20


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('pid', type=int)
    ap.add_argument('--context', type=lambda v: int(v, 0), required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--bytes', type=lambda v: int(v, 0), default=32 << 20)
    ap.add_argument('--minutes', type=float, default=40)
    ap.add_argument('--every', type=float, default=0,
                    help='Also save every N seconds (no need to know what is on screen)')
    args = ap.parse_args()
    if args.out.exists() and any(args.out.iterdir()):
        ap.error('Output directory is not empty')
    args.out.mkdir(parents=True, exist_ok=True)
    user32 = C.WinDLL('user32')
    reader = ProcessReader(args.pid)
    log = open(args.out / 'ram.jsonl', 'a', encoding='utf-8')
    n, down, last = 0, False, 0.0
    end = time.monotonic() + args.minutes * 60
    print('Watching F9 for RAM snapshots.', flush=True)
    try:
        while time.monotonic() < end:
            pressed = bool(user32.GetAsyncKeyState(F9) & 0x8000)
            timed = args.every > 0 and time.monotonic() - last >= args.every
            if (pressed and not down) or timed:
                last = time.monotonic()
                n += 1
                t = time.monotonic()
                row = dict(index=n, t=t, trigger='F9' if pressed and not down else 'timer', utc=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()))
                try:
                    ctx = struct.unpack('<Q', reader.read(args.context, 8))[0]
                    base = struct.unpack('<Q', reader.read(ctx + 8, 8))[0]
                    parts = []
                    for off in range(0, args.bytes, CHUNK):
                        try:
                            parts.append(reader.read(base + off, min(CHUNK, args.bytes - off)))
                        except OSError:
                            break
                    data = b''.join(parts)
                    name = 'ram-%02d.bin.gz' % n
                    with gzip.open(args.out / name, 'wb', compresslevel=1) as f:
                        f.write(data)
                    row.update(context=hex(ctx), base=hex(base), size=len(data), file=name)
                except OSError as e:
                    row.update(error=str(e))
                log.write(json.dumps(row) + '\n')
                log.flush()
                print(row, flush=True)
            down = pressed
            time.sleep(0.03)
    except KeyboardInterrupt:
        pass
    finally:
        reader.close()
        log.close()


if __name__ == '__main__':
    main()
