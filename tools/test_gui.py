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
