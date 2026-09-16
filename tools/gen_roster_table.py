"""
Generate src/roster_table.h -- the eight per-game character rosters of the
Fighter Awards > Play Stats page.

⭐⭐ WHY THIS IS A GENERATED TABLE AND NOT A HAND-WRITTEN ONE. The rosters are
233 names across 8 games. Pasting them is the shape of error this project keeps
paying for (a roster count written from memory was wrong for two weeks and
closed a route -- see memory cfc2-fighter-awards). Every number here is read
out of the game's own msg.arc.

⭐ THE TABLE IS TINY BECAUSE THE CORPUS IS REGULAR. Each roster is a UNIFORM
stride-2 run of GMD indices (STATS_<GAME>_NN_NAME alternates with _DESC), so a
roster is (base, count) and name N is base + 2*N. That regularity is CHECKED
here, not assumed: a non-uniform family is a hard error rather than something
that silently ships an off-by-one name.

⭐⭐ THE ORDER OF THE EIGHT ENTRIES IS LOAD-BEARING. It is the game's own game
index (screen+0x338), and it is verified against the count table the game
itself uses at 0x1411f7b30:

    index 0 1 2 3 4 5 6 7
    count 35 48 23 39 10 14 40 24
    game  CVS1 CVS2 CFJ Z3U PWS1 PWS2 MJUS STG2

If a roster's corpus size disagrees with that table, this script FAILS. Two
independent sources (the exe's table, the message corpus) must agree before a
character name can be spoken, because the tab index is what tells Project
Justice's 40-character roster from the 40 achievements.

Usage:  python tools/gen_roster_table.py [--game-dir DIR]
"""

import argparse
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmd import iter_gmds, parse_gmd  # noqa: E402

DEFAULT_GAME_DIR = (
    r"C:\Program Files (x86)\Steam\steamapps\common"
    r"\CAPCOM FIGHTING COLLECTION 2"
)

# ⭐ The game's own order, index 0..7 = the value of screen+0x338.
# ⚠ Not alphabetical, not the corpus order -- the ORDER THE EXE USES.
GAME_ORDER = ["CVS1", "CVS2", "CFJ", "Z3U", "PWS1", "PWS2", "MJUS", "STG2"]

# ⭐⭐ Read from the exe at 0x1411f7b30 (see tools/disasm.py around
# 0x14004ae90, the bounds-checked `cmp edx,8` lookup the screen calls to size
# its list). This is the CONTROL: the corpus must reproduce it exactly.
EXE_COUNTS = [35, 48, 23, 39, 10, 14, 40, 24]

# Spoken names for the eight games, for the log and for a screen name.
SPOKEN = {
    "CVS1": "Capcom vs. SNK",
    "CVS2": "Capcom vs. SNK 2",
    "CFJ":  "Capcom Fighting Evolution",
    "Z3U":  "Street Fighter Alpha 3 Upper",
    "PWS1": "Power Stone",
    "PWS2": "Power Stone 2",
    "MJUS": "Project Justice",
    "STG2": "Plasma Sword",
}


def load_labels(game_dir):
    arc = os.path.join(game_dir, "nativeDX11x64", "arc", "pc", "msg.arc")
    if not os.path.exists(arc):
        sys.exit("msg.arc not found: %s" % arc)
    for name, raw in iter_gmds(arc):
        if name.endswith("menu_eng"):
            _, _, table = parse_gmd(raw)
            return list(table.keys()), table
    sys.exit("menu_eng not found in msg.arc")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game-dir", default=DEFAULT_GAME_DIR)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    labels, table = load_labels(args.game_dir)
    index_of = {l: i for i, l in enumerate(labels)}

    # ⭐ ANCHOR CHECK. The achievement indices are already shipped and
    # user-verified (g_fighter_awards_rows). If this enumeration does not
    # reproduce them, the index convention has changed and every number below
    # would be wrong in the same invisible way.
    for label, expect in (("ACHIEVEMENT_01_NAME", 2994),
                          ("ACHIEVEMENT_40_NAME", 3072)):
        got = index_of.get(label)
        if got != expect:
            sys.exit("ANCHOR FAILED: %s is %r, shipped code says %d. The GMD "
                     "index convention changed; refusing to emit a table."
                     % (label, got, expect))

    fam = collections.defaultdict(list)
    for l in labels:
        m = re.match(r"^STATS_([A-Z0-9]+)_(\d+)_NAME$", l)
        if m:
            fam[m.group(1)].append((int(m.group(2)), index_of[l], table[l]))

    rosters = []
    for slot, game in enumerate(GAME_ORDER):
        entries = sorted(fam.get(game, []))
        if not entries:
            sys.exit("no STATS_%s_*_NAME labels in the corpus" % game)

        nums = [n for n, _, _ in entries]
        ids = [i for _, i, _ in entries]

        # The numbering must be a dense 1..N -- a gap means a roster slot the
        # corpus does not name, and base+2*N would then be off past the gap.
        if nums != list(range(1, len(nums) + 1)):
            sys.exit("STATS_%s numbering is not a dense 1..N: %r" % (game, nums))

        # ⭐ UNIFORMITY IS CHECKED, NOT ASSUMED. base + stride*N only names the
        # right character if the run really is uniform.
        strides = {ids[k + 1] - ids[k] for k in range(len(ids) - 1)}
        if strides != {2}:
            sys.exit("STATS_%s is not a uniform stride-2 run (strides=%r). A "
                     "(base,count) pair cannot describe it; it needs an "
                     "explicit index list." % (game, sorted(strides)))

        # ⭐⭐ THE CROSS-CHECK against the exe's own count table.
        if len(entries) != EXE_COUNTS[slot]:
            sys.exit("MISMATCH at game index %d (%s): the corpus has %d names "
                     "but the exe's table at 0x1411f7b30 says %d. Two sources "
                     "disagree -- refusing to emit."
                     % (slot, game, len(entries), EXE_COUNTS[slot]))

        rosters.append((game, ids[0], len(entries),
                        [t for _, _, t in entries]))

    out = args.out or os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "src", "roster_table.h")

    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* GENERATED by tools/gen_roster_table.py -- do not edit.\n"
                " *\n"
                " * The eight per-game character rosters of Fighter Awards >\n"
                " * Play Stats, indexed by the game's OWN game index, read\n"
                " * live from screen+0x338.\n"
                " *\n"
                " * Each roster is a uniform stride-2 GMD run, so character N\n"
                " * (0-based) is `base + 2*N`. Uniformity and density are\n"
                " * checked by the generator.\n"
                " *\n"
                " * ⭐ `count` reproduces the exe's own table at 0x1411f7b30\n"
                " * exactly; the generator fails if the two disagree. That\n"
                " * agreement is what lets the 40-name Project Justice roster\n"
                " * be told from the 40 achievements.\n"
                " */\n"
                "#ifndef CFC2_ROSTER_TABLE_H\n"
                "#define CFC2_ROSTER_TABLE_H\n\n"
                "typedef struct {\n"
                "    const char *tag;    /* the corpus family: CVS1, MJUS, ... */\n"
                "    const char *name;   /* spoken game name */\n"
                "    int         base;   /* GMD index of character 1 */\n"
                "    int         count;  /* characters in this roster */\n"
                "} mc_roster;\n\n"
                "/* ⚠ ORDER IS THE GAME'S OWN INDEX -- do not sort. */\n"
                "static const mc_roster g_rosters[8] = {\n")
        for slot, (game, base, count, names) in enumerate(rosters):
            preview = ", ".join(names[:3])
            f.write('    /* %d */ { "%s", "%s", %d, %d },  /* %s, ... */\n'
                    % (slot, game, SPOKEN[game], base, count, preview))
        f.write("};\n\n#endif /* CFC2_ROSTER_TABLE_H */\n")

    print("wrote %s" % out)
    for slot, (game, base, count, names) in enumerate(rosters):
        print("  %d  %-5s base=%-5d count=%-3d  %s, ..."
              % (slot, game, base, count, ", ".join(names[:3])))
    print("\nall eight rosters agree with the exe's count table at 0x1411f7b30")


if __name__ == "__main__":
    main()
