"""
Generate src/soundtrack_table.h -- the nine soundtracks of Museum > Music's
game strip, in the order the strip presents them.

⭐⭐ WHY THE STRIP NEEDED THIS AT ALL. Caller 0x14009af21 is closed to every
other route: no 9-item run of game names exists in the GMD, its confirm handler
indexes resource/layout ids rather than label indices, and -- measured across
five runs -- it CAPTIONS NOTHING per movement (the only live text near it is the
player's "0:00 / 0:00"). So MC_GAME_STRIP, which names from a caption, cannot
serve it.

⭐⭐⭐ THE ANCHOR CAME FROM THE USER: "I think the first option is the CFC2
music." Confirmed from the same run -- position 1 opened a FIVE-track list of
SOUND_DINGO_01..05 ("01. Title: Take Your Best Shot"), and DINGO_2400430 is
CFC2's own engine codename. So position 1 is the collection itself and the
eight games follow.

⭐ THE ORDER IS THE CORPUS'S OWN. The nine SOUND_ families appear in the GMD in
exactly one order, and after DINGO it is the canonical ROM-table order that
rom_table.h and g_rosters already use. That is checked here, not assumed.

⭐⭐ AND THE TRACK COUNTS ARE THE CROSS-CHECK. All nine differ, so the size of
the list a position opens independently confirms which soundtrack it is -- the
same two-source standard the Play Stats rosters meet. The generator FAILS if
any two counts collide, because that is what makes the check meaningful.

Usage:  python tools/gen_soundtrack_table.py [--game-dir DIR]
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

# ⭐ Position 1 is the collection; positions 2-9 are the eight games in the
# canonical order the ROM table and the rosters use.
# ⚠ Not alphabetical, not corpus-discovery order -- the order the STRIP shows.
STRIP_ORDER = ["DINGO", "CVS1", "CVS2", "CFJ", "Z3U",
               "PWS1", "PWS2", "MJUS", "STG2"]

SPOKEN = {
    "DINGO": "Capcom Fighting Collection 2",
    "CVS1":  "Capcom vs. SNK",
    "CVS2":  "Capcom vs. SNK 2",
    "CFJ":   "Capcom Fighting Evolution",
    "Z3U":   "Street Fighter Alpha 3 Upper",
    "PWS1":  "Power Stone",
    "PWS2":  "Power Stone 2",
    "MJUS":  "Project Justice",
    "STG2":  "Plasma Sword",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game-dir", default=DEFAULT_GAME_DIR)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    arc = os.path.join(args.game_dir, "nativeDX11x64", "arc", "pc", "msg.arc")
    if not os.path.exists(arc):
        sys.exit("msg.arc not found: %s" % arc)
    table = None
    for name, raw in iter_gmds(arc):
        if name.endswith("menu_eng"):
            _, _, table = parse_gmd(raw)
            break
    if table is None:
        sys.exit("menu_eng not found in msg.arc")

    labels = list(table.keys())
    index_of = {l: i for i, l in enumerate(labels)}

    fam = collections.defaultdict(list)
    for l in labels:
        m = re.match(r"^SOUND_([A-Z0-9]+)_(\d+)$", l)
        if m:
            fam[m.group(1)].append((int(m.group(2)), index_of[l], table[l]))

    # ⭐ Every family the corpus has must be accounted for. A tenth soundtrack
    # would mean the strip is not nine items and this table would be wrong.
    extra = set(fam) - set(STRIP_ORDER)
    if extra:
        sys.exit("unexpected SOUND_ families not in the strip order: %s"
                 % sorted(extra))
    missing = set(STRIP_ORDER) - set(fam)
    if missing:
        sys.exit("no SOUND_ labels for: %s" % sorted(missing))

    rows = []
    for slot, tag in enumerate(STRIP_ORDER):
        entries = sorted(fam[tag])
        nums = [n for n, _, _ in entries]
        if nums != list(range(1, len(nums) + 1)):
            sys.exit("SOUND_%s numbering is not a dense 1..N: %r" % (tag, nums))
        rows.append((tag, min(i for _, i, _ in entries), len(entries),
                     entries[0][2]))

    # ⭐⭐ THE ORDER IS VERIFIED, NOT ASSUMED: the families must appear in the
    # GMD in exactly the strip order. If the corpus disagrees, the position
    # mapping is a guess and this refuses to emit one.
    firsts = [r[1] for r in rows]
    if firsts != sorted(firsts):
        sys.exit("the SOUND_ families are NOT in strip order in the GMD "
                 "(first indices %r). The position mapping would be a guess."
                 % firsts)

    # ⭐⭐ THE COUNTS MUST DISCRIMINATE -- that is the whole cross-check.
    counts = [r[2] for r in rows]
    dupes = [c for c, n in collections.Counter(counts).items() if n > 1]
    if dupes:
        sys.exit("track counts collide (%r), so the list size cannot confirm "
                 "the position. Refusing to emit a table whose check is "
                 "meaningless." % sorted(dupes))

    out = args.out or os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "src", "soundtrack_table.h")

    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* GENERATED by tools/gen_soundtrack_table.py -- do not edit.\n"
                " *\n"
                " * Museum > Music's 9-item game strip (caller 0x14009af21),\n"
                " * in the order the strip presents them: the collection's own\n"
                " * soundtrack first, then the eight games in ROM-table order.\n"
                " *\n"
                " * ⭐ Position 1 = DINGO is USER-CONFIRMED: opening it gave a\n"
                " * five-track list of SOUND_DINGO_01..05, and DINGO_2400430 is\n"
                " * CFC2's own engine codename.\n"
                " *\n"
                " * ⭐⭐ `tracks` is the CROSS-CHECK, not decoration: all nine\n"
                " * differ, so the size of the list a position opens confirms\n"
                " * which soundtrack it is. The generator fails if any two\n"
                " * collide.\n"
                " */\n"
                "#ifndef CFC2_SOUNDTRACK_TABLE_H\n"
                "#define CFC2_SOUNDTRACK_TABLE_H\n\n"
                "typedef struct {\n"
                "    const char *tag;     /* SOUND_ family: DINGO, CVS1, ... */\n"
                "    const char *name;    /* spoken name */\n"
                "    int         tracks;  /* how many tracks it holds */\n"
                "} mc_soundtrack;\n\n"
                "/* ⚠ ORDER IS THE STRIP'S OWN -- do not sort. */\n"
                "static const mc_soundtrack g_soundtracks[9] = {\n")
        for slot, (tag, first, n, sample) in enumerate(rows):
            f.write('    /* %d */ { "%s", "%s", %d },  /* %s */\n'
                    % (slot, tag, SPOKEN[tag], n, sample.replace("*/", "")[:40]))
        f.write("};\n\n#endif /* CFC2_SOUNDTRACK_TABLE_H */\n")

    print("wrote %s" % out)
    for slot, (tag, first, n, sample) in enumerate(rows):
        print("  pos %d  %-6s %3d tracks  firstIdx=%-5d  %s"
              % (slot + 1, tag, n, first, sample[:40]))
    print("\nall nine track counts are distinct; families are in strip order")


if __name__ == "__main__":
    main()
