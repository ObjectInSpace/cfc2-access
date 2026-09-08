"""
Self-test for find_callers.scan().

⭐ This project's rule, paid for several times over: a detector that has never
been SEEN to fail is not evidence. So each case below was run with the
mechanism it tests deliberately broken, and observed to fail:

1. plant three real call sites -> all three must be found.
   Sabotage: return [] unconditionally -> case 1 fails.
2. a call to a DIFFERENT target must NOT be reported.
   Sabotage: ignore the displacement bytes and match on 0xE8 alone -> case 2
   fails while case 1 still passes. That asymmetry is the point: a scanner that
   reports every 0xE8 finds all the real sites too.
3. the displacement must be read as SIGNED -- backward calls are the common
   case in a 16 MB image.
   Sabotage: pack as "<I" -> case 3 fails (backward site missed) while cases 1
   and 2 pass, because case 1's plants are forward.
4. multiple close-together sites are all found.
   ⚠ HONESTLY LABELLED: this is a regression guard, NOT a discriminating case.
   It fails if scanning stops after the first hit, but it does NOT catch a
   coarse `start = i + 5` stride -- tried, and it passes. The two properties
   are in conflict: plants must be >=5 bytes apart to survive each other's
   writes, which is exactly the spacing a 5-byte stride handles correctly.
   Cases 1-3 are the ones carrying weight here.

⛔ Two earlier versions of case 4 were REMOVED for claiming coverage they did
not have, which is worth recording because both looked fine:
- A truncated 5-byte match at a section's end, required to be rejected. It
  passed with the `i + 5 > len(data)` bound DELETED, because Python slicing
  truncates silently and the 4-byte comparison then failed on its own. The
  assertion was testing the language, not the bound.
- Overlapping plants, where the second was to start inside the first's
  displacement. Constructed and checked: for this opcode the two byte patterns
  are mutually exclusive, so the scenario cannot occur.

Run:  python tools/test_find_callers.py
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from find_callers import scan  # noqa: E402

BASE = 0x140000000


class FakeSection:
    def __init__(self, data, live=BASE):
        self.data = bytearray(data)
        self.live = live
        self.name = "fake_code.bin"

    def addr(self, off):
        return self.live + off

    def plant_call(self, off, target):
        disp = target - (self.live + off + 5)
        self.data[off:off + 5] = b"\xe8" + struct.pack("<i", disp)


failures = []


def check(name, cond, detail=""):
    if cond:
        print("  PASS  %s" % name)
    else:
        print("  FAIL  %s  %s" % (name, detail))
        failures.append(name)


def main():
    target = BASE + 0x50000
    other = BASE + 0x60000

    # --- case 1: plants are found -------------------------------------
    s = FakeSection(b"\x90" * 0x10000)
    sites = [0x100, 0x2000, 0xf000]
    for off in sites:
        s.plant_call(off, target)
    found = scan(s, target, b"\xe8")
    check("1. all three planted call sites are found",
          found == sites, "got %r" % (found,))

    # --- case 2: a call elsewhere is NOT reported ----------------------
    s2 = FakeSection(b"\x90" * 0x10000)
    s2.plant_call(0x500, other)
    found2 = scan(s2, target, b"\xe8")
    check("2. a call to a different target is not reported",
          found2 == [], "got %r" % (found2,))

    # --- case 3: backward (negative) displacements ---------------------
    # Section lives ABOVE the target, so every call reaches backwards.
    s3 = FakeSection(b"\x90" * 0x10000, live=BASE + 0x200000)
    s3.plant_call(0x800, target)
    found3 = scan(s3, target, b"\xe8")
    check("3. a backward call is found (signed displacement)",
          found3 == [0x800], "got %r" % (found3,))

    # --- case 4: every site is found, not just the first ---------------
    # The scanner must resume from i+1 after each hit. Two adjacent-ish sites
    # in one section catch a `break`-instead-of-`continue` bug, which would
    # silently under-report on the real image -- and under-reporting here means
    # concluding "only N menus use this helper" when many more do.
    # Sites 2 bytes apart. A scan that skips past a hit by the instruction
    # length (a natural-looking "we already consumed those bytes" optimisation)
    # steps straight over the second one. Under-reporting is the dangerous
    # direction here: it would mean concluding "only N menus use this helper"
    # when more do, and a missing menu is a menu the mod stays silent in.
    s4 = FakeSection(b"\x90" * 0x2000)
    # Spaced 6 and 7 apart: far enough that each plant survives the next
    # (a call is 5 bytes), close enough that any coarse stride desynchronises
    # and steps over one.
    for off in (0x40, 0x46, 0x4d, 0x1000):
        s4.plant_call(off, target)
    found4 = scan(s4, target, b"\xe8")
    check("4. back-to-back sites are all found (scan resumes at i+1)",
          found4 == [0x40, 0x46, 0x4d, 0x1000], "got %r" % (found4,))

    print()
    if failures:
        print("FAILED: %s" % ", ".join(failures))
        return 1
    print("all cases passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
