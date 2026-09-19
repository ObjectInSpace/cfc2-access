"""Read the code-derived CVS2 state block without listening for keypresses.

Research tool, not a selection reader. Addresses are for the audited CFC2 dump.
Usage: python tools/cvs2_state_snapshot.py PID --out build/cvs2-state/label
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import struct


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('pid', type=int)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    if C.sizeof(C.c_void_p) != 8:
        raise SystemExit('64-bit Python required')
    k = C.WinDLL('kernel32', use_last_error=True)
    k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    k.OpenProcess.restype = W.HANDLE
    k.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p,
                                   C.c_size_t, C.POINTER(C.c_size_t)]
    k.ReadProcessMemory.restype = W.BOOL
    k.CloseHandle.argtypes = [W.HANDLE]
    handle = k.OpenProcess(0x0010 | 0x1000, False, args.pid)
    if not handle:
        raise C.WinError(C.get_last_error())
    try:
        def read(addr, size):
            buf = C.create_string_buffer(size)
            got = C.c_size_t()
            if not k.ReadProcessMemory(handle, addr, buf, size, C.byref(got)):
                raise C.WinError(C.get_last_error())
            if got.value != size:
                raise RuntimeError('Partial memory read')
            return buf.raw

        def qword(addr):
            return struct.unpack('<Q', read(addr, 8))[0]

        # Verify instructions that establish the native-state pointer.
        expected = bytes.fromhex('488b481048890d9048c301')
        if read(0x1406AA335, len(expected)) != expected:
            raise RuntimeError('Executable differs from audited instructions, or is still packed')
        session = qword(0x141C14F98)
        header = read(session, 0x60)
        arena = struct.unpack_from('<Q', header)[0]
        game = struct.unpack_from('<I', header, 8)[0]
        rom = struct.unpack_from('<I', header, 0x44)[0]
        if game != 1 or rom not in (2, 3):
            raise RuntimeError(f'CVS2 is not active: game={game}, rom={rom}')
        context = qword(0x142C79B40)
        archive, mapped, state, scratch = struct.unpack('<4Q', read(context, 32))
        root = qword(0x1422DEBD0)
        phase = qword(0x1422DEC30)
        if not (context == arena + 0x8000000 and archive == arena
                and mapped == arena + 0x8400000 and root == state
                and phase == root + 0x78
                and arena + 0xC000000 <= root
                and root + 0x216B0 <= arena + 0x10000000):
            raise RuntimeError('Code-derived pointer relationships do not hold')
        # Two reads reduce accidental capture across reinitialization. They
        # are not an atomic game snapshot; animation may change between reads.
        payload = read(root, 0x216B0)
        if qword(0x1422DEBD0) != root or qword(0x141C14F98) != session:
            raise RuntimeError('State changed during capture; retry')
        meta = dict(pid=args.pid, game=game, rom=rom,
                    pointers={n: hex(v) for n, v in dict(session=session,
                    arena=arena, context=context, mapped=mapped,
                    native_state=root, phase=phase, scratch=scratch).items()},
                    phase_bytes=payload[0x78:0x80].hex(),
                    note='Uninterpreted state capture, not validated selection data')
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.with_suffix('.bin').open('xb') as f:
            f.write(payload)
        with args.out.with_suffix('.json').open('x', encoding='utf-8') as f:
            json.dump(meta, f, indent=2)
        print(json.dumps(meta, indent=2))
    finally:
        k.CloseHandle(handle)


if __name__ == '__main__':
    main()
