"""Capture the CVS2 emulator arena outside the two already-known windows.

One VirtualQuery per REGION (not per page), then bulk block reads. Writes a
.bin of the committed readable bytes plus a .json index of (offset,size) runs
so a later diff knows exactly what each byte is.
"""
import ctypes as C, struct, sys, json
from ctypes import wintypes as W
from pathlib import Path

BLOCK = 1 << 20

class MBI(C.Structure):
    _fields_ = [("BaseAddress", C.c_void_p), ("AllocationBase", C.c_void_p),
                ("AllocationProtect", W.DWORD), ("__align", W.DWORD),
                ("RegionSize", C.c_size_t), ("State", W.DWORD),
                ("Protect", W.DWORD), ("Type", W.DWORD), ("__align2", W.DWORD)]

def main():
    pid = int(sys.argv[1]); out = Path(sys.argv[2])
    k = C.WinDLL('kernel32', use_last_error=True)
    k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]; k.OpenProcess.restype = W.HANDLE
    k.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
    k.VirtualQueryEx.argtypes = [W.HANDLE, C.c_void_p, C.POINTER(MBI), C.c_size_t]
    h = k.OpenProcess(0x0010 | 0x1000, False, pid)
    if not h: raise SystemExit("OpenProcess failed")

    def rd(a, n):
        buf = C.create_string_buffer(n); got = C.c_size_t()
        if not k.ReadProcessMemory(h, C.c_void_p(a), buf, n, C.byref(got)): return None
        return buf.raw[:got.value]
    def q(a):
        r = rd(a, 8); return struct.unpack('<Q', r)[0] if r else None

    session = q(0x141C14F98); arena = q(session); ctx = q(0x142C79B40)
    mapped = q(ctx + 8); root = q(0x1422DEBD0)
    ARENA_SZ = 0x10000000
    known = [(mapped, 32 << 20), (root, 0x216B0)]   # already captured
    print("arena=0x%x mapped=0x%x root=0x%x" % (arena, mapped, root))

    # One VirtualQuery per REGION across the arena.
    regions = []; a = arena; vq = 0
    while a < arena + ARENA_SZ:
        mbi = MBI()
        if not k.VirtualQueryEx(h, C.c_void_p(a), C.byref(mbi), C.sizeof(mbi)): break
        vq += 1
        size = mbi.RegionSize
        base = mbi.BaseAddress or a
        if mbi.State == 0x1000 and not (mbi.Protect & 0x101):  # committed, not NOACCESS/GUARD
            regions.append((base, size))
        a = base + size
        if size == 0: break
    print("VirtualQuery calls: %d ; committed regions: %d" % (vq, len(regions)))

    def overlaps_known(s, e):
        for ks, kl in known:
            if s < ks + kl and ks < e: return True
        return False

    blob = bytearray(); index = []
    for base, size in regions:
        end = min(base + size, arena + ARENA_SZ)
        cur = max(base, arena)
        while cur < end:
            n = min(BLOCK, end - cur)
            if not overlaps_known(cur, cur + n):
                c = rd(cur, n)
                if c:
                    index.append({"addr": cur, "off": len(blob), "len": len(c)})
                    blob += c
            cur += n
    out.parent.mkdir(parents=True, exist_ok=True)
    out.with_suffix('.bin').write_bytes(bytes(blob))
    out.with_suffix('.json').write_text(json.dumps(
        {"pid": pid, "arena": arena, "mapped": mapped, "root": root,
         "runs": index, "total": len(blob)}, indent=1))
    print("captured %.1f MiB in %d runs -> %s" % (len(blob)/(1<<20), len(index), out))

main()
