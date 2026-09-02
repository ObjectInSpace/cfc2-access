"""
Reader for CFC2's MT Framework GUI layout resources (`GUI\\0`, version 0x22715).

Why this exists: the shell's menu text is bound at RUNTIME (a layout contains
no GMD label hashes -- verified: 0 matches across all 4165 labels), so knowing
which item is focused means understanding the GUI node tree, not the message
file. Every interactive layout carries named animation states -- DEFAULT,
SELECT, DECIDE, DISABLE -- so selection is modelled explicitly and is the thing
to detect.

Format facts established by inspecting the shipped files (menu.arc, 25
layouts). Anything not yet proven is reported as unknown rather than guessed:
a plausible-looking field that is actually something else is exactly how this
project has burned time before.

Header (little-endian):
    +0x00  char[4]  "GUI\\0"
    +0x04  u32      version   (0x00022715 in every CFC2 layout)
    +0x08  u32      file size (matches the inflated length exactly)
    +0x0c  u32      1
    +0x10  u32      timestamp-ish
    +0x18  u32[13]  element counts
    +0x98  u32      screen width  (1920)
    +0x9c  u32      screen height (1080)
    +0xa8  u64[...] section offsets, one per section, zero-padded

The name pool is a contiguous NUL-separated ASCII block holding BOTH node
names and animation-state names, interleaved in tree order.

Usage:
    python tools/gui.py [--layout main_menu_2] [--list]
"""

import argparse
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmd import read_arc  # noqa: E402

DEFAULT_GAME_DIR = (r"C:\Program Files (x86)\Steam\steamapps\common"
                    r"\CAPCOM FIGHTING COLLECTION 2")

GUI_MAGIC = b"GUI\0"
GUI_VERSION = 0x00022715
TYPE_GUI = 0x22948394

# The animation states that model selection. Presence of SELECT/DECIDE is what
# marks a layout as interactive.
FOCUS_STATES = ("DEFAULT", "SELECT", "DECIDE", "DISABLE", "FOCUS")

# ⚠ A PREFIX TEST ON "SELECT" IS WRONG. Surveying all 25 shipped layouts turns
# up names that start with SELECT but do NOT mean "this item just became
# focused":
#
#   SELECTED_DEFAULT / SELECTED_SELECT / SELECTED_DECIDE / SELECTED_UNFOCUS /
#   SELECTED_F0CUS   -- sub-states of an ALREADY-selected item (option_match).
#                       SELECTED_UNFOCUS in particular fires as an item LOSES
#                       focus, so a strncmp(name,"SELECT",6) hook would
#                       announce exactly the wrong thing.
#   DISABLE_SELECT   -- contains "SELECT" but is a disabled state.
#
# ⚠ And note SELECTED_F0CUS is spelled with a DIGIT ZERO, not the letter O --
# a typo in Capcom's own data. Matching on "FOCUS" silently misses it. This is
# why the classifier works from an explicit surveyed vocabulary rather than
# from pattern-matching what the names look like they ought to be.
_STATE_RE = re.compile(r"^(DEFAULT|SELECT|DECIDE|DISABLE)(\d*)$")

# Names that carry a state prefix but are NOT a plain focus transition.
# Enumerated from the shipped data, not guessed.
NON_PLAIN_STATES = {
    "DECIDE_MINE", "DEFAULT_MINE", "DEFAULT_SCATK", "DISABLE_SELECT",
    "SELECTED_DECIDE", "SELECTED_DEFAULT", "SELECTED_F0CUS",
    "SELECTED_SELECT", "SELECTED_UNFOCUS", "SELECT_MINE", "SELECT_SCATK",
}


def classify_state(name):
    """What does this animation-state name mean for focus?

    Returns one of: 'select', 'decide', 'default', 'disable', or None.
    Works from the surveyed vocabulary above, so a name that merely looks
    like a selection state does not get treated as one.
    """
    m = _STATE_RE.match(name)
    if m:
        return m.group(1).lower()

    # The compound names. Each is classified by what it actually signifies,
    # which is not always what its prefix suggests.
    special = {
        # An item that is the current selection, in various sub-states.
        "SELECTED_SELECT": "select",
        "SELECTED_F0CUS": "select",     # sic: digit zero
        "SELECTED_DEFAULT": None,       # selected but idle -- not a transition
        "SELECTED_UNFOCUS": None,       # LOSING focus -- must not announce
        "SELECTED_DECIDE": "decide",
        "DISABLE_SELECT": "disable",
        # *_MINE / *_SCATK are lobby/online variants of the plain states.
        "SELECT_MINE": "select",
        "DECIDE_MINE": "decide",
        "DEFAULT_MINE": "default",
        "SELECT_SCATK": "select",
        "DEFAULT_SCATK": "default",
    }
    return special.get(name)


class GuiLayout:
    def __init__(self, name, raw):
        if raw[:4] != GUI_MAGIC:
            raise ValueError("%s: not a GUI resource" % name)
        self.name = name
        self.raw = raw
        self.version, self.size = struct.unpack_from("<II", raw, 4)
        if self.version != GUI_VERSION:
            raise ValueError("%s: unexpected GUI version 0x%x"
                             % (name, self.version))
        if self.size != len(raw):
            raise ValueError("%s: size field %d != actual %d"
                             % (name, self.size, len(raw)))

        self.counts = list(struct.unpack_from("<13I", raw, 0x18))
        self.screen_w, self.screen_h = struct.unpack_from("<II", raw, 0x98)

        # Section offsets: 64-bit, from 0xa8 until they stop being sane.
        self.sections = []
        off = 0xA8
        while off + 8 <= len(raw):
            q = struct.unpack_from("<Q", raw, off)[0]
            if q == 0 or q >= len(raw):
                if self.sections:
                    break
            else:
                self.sections.append(q)
            off += 8
            if off > 0x200:
                break
        self.sections = sorted(set(self.sections))

        self.pool_start, self.pool_end, self.names = self._find_pool()

    def _find_pool(self):
        """The name pool: the largest contiguous printable/NUL run.

        Located by content rather than by a header offset, because which
        section index holds it has not been proven. Finding it by shape is
        honest about that; hardcoding a guessed section index would not be.
        """
        raw = self.raw
        best = (0, 0)
        i = 0
        n = len(raw)
        while i < n:
            if not (32 <= raw[i] < 127):
                i += 1
                continue
            j = i
            while j < n and (32 <= raw[j] < 127 or raw[j] == 0):
                j += 1
            if j - i > best[1] - best[0]:
                best = (i, j)
            i = j
        s, e = best
        names = [x.decode("ascii", "replace")
                 for x in raw[s:e].split(b"\0") if x]
        return s, e, names

    @property
    def is_interactive(self):
        """Does this layout model selection at all?"""
        return "SELECT" in self.names and "DECIDE" in self.names

    def state_names(self):
        return [n for n in self.names if n in FOCUS_STATES
                or re.fullmatch(r"(DEFAULT|SELECT|DECIDE|DISABLE)\d*", n)]

    def text_nodes(self):
        """Nodes whose name marks them as text (txt_msg00, etc.)."""
        return [n for n in self.names if n.startswith("txt")]

    def describe(self):
        L = []
        L.append("%s  (%d bytes, %dx%d)"
                 % (self.name, self.size, self.screen_w, self.screen_h))
        L.append("  counts:   %s" % self.counts)
        L.append("  sections: %d  name pool 0x%x..0x%x (%d names)"
                 % (len(self.sections), self.pool_start, self.pool_end,
                    len(self.names)))
        L.append("  interactive: %s" % ("YES" if self.is_interactive else "no"))
        L.append("  states:   %s" % ", ".join(sorted(set(self.state_names()))))
        L.append("  text:     %s" % ", ".join(self.text_nodes()) or "(none)")
        return "\n".join(L)


def iter_layouts(game_dir=DEFAULT_GAME_DIR):
    """Yield every GUI layout in menu.arc as a GuiLayout."""
    arc = os.path.join(game_dir, "nativeDX11x64", "arc", "pc", "menu.arc")
    if not os.path.isfile(arc):
        raise SystemExit("menu.arc not found at %s" % arc)
    raw, entries = read_arc(arc)
    for e in entries:
        if e.type_hash != TYPE_GUI:
            continue
        blob = zlib.decompress(raw[e.offset:e.offset + e.comp_size])
        short = e.name.split("\\")[-1]
        try:
            yield GuiLayout(short, blob)
        except ValueError as exc:
            print("  warning: %s" % exc)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game-dir", default=DEFAULT_GAME_DIR)
    ap.add_argument("--layout", help="only this layout")
    ap.add_argument("--names", action="store_true",
                    help="dump the full name pool")
    args = ap.parse_args()

    total = interactive = 0
    for lay in iter_layouts(args.game_dir):
        if args.layout and lay.name != args.layout:
            continue
        total += 1
        if lay.is_interactive:
            interactive += 1
        print(lay.describe())
        if args.names:
            for n in lay.names:
                print("      %s" % n)
        print()

    print("%d layouts, %d interactive (model SELECT/DECIDE)"
          % (total, interactive))


if __name__ == "__main__":
    main()
