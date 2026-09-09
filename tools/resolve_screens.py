"""
Resolve unnamed screens from a run's log, by INTERSECTING their positions.

⭐⭐ WHY THIS EXISTS
The log already records every movement: `mc: ... (item 4 of 6 ...) caller=...`
gives the exact index and count each time the user moves. So the set of
positions visited on each screen is known for free -- it does not need a
special discovery mode, and it never did.

A candidate base is consistent with a screen only if it produces a plausible
menu item at EVERY position observed there. One position cannot discriminate
(base 250 says "Resume", base 587 says "Original (4:3)" -- both fit a single
reading); several positions usually can. This intersects them.

⛔ WHAT IT DOES NOT DO
It does not name anything. It narrows the shortlist using evidence already
collected, and prints what each survivor would say at each observed position so
a person can match it against the screen. Every shipped g_screens row still
needs MC_VERIFIED_LIVE_TEXT.

Usage:
    python tools/resolve_screens.py [path-to-CFC2Access.log]
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmd import iter_gmds, parse_gmd  # noqa: E402

GAME = (r"C:\Program Files (x86)\Steam\steamapps\common"
        r"\CAPCOM FIGHTING COLLECTION 2")
DEFAULT_LOG = os.path.join(GAME, "CFC2Access.log")
ARC = os.path.join(GAME, "nativeDX11x64", "arc", "pc", "msg.arc")

# Callers already named in src/menu_speak.c -- skip them.
NAMED = {
    0x140097A59, 0x1400A20FC, 0x140099B23,
    0x1400B1B69, 0x1400B1BD9, 0x1400A1757,
}

MOVE = re.compile(
    r"mc: \w+\s+cursor \d+ -> \d+\s+\(item (\d+) of (\d+),.*?caller=([0-9A-Fa-f]+)")


def load_labels():
    sep = chr(92)
    for name, raw in iter_gmds(ARC):
        if name.split(sep)[-1] == "menu_eng":
            _n, _lang, d = parse_gmd(raw)
            return [v for _k, v in d.items()]
    raise SystemExit("menu_eng not found in msg.arc")


def is_sentence(v):
    return bool(v) and v[-1] in ".?!"


def plausible_item(v):
    """Could this string be a menu item? Same rule the in-game sweep uses."""
    return bool(v) and not is_sentence(v) and len(v) <= 32 and v[0] != "<"


def candidates(labels, count, cap=None):
    """Bases whose `count` labels all look like items, with a blurb after.

    ⚠ Two tiers, exactly as the shipped sweep does it: a full parallel HELP run
    first, and if that leaves fewer than two, one trailing sentence. Strict
    alone has been measured to DELETE the answer -- for a 9-item screen it left
    only base 248 ("Heavy Kick"), while the true run was among the 76 it threw
    away.
    """
    n = len(labels)
    for need_all in (True, False):
        out = []
        for b in range(n if cap is None else min(cap, n)):
            if b + count >= n:
                break
            if not all(plausible_item(labels[b + i]) for i in range(count)):
                continue
            need = count if need_all else 1
            if not all(b + count + i < n and is_sentence(labels[b + count + i])
                       for i in range(need)):
                continue
            out.append(b)
        if len(out) >= 2 or not need_all:
            return out, ("strict" if need_all else "relaxed")
    return [], "none"


def main():
    log = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_LOG
    if not os.path.isfile(log):
        sys.exit("log not found: %s" % log)

    labels = load_labels()

    # caller -> (count, {positions})
    screens = {}
    for line in open(log, encoding="utf-8", errors="replace"):
        m = MOVE.search(line)
        if not m:
            continue
        pos, count, caller = int(m.group(1)), int(m.group(2)), int(m.group(3), 16)
        entry = screens.setdefault(caller, [count, set()])
        # ⚠ A caller whose count CHANGES between sightings is a value row of a
        # settings screen, not a menu -- flag it rather than intersect nonsense.
        if entry[0] != count:
            entry[0] = -1
        entry[1].add(pos)

    print("screens seen in this log: %d" % len(screens))
    todo = [(c, v) for c, v in screens.items() if c not in NAMED]
    print("of those, not yet named: %d\n" % len(todo))

    for caller, (count, positions) in sorted(todo):
        if count == -1:
            print("%#x: item count VARIES between sightings -- a settings "
                  "value row, not a flat menu. Skipping.\n" % caller)
            continue
        cands, tier = candidates(labels, count)
        print("%#x  %d items, positions seen %s  [%s: %d candidate(s)]"
              % (caller, count, sorted(positions), tier, len(cands)))
        if not cands:
            print("   no candidate run fits.\n")
            continue
        # ⭐ Print each survivor across EVERY observed position. That side-by-
        # side is what a single reading cannot give.
        for b in cands[:10]:
            cells = ["%d:%s" % (p, labels[b + p - 1]) for p in sorted(positions)]
            print("   base %-4d %s" % (b, "   ".join(cells)))
        print()


if __name__ == "__main__":
    main()
