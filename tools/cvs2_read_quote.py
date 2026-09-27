"""Read the completed CVS2 draw buffers; optionally speak through NVDA.

Offsets are traced through 0x1406D5F80 -> 0x1406D6D32 -> 0x1406D706A.
No winner/index inference, game writes, screenshots, or arena scans.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
import struct


def decode(block):
    mode, lines = struct.unpack_from('<HH', block, 0x0e)
    lengths = struct.unpack_from('<4H', block, 0x24)
    if mode > 2 or lines not in (1, 2) or block[0x20] not in (3, 4):
        return None
    if mode == 0:
        lines = 2  # the two-speaker drawing branch always draws both lines
    result = []
    for i in range(lines):
        n = lengths[i]
        line = block[0x2c + i * 0x50:0x2c + (i + 1) * 0x50]
        if not 0 < n < 0x50 or lengths[2+i] != n or line[n] != 0:
            return None
        if any(c < 32 or c > 126 for c in line[:n]):
            return None
        result.append(line[:n].decode('ascii').strip())
    return ' '.join(result)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('pid', type=int)
    ap.add_argument('--speak', action='store_true')
    args = ap.parse_args()
    k = C.WinDLL('kernel32', use_last_error=True)
    k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    k.OpenProcess.restype = W.HANDLE
    k.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p,
                                   C.c_size_t, C.POINTER(C.c_size_t)]
    k.CloseHandle.argtypes = [W.HANDLE]
    h = k.OpenProcess(0x1010, False, args.pid)
    if not h:
        raise C.WinError(C.get_last_error())
    def read(a, n):
        b, got = C.create_string_buffer(n), C.c_size_t()
        if not k.ReadProcessMemory(h, a, b, n, C.byref(got)) or got.value != n:
            raise C.WinError(C.get_last_error())
        return b.raw
    try:
        # Same native root as the shipping reader, for the audited executable.
        session = struct.unpack('<Q', read(0x141C14F98, 8))[0]
        header = read(session, 0x48)
        if struct.unpack_from('<I', header, 8)[0] != 1 or struct.unpack_from('<I', header, 0x44)[0] not in (2, 3):
            raise SystemExit('CVS2 is not loaded.')
        root = struct.unpack('<Q', read(0x1422DEBD0, 8))[0]
        phase = read(root + 0x78, 5)
        if phase != bytes([2, 1, 2, 3, 2]):
            raise SystemExit('No active quote display.')
        block = read(root + 0x20f30, 0xd0)
        text = decode(block)
        if not text or block != read(root + 0x20f30, 0xd0) or phase != read(root + 0x78, 5):
            raise SystemExit('Quote is incomplete or changing; try again.')
        print(text)
        if args.speak:
            game = Path(r'C:\Program Files (x86)\Steam\steamapps\common\CAPCOM FIGHTING COLLECTION 2')
            nvda = C.WinDLL(str(game / 'nvdaControllerClient.dll'))
            nvda.nvdaController_speakText.argtypes = [C.c_wchar_p]
            rc = nvda.nvdaController_speakText(text)
            if rc:
                raise SystemExit('NVDA rejected speech: %d' % rc)
            print('NVDA accepted the quote.')
    finally:
        k.CloseHandle(h)


if __name__ == '__main__':
    main()
