"""
Replay a run's cursor movements against the CURRENT shipped screen table and
report what fraction of them would now speak a NAME.

⭐⭐ WHY THIS EXISTS -- the measurement that fooled me.

Counting `spoke "N of M"` lines in a log answers "what did the build that
produced this log say?". I read that number (16% named) as the state of the
project, and it was the state of a build FOUR COMMITS OLD: the run started at
02:10:34 and the commits naming the pause menu, keyboard settings, the in-game
AV screen and the move list all landed between 02:18 and 02:39. A log cannot
show the effect of code written after it.

⛔ THE TRAP IS STRUCTURAL, not a one-off slip. Every log in docs/ ages the
moment the table grows, so the naive count DRIFTS DOWNWARD away from the truth
as the project improves -- the most recently added screens are exactly the ones
the newest log was recorded before. See memory
[[verify-tools-must-audit-newest-build]] and [[benchmark-drifts-from-the-code]]:
this is the same defect, in a measurement rather than a benchmark.

⭐ So this tool reads the movements (caller + count + position, which are facts
about the GAME and do not age) and joins them to the table as it stands in
src/menu_speak.c RIGHT NOW. Re-running it after adding a row shows that row's
effect without another launch.

⚠ WHAT THIS DOES *NOT* ESTABLISH. It reports whether a row map COVERS a
position, not whether the label on it is the right one. Only a live run does
that, and every shipped row still needs MC_VERIFIED_LIVE_TEXT. A screen can be
100% covered here and speak the wrong words -- that is this project's signature
bug. Coverage is necessary, never sufficient.

Run:  python tools/coverage.py [log ...]      (default: the newest docs/run-*)
"""

import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from name_screens import load_corpus  # noqa: E402

MOVE = re.compile(r"item (?P<pos>\d+) of (?P<total>\d+).*?caller=0*(?P<c>[0-9A-Fa-f]+)")

# Kinds that name an item WITHOUT a label map -- they read the game's own live
# text or derive the name another way, so a position they cover is named even
# though `labels` is NULL.
LIVE_KINDS = {"MC_NUMBERED_LIVE", "MC_CHAR_PICKER", "MC_GAME_STRIP",
              # Named by the objects' own screen coordinates -- no map, and the
              # count check in gt_item_at_position() is what keeps it honest.
              "MC_COORD_LIVE"}

# A value axis speaks "<row>, <value>" and is driven by the row map of its
# SIBLING screen (same `name`), not by a map of its own.
VALUE_KINDS = {"MC_SETTINGS_VALUE"}


def parse_maps(src):
    """`static const int g_x[] = {...}` -> list of indices, None = unknown."""
    out = {}
    for name, body in re.findall(
            r"static const int (g_\w+)\[\]\s*=\s*\{([^}]*)\}", src):
        clean = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
        # ⚠ Keep MC_LABEL_UNKNOWN as a real element -- dropping it misaligns
        # every row after it. Same trap test_screen_maps.py documents.
        out[name] = [None if t in ("MC_LABEL_UNKNOWN", "-1") else int(t)
                     for t in re.findall(r"MC_LABEL_UNKNOWN|-?\d+", clean)]
    return out


def parse_screens(src):
    """Rows of g_screens[]: (caller, base, count, name, kind, labels_ident).

    ⭐ `base` is the ORIGINAL naming model: a screen whose items are a
    contiguous GMD run names item i as base+i and carries no map at all
    (base = -1 means "map instead"). Ignoring it made this tool report a false
    gap on the main menu -- the first screen the project ever named, and one the
    same log shows speaking "Offline Play, 1 of 6" correctly. ⚠ A coverage
    number is only as trustworthy as its model of what counts as covered.
    """
    m = re.search(r"g_screens\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        sys.exit("g_screens[] not found in src/menu_speak.c -- "
                 "did the declaration style change?")
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)

    rows = []
    for entry in re.findall(r"\{([^{}]*)\}", body):
        f = [x.strip() for x in entry.split(",")]
        if len(f) < 6 or not f[0].startswith("0x"):
            continue
        caller = int(f[0].rstrip("ULul"), 16)
        try:
            base = int(f[1])
            count = int(f[2])
        except ValueError:
            continue
        name = f[3].strip('"')
        kind = f[5]
        labels = next((x for x in f if x.startswith("g_") and "row" in x), None)
        rows.append((caller, base, count, name, kind, labels))
    return rows


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

    logs = sys.argv[1:]
    if not logs:
        cand = sorted(glob.glob(os.path.join(ROOT, "docs", "run-*.log")))
        if not cand:
            sys.exit("no docs/run-*.log found and none given on the command line")
        logs = [cand[-1]]

    src = open(os.path.join(ROOT, "src", "menu_speak.c"), encoding="utf-8").read()
    maps = parse_maps(src)
    screens = parse_screens(src)
    table = load_corpus()
    labels = list(table.keys())

    # name -> the row map of whichever sibling screen carries one. A value axis
    # borrows it, exactly as menu_speak_sibling_row() does at runtime.
    sibling = {}
    for _, _, _, nm, _, lab in screens:
        if lab and nm not in sibling:
            sibling[nm] = lab

    print("screen table: %d rows, %d label maps" % (len(screens), len(maps)))
    print()

    total = named = 0
    gaps = {}
    for path in logs:
        for line in open(path, encoding="utf-8", errors="replace"):
            mv = MOVE.search(line)
            if not mv:
                continue
            caller = int(mv.group("c"), 16)
            count = int(mv.group("total"))
            pos = int(mv.group("pos"))
            total += 1

            # menu_speak_lookup_n: caller AND count, with count 0 = any.
            row = next((r for r in screens
                        if r[0] == caller and r[2] in (count, 0)), None)
            if row is None:
                gaps.setdefault(("unnamed caller", caller, count), 0)
                gaps[("unnamed caller", caller, count)] += 1
                continue

            _, base, _, nm, kind, lab = row
            if kind in LIVE_KINDS:
                named += 1
                continue
            # A contiguous run: item i is base+i, no map involved.
            if base >= 0 and 0 <= base + pos - 1 < len(labels):
                named += 1
                continue
            if kind in VALUE_KINDS:
                lab = lab or sibling.get(nm)
            if not lab or lab not in maps:
                gaps.setdefault(("no label map", caller, count), 0)
                gaps[("no label map", caller, count)] += 1
                continue

            idxs = maps[lab]
            if pos - 1 < len(idxs) and idxs[pos - 1] is not None:
                named += 1
            else:
                gaps.setdefault(("row unestablished", caller, count), 0)
                gaps[("row unestablished", caller, count)] += 1

    if not total:
        sys.exit("no cursor movements found in %s" % ", ".join(logs))

    print("%d movements: %d would speak a NAME (%d%%), %d would not"
          % (total, named, 100 * named // total, total - named))

    if gaps:
        print()
        print("what is left, by cause:")
        for (why, caller, count), n in sorted(gaps.items(), key=lambda x: -x[1]):
            print("   %-18s 0x%x (%d items): %d movements" % (why, caller, count, n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
