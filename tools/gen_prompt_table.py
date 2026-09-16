"""
Generate src/prompt_table.h -- captured GUI text -> the GMD label that produced it.

⭐ WHY A GENERATED TABLE. The mod already reads the live GMD at runtime
(gmd_find.c), but a reverse lookup needs the whole string set indexed by TEXT,
and building that in-process means walking 4165 strings on the worker thread.
The corpus is fixed data shipped with the game, so the index is computed here,
offline, and verified against the same source the mod reads.

⭐⭐ WHAT THIS IS FOR, AND WHAT IT IS NOT. It answers "which LABEL produced this
string?". Because CFC2's help strings are per-ROW (see
docs/screen-id-from-prompt-text.md), that answers "which row is focused?"
directly, and "which screen?" only where a label belongs to one screen.

⛔ AMBIGUITY IS A VALUE, NOT A GUESS. Measured over the corpus:
  - button hint strips: 74 distinct texts, and after collapsing PLATFORM
    variants (_XB / _PS / _NX_EU / _NX_US -- same screen, different controller
    glyph) exactly ONE text still maps to two distinct screens.
  - prose captions: 164 distinct texts, 4 collisions, all near-synonyms.
Those entries are emitted with PROMPT_AMBIGUOUS so the mod can say nothing
rather than pick. See memory: inconclusive-must-be-a-return-value.

Usage:  python tools/gen_prompt_table.py [--game-dir DIR]
"""

import argparse
import os
import re
import sys
import collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmd import iter_gmds, parse_gmd  # noqa: E402

DEFAULT_GAME_DIR = (r"C:\Program Files (x86)\Steam\steamapps\common"
                    r"\CAPCOM FIGHTING COLLECTION 2")

# Platform variants of ONE screen's strip: the controller glyph differs, the
# screen does not. Collapsing these is what takes the ambiguous-strip count
# from 31 down to 1.
PLATFORM_SUFFIX = re.compile(r"_(XB|XBO|PS|PS4|PS5|NX|NX_EU|NX_US|STM|NX2)$")


def canonical(label):
    """Strip repeated platform suffixes: HELP_X_NX_EU -> HELP_X."""
    prev = None
    while prev != label:
        prev = label
        label = PLATFORM_SUFFIX.sub("", label)
    return label


def c_escape(s):
    out = []
    for ch in s:
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 0x20:
            out.append("\\x%02x" % ord(ch))
        else:
            out.append(ch)
    return "".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game-dir", default=DEFAULT_GAME_DIR)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    out_path = args.out or os.path.join(root, "src", "prompt_table.h")

    arc = os.path.join(args.game_dir, "nativeDX11x64", "arc", "pc", "msg.arc")
    if not os.path.isfile(arc):
        sys.exit("msg.arc not found: %s" % arc)

    table = None
    for name, raw in iter_gmds(arc):
        if name.endswith("menu_eng"):
            _, _, table = parse_gmd(raw)
            break
    if table is None:
        sys.exit("menu_eng not found in msg.arc")

    labels = list(table.keys())

    # Only the help family: these are what the shell pushes through setText as
    # captions and hint strips. Other labels (move names, rosters) are item
    # DATA and are already reachable by index.
    def is_prompt(label):
        # HELP_/HB_ are the per-row captions and button strips.
        if label.startswith(("HELP_", "HB_")):
            return True
        # ⭐⭐ ACHIEVEMENT NAMES ARE ITEM LABELS, and the Fighter Awards screen
        # hands them to us as live text on every cursor move:
        #     "The Year We Made Contact"  = ACHIEVEMENT_07_NAME
        #     "Fighting Game Fans Rejoice!" = ACHIEVEMENT_01_NAME
        # 40 of them, matching that screen's 40-item horizontal list exactly.
        #
        # ⛔ They were being REFUSED and logged as "caption not in the corpus",
        # so the mod threw away the one thing that names those rows. That
        # screen's captions do not track the cursor at all (the same
        # "View all of your challenges." arrives at every position), so this is
        # the ONLY route to a name there.
        #
        # ⚠ Included because they are unambiguous item names verified against
        # the shipped corpus -- the same standard every other entry meets --
        # not because the screen needs rescuing.
        if label.startswith("ACHIEVEMENT_") and label.endswith("_NAME"):
            return True
        # ⭐⭐⭐ ACHIEVEMENT DESCRIPTIONS ARE THE "HINTS" VIEW'S ENTIRE CONTENT.
        #
        # MEASURED 2026-09-30 (02:38 run). The Fighter Awards button strip
        # offers "<ICON DECIDE>Hints"; pressing it opens a modal whose strip
        # collapses to "<ICON CANCEL>Back", and the only text it sets is one
        # achievement DESCRIPTION -- the hint for the row you are standing on:
        #
        #     "Encounter Shin Akuma or Ultimate Rugal in Arcade Mode of
        #      Capcom vs. SNK 2..."   = ACHIEVEMENT_11_DESC
        #
        # and the cursor was on "The True(r) Final Battle" = ACHIEVEMENT_11_NAME.
        # The pairing is exact.
        #
        # ⛔ It was REFUSED -- "caption not in the corpus, not spoken" -- so the
        # mod discarded the one string that answers the screen. Precisely the
        # bug the _NAME clause above was added to fix, one family over.
        #
        # ⚠ Same standard as every other entry, not a rescue: all 40 texts are
        # UNIQUE within the family AND against the whole 4165-string corpus
        # (0 collisions, verified), so identification cannot be ambiguous.
        #
        # ⚠ These are DESCRIPTIONS, so a caller must not speak one as a row
        # LABEL. The hint view has no cursor rows at all -- see the modal path.
        if label.startswith("ACHIEVEMENT_") and label.endswith("_DESC"):
            return True
        return False

    # index every prompt string by its exact text
    by_text = collections.defaultdict(set)
    index_of = {}
    skipped_blank = []
    for i, label in enumerate(labels):
        index_of[label] = i
        if not is_prompt(label):
            continue
        text = table[label]
        # ⛔ A blank placeholder identifies nothing and would match noise.
        # HELP_BUTTON_SPACE is literally " " -- the screen with no
        # affordances. Exactly one such string exists; it is EXCLUDED and
        # REPORTED, never silently dropped.
        if len(text.strip()) < 3:
            skipped_blank.append((label, text))
            continue
        by_text[text].add(label)

    rows = []
    ambiguous = 0
    for text in sorted(by_text):
        group = by_text[text]
        canon = {canonical(l) for l in group}
        # Pick a representative: the shortest canonical name is the base form.
        rep = sorted(group, key=lambda l: (len(l), l))[0]
        amb = len(canon) > 1
        if amb:
            ambiguous += 1
        rows.append((text, rep, index_of[rep], amb, sorted(canon)))

    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* GENERATED by tools/gen_prompt_table.py -- do not edit.\n"
                " *\n"
                " * Captured GUI text -> the GMD label that produced it.\n"
                " * Source: menu_eng in arc/pc/msg.arc (the shipped corpus).\n"
                " *\n"
                " * ⭐ Answers \"which ROW is focused?\" -- CFC2's help strings\n"
                " * are per-ROW, not per-screen. See\n"
                " * docs/screen-id-from-prompt-text.md before using this to\n"
                " * name a SCREEN.\n"
                " *\n"
                " * ⛔ Entries flagged PROMPT_AMBIGUOUS map to more than one\n"
                " * screen even after platform variants are collapsed. The\n"
                " * lookup must report that, never pick one.\n"
                " */\n"
                "#ifndef CFC2_PROMPT_TABLE_H\n"
                "#define CFC2_PROMPT_TABLE_H\n\n"
                "#define PROMPT_AMBIGUOUS 1\n\n"
                "typedef struct {\n"
                "    const char *text;   /* exact string the game set */\n"
                "    const char *label;  /* the GMD label it came from */\n"
                "    int         index;  /* that label's GMD index */\n"
                "    int         flags;  /* PROMPT_AMBIGUOUS, or 0 */\n"
                "} prompt_row;\n\n")
        f.write("static const prompt_row PROMPT_ROWS[] = {\n")
        for text, rep, idx, amb, canon in rows:
            if amb:
                f.write("    /* ambiguous: %s */\n" % ", ".join(canon))
            f.write('    { "%s", "%s", %d, %d },\n'
                    % (c_escape(text), rep, idx, 1 if amb else 0))
        f.write("};\n\n")
        f.write("#define PROMPT_ROW_COUNT %d\n\n" % len(rows))
        f.write("#endif /* CFC2_PROMPT_TABLE_H */\n")

    print("wrote %s" % out_path)
    print("  %d prompt labels -> %d distinct texts" %
          (sum(len(v) for v in by_text.values()), len(rows)))
    print("  %d texts flagged AMBIGUOUS (map to >1 screen after collapsing "
          "platform variants)" % ambiguous)
    print("  %d blank placeholders excluded: %s"
          % (len(skipped_blank),
             ", ".join(l for l, _ in skipped_blank) or "none"))
    for text, rep, idx, amb, canon in rows:
        if amb:
            print("     %-46r -> %s" % (text[:44], ", ".join(canon)))


if __name__ == "__main__":
    main()
