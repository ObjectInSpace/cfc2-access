"""
Identify shell screens by the GUI LAYOUT each one loads.

⭐⭐ WHY THIS IS BETTER THAN CANDIDATE LABEL RUNS
Naming a screen from its label run is a shortlist: many runs in the GMD have
the same shape, so structure alone cannot pick one, and every screen ended up
costing the user a confirmation.

But every menu layout is named in the image as a plain string --
`ui\\1_menu\\12_mainMenu\\main_menu_2`, `ui\\1_menu\\13_gameSelect\\game_select_2`,
and so on. The function that loads a layout references that string with a
RIP-relative `lea`. So: find the lea, find its enclosing function, and the
screen names itself. That is a fact about the program, not a guess.

⚠ VALIDATED AGAINST A KNOWN SCREEN. The cursor site 0x140097a59 -- the main
menu, established independently from live text -- resolves to
`12_mainMenu/main_menu_2`, and 0x1400b1bd9 (Select Game) to
`13_gameSelect/game_select_2`. A mapping that failed those would be worthless.

⛔ WHAT IT DOES NOT GIVE YOU
The layout name identifies the SCREEN, never its item labels. Knowing a screen
is `11_option/option_2` says it is an options screen; it does not say which GMD
labels it draws or in what order. Every g_screens row still needs live text.

⚠ COVERAGE IS PARTIAL, BY CONSTRUCTION. Only ~7 of the 68 cursor call sites sit
in a function that loads a layout directly; the rest are loaded by a parent or
through a vtable. This is a strong signal where it fires, not a complete map --
and a screen it cannot place is not thereby unidentifiable, just not
identifiable this way.

⛔⛔ A WIDENING THAT WAS TRIED AND REJECTED -- do not repeat it.
24 of the 37 screen functions appear in a vtable, so it looked possible to
place the other 61 sites by scanning a function's vtable NEIGHBOURS for one
that loads a layout. It "placed" 13 more functions -- and it FAILED ITS
CONTROLS: the Options screen (0x1400a2050, known from live text) came out as
`12_mainMenu/main_menu_2`, and the Gallery as `13_gameSelect/game_select_3`.
The +-24 slot window walks off the end of one vtable into the next, so the
neighbour it finds belongs to a different class.

⭐ It produced thirteen confident, plausible, WRONG answers, and only the
controls caught it. That is the whole reason the controls are in this file.

Usage:
    python tools/screen_layouts.py
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_dump import read_manifest, DEFAULT_DUMP  # noqa: E402

CODE_SECTION = 0x140001000
# The two generic cursor helpers every shell menu moves its selection through.
CURSOR_HELPERS = {0x14007CDC0, 0x14007C9F0}
LAYOUT_RE = re.compile(rb"ui.1_menu.[ -~]{4,50}")

# ⭐ Screens established independently, by live text in a real run. If the
# layout mapping disagrees with these, the mapping is wrong -- not the rows.
CONTROLS = {
    0x140097A59: "12_mainMenu",
    0x1400B1BD9: "13_gameSelect",
}


def function_entry(sec, addr, max_back=0x4000):
    """Walk back to the int3 padding that precedes a function."""
    off = addr - sec.live
    for back in range(max_back):
        p = off - back
        if p < 3:
            return None
        if sec.data[p - 1] == 0xCC and sec.data[p - 2] == 0xCC:
            return sec.live + p
    return None


def main():
    secs = read_manifest(DEFAULT_DUMP)
    sec = [s for s in secs if s.live == CODE_SECTION][0]

    names = {}
    for s in secs:
        for m in LAYOUT_RE.finditer(s.data):
            names[s.live + m.start()] = m.group(0).decode("ascii", "replace")
    print("layout name strings found: %d" % len(names))

    # Every RIP-relative lea that points at one, grouped by enclosing function.
    d = sec.data
    by_fn = {}
    for i in range(len(d) - 7):
        if d[i] not in (0x48, 0x4C) or d[i + 1] != 0x8D:
            continue
        if (d[i + 2] & 0xC7) != 0x05:
            continue
        disp = int.from_bytes(d[i + 3:i + 7], "little", signed=True)
        tgt = sec.live + i + 7 + disp
        if tgt not in names:
            continue
        fn = function_entry(sec, sec.live + i)
        if fn:
            by_fn.setdefault(fn, set()).add(names[tgt])

    # Every cursor call site, by the `call rel32` encoding.
    sites = []
    for i in range(len(d) - 5):
        if d[i] != 0xE8:
            continue
        rel = int.from_bytes(d[i + 1:i + 5], "little", signed=True)
        if sec.live + i + 5 + rel in CURSOR_HELPERS:
            sites.append(sec.live + i)
    print("cursor call sites: %d\n" % len(sites))

    placed, failures = 0, []
    for a in sites:
        fn = function_entry(sec, a)
        layouts = sorted(by_fn.get(fn, [])) if fn else []
        if not layouts:
            continue
        placed += 1
        print("  %#x  fn %#x  %s" % (a, fn, layouts[0]))
        for known, expect in CONTROLS.items():
            if a == known and expect not in layouts[0]:
                failures.append((a, expect, layouts[0]))

    print("\nplaced %d of %d cursor sites (the rest load their layout "
          "elsewhere or via a vtable)" % (placed, len(sites)))

    # ⭐⭐ The controls must pass, and their absence must fail too: a mapping
    # that placed nothing would otherwise "pass" by vacuous agreement.
    for known in CONTROLS:
        if not any(a == known for a in sites
                   if by_fn.get(function_entry(sec, a))):
            failures.append((known, CONTROLS[known], "NOT PLACED AT ALL"))
    if failures:
        print("\n⛔ CONTROL FAILED -- the mapping disagrees with a screen "
              "established from live text:")
        for a, expect, got in failures:
            print("   %#x expected %s, got %s" % (a, expect, got))
        sys.exit(1)
    print("controls pass: the known screens map to their known layouts.")


if __name__ == "__main__":
    main()
