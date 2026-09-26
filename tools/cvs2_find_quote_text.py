"""Scan the live arena for the ASCII of a character's eight candidate quotes.

WHY: the quote region itself is byte-identical to the archive while a quote is
displayed (tools/cvs2_quote_diff.py, clean negative), so the selection is not
recorded there. If the game COPIES the chosen quote somewhere to render it,
that copy is findable by its text -- and its address is the thing a reader
could watch.

WHAT A HIT MEANS, and what it does not:
  - the STATIC region copy at arena+0x1FD800 will always match; it is excluded
    by address so it cannot be mistaken for a render buffer;
  - a hit ELSEWHERE is a live copy of that specific quote, which is the lead;
  - hits for SEVERAL different quotes of the same character mean the thing
    found is another full copy of the table, NOT the selection. That is the
    result most likely to be misread as success, so it is called out.

SYSCALL BUDGET (CLAUDE.md: count them before looping over memory). One
VirtualQuery per REGION, then block reads -- never one query per page. The
arena is ~256 MB; at 4 MB blocks that is ~64 reads per region walked, not
65,000.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cvs2_quotes

QUOTE_REGION_OFF = 0x1FD800
QUOTE_REGION_SIZE = 25676
BLOCK = 4 << 20

k = C.WinDLL('kernel32', use_last_error=True)


class MBI(C.Structure):
    _fields_ = [('BaseAddress', C.c_void_p), ('AllocationBase', C.c_void_p),
                ('AllocationProtect', W.DWORD), ('__a', W.DWORD),
                ('RegionSize', C.c_size_t), ('State', W.DWORD),
                ('Protect', W.DWORD), ('Type', W.DWORD), ('__b', W.DWORD)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('pid', type=int)
    ap.add_argument('--char', type=int, required=True,
                    help="character ID whose eight quotes to search for")
    ap.add_argument('--arc', default=cvs2_quotes.DEFAULT_ARC)
    args = ap.parse_args()

    quotes = cvs2_quotes.decode(
        cvs2_quotes.load_region(cvs2_quotes.extract_blob(args.arc)))
    if not 0 <= args.char < len(quotes):
        raise SystemExit('character id out of range')
    cand = quotes[args.char]

    # Search for the FIRST LINE of each quote: the stored text is split into
    # NUL-terminated lines, so the joined sentence never appears contiguously.
    needles = []
    for i, q in enumerate(cand):
        first = q.split('. ')[0][:24].strip()
        if len(first) >= 10:
            needles.append((i, first.encode('ascii', 'ignore')))
    print('character %d: searching for %d distinct quote openings'
          % (args.char, len(needles)))

    h = k.OpenProcess(0x0410, False, args.pid)
    if not h:
        raise SystemExit('OpenProcess failed; is the game running?')

    # Locate the arena exactly as the snapshot tool does, so "excluded" is
    # the real static copy and not a guess.
    def read(addr, size):
        buf = C.create_string_buffer(size)
        got = C.c_size_t()
        if not k.ReadProcessMemory(h, C.c_void_p(addr), buf, size, C.byref(got)):
            return None
        return buf.raw[:got.value]

    sess = int.from_bytes(read(0x141C14F98, 8), 'little')
    arena = int.from_bytes(read(sess, 8), 'little')
    static_lo = arena + QUOTE_REGION_OFF
    static_hi = static_lo + QUOTE_REGION_SIZE
    print('arena 0x%X; excluding the static region 0x%X..0x%X'
          % (arena, static_lo, static_hi))

    hits = {}
    addr, queries, reads = arena, 0, 0
    end = arena + (256 << 20)
    mbi = MBI()
    while addr < end:
        queries += 1
        # ⛔⛔ VirtualQueryEx, NOT VirtualQuery. The first version of this tool
        # called VirtualQuery, which queries OUR OWN process -- the arena
        # address is FREE here, so the walk exited on the first region and
        # reported "0 block reads ... NO LIVE COPY FOUND". A confident false
        # negative that looked exactly like a result.
        # ⭐ It was caught only because the tool PRINTS its walk counts. A scan
        # that does not report how much it examined cannot be told from one
        # that examined nothing.
        if not k.VirtualQueryEx(h, C.c_void_p(addr), C.byref(mbi), C.sizeof(mbi)):
            break
        base, size, state = mbi.BaseAddress or addr, mbi.RegionSize, mbi.State
        if state == 0x1000 and not (mbi.Protect & 0x101):   # committed, not NOACCESS/GUARD
            off = 0
            while off < size:
                n = min(BLOCK, size - off)
                blk = read(base + off, n)
                reads += 1
                if blk:
                    for qi, needle in needles:
                        start = 0
                        while True:
                            j = blk.find(needle, start)
                            if j < 0:
                                break
                            a = base + off + j
                            if not (static_lo <= a < static_hi):
                                hits.setdefault(qi, []).append(a)
                            start = j + 1
                off += n
        addr = base + size

    # ⭐⭐ POSITIVE CONTROL, and it is not optional. The first version of this
    # tool reported "NO LIVE COPY FOUND" while having read ZERO bytes (it called
    # VirtualQuery instead of VirtualQueryEx). A negative from a scanner that
    # has not been shown to find anything is worthless.
    #
    # The control: the STATIC region copy is known to be present at a known
    # address. Re-read it and confirm the needles are there. If this fails, the
    # scan is broken and NO conclusion may be drawn from an empty result.
    control = read(static_lo, QUOTE_REGION_SIZE)
    found_static = 0
    if control:
        found_static = sum(1 for _qi, nd in needles if nd in control)
    k.CloseHandle(h)

    print('walked %d region(s) with %d VirtualQueryEx and %d block reads (%.1f MB)'
          % (queries, queries, reads, reads * BLOCK / (1 << 20)))
    print('positive control: %d of %d needles found in the known static copy'
          % (found_static, len(needles)))

    if found_static != len(needles) or reads == 0:
        print("""
INCONCLUSIVE -- THE SCAN PROVED NOTHING.
The control failed, or no memory was read at all. Either the process walk is
broken or the arena was not located. This is NOT evidence that the quote is
absent; do not record it as one. Fix the scan and re-run.""")
        return 2

    if not hits:
        print("""
NO LIVE COPY FOUND. The chosen quote is not present in the arena as ASCII
outside the static table, so nothing copies it in readable form. Combined with
the clean negative from cvs2_quote_diff.py, both data-side routes are closed:
the selection is made and consumed inside emulated SH-4 without leaving a
host-readable trace.""")
        return 0

    print('\nquotes with a live copy outside the static region: %d of %d'
          % (len(hits), len(needles)))
    for qi in sorted(hits):
        print('  [%d] %-58s  %d hit(s)  %s'
              % (qi, cand[qi][:58], len(hits[qi]),
                 ' '.join('0x%X' % a for a in hits[qi][:4])))

    if len(hits) > 1:
        print("""
WARNING: MORE THAN ONE quote has a live copy. That is another copy of the
whole table, not the selection -- do NOT read the displayed quote from it.
Only a result where exactly ONE of the eight appears is a selection.""")
    else:
        qi = next(iter(hits))
        print("""
EXACTLY ONE of the eight appears live: quote %d. That is consistent with a
render copy of the DISPLAYED quote.

Confirm before believing it: capture another win by this same character. If the
live quote changes with the display, this is the mechanism. If it is quote %d
every time, it is something else about this character.""" % (qi, qi))
    return 0


if __name__ == '__main__':
    sys.exit(main())
