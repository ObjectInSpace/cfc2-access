"""
The ONE way to pair a log event to the movement that caused it.

⛔⛔ WHY THIS EXISTS. Five confident wrong readings in a single session, every
one of them from an ad-hoc pairing window written inline for that analysis:

  1. a caption attributed across an AXIS BOUNDARY -- it belonged to a 4-item
     vertical list, not the 8-item horizontal strip beside it. Nearly shipped a
     wrong game mapping from it.
  2. stale captions read as per-row evidence ("Arcade Mode", "P1" against
     unrelated positions on three different lists).
  3. a "contaminated" value count -- row 1 appearing to have 2, 3 AND 8 values,
     which made me refuse a CORRECT value set for a day.
  4. the Music chooser nearly REOPENED on four positions that matched an
     already-confirmed mapping, entirely from window slop.
  5. "the picker is 20 of 20 off by one" -- which reversed completely once
     keyed on the same timestamp. There was never a direction question.

Each time the fix was the same and each time I rewrote it slightly differently.

## ⭐ THE TWO RULES, in order of strength

**1. Key on the CURSOR ADDRESS when the event is a value movement.**
`(cursor - screen - values_off) / 4` names the row outright. It is arithmetic on
values the hook was handed, not a guess about timing, and it is the only method
that has never produced a wrong answer here. It resolved every screen's value
counts to exactly ONE count per row where timing-based pairing showed three.

**2. Otherwise require the SAME TIMESTAMP.**
The shipped screens that work (Gallery, Ranked Leaderboard) set their text in
the same millisecond as the movement. Anything looser admits the whole screen's
traffic -- and on these screens that traffic is dominated by NEIGHBOURING rows
re-setting their own objects, which is exactly what produced readings 1-5.

⚠ A looser window is not weaker evidence, it is evidence about a DIFFERENT
question. Ask what a window ADMITS before trusting what it FOUND: on a screen
with a playback timer there is always a "next event" to find.
"""

import re

# `mc:` movement lines carry everything needed to identify the axis and row.
MOVE = re.compile(
    r'^\[([0-9:.]+)\] mc: (\w+)\s+cursor \d+ -> \d+\s+'
    r'\(item (\d+) of (\d+).*?caller=0*([0-9A-F]+) screen=([0-9A-F]+) '
    r'cursor=([0-9A-F]+)')
TEXT = re.compile(r'^\[([0-9:.]+)\].*TEXT SET on ([0-9A-F]+): "([^"]*)"')
SPOKE = re.compile(r'^\[([0-9:.]+)\] mcspeak: spoke "([^"]*)"')


class Movement:
    """One cursor movement, with the row it resolves to."""

    __slots__ = ("ts", "axis", "pos", "total", "caller", "screen", "cursor",
                 "line")

    def __init__(self, m, line):
        self.ts = m.group(1)
        self.axis = m.group(2)
        self.pos = int(m.group(3))
        self.total = int(m.group(4))
        self.caller = m.group(5).lower()
        self.screen = int(m.group(6), 16)
        self.cursor = int(m.group(7), 16)
        self.line = line

    def row(self, values_off):
        """⭐ RULE 1: the row this value movement belongs to, or None.

        ⚠ Returns None rather than a plausible number when the arithmetic does
        not land cleanly -- a wrong row names the wrong setting, which is the
        failure the whole project is organised against.
        """
        if values_off <= 0:
            return None
        delta = self.cursor - self.screen - values_off
        if delta < 0 or delta % 4:
            return None
        r = delta // 4
        return r if 0 <= r < 64 else None


def movements(path):
    """Every movement in a log, in order."""
    out = []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = MOVE.match(line)
        if m:
            out.append(Movement(m, line.rstrip()))
    return out


def text_at(path, ts):
    """⭐ RULE 2: every string drawn at EXACTLY this timestamp.

    ⚠ Same timestamp, never "within N lines". See the module header for the
    five readings that window slop produced.
    """
    out = []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = TEXT.match(line)
        if m and m.group(1) == ts:
            out.append((m.group(2), m.group(3)))
    return out


def paired(path, values_off_by_caller=None):
    """Movements with the text drawn at the same instant, and the derived row.

    Yields (Movement, row_or_None, [(object, text), ...]).

    ⚠ The text list is frequently EMPTY and that is the normal case: only 38%
    of value movements have any text at the same instant. An empty list means
    "the screen drew nothing then", not "the lookup failed".
    """
    voff = values_off_by_caller or {}
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    by_ts = {}
    for line in lines:
        m = TEXT.match(line)
        if m:
            by_ts.setdefault(m.group(1), []).append((m.group(2), m.group(3)))
    for line in lines:
        m = MOVE.match(line)
        if not m:
            continue
        mv = Movement(m, line)
        yield mv, mv.row(voff.get(mv.caller, 0)), by_ts.get(mv.ts, [])
