"""Read the code-derived CVS2 state block without listening for keypresses.

Research tool, not a selection reader. Addresses are for the audited CFC2 dump.
Usage: python tools/cvs2_state_snapshot.py PID --out build/cvs2-state/label

--fields reports the player-struct fields currently under investigation. It
adds no new memory reads -- they are already inside the block every capture
writes -- but it REFUSES a capture taken outside an in-round substate, since
only same-substate captures may be compared, and prints the fields so a useless
capture is visible at capture time rather than during analysis days later.

STOP: It reports; it does not interpret. An earlier version of this flag was built
around "B+0x0D8 == 20*B+0x0D2 + c" and told the user how to read c. That law was
an artifact: 0x0D8 is a 64-bit POINTER (every mov writing it in the CVS2 range
is a qword store) and the arithmetic was on its low 16 bits. Player X is UNKNOWN
again. See the refutation section in docs/cvs2-state-access-trace.md.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import struct

# Player struct, from the state trace: B = R+0x1EBA8 + player*0x678.
PLAYER_BASE = 0x1EBA8
PLAYER_STRIDE = 0x678
# Fields under investigation. WIDTHS ARE FROM THE INSTRUCTIONS THAT WRITE THEM,
# not from a diff tool's stride -- assuming the width is what produced the
# refuted 20:1 "law".
#   0x0D2  u16, copied from [rdx+4] at 0x1406B79D7
#   0x0D8  u64, a guest pointer; 18 qword stores across the CVS2 range
#   0x16C  u16, the super meter (traced to its writers, behaviourally confirmed)
FIELDS = (('x_cand', 0x0D2, 'H'), ('record_ptr', 0x0D8, 'Q'),
          ('super', 0x16C, 'H'))
# Phase byte 4: 3 = intro, 4 = in-round, 5 = KO/round-end. Only 4 is
# comparable with other in-round captures; the trace doc's rule.
SUBSTATE_IN_ROUND = 4


def player_fields(payload):
    """Both players' fields of interest, each at its true width.

    No derived quantities: the last derived quantity here (c) was an artifact
    of a wrong width, and it read as evidence for a whole experiment.
    """
    out = []
    for p in (0, 1):
        b = PLAYER_BASE + p * PLAYER_STRIDE
        e = {'player': p + 1}
        for name, off, fmt in FIELDS:
            e[name] = struct.unpack_from('<' + fmt, payload, b + off)[0]
        out.append(e)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('pid', type=int)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--fields', '--position', dest='fields',
                    action='store_true',
                    help='require an in-round capture and report the player '
                         'fields under investigation, each at its true width.')
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

        if args.fields:
            # ⛔ Refuse rather than write a capture that cannot be compared.
            # Nothing is saved on refusal, so a retry needs no cleanup and the
            # --out label stays free for the real capture.
            substate = payload[0x7C]
            if substate != SUBSTATE_IN_ROUND:
                where = {3: 'round intro', 5: 'KO / round end'}.get(
                    substate, f'unknown substate {substate}')
                raise SystemExit(
                    f'Not an in-round capture ({where}, phase byte 4 = '
                    f'{substate}). Field captures must be substate '
                    f'{SUBSTATE_IN_ROUND}: only same-substate captures may be '
                    f'compared. Nothing was written -- retry during the round.')
            meta['fields'] = player_fields(payload)
            meta['note'] = ('Field capture; x_cand is NOT established as '
                            'position and record_ptr is a guest pointer')

        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.with_suffix('.bin').open('xb') as f:
            f.write(payload)
        with args.out.with_suffix('.json').open('x', encoding='utf-8') as f:
            json.dump(meta, f, indent=2)
        print(json.dumps(meta, indent=2))

        if args.fields:
            print('\nPlayer fields (x_cand is NOT established as position):')
            for e in meta['fields']:
                print(f"  P{e['player']}: x_cand={e['x_cand']:5d}"
                      f"  record_ptr=0x{e['record_ptr']:012X}"
                      f"  super={e['super']:6d}")
    finally:
        k.CloseHandle(handle)


if __name__ == '__main__':
    main()
