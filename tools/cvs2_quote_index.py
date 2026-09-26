"""Find the win-quote INDEX in the arena by differential capture.

THE METHOD, and why it is justified here
----------------------------------------
This project is 0-for-4 on "scan memory for something shaped like the answer"
and 4-for-4 on "read the answer out of the instructions". But the host-code
route is CLOSED for this particular value, with evidence: the pick is made by
emulated SH-4, the RNG at 0x1400BD7D0 has 79 host callers and none in the CVS2
range, and the region offsets appear zero times in host code. There is no
instruction to read it out of.

So this is the same situation that justified src/emu_cursor.c: a value that
exists only in the emulated machine's RAM. The discipline transfers even though
the target does not.

⛔ WHAT DOES NOT TRANSFER: reversibility. The cursor probe's load-bearing test
is S3 == S1 -- press right, press left, return. A win quote cannot be
un-displayed, so that test is unavailable and something must replace it.

THE CONSTRAINTS THAT REPLACE IT
-------------------------------
  (a) SAME-SCREEN CONTROL. Two captures seconds apart, one quote on screen:
      the index must NOT change. This is the S0 == S1 of this probe and it
      kills animation, timers and RNG.

      ⛔⛔ THE CONTROL IS ONLY AS GOOD AS THE TIME THAT PASSES BETWEEN THE TWO
      CAPTURES, AND A PAUSED GAME PASSES NONE. Measured on real pairs:

          live, seconds apart      130,260 bytes differ  (0.0243%)
          PAUSED, same instant       1,483 bytes differ  (0.0003%)

      An 80x difference. A pair taken while the emulator is paused does not
      test "what holds still across a quote screen" -- it tests "what changes
      while the machine is frozen", which is almost nothing, so almost
      everything survives.

      ⚠ This fails SAFE (too many candidates, not too few) but it silently
      wastes the strongest filter available. Captures now record whether the
      frame counter advanced between them, and a pair that did not advance is
      reported as a WEAK control rather than counted as a real one.

  (b) VALUE IN 0..7. A 3-bit value. Weak alone, decisive combined.

  (c) DIFFERS ACROSS SCREENS for the SAME character. The pick is random, so a
      field constant for a character is a property OF the character, not the
      index. This is what killed the "+2" lead's predecessor.

  (d) ⭐⭐ THE PREDICTION TEST, which is the real discriminator and needs
      nothing the user can see. We hold all 384 quotes offline. A surviving
      candidate must, across N screens, select a quote CONSISTENT with every
      observation. With enough screens the survivor set collapses to one.

⚠ WHY (d) MATTERS MORE THAN THE REST. The field this project already shipped
wrongly (R+0x05931) passed every shape test on two samples. What it never had
to do was PREDICT something checkable and be right. Shape survives inspection
forever; a prediction can be wrong out loud.

⚠⚠ AND THE HONEST LIMIT: ground truth for the displayed TEXT cannot come from
the user -- they are blind, which is the whole point of the mod. So (d) can
only ever narrow candidates by CONSISTENCY, never confirm one absolutely. This
tool reports how many survive and says plainly when that is more than one.

USAGE
  # while a quote is displayed, twice a few seconds apart:
  python tools/cvs2_quote_index.py capture PID --label q05-a
  python tools/cvs2_quote_index.py capture PID --label q05-b --winner 29
  # after several screens:
  python tools/cvs2_quote_index.py solve
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os
import struct
import sys

# ⚠ This console is cp1252, and a print() carrying a warning glyph raises
# UnicodeEncodeError -- which turns a report into a crash and, worse, loses the
# warning it was carrying. Replace unencodable characters rather than failing.
try:
    sys.stdout.reconfigure(errors='replace')
    sys.stderr.reconfigure(errors='replace')
except Exception:
    pass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cvs2_quotes

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       '..', 'build', 'cvs2-quoteidx')
BLOCK = 4 << 20
ARENA_SPAN = 256 << 20

k = C.WinDLL('kernel32', use_last_error=True)


class MBI(C.Structure):
    _fields_ = [('BaseAddress', C.c_void_p), ('AllocationBase', C.c_void_p),
                ('AllocationProtect', W.DWORD), ('__a', W.DWORD),
                ('RegionSize', C.c_size_t), ('State', W.DWORD),
                ('Protect', W.DWORD), ('Type', W.DWORD), ('__b', W.DWORD)]


def open_game(pid):
    h = k.OpenProcess(0x0410, False, pid)
    if not h:
        raise SystemExit('OpenProcess failed; is the game running?')
    return h


def reader(h):
    def read(addr, size):
        buf = C.create_string_buffer(size)
        got = C.c_size_t()
        if not k.ReadProcessMemory(h, C.c_void_p(addr), buf, size, C.byref(got)):
            return None
        return buf.raw[:got.value]
    return read


def resolve(read):
    sess = read(0x141C14F98, 8)
    if not sess:
        raise SystemExit('could not read the session slot')
    sess = int.from_bytes(sess, 'little')
    if not sess:
        raise SystemExit('no game is loaded')
    arena = int.from_bytes(read(sess, 8), 'little')
    root = int.from_bytes(read(0x1422DEBD0, 8), 'little')
    return arena, root


def snapshot(h, read, arena):
    """arena offset -> bytes. Never resumed across ticks: a blended snapshot
    would report changes that never coexisted (emu_cursor.h says so at length).
    Measured 0.32 s for 512 MB on this machine."""
    out = {}
    addr, end, mbi = arena, arena + ARENA_SPAN, MBI()
    while addr < end:
        if not k.VirtualQueryEx(h, C.c_void_p(addr), C.byref(mbi), C.sizeof(mbi)):
            break
        base = mbi.BaseAddress or addr
        size = mbi.RegionSize
        if mbi.State == 0x1000 and not (mbi.Protect & 0x101):
            off = 0
            while off < size:
                n = min(BLOCK, size - off)
                blk = read(base + off, n)
                if blk:
                    out[base + off - arena] = blk
                off += n
        addr = base + size
    return out


def cmd_capture(args):
    h = open_game(args.pid)
    try:
        read = reader(h)
        arena, root = resolve(read)
        phase = read(root + 0x78, 8)
        if phase[:4] != b'\x02\x01\x02\x03':
            raise SystemExit(
                'Not a win-quote screen (phase %s). Nothing written -- capture '
                'while the quote is displayed.' % phase[:5].hex())
        snap = snapshot(h, read, arena)
        os.makedirs(OUT_DIR, exist_ok=True)
        path = os.path.join(OUT_DIR, args.label + '.snap')
        if os.path.exists(path):
            raise SystemExit('%s exists; pick another label' % path)
        # ⚠ 512 MB raw per capture, and this needs several SCREENS x 2 captures
        # each -- 1.1 GB for the first screen alone. Compressed, because the
        # arena is mostly zeros and repeated structure; the solver decompresses
        # one block at a time.
        import zlib
        with open(path, 'wb') as f:
            f.write(b'QSNZ')
            f.write(struct.pack('<Q', len(snap)))
            for off in sorted(snap):
                comp = zlib.compress(snap[off], 1)
                f.write(struct.pack('<QQQ', off, len(snap[off]), len(comp)))
                f.write(comp)
        # ⭐ LIVENESS. R+0x186 is the 0..59 sub-second frame counter beside the
        # round timer (docs/cvs2-state-access-trace.md). Recording it lets the
        # solver tell a real seconds-apart pair from two captures of a PAUSED
        # machine, which look identical in every other respect and quietly
        # destroy the control. Nothing else here reveals that the game was
        # frozen.
        tick = read(root + 0x186, 2)
        meta = dict(label=args.label, arena=hex(arena),
                    phase=phase[:8].hex(), winner=args.winner,
                    frame_tick=int.from_bytes(tick, 'little') if tick else None,
                    blocks=len(snap),
                    bytes=sum(len(v) for v in snap.values()))
        with open(path + '.json', 'w', encoding='utf-8') as f:
            json.dump(meta, f, indent=2)
        print(json.dumps(meta, indent=2))
    finally:
        k.CloseHandle(h)
    return 0


def load(path):
    import zlib
    with open(path, 'rb') as f:
        magic = f.read(4)
        if magic != b'QSNZ':
            # Uncompressed captures written before compression was added.
            f.seek(0)
            (n,) = struct.unpack('<Q', f.read(8))
            out = {}
            for _ in range(n):
                off, ln = struct.unpack('<QQ', f.read(16))
                out[off] = f.read(ln)
            return out
        (n,) = struct.unpack('<Q', f.read(8))
        out = {}
        for _ in range(n):
            off, ln, cl = struct.unpack('<QQQ', f.read(24))
            out[off] = zlib.decompress(f.read(cl))
            if len(out[off]) != ln:
                raise SystemExit('corrupt capture: %s block 0x%X' % (path, off))
    return out


def _nonzero(buf):
    """Indices of nonzero bytes, found by bytes.find rather than a Python loop
    over every byte. Fast when hits are sparse, which the 0.0054% churn
    measurement says they are."""
    i = 0
    while True:
        j = _find_nonzero(buf, i)
        if j < 0:
            return
        yield j
        i = j + 1


def _find_nonzero(buf, start):
    """First index >= start whose byte is not 0."""
    n = len(buf)
    i = start
    while i < n:
        # Skip long zero runs cheaply: compare a slice against zeros.
        step = 4096
        if buf[i:i + step] == b'\x00' * min(step, n - i):
            i += step
            continue
        while i < n and buf[i] == 0:
            i += 1
        return i if i < n else -1
    return -1


def cmd_solve(args):
    snaps = sorted(f for f in os.listdir(OUT_DIR) if f.endswith('.snap'))
    if len(snaps) < 2:
        raise SystemExit('need at least two captures (a same-screen pair)')

    groups = {}
    for s in snaps:
        meta = json.load(open(os.path.join(OUT_DIR, s + '.json'), encoding='utf-8'))
        stem = meta['label'].rsplit('-', 1)[0]
        groups.setdefault(stem, []).append((meta, load(os.path.join(OUT_DIR, s))))

    print('screens: %d  (%s)' % (len(groups), ', '.join(sorted(groups))))

    # Offsets common to every capture, byte-addressable.
    common = None
    for g in groups.values():
        for _meta, snap in g:
            keys = set(snap)
            common = keys if common is None else (common & keys)
    print('blocks common to all captures: %d' % len(common))

    # ⛔ A PYTHON SET OF (block, index) TUPLES DIES HERE. The first version did
    # exactly that and hit MemoryError: 512 MB of candidate bytes is ~500M
    # tuples, tens of gigabytes of objects. The filter is per-byte over half a
    # gigabyte, so the working set must be FLAT -- one bytearray mask per block,
    # 1 byte per candidate, 512 MB worst case and falling fast after (a).
    #
    # ⭐ Same shape as emu_cursor.c's two-phase design and for the same reason:
    # the wide pass must stay cheap per byte, because there are a lot of bytes.
    mask = {blk: bytearray(b'\x01' * len(groups[next(iter(groups))][0][1][blk]))
            for blk in common}

    def alive():
        return sum(m.count(1) for m in mask.values())

    # (a) SAME-SCREEN CONTROL: within one screen, the byte must NOT change.
    # Measured: only 0.0054% of the arena changes in a second on a live quote
    # screen, so this is the strongest filter available and it runs first.
    # ⭐ int.from_bytes/XOR does the compare in C rather than per byte in
    # Python: the whole-block XOR is nonzero exactly where the two captures
    # differ. Measured ~45 s per per-byte pass over 512 MB versus well under a
    # second this way, and the filters after this one run on a far smaller set.
    paired = 0
    for stem, g in groups.items():
        if len(g) < 2:
            continue
        paired += 1
        base = g[0][1]
        for blk in common:
            ref, m = base[blk], mask[blk]
            for _meta, other in g[1:]:
                o = other[blk]
                if ref == o:
                    continue          # identical block: nothing to clear
                x = (int.from_bytes(ref, 'big') ^ int.from_bytes(o, 'big'))
                d = x.to_bytes(len(ref), 'big')
                for i in _nonzero(d):
                    m[i] = 0
        # ⚠ Report whether the machine actually ADVANCED between the captures.
        # A pair taken while paused filters almost nothing, and without this
        # line that is invisible -- the counts look the same either way.
        ticks = [m.get('frame_tick') for m, _s in g]
        if all(t is not None for t in ticks) and len(set(ticks)) == 1:
            print('  %-12s WEAK CONTROL: frame counter did not advance '
                  '(%s) -- was the game paused? This pair filters almost '
                  'nothing.' % (stem, ticks[0]))
        print('  %-12s after same-screen control: %d' % (stem, alive()))

    if not paired:
        raise SystemExit(
            'No screen has a same-screen PAIR. The control is what makes this '
            'work -- capture each screen twice, a few seconds apart.')

    # (b) value in 0..7 in every screen
    for stem, g in groups.items():
        snap = g[0][1]
        for blk in common:
            s, m = snap[blk], mask[blk]
            for i in range(len(s)):
                if m[i] and s[i] > 7:
                    m[i] = 0
    print('and always in 0..7: %d' % alive())

    # (c) must DIFFER across at least two screens -- the pick is random, so a
    # byte constant across every screen is a property of something else.
    stems = sorted(groups)
    if len(stems) >= 2:
        first = groups[stems[0]][0][1]
        for blk in common:
            m = mask[blk]
            others = [groups[s][0][1][blk] for s in stems[1:]]
            ref = first[blk]
            for i in range(len(ref)):
                if m[i] and all(o[i] == ref[i] for o in others):
                    m[i] = 0
        print('and not constant across screens: %d' % alive())

    # (d) prediction: same winner => the value must vary; different winners
    # cannot be cross-checked without more screens.
    byw = {}
    for stem in groups:
        w = groups[stem][0][0].get('winner')
        if w is not None:
            byw.setdefault(w, []).append(stem)
    repeats = {w: ss for w, ss in byw.items() if len(ss) > 1}
    if repeats:
        print('\ncharacters with more than one screen: %s' % repeats)
        for _w, ss in repeats.items():
            ref_snaps = [groups[s][0][1] for s in ss]
            for blk in common:
                m = mask[blk]
                cols = [sn[blk] for sn in ref_snaps]
                first = cols[0]
                for i in range(len(first)):
                    if m[i] and all(c[i] == first[i] for c in cols[1:]):
                        m[i] = 0
        print('and varying between two wins by the SAME character: %d' % alive())
    else:
        print('\nNo character has won twice yet. Constraint (d) -- the one that '
              'separates a real index from a per-character property -- is NOT '
              'yet applied. Treat every survivor below as unconfirmed.')

    # ⛔ Do NOT materialize the survivor list before checking how big it is.
    # With one screen ~335 million bytes survive, and building that list is a
    # MemoryError, i.e. a crash where the correct output is "far too many, go
    # capture more screens". The count comes from the mask; the LIST is built
    # only once it is small enough to be worth reading.
    n_surv = alive()
    print('\nSURVIVORS: %d' % n_surv)
    REPORT_CAP = 200
    if n_surv > REPORT_CAP:
        need = []
        if len([s for s in groups if len(groups[s]) >= 2]) == len(groups) and len(groups) < 2:
            need.append('more SCREENS (constraint c needs at least two)')
        if not repeats:
            need.append('a repeat win by the SAME character (constraint d)')
        print("""
TOO MANY TO REPORT, and that is the expected result at this stage -- not a
failure. %d bytes is what survives when the strongest constraints have nothing
to bite on yet.

What would collapse it: %s.

⚠ Do not go looking through these by hand for one that "looks like" an index.
That is precisely how R+0x05931 was adopted, and it named the wrong character
in play.""" % (n_surv, '; '.join(need) if need else 'more screens'))
        return 0

    surv = [(blk, i) for blk in sorted(common)
            for i in range(len(mask[blk])) if mask[blk][i]]
    quotes = cvs2_quotes.decode(
        cvs2_quotes.load_region(cvs2_quotes.extract_blob(args.arc)))
    for blk, i in surv[:40]:
        vals = {s: groups[s][0][1][blk][i] for s in sorted(groups)}
        print('  arena+0x%08X  %s' % (blk + i, vals))
        for s, v in vals.items():
            w = groups[s][0][0].get('winner')
            if w is not None and 0 <= w < len(quotes):
                print('      %-10s -> %s' % (s, quotes[w][v][:64]))
    if len(surv) > 40:
        print('  ... and %d more' % (len(surv) - 40))

    if len(surv) == 0:
        print("""
NO SURVIVOR. Either the index is not a plain byte in the arena, or a capture
was mistimed. This is a real negative only if every screen had a clean
same-screen pair -- check the counts above before recording it as one.""")
    elif len(surv) > 1:
        print("""
MORE THAN ONE SURVIVOR -- nothing is confirmed. Capture more screens,
especially repeat wins by the SAME character, which is the constraint that
collapses this set. Do NOT pick one by how plausible its quote reads: any
value 0..7 yields a plausible quote, and that is exactly what a wrong answer
looks like.""")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('capture')
    c.add_argument('pid', type=int)
    c.add_argument('--label', required=True,
                   help='screen id plus a suffix, e.g. q05-a / q05-b')
    c.add_argument('--winner', type=int, help='winning character ID if known')
    c.set_defaults(fn=cmd_capture)
    s = sub.add_parser('solve')
    s.add_argument('--arc', default=cvs2_quotes.DEFAULT_ARC)
    s.set_defaults(fn=cmd_solve)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == '__main__':
    sys.exit(main())
