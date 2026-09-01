"""
Audit the markup translator against the entire extracted corpus, offline.

Proves three things before any of this reaches the game:
  1. every ICON token in the corpus has a spoken form (no silent drops),
  2. no markup survives into the spoken output,
  3. the results look right -- samples are printed for human judgement,
     because "no tags left" does not mean "reads well".

Reports a POSITIVE count of what was checked, not just a failure count:
"0 failed" is meaningless if 0 were examined.

Usage:  python tools/audit_markup.py [--lang english] [--samples N]
"""

import argparse
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from markup import (Translator, load_legends, load_legends_per_game,  # noqa: E402
                    unknown_tags, unknown_tokens)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lang", default="english")
    ap.add_argument("--samples", type=int, default=18)
    ap.add_argument("--data", default=None)
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    data_dir = args.data or os.path.join(os.path.dirname(here), "data")
    path = os.path.join(data_dir, "text_%s.json" % args.lang)
    if not os.path.isfile(path):
        sys.exit("%s not found -- run tools/extract_gmd.py first" % path)

    with open(path, encoding="utf-8") as f:
        strings = json.load(f)

    legends = load_legends(strings)
    per_game = load_legends_per_game(strings)
    tr = Translator(legends, per_game)

    def speak(label, text):
        """Translate the way the mod will: legend strings and game context."""
        m = re.match(r"HELP_(?:MOVES|EXMOVE)_([A-Z0-9]+)$", label)
        if m or label == "HELP_SPMOVE":
            return tr.clean(text, legend=True)
        # A label naming a game (CMD_NAME_STG2_.., SUR_.., HELP_..._CVS1)
        # selects that game's icon meanings.
        game = None
        for g in per_game:
            if ("_%s_" % g) in label or label.endswith("_%s" % g):
                game = g
                break
        return tr.clean(text, game=game)

    print("corpus: %d strings (%s)" % (len(strings), args.lang))
    print("legends derived from the game's own HELP_MOVES_*/HELP_EXMOVE_*: "
          "%d icons across %d games" % (len(legends), len(per_game)))
    print()

    # --- 1. token coverage -------------------------------------------------
    all_tokens = set()
    for text in strings.values():
        all_tokens.update(re.findall(r"<ICON ([A-Za-z0-9_]+)>", text))
    missing = unknown_tokens(strings, tr)
    print("ICON tokens: %d distinct in corpus, %d named, %d UNKNOWN"
          % (len(all_tokens), len(all_tokens) - len(missing), len(missing)))
    if missing:
        print("  UNKNOWN: %s" % ", ".join(missing))

    # --- 2. no markup survives --------------------------------------------
    translated = 0
    residue = []
    for label, text in strings.items():
        out = speak(label, text)
        translated += 1
        if re.search(r"<[A-Za-z/][^>]*>", out):
            residue.append((label, out))
    print("strings translated: %d, with surviving markup: %d"
          % (translated, len(residue)))
    for label, out in residue[:5]:
        print("  RESIDUE %-24s %s" % (label, out[:80]))

    # --- 3. samples for human judgement ------------------------------------
    print()
    print("--- samples (icon-bearing strings, read these) ---")
    shown = 0
    interesting = [k for k in sorted(strings) if "<ICON" in strings[k]]
    # Prefer variety: inputs, prompts, legends, and a long one.
    priority = ["ACHIEVEMENT_35_TIPS", "MENU_PRESS_A", "MID_IGAI", "PRE_NAGE3",
                "HELP_MOVES_STG2", "HELP_MOVES_PWS1", "HELP_MOVES_6BTN",
                "SUR_CANON", "PRE_SDASH", "HB_TRAINING"]
    order = [k for k in priority if k in strings] + [k for k in interesting if k not in priority]
    for label in order:
        if shown >= args.samples:
            break
        raw = strings[label].replace("\r\n", " / ")
        out = speak(label, strings[label])
        print("  %s" % label)
        print("    raw:    %s" % raw[:110])
        print("    spoken: %s" % out[:110])
        shown += 1

    print()
    print("--- per-game icon meanings (same token, different games) ---")
    contested = {}
    for game, table in per_game.items():
        for tok, name in table.items():
            contested.setdefault(tok, {})[game] = name
    for tok, games in sorted(contested.items()):
        if len(set(games.values())) > 1:
            print("  %-8s %s" % (tok, "; ".join("%s=%s" % (g, n) for g, n in sorted(games.items()))))

    # --- 4. every language, not just this one ------------------------------
    # <ltr> occurs once in the entire corpus, only in Arabic; auditing English
    # alone missed it. A tag's absence in one language proves nothing.
    print()
    print("--- all languages ---")
    other_total = other_residue = 0
    stray_tags = {}
    for other in sorted(glob.glob(os.path.join(data_dir, "text_*.json"))):
        with open(other, encoding="utf-8") as f:
            table = json.load(f)
        otr = Translator(load_legends(table), load_legends_per_game(table))
        bad = 0
        for lbl, txt in table.items():
            if re.search(r"<[A-Za-z/][^>]*>", otr.clean(txt)):
                bad += 1
        tags = unknown_tags(table)
        if tags:
            stray_tags[os.path.basename(other)] = tags
        other_total += len(table)
        other_residue += bad
        print("  %-24s %5d strings, %d residue%s"
              % (os.path.basename(other), len(table), bad,
                 ("  unhandled tags: " + ", ".join(tags)) if tags else ""))
    print("  %d strings across %d languages, %d with residue"
          % (other_total, len(glob.glob(os.path.join(data_dir, "text_*.json"))), other_residue))

    print()
    ok = not missing and not residue and not other_residue and not stray_tags
    print("RESULT: %s (%d strings in %s, %d total across all languages, "
          "%d distinct ICON tokens checked)"
          % ("PASS" if ok else "FAIL", translated, args.lang, other_total,
             len(all_tokens)))
    if not ok:
        sys.exit(1)


if __name__ == "__main__":
    main()
