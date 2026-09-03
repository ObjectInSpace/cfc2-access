"""
Generate src/gmd_probes.h -- the known-label table the heap scanner searches for.

Why generated rather than hand-written: an earlier hand-picked probe set was
chosen by JSON key order instead of GMD file order, and every offset derived
from it was wrong. Constants that are copied once and never re-checked are
exactly how that happens. This reads the shipped msg.arc every time and emits
the values straight from it.

Probes are picked by FILE INDEX stride (every Nth entry of the label array),
which is the ordering the on-disk table actually uses. Each probe carries:

  label, hash1, hash2, file_index, label_offset

sorted by hash1 so the scanner can binary-search. A handful also carry their
expected English display string, used for the end-to-end verification.

Usage:
    python tools/gen_probes.py [--game-dir DIR] [--count 64] [--check]

--check exits nonzero if the existing header does not match a fresh parse,
which is what keeps the checked-in constants honest.
"""

import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmd import read_arc, parse_gmd  # noqa: E402

DEFAULT_GAME_DIR = (r"C:\Program Files (x86)\Steam\steamapps\common"
                    r"\CAPCOM FIGHTING COLLECTION 2")

# Labels whose display text is embedded for the end-to-end check. Short ones,
# so the header stays readable and the comparison is cheap.
VERIFY_LABELS = 3


def crc(data):
    return (zlib.crc32(data) & 0xFFFFFFFF) ^ 0xFFFFFFFF


def load_menu_eng(game_dir):
    """Return the inflated menu_eng GMD bytes."""
    arc = os.path.join(game_dir, "nativeDX11x64", "arc", "pc", "msg.arc")
    if not os.path.isfile(arc):
        sys.exit("msg.arc not found at %s\n(use --game-dir)" % arc)
    raw, entries = read_arc(arc)
    for e in entries:
        if e.name.endswith("menu_eng"):
            return zlib.decompress(raw[e.offset:e.offset + e.comp_size])
    sys.exit("menu_eng not found in msg.arc")


def load_all_languages(game_dir):
    """Return {label: [display string in every shipped language]}.

    The live table's strings are whatever locale the player runs the game in.
    An English-only expectation reports text_verified=no on a perfectly good
    find -- which is exactly what happened on a run in Italian ("Giapponese
    [jp]" vs the expected "Japanese [jp]"). Every locale's real shipped string
    is emitted instead, so the check stays an EXACT match against known data
    rather than being weakened to "any non-empty string".
    """
    arc = os.path.join(game_dir, "nativeDX11x64", "arc", "pc", "msg.arc")
    if not os.path.isfile(arc):
        sys.exit("msg.arc not found at %s\n(use --game-dir)" % arc)
    raw, entries = read_arc(arc)
    out = {}
    for e in entries:
        if not e.name.split("\\")[-1].startswith("menu_"):
            continue
        try:
            _n, _lang, table = parse_gmd(
                zlib.decompress(raw[e.offset:e.offset + e.comp_size]))
        except (ValueError, zlib.error):
            continue
        for label, text in table.items():
            out.setdefault(label, []).append(text)
    return out


def read_entries(blob):
    """Parse the label-entry array in FILE ORDER.

    Returns (header_dict, [(index, label, hash1, hash2, label_offset, pad)]).
    """
    label_count, section_count, label_size, section_size, name_size = \
        struct.unpack_from("<IIIII", blob, 0x14)
    base = 0x28 + name_size + 1
    labels_at = base + label_count * 0x20 + 0x100 * 8
    label_blob = blob[labels_at:labels_at + label_size]

    out = []
    for i in range(label_count):
        sid, h1, h2, pad, loff, link = struct.unpack_from(
            "<iIIIqq", blob, base + i * 0x20)
        end = label_blob.index(b"\0", loff)
        label = label_blob[loff:end].decode("ascii", "replace")
        out.append((i, label, h1, h2, loff, pad, sid))

    hdr = {
        "label_count": label_count,
        "label_size": label_size,
        "section_size": section_size,
        "name_size": name_size,
        "entries_at": base,
        "labels_at": labels_at,
        "strings_at": len(blob) - section_size,
        "file_len": len(blob),
    }
    return hdr, out


def build_header(game_dir, count):
    blob = load_menu_eng(game_dir)
    hdr, entries = read_entries(blob)
    _name, _lang, table = parse_gmd(blob)
    all_langs = load_all_languages(game_dir)

    # Self-checks on the invariants the scanner relies on. If any of these ever
    # stops holding, the scanner's logic is wrong and it must fail loudly here
    # rather than silently look for the wrong shape.
    assert all(e[6] == e[0] for e in entries), "sectionId != index"
    assert all(e[5] == 0xCDCDCDCD for e in entries), "pad is not 0xCDCDCDCD"
    h1s = [e[2] for e in entries]
    assert len(set(h1s)) == len(h1s), "hash1 values are not unique"

    # Pick by file-index stride, not by any sorted order.
    step = max(1, len(entries) // count)
    picks = [entries[i] for i in range(0, len(entries), step)][:count]

    # Verification set: short strings, taken from the picks.
    #
    # Each entry carries EVERY shipped language's string for that label, not
    # just English -- the live table holds whichever locale the player runs.
    # Non-ASCII locales (jp/kr/zh/ru/ar) are dropped from the variant list
    # rather than escaped: the runtime compares bytes it read from memory
    # against a C string literal, and keeping those readable in the header
    # matters more than covering locales we cannot compare cheaply. A label is
    # only usable if at least one ASCII variant survives.
    verify = []
    for idx, label, h1, h2, loff, _pad, _sid in picks:
        variants = []
        for text in all_langs.get(label, []):
            if not (4 <= len(text) <= 40):
                continue
            if not text.isprintable() or '"' in text or '\\' in text:
                continue
            if not text.isascii():
                continue
            if text not in variants:
                variants.append(text)
        # The English string must lead: it is the one a reader recognises.
        eng = table.get(label, "")
        if eng in variants:
            variants.remove(eng)
            variants.insert(0, eng)
        if variants:
            verify.append((idx, label, variants))
        if len(verify) >= VERIFY_LABELS:
            break

    picks_sorted = sorted(picks, key=lambda e: e[2])   # by hash1

    L = []
    a = L.append
    a("/*")
    a(" * GENERATED by tools/gen_probes.py -- do not edit by hand.")
    a(" *")
    a(" * Known GMD label entries used to locate the live message table on the")
    a(" * heap. Picked by FILE INDEX stride (the on-disk label array's own")
    a(" * order), sorted by hash1 for binary search.")
    a(" *")
    a(" * Regenerate with:  python tools/gen_probes.py")
    a(" * Verify with:      python tools/gen_probes.py --check")
    a(" */")
    a("#ifndef CFC2_GMD_PROBES_H")
    a("#define CFC2_GMD_PROBES_H")
    a("")
    a("/* menu_eng layout, straight from the shipped file. */")
    a("#define GMD_LABEL_COUNT   %d" % hdr["label_count"])
    a("#define GMD_LABEL_SIZE    0x%x" % hdr["label_size"])
    a("#define GMD_SECTION_SIZE  0x%x" % hdr["section_size"])
    a("#define GMD_ENTRIES_AT    0x%x   /* from file start */" % hdr["entries_at"])
    a("#define GMD_LABELS_AT     0x%x   /* from file start */" % hdr["labels_at"])
    a("#define GMD_STRINGS_AT    0x%x   /* from file start */" % hdr["strings_at"])
    a("#define GMD_FILE_LEN      0x%x" % hdr["file_len"])
    a("")
    a("/* Offset of the label blob relative to the ENTRY ARRAY base, which is")
    a(" * what the scanner actually finds. */")
    a("#define GMD_LABELS_FROM_ENTRIES  0x%x"
      % (hdr["labels_at"] - hdr["entries_at"]))
    a("#define GMD_STRINGS_FROM_ENTRIES 0x%x"
      % (hdr["strings_at"] - hdr["entries_at"]))
    a("")
    a("typedef struct {")
    a("    const char *label;")
    a("    unsigned    hash1;      /* ~crc32(label * 2) */")
    a("    unsigned    hash2;      /* ~crc32(label * 3) */")
    a("    int         index;      /* position in the on-disk entry array */")
    a("    unsigned    label_off;  /* offset into the label blob */")
    a("} gmd_probe;")
    a("")
    a("/* Sorted by hash1. */")
    a("static const gmd_probe g_gmd_probes[] = {")
    for idx, label, h1, h2, loff, _pad, _sid in picks_sorted:
        a('    { "%s", 0x%08xu, 0x%08xu, %d, 0x%xu },'
          % (label, h1, h2, idx, loff))
    a("};")
    a("#define GMD_NPROBES (int)(sizeof(g_gmd_probes) / sizeof(g_gmd_probes[0]))")
    a("")
    a("/* End-to-end check: label -> its actual display string.")
    a(" *")
    a(" * ⚠ Every SHIPPED LANGUAGE's string is listed, not just English. The")
    a(" * live table holds whichever locale the player runs the game in, so an")
    a(" * English-only expectation reports text_verified=no on a perfectly")
    a(" * good find -- observed on an Italian run: the table was correct and")
    a(" * label-verified, but \"Giapponese [jp]\" != \"Japanese [jp]\".")
    a(" * This stays an EXACT match against known shipped data; it is not")
    a(" * weakened to accepting any non-empty string. text[0] is English.")
    a(" * Non-ASCII locales are omitted (see gen_probes.py). */")
    a("#define GMD_MAXVARIANTS %d" % max(len(v) for _i, _l, v in verify))
    a("")
    a("typedef struct {")
    a("    int         index;")
    a("    const char *label;")
    a("    int         ntext;")
    a("    const char *text[GMD_MAXVARIANTS];")
    a("} gmd_verify;")
    a("")
    a("static const gmd_verify g_gmd_verify[] = {")
    for idx, label, variants in verify:
        lits = ", ".join('"%s"' % t for t in variants)
        a('    { %d, "%s", %d, { %s } },' % (idx, label, len(variants), lits))
    a("};")
    a("#define GMD_NVERIFY (int)(sizeof(g_gmd_verify) / sizeof(g_gmd_verify[0]))")
    a("")
    a("#endif /* CFC2_GMD_PROBES_H */")
    return "\n".join(L) + "\n", hdr, len(picks), len(verify)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game-dir", default=DEFAULT_GAME_DIR)
    ap.add_argument("--count", type=int, default=64)
    ap.add_argument("--check", action="store_true",
                    help="fail if the existing header is stale")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    dest = os.path.join(os.path.dirname(here), "src", "gmd_probes.h")

    text, hdr, nprobes, nverify = build_header(args.game_dir, args.count)

    if args.check:
        if not os.path.exists(dest):
            sys.exit("FAIL: %s does not exist" % dest)
        with open(dest, "r", encoding="utf-8") as f:
            existing = f.read()
        if existing != text:
            sys.exit("FAIL: %s is STALE -- regenerate with "
                     "python tools/gen_probes.py" % dest)
        print("OK: %s matches a fresh parse of msg.arc" % dest)
        print("    %d probes, %d verification strings, %d labels total"
              % (nprobes, nverify, hdr["label_count"]))
        return

    with open(dest, "w", encoding="utf-8") as f:
        f.write(text)
    print("wrote %s" % dest)
    print("  %d probes (by file-index stride), %d verification strings"
          % (nprobes, nverify))
    print("  label_count=%d entries_at=0x%x labels_at=0x%x strings_at=0x%x"
          % (hdr["label_count"], hdr["entries_at"], hdr["labels_at"],
             hdr["strings_at"]))
    print("  invariants checked: sectionId==index, pad==0xCDCDCDCD, "
          "hash1 unique")


if __name__ == "__main__":
    main()
