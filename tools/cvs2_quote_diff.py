"""Compare a LIVE win-quote region against the pristine archive copy.

THE QUESTION THIS ANSWERS
-------------------------
CVS2 shows one of the winner's eight quotes, chosen at RANDOM at display time.
The text is fully decoded offline (tools/cvs2_quotes.py, 384 quotes) and the
winner is identifiable, but WHICH of the eight is on screen is not known, and
without it nothing truthful can be spoken.

Every previous capture held only the 136 KB `R` block. The quote region lives
at arena+0x1FD800 -- outside it. So the earlier searches for an index were
looking at data that structurally could not contain one.

cvs2_state_snapshot.py now also writes `<label>.quote.bin`. This tool diffs it
against the same region as it ships in the archive:

  IDENTICAL  -> the game does not write here while displaying. A clean negative.
               The selection lives elsewhere; this route is closed.
  DIFFERS    -> the game writes something during display. The changed bytes are
               then the best lead available, and this prints them with their
               offsets and any interpretation the known layout supports.

⭐ EITHER OUTCOME IS USEFUL, which is the point of running it before writing
any more reader code. A negative closes a route cheaply; a positive names the
displayed quote directly, with no 1-in-8 inference.

⚠ WHAT THIS TOOL MUST NOT DO: guess. If the region differs in ways that do not
resolve to a quote, it says so. A plausible-looking difference is not an index,
and this project has already shipped one field that "looked right" on two
samples and named the wrong character in play.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cvs2_quotes

# The region head, from docs/cvs2-state-access-trace.md.
IDX_OFF = 0x62CC      # 48 u32 -> per-character quote sub-tables
WIDTH_OFF = 0x638C    # 48 u32 -> per-character width data
TEXT_OFF = 0x01F4     # first line of text


def describe(off):
    """Name the structure a changed offset falls in, without over-claiming."""
    if off < 0x0008:
        return 'header table offsets'
    if off < 0x0074:
        return 'font glyph remap table'
    if off < TEXT_OFF:
        return 'per-line width/advance data'
    if off < IDX_OFF:
        return 'TEXT'
    if off < WIDTH_OFF:
        return 'character -> quote sub-table index'
    if off < WIDTH_OFF + 48 * 4:
        return 'character -> width data index'
    return 'past the known tables'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('live', help='a <label>.quote.bin written by the snapshot tool')
    ap.add_argument('--arc', default=cvs2_quotes.DEFAULT_ARC)
    args = ap.parse_args()

    with open(args.live, 'rb') as f:
        live = f.read()

    blob = cvs2_quotes.extract_blob(args.arc)
    pristine = cvs2_quotes.load_region(blob)

    print('live     %s  %d bytes' % (os.path.basename(args.live), len(live)))
    print('archive  %d bytes' % len(pristine))
    if len(live) != len(pristine):
        n = min(len(live), len(pristine))
        print('WARNING: SIZES DIFFER -- comparing the first %d bytes only.' % n)
        live, pristine = live[:n], pristine[:n]

    diffs = [i for i in range(len(live)) if live[i] != pristine[i]]
    print('\nbytes differing: %d of %d' % (len(diffs), len(live)))

    if not diffs:
        print("""
CLEAN NEGATIVE: the live region is byte-for-byte the archive copy.

The game does not write into this region while a quote is displayed, so it
holds no index, no resolved pointer and no render buffer. Reading the displayed
quote from here is CLOSED -- not 'not yet found', closed.

NEXT per the plan: step 2b, scan the arena for the ASCII of the winner's
eight candidate quotes while a quote is on screen. A hit gives a live address
whose writer can be watched. If that also fails, the honest answer is that the
displayed quote cannot be read with the machinery available.""")
        return 0

    # Group contiguous runs: a scattered byte here and there is noise; a run is
    # a structure.
    runs, start, prev = [], diffs[0], diffs[0]
    for o in diffs[1:]:
        if o != prev + 1:
            runs.append((start, prev))
            start = o
        prev = o
    runs.append((start, prev))

    print('contiguous runs: %d\n' % len(runs))
    for a, b in runs[:40]:
        print('  +0x%04X..+0x%04X  (%d bytes)  %s' % (a, b, b - a + 1, describe(a)))
        print('     archive: %s' % ' '.join('%02X' % x for x in pristine[a:min(b + 1, a + 16)]))
        print('     live   : %s' % ' '.join('%02X' % x for x in live[a:min(b + 1, a + 16)]))
    if len(runs) > 40:
        print('  ... and %d more runs' % (len(runs) - 40))

    print("""
HOW TO READ THIS. A difference is a LEAD, not an answer.

- A run inside the 'character -> quote sub-table index' region is the most
  promising shape: the game may have written the resolved selection there.
- A run in TEXT means the region is being used as a render buffer, in which
  case the displayed quote may be readable directly -- check whether the live
  bytes spell one of the winner's eight (tools/cvs2_quotes.py --char N).
- Scattered single bytes across unrelated structures are more likely emulator
  bookkeeping than a selection.

Do NOT adopt a candidate from ONE capture. Two captures agreeing is what
made R+0x05931 look like the winner, and it named the wrong character in play.
A candidate must be checked against captures where the answers differ.""")
    return 0


if __name__ == '__main__':
    sys.exit(main())
