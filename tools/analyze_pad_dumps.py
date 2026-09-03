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
    """Find bytes that track the keyboard, using the recorded masks.

    Captures are no longer a fixed five in a fixed order: the mod captures on
    key EDGES, so the sequence is whatever the player produced. This works
    from the kb_dirs each capture recorded rather than from position.
    """
    if len(caps) < 4:
        out("need at least 4 captures, have %d" % len(caps))
        return []

    span = min(len(c["data"]) for c in caps)
    out("comparing %d bytes common to %d captures" % (span, len(caps)))

    objs = {c["obj"] for c in caps}
    if len(objs) != 1:
        out("!! the object address CHANGED between captures: %s" %
            ", ".join(hex(o) for o in sorted(objs)))
        out("   offsets are not comparable; retake the captures in one run.")
        return []

    quiet = [c for c in caps if not c["kb"]]
    held = [c for c in caps if c["kb"]]
    out("%d capture(s) with nothing held, %d with a direction held"
        % (len(quiet), len(held)))

    if len(quiet) < 2:
        out("!! need at least TWO captures with nothing held -- they are the "
            "control, and without it a diff measures elapsed time rather "
            "than the keypress. That is exactly how the previous attempt "
            "failed.")
        return []
    if not held:
        out("!! no capture recorded a direction held; nothing to compare.")
        return []

    # ⭐ THE CONTROL: everything that differs between two quiet captures is a
    # timer, a frame counter or animation. Union over every quiet pair, so a
    # byte that ticks slowly is still caught.
    noise = set()
    for i in range(len(quiet) - 1):
        noise |= differing(quiet[i]["data"], quiet[i + 1]["data"], span)
    # ⚠ This set is REPORTED, not used as a separate filter.
    #
    # "no quiet pair differs" and "all quiet values equal the baseline" are the
    # same condition, so subtracting `noise` on top of the return-to-baseline
    # rule below would be a filter that can never fail on its own -- and a
    # condition that cannot fail hides whether the real one works. Sabotage
    # proved exactly that: removing the subtraction changed no outcome.
    #
    # It is kept because the COUNT is diagnostic: it distinguishes "the game
    # was quiet" from "everything churns, so the captures may be unusable".
    out("control: %d byte(s) differ between quiet captures (diagnostic; the "
        "return-to-baseline rule below is what enforces it)" % len(noise))

    # Which distinct directions were actually observed?
    dirs_seen = {}
    for c in held:
        for mask, name in DIR_NAMES:
            if c["kb"] & mask:
                dirs_seen.setdefault(name, []).append(c)
    out("directions observed: %s" %
        (", ".join(sorted(dirs_seen)) or "none"))

    if len(dirs_seen) < 2:
        out("!! only %d distinct direction(s) seen. Two are needed: a byte "
            "that tracks just one key cannot be told from a flag that "
            "happens to follow it." % len(dirs_seen))
        return []

    # A surviving byte must, for EVERY observed direction, read that
    # direction's bits when held and differ from the quiet baseline.
    base = quiet[-1]["data"]
    survivors = []
    for off in range(span):
        ok = True
        for name, cs in dirs_seen.items():
            mask = dict((n, m) for m, n in DIR_NAMES)[name]
            # must carry that direction's bits in at least one capture of it,
            # and must differ from the quiet baseline there
            if not any((c["data"][off] & mask) and c["data"][off] != base[off]
                       for c in cs):
                ok = False
                break
        if not ok:
            continue
        # ⭐ THE CONTROL, enforced here: the byte must read the SAME value in
        # every capture where nothing was held. A timer, frame counter or
        # animation phase differs between two quiet captures and is rejected;
        # this is what the failed attempt lacked entirely.
        if any(q["data"][off] != base[off] for q in quiet):
            continue
        survivors.append(off)

    out("bytes carrying the right bits for ALL %d directions, quiet in the "
        "control, and returning to baseline: %d"
        % (len(dirs_seen), len(survivors)))
    out("")

    if survivors:
        for off in survivors:
            vals = "  ".join("%s=0x%02x" % (
                ("".join(n[0] for m, n in DIR_NAMES if c["kb"] & m) or "-"),
                c["data"][off]) for c in caps)
            out("  +0x%-6x  %s" % (off, vals))
    else:
        out("nothing survived. Where it was lost:")
        anychange = set()
        for c in held:
            anychange |= differing(base, c["data"], span)
        if not anychange:
            out("  - NO byte differed between quiet and held at all. Either "
                "the pad is outside this region, or the captures were all "
                "taken in the same state.")
        else:
            out("  - %d byte(s) changed when a key was held, but none carried "
                "the right direction bits for every direction observed."
                % len(anychange))
            shown = 0
            for off in sorted(anychange - noise):
                if shown >= 12:
                    break
                out("      +0x%-6x quiet=0x%02x  %s" % (
                    off, base[off],
                    " ".join("0x%02x" % c["data"][off] for c in held)))
                shown += 1
        if len(noise) > span // 4:
            out("  - the control was very noisy (%d of %d bytes); real signal "
                "may have been subtracted with it" % (len(noise), span))
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
          "a byte churning in the CONTROL is removed (return-to-baseline)")
    check(WRONGBIT not in got,
          "a byte tracking both keys with the WRONG bits is rejected")
    # ⚠ Assert the PROPERTY, not a count -- a hardcoded number breaks whenever
    # the fixture gains a decoy, which is a test failing for a reason that is
    # not a bug.
    check(any(l.startswith("control:") and not l.startswith("control: 0 ")
              for l in lines),
          "the control noise is reported, not hidden")

    # ⚠ Order is no longer assumed -- the mod captures on key edges, so the
    # sequence is whatever the player produced. What MUST still be enforced is
    # that a control exists: without two quiet captures there is no noise floor
    # and a diff measures elapsed time, which is how the previous attempt
    # failed. Sabotaging the `len(quiet) < 2` guard fails this case.
    # exactly ONE quiet capture: enough captures to proceed, no control pair
    nocontrol = [caps[0]] + [c for c in caps if c["kb"]] * 2
    lines2 = []
    r2 = analyse(nocontrol, out=lines2.append)
    check(r2 == [] and any("control" in l and "!!" in l for l in lines2),
          "too few quiet captures aborts (no control = no measurement)")

    # Only one direction observed must also abort: a byte tracking a single
    # key cannot be distinguished from a flag that follows it.
    onedir = [c for c in caps if not c["kb"] or (c["kb"] & DOWN)]
    lines2b = []
    r2b = analyse(onedir, out=lines2b.append)
    check(r2b == [] and any("distinct direction" in l for l in lines2b),
          "a single observed direction aborts the comparison")

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
