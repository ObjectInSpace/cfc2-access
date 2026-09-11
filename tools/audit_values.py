"""
Audit SPOKEN values against the text the game actually drew.

⭐⭐ WHY THIS EXISTS. The user reported values "missing or inaccurate,
particularly on the online screens but also in options" -- and every metric this
project had was blind to it. Counting named-vs-bare lines says nothing about
whether a name is CORRECT. Two real defects were found by ear, not by
measurement:

  - eight of Lobby Settings' ten rows had no value labels at all, so the mod
    said "2 of 2" while the screen showed "On";
  - game titles spoke the JP name ("Moero! Justice Gakuen") while the screen
    displayed the US one ("Project Justice").

⛔ IT IS AN AUDIT, NOT A SOURCE OF TRUTH. Measured over every log:
  - only 38% of value movements have any text drawn at that instant, so live
    text cannot REPLACE the shipped tables (it would silence the other 62%);
  - and against a value set known to be correct (Number of Players = "2".."9")
    the drawn text disagrees 25% of the time, because neighbouring rows on the
    same screen re-set their own objects constantly.

So a single disagreement is NOT a bug report. A row where disagreements
CLUSTER is. This prints the clustering and leaves the judgement to a human,
which is the same standard every shipped row here meets.

Run:  python tools/audit_values.py [logfile ...]
"""

import collections
import glob
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# A spoken line that NAMES a value looks like "Row, Value, N of M".
SPOKE = re.compile(r'^\[([0-9:.]+)\] mcspeak: spoke "([^",]+), ([^",]+), '
                   r'(\d+) of (\d+)"')
DREW = re.compile(r'^\[([0-9:.]+)\].*TEXT SET on ([0-9A-F]+): "([^"]*)"')


def audit(paths):
    agree = collections.Counter()
    differ = collections.Counter()
    examples = collections.defaultdict(list)

    for path in paths:
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
        for i, line in enumerate(lines):
            m = SPOKE.match(line)
            if not m:
                continue
            ts, row, val = m.group(1), m.group(2), m.group(3)
            # The game's own text at the SAME INSTANT. ⚠ Same-timestamp only:
            # a looser window admits the whole screen's traffic, which on these
            # screens is dominated by neighbouring rows re-setting themselves.
            for j in range(i, min(i + 4, len(lines))):
                d = DREW.match(lines[j])
                if not d or d.group(1) != ts:
                    continue
                drew = d.group(3)
                if not drew.strip() or drew.startswith("<"):
                    break
                if drew.lower() == val.lower():
                    agree[row] += 1
                else:
                    differ[row] += 1
                    if len(examples[row]) < 4:
                        examples[row].append((val, drew, os.path.basename(path)))
                break

    rows = sorted(set(agree) | set(differ),
                  key=lambda r: -differ[r])
    print("row                        agree  differ   verdict")
    for r in rows:
        a, d = agree[r], differ[r]
        tot = a + d
        # ⚠ The threshold is deliberately blunt: this flags CLUSTERS for a human
        # to look at. With a 25% baseline disagreement from neighbouring
        # objects, anything under half is unremarkable.
        if d == 0:
            verdict = "clean"
        elif d > a:
            verdict = "** LOOK: disagrees more often than it agrees"
        else:
            verdict = "(some disagreement -- expected, see the header)"
        print(f"  {r[:24]:<24} {a:>5}  {d:>6}   {verdict}")
        if d > a:
            for val, drew, src in examples[r]:
                print(f"        spoke {val!r} but the game drew {drew!r}  [{src}]")

    print()
    print(f"{sum(agree.values())} agreements, {sum(differ.values())} "
          f"disagreements across {len(paths)} log(s).")

    # ⭐ The second, harsher question: which shipped strings has the game ever
    # actually been seen drawing? See sightings() for why this is not a bug list.
    drawn, spoken = sightings(paths)
    src = open(os.path.join(ROOT, "src", "menu_speak.c"),
               encoding="utf-8").read()
    sys.path.insert(0, HERE)
    from name_screens import load_corpus
    corpus = load_corpus()
    labels = list(corpus.keys())

    total = sighted = 0
    unsighted_spoken = []
    for name, body in re.findall(
            r"static const mc_value_set (g_\w+)\[\]\s*=\s*\{(.*?)\n\};",
            src, re.S):
        body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
        for row, cnt, first in re.findall(
                r"\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", body):
            for j in range(int(cnt)):
                idx = int(first) + j
                if idx >= len(labels):
                    continue
                val = corpus[labels[idx]]
                total += 1
                if val in drawn:
                    sighted += 1
                elif val in spoken:
                    # ⚠ The sharp end: spoken to the user, never confirmed
                    # against the screen. A wrong entry here reaches the ear.
                    unsighted_spoken.append((name, int(row), val))

    print(f"{sighted} of {total} shipped value strings have been SEEN drawn "
          f"({total - sighted} rest on inference).")
    if unsighted_spoken:
        print("  spoken to the user but never confirmed on screen:")
        for name, row, val in unsighted_spoken[:12]:
            print(f"    {name} row {row}: {val!r}")
    # ⭐ A return code a CI step could use, but NOT a pass/fail claim about the
    # mod: only the clustered rows mean anything.
    return 1 if any(differ[r] > agree[r] for r in rows) else 0


def sightings(paths):
    """⭐⭐ Which shipped value STRINGS has the game ever been seen drawing?

    ⚠ This is a different and harsher question than the agreement audit above.
    A value set can be internally consistent, corpus-validated and derived from
    the renderer's own table, and STILL contain strings no run has ever put on
    screen. Measured 2026-09-10: **59 of 104 shipped value strings had never
    been sighted**, which is far more inference than the commit messages
    implied.

    ⛔ A never-sighted string is NOT a bug. Most are simply positions nobody has
    visited -- the 14 display languages, eight lobby comment presets, P3/P4 on
    four-player games. Listing them is how we know WHICH parts of the shipping
    data are still resting on inference rather than on a sighting.

    ⭐ It is also how a run gets prioritised: the strings the mod has already
    SPOKEN but never had confirmed are the ones a wrong entry would have
    reached the user through.
    """
    import glob as _glob
    drawn = set()
    spoken = set()
    for p in paths:
        text = open(p, encoding="utf-8", errors="replace").read()
        drawn |= set(re.findall(r'TEXT SET on [0-9A-F]+: "([^"]*)"', text))
        spoken |= set(re.findall(r'spoke "[^",]+, ([^",]+), \d+ of \d+"', text))
    return drawn, spoken


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8",
                                      errors="replace")
    paths = sys.argv[1:] or sorted(glob.glob(os.path.join(ROOT, "docs",
                                                          "run-*.log")))
    if not paths:
        sys.exit("no logs found")
    sys.exit(audit(paths))


if __name__ == "__main__":
    main()
