"""Bounded read-only collection metadata capture; no gameplay-state decoding.

See docs/collection-metadata-run-card.md. Matching bracket reads cannot prove
atomicity, readiness, or exclude same-address reuse. No process writes/hooks.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import time

MANIFEST = Path(__file__).resolve().parents[1] / 'docs/collection-lifecycle-manifest.json'
SESSION = 0x141C14F98


def utc():
    return datetime.now(timezone.utc).isoformat()


def exact(read, address, size):
    data = read(address, size)
    if len(data) != size:
        raise OSError(f'Partial read at {address:#x}: {len(data)}/{size}')
    return data


def verify_build(read, manifest):
    """Check reviewed code spans only, not a full live executable hash."""
    checked = []
    for span in manifest['spans']:
        start, end = int(span['start'], 16), int(span['end_exclusive'], 16)
        if not 0 < end - start <= 4096:
            raise ValueError('Invalid signature span size')
        raw = exact(read, start, end - start)
        if hashlib.sha256(raw).hexdigest() != span['sha256']:
            raise ValueError(f"Reviewed code differs or remains packed: {span['label']}")
        checked.append(span['label'])
    if not checked:
        raise ValueError('No reviewed code spans')
    return checked


def observe(read, manifest):
    records, errors = {}, []

    def block(key, address, size):
        row = dict(address=hex(address), size=size)
        records[key] = row
        try:
            raw = exact(read, address, size)
        except OSError as exc:
            row.update(status='unreadable', error=str(exc))
            errors.append(key)
            return None
        row.update(status='read', hex=raw.hex())
        return raw

    def integer(raw, offset=0, fmt='Q'):
        return struct.unpack_from('<' + fmt, raw, offset)[0]

    session_raw = block('session_slot', SESSION, 8)
    session = integer(session_raw) if session_raw is not None else None
    decoded = dict(session=None if session is None else hex(session))
    if session:
        header = block('session_header', session, 0x60)
        published = block('published_blocks', session + 0x1B0, 0x20)
        alpha = block('alpha_discriminator', session + 0x2B4, 4)
        if header is not None:
            decoded.update(arena=hex(integer(header)), game=integer(header, 8, 'I'),
                           region=integer(header, 12, 'I'), rom_index=integer(header, 0x44, 'I'),
                           callbacks={hex(off): hex(integer(header, off))
                                      for off in (0x10, 0x18, 0x20, 0x28)},
                           name_pointer=hex(integer(header, 0x30)))
            pointer = integer(header, 0x30)
            if pointer:
                name = block('rom_name', pointer, 16)
                if name is not None:
                    try:
                        if b'\0' not in name:
                            raise ValueError('Unterminated name')
                        decoded['rom_name'] = name.split(b'\0', 1)[0].decode('ascii')
                    except (ValueError, UnicodeError):
                        errors.append('rom_name_format')
        if published is not None:
            decoded.update(primary=hex(integer(published)), primary_size=integer(published, 8, 'I'),
                           secondary=hex(integer(published, 16)), secondary_size=integer(published, 24, 'I'))
        if alpha is not None:
            decoded['alpha_discriminator'] = integer(alpha, fmt='I')
    # Always retain every slot, including in shell/unknown identity states.
    # Do not dereference stale context/root pointers or call them active.
    slots = {int(p['root_slot'], 16) for p in manifest['paths']}
    slots.update(int(p['context_slot'], 16) for p in manifest['paths'])
    decoded['slots'] = {}
    for address in sorted(slots):
        raw = block(f'slot_{address:x}', address, 8)
        decoded['slots'][hex(address)] = hex(integer(raw)) if raw is not None else None
    return dict(records=records, decoded=decoded, errors=errors)


def capture(read, manifest):
    started, start = utc(), time.perf_counter()
    before, after = observe(read, manifest), observe(read, manifest)
    status = ('unreadable' if before['errors'] or after['errors'] else
              'matching' if before['records'] == after['records'] else 'changed')
    return dict(started_utc=started, elapsed_ms=(time.perf_counter() - start) * 1000,
                bracket_status=status, before=before, after=after,
                note='Metadata only; matching does not prove active/coherent state or exclude ABA reuse.')


class ProcessReader:
    def __init__(self, pid):
        if C.sizeof(C.c_void_p) != 8:
            raise RuntimeError('64-bit Python required')
        self.kernel = C.WinDLL('kernel32', use_last_error=True)
        k = self.kernel
        k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
        k.OpenProcess.restype = W.HANDLE
        k.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p,
                                       C.c_size_t, C.POINTER(C.c_size_t)]
        k.ReadProcessMemory.restype = W.BOOL
        k.CloseHandle.argtypes = [W.HANDLE]
        k.CloseHandle.restype = W.BOOL
        self.handle = k.OpenProcess(0x0010 | 0x1000, False, pid)
        if not self.handle:
            raise C.WinError(C.get_last_error())

    def read(self, address, size):
        buf, got = C.create_string_buffer(size), C.c_size_t()
        if not self.kernel.ReadProcessMemory(self.handle, address, buf, size, C.byref(got)):
            raise C.WinError(C.get_last_error())
        if got.value != size:
            raise OSError(f'Partial process read: {got.value}/{size}')
        return buf.raw

    def close(self):
        self.kernel.CloseHandle(self.handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', required=True, help='Observed screen/action; do not infer it from pointers')
    parser.add_argument('--mode', required=True)
    parser.add_argument('--language', required=True, help='Observed shell/ROM language, or unknown')
    parser.add_argument('--count', type=int, default=1)
    parser.add_argument('--interval', type=float, default=0.25, help='Seconds between completed samples')
    args = parser.parse_args()
    if args.pid <= 0 or not 1 <= args.count <= 240 or not 0.1 <= args.interval <= 5:
        parser.error('Require PID > 0, count 1..240 and interval 0.1..5 seconds')
    if args.count * args.interval > 60:
        parser.error('Requested sampling schedule exceeds 60 seconds')
    if args.out.exists():
        parser.error('Output already exists; choose a new label/path')
    raw_manifest = MANIFEST.read_bytes()
    manifest = json.loads(raw_manifest)
    reader = ProcessReader(args.pid)
    try:
        checked = verify_build(reader.read, manifest)
        provenance = dict(schema=1, kind='collection-metadata-capture', pid=args.pid,
                          created_utc=utc(), label=args.label, mode=args.mode, language=args.language,
                          tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                          manifest_sha256=hashlib.sha256(raw_manifest).hexdigest(),
                          reviewed_dump_sections=manifest['sections'], checked_code_spans=checked,
                          build_check='Bounded live code spans match; full live code/DLL hashes not measured.',
                          requested_count=args.count, interval_seconds=args.interval)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.open('x', encoding='utf-8') as output:
            output.write(json.dumps(provenance) + '\n')
            for index in range(args.count):
                if index:
                    time.sleep(args.interval)
                row = capture(reader.read, manifest)
                row['sample_index'] = index
                output.write(json.dumps(row) + '\n')
                output.flush()
                print(f"Sample {index + 1}: {row['bracket_status']}, {row['elapsed_ms']:.1f} ms", flush=True)
        print(f'Wrote {args.out}; no active-state or gameplay validation claimed.')
    finally:
        reader.close()


if __name__ == '__main__':
    main()
