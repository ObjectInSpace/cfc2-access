"""Diff a SERIES of display-list captures and find what moves with the cursor.

WHY THIS EXISTS
---------------
Run 19 refuted the framebuffer: arena+0x8900000 is 99.99% 0xCDCD fill, not
ARGB1555 pixels. What it actually holds is a DISPLAY LIST -- float quad vertex
coordinates plus NAOMI guest pointers (0x0CE80010, 0x0CC00000).

A display list NAMES WHAT IS BEING DRAWN. If the character-select highlight is a
quad, its vertex coordinates give the screen position directly, and the grid
geometry is already measured. If its texture pointer changes as the cursor
moves, that pointer identifies the character outright.

⭐⭐ THIS IS A DIFF, NOT A SEARCH. Every route that failed on this project hunted
something cursor-shaped in an unknown place, and memory is full of look-alikes.
This asks a different question: across a series captured while the player moved,
WHICH WORDS CHANGED? Identify last.

WHAT WOULD MAKE A HIT TRUSTWORTHY, AND WHAT MUST NOT COUNT
----------------------------------------------------------
⛔ "It changed" is not enough. In a live render buffer, ANIMATION changes
constantly without any input -- the portrait pulses, timers tick. A field that
takes a new value every single slot is an animation, not a cursor.

⚠ So the discriminator is the SHAPE OF THE SERIES:

  - a CURSOR holds a value for several slots, then steps, then holds again --
    few distinct values, few transitions, and it DWELLS
  - an ANIMATION changes almost every slot -- many transitions, no dwell
  - a CONSTANT never changes and is invisible either way

⭐ That is the same rule that cracked the shell: watch the SERIES, not the delta.

⚠ A relational filter cannot exclude a constant, so every filter here is stated
against measured counts, and the report prints the POPULATION each filter saw.
"Found nothing" and "looked at nothing" must never read alike.
"""
import argparse
import collections
import glob
import os
import struct
import sys


def load_series(paths):
    bufs = []
    for p in paths:
        with open(p, 'rb') as f:
            bufs.append(f.read())
    if not bufs:
        return [], 0
    n = min(len(b) for b in bufs)
    if any(len(b) != n for b in bufs):
        print("    ⚠ slots differ in length; comparing the first %d bytes" % n)
    return bufs, n


FILL = (0xCDCDCDCD, 0x00000000, 0xFFFFFFFF, 0xDDDDDDDD, 0xABABABAB)


def is_guest_ptr(v):
    """NAOMI guest address: 0x0C000000-0x0D000000 is the uncached view of the
    16 MB of system RAM. Run 19 found 0x0CE80010 and 0x0CC00000 here."""
    return 0x0C000000 <= v < 0x0D000000


def as_float(v):
    return struct.unpack('<f', struct.pack('<I', v))[0]


def plausible_coord(v):
    """A vertex coordinate is a modest float. Rejects both fill and garbage."""
    try:
        f = as_float(v)
    except Exception:
        return False
    if f != f or f in (float('inf'), float('-inf')):
        return False
    return 0.0 < abs(f) < 4096.0


def classify(series):
    """Describe one word's behaviour across the series.

    Returns (n_distinct, n_transitions, max_dwell).
    """
    distinct = len(set(series))
    trans = sum(1 for i in range(1, len(series)) if series[i] != series[i - 1])
    dwell = best = 1
    for i in range(1, len(series)):
        if series[i] == series[i - 1]:
            dwell += 1
            best = max(best, dwell)
        else:
            dwell = 1
    return distinct, trans, best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='*',
                    help='cfc2_ram_dl00.bin ... (default: glob in cwd)')
    ap.add_argument('--max-report', type=int, default=40)
    args = ap.parse_args()

    paths = args.files or sorted(glob.glob('cfc2_ram_dl*.bin'))
    paths = [p for p in paths if os.path.exists(p)]
    if len(paths) < 3:
        print("⛔ need at least 3 slots to see a SERIES; found %d." % len(paths))
        print("   A single capture cannot show a change, and two cannot "
              "distinguish a step from an animation.")
        return 2

    print("=" * 70)
    print("DISPLAY-LIST SERIES: %d slot(s)" % len(paths))
    for p in paths:
        print("    %s  (%d KB)" % (os.path.basename(p), os.path.getsize(p) // 1024))
    print("=" * 70)
    print()

    bufs, n = load_series(paths)
    nw = n // 4
    cols = [struct.unpack('<%dI' % nw, b[:nw * 4]) for b in bufs]

    # ⛔⛔ THE CHECK THIS TOOL SHIPPED WITHOUT, AND THE RUN THAT EXPOSED IT.
    #
    # Run 20 captured 16 slots and this reported 16384 of 16384 words as
    # "cursor-like". Not a finding -- an INSTRUMENT FAILURE. The cause:
    #
    #     slots 00-13  byte-identical to each other
    #     slots 14-15  byte-identical to each other, every word different
    #
    # The region is REUSED (display list, then pixel data blitted over it), so
    # every word had transitions=1, dwell=14 and sailed through the dwell
    # filter. A per-field discriminator cannot describe a WHOLE-REGION SWAP,
    # and my synthetic fixture -- where the cursor was the only thing moving --
    # could never have produced one.
    #
    # ⭐ So identify the swap FIRST, at slot granularity, before looking at any
    # field. ⚠ 100% of fields passing a discriminator is always the instrument,
    # never the data.
    groups = []
    for i, b in enumerate(bufs):
        for g in groups:
            if bufs[g[0]][:n] == b[:n]:
                g.append(i)
                break
        else:
            groups.append([i])
    if len(groups) < len(bufs):
        print("--- ⚠ SLOT-LEVEL IDENTITY (checked before any field analysis)")
        for g in groups:
            print("    slots %s are BYTE-IDENTICAL"
                  % ', '.join('%02d' % i for i in g))
        print()
    if len(groups) <= 2:
        print("⛔ THE REGION WAS REPLACED, NOT EDITED -- %d distinct content(s) "
              "across %d slots." % (len(groups), len(bufs)))
        print("   Every word changes at the same instant, so NO per-field")
        print("   measurement here means anything. This is one buffer being")
        print("   reused for different data, not a cursor moving.")
        print("   ⚠ Do NOT read the field list below as candidates; there is")
        print("     no field-level signal to read.")
        return 1

    # --- population, stated before any filter ---------------------------------
    fill = sum(1 for i in range(nw) if all(c[i] in FILL for c in cols))
    const = sum(1 for i in range(nw)
                if cols[0][i] not in FILL and
                all(c[i] == cols[0][i] for c in cols))
    changing = [i for i in range(nw)
                if any(c[i] != cols[0][i] for c in cols)]

    print("--- population (%d words of %d KB examined)" % (nw, n // 1024))
    print("    %6d  fill (0xCDCD etc) in every slot" % fill)
    print("    %6d  constant, real value" % const)
    print("    %6d  CHANGED across the series" % len(changing))
    print()

    if not changing:
        print("⛔ NOTHING CHANGED in %d words across %d slots." % (nw, len(paths)))
        print("   ⚠ That is only meaningful if the player was MOVING THE CURSOR")
        print("     while these were captured. If they were, the highlight is")
        print("     not in this window and the offset is wrong -- not that the")
        print("     display list is static.")
        return 1

    # --- the discriminator ---------------------------------------------------
    rows = []
    for i in changing:
        s = [c[i] for c in cols]
        distinct, trans, dwell = classify(s)
        rows.append((i, s, distinct, trans, dwell))

    slots = len(paths)
    # ⭐ A CURSOR DWELLS. An animation changes nearly every slot. Require at
    # least one hold of 2+ slots AND fewer transitions than slots-1.
    cursorish = [r for r in rows if r[4] >= 2 and r[3] <= max(1, slots - 2)]
    animated = [r for r in rows if r not in cursorish]

    print("--- shape of the series (⭐ a CURSOR DWELLS; an ANIMATION does not)")
    print("    %6d  animation-like (changes nearly every slot)" % len(animated))
    print("    %6d  cursor-like    (holds, steps, holds)" % len(cursorish))
    print()

    if not cursorish:
        print("⛔ NO CURSOR-LIKE FIELD. %d words changed, but every one changes"
              % len(changing))
        print("   nearly every slot -- that is animation, not a selection.")
        print("   ⚠ Do NOT pick the 'best' of these. A field that changes")
        print("     constantly cannot encode a cursor that is held.")
        return 1

    # --- identify last -------------------------------------------------------
    cursorish.sort(key=lambda r: (r[3], -r[4]))
    print("--- cursor-like fields (offset, then the series)")
    for off, s, distinct, trans, dwell in cursorish[:args.max_report]:
        note = ''
        if all(is_guest_ptr(v) or v == 0 for v in s):
            note = '  ⭐⭐ GUEST POINTER (0x0C......) -- may NAME the character'
        elif all(plausible_coord(v) for v in s):
            note = '  ⭐ floats: %s' % ' '.join('%+.1f' % as_float(v)
                                                for v in s[:6])
        print("    +0x%04x  distinct=%-3d transitions=%-3d dwell=%-3d%s"
              % (off * 4, distinct, trans, dwell, note))
        print("             %s" % ' '.join('%08x' % v for v in s[:10]))

    if len(cursorish) > args.max_report:
        print("    ... and %d more (raise --max-report)"
              % (len(cursorish) - args.max_report))

    print()
    print("▶ NEXT: a field whose value steps as the player crossed characters is")
    print("  the highlight. If it is a FLOAT, map it through the grid geometry")
    print("  (5x10, 64px cells, 74px pitch, origin 180,32). If it is a GUEST")
    print("  POINTER, it may name the character directly -- better still.")
    print("  ⚠ Confirm against how many times the player ACTUALLY moved. A field")
    print("    with 3 steps in a run of 12 movements is not the cursor.")
    return 0


if __name__ == '__main__':
    sys.exit(main())
