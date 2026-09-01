"""
Extract every GMD message file from Capcom Fighting Collection 2 to JSON.

Writes data/text_<lang>.json, each a {label: string} map. All 14 languages
share one label set, so the mod can follow the game's language setting and
look up the same labels regardless.

Usage:
    python tools/extract_gmd.py [--game-dir DIR] [--out DIR]

Every file is hash-verified as it is read: a wrong entry stride still produces
readable-looking strings, so string sanity is not evidence on its own. The run
fails loudly rather than emitting data it could not prove correct.
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmd import iter_gmds, parse_gmd, verify_gmd  # noqa: E402

DEFAULT_GAME_DIR = r"C:\Program Files (x86)\Steam\steamapps\common\CAPCOM FIGHTING COLLECTION 2"

# GMD's language field is an index; CFC2 ships these 14 in this order. The
# entry name (menu_eng etc.) is the reliable identifier, so it drives the
# output filename and this map only labels the report.
LANG_NAMES = {
    "menu_jpn": "japanese", "menu_eng": "english", "menu_fre": "french",
    "menu_ita": "italian", "menu_ger": "german", "menu_spa": "spanish",
    "menu_oth": "spanish_mx", "menu_kor": "korean", "menu_pol": "polish",
    "menu_por": "portuguese", "menu_rus": "russian", "menu_chT": "chinese_trad",
    "menu_chS": "chinese_simp", "menu_ara": "arabic",
}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game-dir", default=DEFAULT_GAME_DIR)
    ap.add_argument("--out", default=None, help="output dir (default: ../data)")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = args.out or os.path.join(os.path.dirname(here), "data")
    os.makedirs(out_dir, exist_ok=True)

    arc = os.path.join(args.game_dir, "nativeDX11x64", "arc", "pc", "msg.arc")
    if not os.path.isfile(arc):
        sys.exit("msg.arc not found at %s\n(use --game-dir)" % arc)

    total_files = total_labels = total_failed = 0
    label_sets = {}

    for entry_name, raw in iter_gmds(arc):
        short = entry_name.split("\\")[-1]

        checked, failed = verify_gmd(raw)
        name, language, table = parse_gmd(raw)
        total_files += 1
        total_labels += checked
        total_failed += failed

        lang = LANG_NAMES.get(short, short)
        path = os.path.join(out_dir, "text_%s.json" % lang)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(table, f, ensure_ascii=False, indent=1, sort_keys=True)

        label_sets[short] = set(table)
        status = "OK" if failed == 0 else "%d HASH FAILURES" % failed
        print("  %-12s -> %-24s %5d labels, %5d hashes checked  %s"
              % (short, os.path.basename(path), len(table), checked, status))

    if total_files == 0:
        sys.exit("no GMD files found in msg.arc")

    # All languages should share one label set; a divergence would break
    # label-keyed lookup at runtime, so check rather than assume.
    reference = next(iter(label_sets.values()))
    mismatched = [n for n, s in label_sets.items() if s != reference]

    print()
    print("files:  %d" % total_files)
    print("labels: %d verified across all files, %d failed" % (total_labels, total_failed))
    print("label sets identical across languages: %s"
          % ("yes" if not mismatched else "NO -- differs in %s" % ", ".join(mismatched)))
    print("output: %s" % out_dir)

    if total_failed or mismatched:
        sys.exit("FAILED: refusing to vouch for output that did not verify")


if __name__ == "__main__":
    main()
