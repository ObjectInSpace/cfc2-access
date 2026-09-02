"""
Tests for the GUI layout reader and the focus-state classifier.

The classifier is the thing a runtime hook will depend on, and getting it
wrong is not loud: announcing SELECTED_UNFOCUS as a selection would make the
screen reader say the wrong item at the moment focus LEAVES it, which reads as
"the mod is just buggy" rather than as a specific defect. So the vocabulary is
pinned here against the shipped data.

Usage:  python tools/test_gui.py
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gui import iter_layouts, classify_state, NON_PLAIN_STATES  # noqa: E402


def main():
    fails = []

    def check(name, cond, detail=""):
        print("  %-52s %s" % (name, "PASS" if cond else "FAIL"))
        if not cond:
            fails.append(name + (" -- " + detail if detail else ""))

    print("GUI reader / focus classifier tests")
    print("-" * 62)

    layouts = list(iter_layouts())
    check("all 25 layouts parse", len(layouts) == 25,
          "got %d" % len(layouts))

    interactive = [l for l in layouts if l.is_interactive]
    check("13 layouts are interactive", len(interactive) == 13,
          "got %d" % len(interactive))

    # Every interactive layout must expose at least one 'select' state,
    # otherwise focus detection has nothing to hook there.
    for lay in interactive:
        sel = [n for n in lay.names if classify_state(n) == "select"]
        if not sel:
            fails.append("%s has no select state" % lay.name)
    check("every interactive layout has a select state",
          not any("has no select state" in f for f in fails))

    # THE REGRESSION THIS FILE EXISTS FOR: a prefix test would classify these
    # as selections. They must not be.
    must_not_select = ["SELECTED_UNFOCUS", "SELECTED_DEFAULT", "DISABLE_SELECT"]
    for n in must_not_select:
        check("NEGATIVE: %s is not a selection" % n,
              classify_state(n) != "select",
              "classified as %r" % classify_state(n))

    # A naive prefix test must actually differ from ours -- proving the test
    # above is testing something real rather than passing vacuously.
    naive = [n for n in must_not_select if n.startswith("SELECT")]
    check("a naive SELECT-prefix test WOULD have been wrong",
          len(naive) > 0, "no prefix-colliding names found")

    # The typo'd name must be handled: F0CUS with a digit zero.
    check("SELECTED_F0CUS (digit zero) is a selection",
          classify_state("SELECTED_F0CUS") == "select")
    check("the typo is real: FOCUS spelling is absent from the data",
          not any("SELECTED_FOCUS" in l.names for l in layouts))

    # Plain numbered variants all classify the same as their base.
    for base in ("DEFAULT", "SELECT", "DECIDE", "DISABLE"):
        ok = all(classify_state(base + s) == base.lower()
                 for s in ("", "2", "3", "4", "5", "6", "7"))
        check("%s2..7 classify as %s" % (base, base.lower()), ok)

    # Every state-prefixed name in the shipped data is accounted for: either
    # it matches the plain pattern or it is in the surveyed exception list.
    unknown = set()
    for lay in layouts:
        for n in lay.names:
            if not n.startswith(("DEFAULT", "SELECT", "DECIDE", "DISABLE")):
                continue
            if re.fullmatch(r"(DEFAULT|SELECT|DECIDE|DISABLE)\d*", n):
                continue
            if n not in NON_PLAIN_STATES:
                unknown.add(n)
    check("no unclassified state-prefixed names remain",
          not unknown, "unknown: %s" % sorted(unknown))

    # --- Anchors: every layout must be identifiable at runtime -------------
    # A layout with no anchor is a screen that can never announce itself, and
    # that must be a visible failure rather than a quiet omission.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from gen_gui_probes import build  # noqa: E402
    _text, rows, _states, _decoys = build(os.environ.get(
        "CFC2_GAME_DIR",
        r"C:\Program Files (x86)\Steam\steamapps\common"
        r"\CAPCOM FIGHTING COLLECTION 2"))

    unidentifiable = [r[0] for r in rows if not r[1] and not r[4]]
    check("every layout has an anchor or a discriminator",
          not unidentifiable, "stuck: %s" % unidentifiable)

    inter_stuck = [r[0] for r in rows if r[2] and not r[1] and not r[4]]
    check("every INTERACTIVE layout is identifiable",
          not inter_stuck, "stuck: %s" % inter_stuck)

    # Anchors must be long enough not to collide with arbitrary memory. The
    # pool is found by scanning printable runs, so short fragments like "D"
    # are binary noise from the pool edges, not real names.
    short = [(r[0], r[1]) for r in rows if r[1] and len(r[1]) < 8]
    check("no anchor is shorter than 8 chars", not short, "%s" % short)

    # Each anchor must genuinely be unique across all layouts.
    from collections import Counter
    freq = Counter()
    for lay in layouts:
        for n in set(lay.names):
            freq[n] += 1
    bad = [(r[0], r[1]) for r in rows if r[1] and freq[r[1]] != 1]
    check("every primary anchor is unique across layouts", not bad, "%s" % bad)

    print("-" * 62)
    if fails:
        print("%d FAILURES:" % len(fails))
        for f in fails:
            print("  - %s" % f)
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
