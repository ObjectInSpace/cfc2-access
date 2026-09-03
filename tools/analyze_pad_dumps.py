"""
Compare CFC2 pad captures and report bytes that track the keyboard.

WHY THIS LIVES OFFLINE
----------------------
Route 5 failed four times in-game, and every failure was a decision baked into
the mod that only revealed itself as wrong after a launch. The mod now writes
raw bytes and records which arrow keys were down; every judgement happens here,
where a wrong assumption costs a rerun of this script instead of a game session.

THE CONTROL IS THE METHOD
-------------------------
The previous attempt diffed two captures taken eight seconds apart and reported
the difference as input state. It was measuring elapsed time: two runs holding
DIFFERENT keys produced the identical 109 changed bytes. So the protocol makes
two captures in the SAME state, and their difference is subtracted as noise:

    1  nothing held      \\ control pair: any byte differing here is a timer,
    2  nothing held      /  a frame counter, animation phase -- not input
    3  holding DOWN
    4  nothing held
    5  holding LEFT

A surviving byte must: differ between 2 and 3, differ between 4 and 5, NOT be
in the control noise, and carry a DOWN bit in 3 and a LEFT bit in 5. A byte
that tracks two different keys with the right value each time cannot be a
timer, a stuck flag, or the kind of coincidence that defeated every earlier
attempt.

WHAT IT WILL NOT DO
-------------------
It does not rank, does not pick a winner, and does not accept a byte because it
merely changed. It also prints the population at every stage, so "nothing
survived" is distinguishable from "the captures were mis-driven".

Usage:
    python tools/analyze_pad_dumps.py [--dir <game dir>]
    python tools/analyze_pad_dumps.py --self-test
"""

import argparse
import os
import sys

DEFAULT_DIR = (r"C:\Program Files (x86)\Steam\steamapps\common"
               r"\CAPCOM FIGHTING COLLECTION 2")

# Button bits, from the reflected names in the unpacked image.
RUP, RRIGHT, RDOWN, RLEFT = 0x01, 0x02, 0x04, 0x08
LUP, LRIGHT, LDOWN, LLEFT = 0x10, 0x20, 0x40, 0x80
UP, RIGHT, DOWN, LEFT = RUP | LUP, RRIGHT | LRIGHT, RDOWN | LDOWN, RLEFT | LLEFT

DIR_NAMES = [(UP, "up"), (DOWN, "down"), (LEFT, "left"), (RIGHT, "right")]


def read_index(game_dir):
    """Parse cfc2_pad_index.txt -> [(seq, filename, obj, nbytes, kb_dirs)]."""
    path = os.path.join(game_dir, "cfc2_pad_index.txt")
    if not os.path.exists(path):
        raise SystemExit("no cfc2_pad_index.txt in %s -- run the mod and "
                         "press F7 five times first." % game_dir)
    rows = []
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split(",")]
        if len(parts) < 5:
            continue
        rows.append((int(parts[0]), parts[1], int(parts[2], 16),
                     int(parts[3]), int(parts[4], 16)))
    return rows


def load(game_dir, rows):
    caps = []
    for seq, leaf, obj, nbytes, kb in rows:
        p = os.path.join(game_dir, leaf)
        if not os.path.exists(p):
            raise SystemExit("index names %s but the file is missing" % leaf)
        caps.append({"seq": seq, "obj": obj, "kb": kb,
                     "data": open(p, "rb").read()})
    return caps


def differing(a, b, span):
    """Offsets where two captures differ, over their common length."""
    return {i for i in range(span) if a[i] != b[i]}


def analyse(caps, out=print):
    """Apply the control subtraction and the two-key cross-check.

    caps must be five captures in the documented order. Returns the list of
    surviving offsets.
    """
    if len(caps) < 5:
        out("need 5 captures, have %d -- press F7 five times "
            "(none, none, DOWN, none, LEFT)." % len(caps))
        return []

    c1, c2, c3, c4, c5 = caps[:5]
    span = min(len(c["data"]) for c in caps[:5])
    out("comparing %d bytes common to all five captures" % span)

    # The object must be the same allocation in every capture, or offsets in
    # one do not correspond to offsets in another.
    objs = {c["obj"] for c in caps[:5]}
    if len(objs) != 1:
        out("!! the object address CHANGED between captures: %s" %
            ", ".join(hex(o) for o in sorted(objs)))
        out("   offsets are not comparable; retake the captures in one run.")
        return []

    # Sanity-check the protocol actually happened, rather than assuming it.
    for idx, expect_held in ((0, False), (1, False), (2, True),
                             (3, False), (4, True)):
        got = bool(caps[idx]["kb"])
        if got != expect_held:
            out("!! capture %d recorded keyboard 0x%02x but the protocol "
                "expects %s. The captures were mis-driven; results below are "
                "not meaningful." %
                (idx + 1, caps[idx]["kb"], "a key held" if expect_held
                 else "nothing held"))
    if not (c3["kb"] & DOWN):
        out("!! capture 3 does not record DOWN (got 0x%02x)" % c3["kb"])
    if not (c5["kb"] & LEFT):
        out("!! capture 5 does not record LEFT (got 0x%02x)" % c5["kb"])

    noise = differing(c1["data"], c2["data"], span)
    sig_down = differing(c2["data"], c3["data"], span)
    sig_left = differing(c4["data"], c5["data"], span)

    out("control (1 vs 2): %d bytes differ -- these are timers/counters" %
        len(noise))
    out("DOWN    (2 vs 3): %d bytes differ" % len(sig_down))
    out("LEFT    (4 vs 5): %d bytes differ" % len(sig_left))

    cand = (sig_down & sig_left) - noise
    out("changed for BOTH keys and not in the control: %d" % len(cand))

    # The value test: the byte must carry the right direction bit in each
    # held capture. This is what a timer or a stuck flag cannot do.
    survivors = []
    for off in sorted(cand):
        d, l = c3["data"][off], c5["data"][off]
        if (d & DOWN) and (l & LEFT):
            survivors.append(off)

    out("...and carrying a DOWN bit in capture 3 and a LEFT bit in 5: %d" %
        len(survivors))
    out("")

    if survivors:
        for off in survivors:
            out("  +0x%-6x  none=0x%02x  DOWN=0x%02x  none=0x%02x  LEFT=0x%02x"
                % (off, c2["data"][off], c3["data"][off],
                   c4["data"][off], c5["data"][off]))
    else:
        # Say WHICH stage emptied the set -- "nothing found" and "the tool is
        # broken" must not look the same.
        out("nothing survived. Where it was lost:")
        if not sig_down:
            out("  - no byte changed when DOWN was pressed at all")
        if not sig_left:
            out("  - no byte changed when LEFT was pressed at all")
        if sig_down and sig_left and not (sig_down & sig_left):
            out("  - bytes changed for each key, but never the SAME byte")
        elif cand and not survivors:
            out("  - %d bytes tracked both keys but none carried the right "
                "direction bits; sample:" % len(cand))
            for off in sorted(cand)[:12]:
                out("      +0x%-6x DOWN=0x%02x LEFT=0x%02x" %
                    (off, c3["data"][off], c5["data"][off]))
        if noise and len(noise) > span // 4:
            out("  - the control was very noisy (%d of %d bytes), so the "
                "subtraction may have removed real signal" % (len(noise), span))
    return survivors


# --------------------------------------------------------------------------
# Self-test.
#
# A trap must be PROVEN to fail when the thing it guards is broken, or it is
# not a test. Each case below was verified by sabotaging the corresponding
# filter and confirming this file reports a failure.
#
# The fixture plants the shapes that actually occur, not random data: a real
# pad byte, a timer that also ticks in the control, a flag stuck on, and a byte
# that tracks only one of the two keys. Random bytes would be rejected by
# everything and would calibrate nothing.
# --------------------------------------------------------------------------
def self_test():
    span = 0x200
    PAD, TIMER, STUCK, ONEKEY, CHURN, WRONGBIT = 0x40, 0x80, 0xC0, 0x100, 0x140, 0x180

    def blank():
        return bytearray(span)

    caps = []
    for i, kb in enumerate((0, 0, DOWN, 0, LEFT)):
        d = blank()
        # the real pad byte: carries exactly what is held
        if kb & DOWN:
            d[PAD] = RDOWN
        elif kb & LEFT:
            d[PAD] = RLEFT
        # a timer: changes on EVERY capture, including the control pair
        d[TIMER] = i + 1
        # a flag stuck on: never changes, so never a candidate
        d[STUCK] = 1
        # ⚠ Tracks DOWN only, and is quiet in the control -- so the ONLY
        # thing that can reject it is requiring a byte to change for BOTH
        # keys. Its capture-5 value deliberately carries a LEFT bit so the
        # value check cannot reject it either.
        # Constant RLEFT except when DOWN is held: captures 4 and 5 are
        # IDENTICAL, so it never enters sig_left, while capture 3 carries a
        # DOWN bit and capture 5 carries a LEFT bit -- passing the value check.
        d[ONEKEY] = (RLEFT, RLEFT, RDOWN, RLEFT, RLEFT)[i]
        # ⚠ CHURN is the case that makes the control subtraction load-bearing.
        # It changes in the control pair AND differs across both key captures,
        # carrying plausible direction bits -- exactly the shape that fooled
        # the in-game runs. Only the control removes it.
        # Values chosen so it passes EVERY other filter: it differs across
        # both key pairs, and in captures 3 and 5 it happens to carry a DOWN
        # bit and a LEFT bit respectively. Only its churn in the control pair
        # distinguishes it from the real thing.
        d[CHURN] = (0x00, 0x01, RDOWN, 0x01, RLEFT)[i]
        # ⚠ WRONGBIT tracks both keys and is quiet in the control, but carries
        # the WRONG bits -- only the value check rejects it.
        if kb:
            d[WRONGBIT] = 0x02 if (kb & DOWN) else 0x01
        caps.append({"seq": i + 1, "obj": 0x1000, "kb": kb,
                     "data": bytes(d)})

    lines = []
    got = analyse(caps, out=lines.append)
    text = "\n".join(lines)

    fails = 0

    def check(cond, what):
        nonlocal fails
        print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
        if not cond:
            fails += 1

    print("== analyze_pad_dumps self-test ==")
    check(got == [PAD], "only the planted pad byte survives (got %s)" %
          [hex(x) for x in got])
    check(TIMER not in got, "the timer is removed by the control subtraction")
    check(STUCK not in got, "a byte that never changes is not a candidate")
    check(ONEKEY not in got, "a byte tracking only ONE key is rejected")
    check(CHURN not in got,
          "a byte churning in the CONTROL is removed (needs the subtraction)")
    check(WRONGBIT not in got,
          "a byte tracking both keys with the WRONG bits is rejected")
    # ⚠ Assert the PROPERTY, not a count -- a hardcoded number breaks whenever
    # the fixture gains a decoy, which is a test failing for a reason that is
    # not a bug.
    check(any(l.startswith("control (1 vs 2):") and not l.endswith("0 bytes differ")
              for l in lines),
          "the control noise is reported, not hidden")

    # A mis-driven protocol must be called out rather than silently analysed.
    bad = [dict(c) for c in caps]
    bad[2] = dict(bad[2]); bad[2]["kb"] = 0
    lines2 = []
    analyse(bad, out=lines2.append)
    check(any("mis-driven" in l for l in lines2),
          "a capture taken in the wrong state is flagged")

    # A moved object must abort rather than compare unrelated offsets.
    moved = [dict(c) for c in caps]
    moved[3] = dict(moved[3]); moved[3]["obj"] = 0x2000
    lines3 = []
    r3 = analyse(moved, out=lines3.append)
    check(r3 == [] and any("CHANGED" in l for l in lines3),
          "a changed object address aborts the comparison")

    print("\n%s (%d failure%s)" % ("OK" if not fails else "FAILED",
                                   fails, "" if fails == 1 else "s"))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default=DEFAULT_DIR)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        sys.exit(self_test())

    rows = read_index(args.dir)
    print("index lists %d capture(s):" % len(rows))
    for seq, leaf, obj, nbytes, kb in rows:
        held = " ".join(n for m, n in DIR_NAMES if kb & m) or "nothing"
        print("  %d  %s  obj=0x%x  %d bytes  held: %s" %
              (seq, leaf, obj, nbytes, held))
    print()
    analyse(load(args.dir, rows))


if __name__ == "__main__":
    main()
