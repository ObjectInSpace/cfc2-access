"""
Generate src/gui_probes.h -- anchors for locating the LIVE GUI tree in memory.

Same strategy that located the GMD message table: anchor on data we can
identify with certainty rather than on code we have to guess at. When a layout
is loaded its name pool must be in memory, and we know those names exactly
from the shipped menu.arc.

Each layout gets an ANCHOR name that is unique to it across all 25 layouts, so
finding that string identifies WHICH screen is loaded, not merely that some
GUI is. Layouts with no unique name are still emitted, flagged, and matched on
a name-set instead -- silently dropping them would leave a screen that never
announces itself with no explanation.

Also emitted: the focus-state vocabulary from gui.py's surveyed classifier, so
the C side never has to re-derive which names mean "selected" (and never
re-introduces the SELECTED_UNFOCUS prefix bug).

Usage:
    python tools/gen_gui_probes.py [--check]
"""

import argparse
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gui import iter_layouts, classify_state, DEFAULT_GAME_DIR  # noqa: E402


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def build(game_dir):
    layouts = list(iter_layouts(game_dir))
    if not layouts:
        sys.exit("no GUI layouts found")

    freq = Counter()
    for lay in layouts:
        for n in set(lay.names):
            freq[n] += 1

    def usable(n):
        """Long enough not to collide with unrelated memory, and ASCII-ish.

        The length floor matters: the pool is found by scanning for printable
        runs, so short fragments like "D" or "8B" are binary noise picked up at
        the pool edges, not real names. Anchoring on one would match anywhere.
        """
        return len(n) >= 8 and all(c.isalnum() or c == "_" for c in n)

    # ⚠ A NAME UNIQUE AMONG PARSED NAMES IS NOT UNIQUE IN BYTES.
    #
    # `freq` counts layouts that list a name in their name pool. The runtime
    # finder searches raw MEMORY, so what matters is whether the byte sequence
    # occurs anywhere in another layout -- including inside a LONGER string.
    #
    # Measured: "icon_status_btl_BM_NOMIP" is a parsed name only in lobby_2,
    # but the bytes also occur in `result` as the tail of the texture path
    # "ui\1_menu\14_netWork\tex\icon_status_btl_BM_NOMIP". Anchoring on it
    # would report lobby_2 while the result screen was up.
    #
    # So uniqueness is tested on the NUL-DELIMITED form (\0name\0), which is
    # how the name actually sits in the pool and how the C side searches for
    # it. That rejects substring-of-a-longer-string collisions by construction.
    def byte_unique(name, owner):
        needle = b"\0" + name.encode("ascii") + b"\0"
        for other in layouts:
            if other is owner:
                continue
            if needle in other.raw:
                return False
        return True

    def anchor_for(lay):
        cands = [n for n in lay.names if freq[n] == 1 and usable(n)]
        cands.sort(key=lambda s: (-len(s), s))
        for n in cands:
            if byte_unique(n, lay):
                return n
        return None

    # Fallback for near-identical sibling screens. ctrl_setting_2 and
    # kbd_setting_2 share 115 of 117 names, so neither has a unique anchor --
    # but a name present in one and absent from the other still separates
    # them. Emit that as a (present, absent) PAIR rather than dropping the
    # layout: ctrl_setting_2 is the controller-settings screen, which a blind
    # player needs, so leaving it unidentifiable would be a real gap.
    def discriminator_for(lay):
        best = None
        for other in layouts:
            if other is lay:
                continue
            only_here = [n for n in set(lay.names) - set(other.names)
                         if usable(n)
                         and (b"\0" + n.encode("ascii") + b"\0")
                         not in other.raw]
            if not only_here:
                continue
            pick = sorted(only_here, key=lambda s: (-len(s), s))[0]
            # Prefer the discriminator against the most similar layout.
            overlap = len(set(lay.names) & set(other.names))
            if best is None or overlap > best[0]:
                best = (overlap, pick, other.name)
        return (best[1], best[2]) if best else (None, None)

    # Corroboration set. Finding ONE string in 1.6 GB of heap proves very
    # little -- a stale freed buffer, or an unrelated copy, matches just as
    # well as the live pool. A real pool holds MANY of the layout's names
    # within a short span, so the finder requires several. These are picked
    # from the layout's own names, longest first (long names are least likely
    # to collide), excluding the anchor itself.
    CORROB = 6

    # ⚠ Corroborators must be DISTINCTIVE, not merely long.
    #
    # Sorting by length alone picks "ReferencePosition", "ResolutionAdjust",
    # "ReferenceRotation" -- MTFramework boilerplate present in nearly every
    # layout. Corroborating with those would confirm "some GUI layout is
    # loaded", which we already knew, and would happily confirm the WRONG
    # layout. Prefer names that appear in few layouts; rarity is what makes a
    # second match evidence rather than noise.
    # A name in more than this many layouts is boilerplate and is not evidence
    # of WHICH layout is loaded. Emitting fewer corroborators is better than
    # padding the list with names that would confirm anything.
    CORROB_MAX_FREQ = 5

    def corroborators_for(lay, anchor):
        cands = [n for n in dict.fromkeys(lay.names)
                 if n != anchor and usable(n) and freq[n] <= CORROB_MAX_FREQ]
        cands.sort(key=lambda s: (freq[s], -len(s), s))
        return cands[:CORROB]

    rows = []
    for lay in layouts:
        anc = anchor_for(lay)
        disc, versus = (None, None) if anc else discriminator_for(lay)
        corr = corroborators_for(lay, anc or disc)
        rows.append((lay.name, anc, lay.is_interactive, len(lay.names),
                     disc, versus, corr))

    # Self-check: every emitted anchor must match its own layout's bytes and
    # NO other layout's, searched exactly as the C side searches -- NUL
    # delimited. This assertion is the thing that caught the texture-path
    # collision described above; without it the header looked correct and the
    # finder would have reported the wrong screen. Fail loudly here rather
    # than shipping a probe that misidentifies a menu.
    by_name = {lay.name: lay for lay in layouts}
    for name, anc, _i, _n, disc, versus, _c in rows:
        for label, probe in (("anchor", anc), ("discrim", disc)):
            if not probe:
                continue
            needle = b"\0" + probe.encode("ascii") + b"\0"
            own = by_name[name]
            if needle not in own.raw:
                sys.exit("FAIL: %s %s %r is not NUL-delimited in its own "
                         "layout" % (name, label, probe))
            if label == "anchor":
                clash = [o.name for o in layouts
                         if o is not own and needle in o.raw]
                if clash:
                    sys.exit("FAIL: %s anchor %r also occurs in %s"
                             % (name, probe, ", ".join(clash)))
            else:
                other = by_name[versus]
                if needle in other.raw:
                    sys.exit("FAIL: %s discrim %r also occurs in %s"
                             % (name, probe, versus))

    # Corroborators must be real NUL-delimited names in their own layout too,
    # or the finder would look for a string that cannot exist in memory and
    # under-count corroboration on a perfectly good pool.
    for name, _a, _i, _n, _d, _v, corr in rows:
        own = by_name[name]
        for c in corr:
            if (b"\0" + c.encode("ascii") + b"\0") not in own.raw:
                sys.exit("FAIL: %s corroborator %r is not NUL-delimited in "
                         "its own layout" % (name, c))

    # The focus vocabulary, taken straight from the classifier so the C side
    # and the Python side can never disagree.
    states = {}
    for lay in layouts:
        for n in lay.names:
            k = classify_state(n)
            if k:
                states[n] = k
    # Names that LOOK like states but explicitly are not -- emitted so the C
    # side can assert it never treats them as selections.
    decoys = sorted({n for lay in layouts for n in lay.names
                     if n.startswith(("SELECT", "DECIDE", "DEFAULT", "DISABLE"))
                     and classify_state(n) is None})

    L = []
    a = L.append
    a("/*")
    a(" * GENERATED by tools/gen_gui_probes.py -- do not edit by hand.")
    a(" *")
    a(" * Anchors for locating the live GUI tree, and the surveyed focus-state")
    a(" * vocabulary. Regenerate:  python tools/gen_gui_probes.py")
    a(" * Verify:                  python tools/gen_gui_probes.py --check")
    a(" */")
    a("#ifndef CFC2_GUI_PROBES_H")
    a("#define CFC2_GUI_PROBES_H")
    a("")
    a("/* ⚠ Every name here is searched NUL-DELIMITED (\\0name\\0).")
    a(" * A name unique among PARSED names need not be unique in BYTES:")
    a(" * \"icon_status_btl_BM_NOMIP\" is a name only in lobby_2, but the same")
    a(" * bytes end a texture path in `result`. Searching bare would report the")
    a(" * wrong screen. The generator asserts the delimited form. */")
    a("#define GUI_NCORROB %d" % CORROB)
    a("")
    a("typedef struct {")
    a("    const char *layout;      /* menu.arc resource name */")
    a("    const char *anchor;      /* name unique to this layout, else NULL */")
    a("    const char *discrim;     /* fallback: present here, absent in the")
    a("                              * most similar layout (see `versus`) */")
    a("    const char *versus;")
    a("    int         interactive; /* models SELECT/DECIDE */")
    a("    int         name_count;")
    a("    /* Other names from the SAME layout. One string matching proves")
    a("     * nothing -- a stale freed buffer matches too. Require several.")
    a("     * Only names appearing in <= %d layouts qualify, so these are"
      % CORROB_MAX_FREQ)
    a("     * evidence of WHICH layout is loaded, not just that one is.")
    a("     * `ncorrob` may be < GUI_NCORROB: a layout short of distinctive")
    a("     * names gets fewer rather than being padded with boilerplate. */")
    a("    int         ncorrob;")
    a("    const char *corrob[GUI_NCORROB];")
    a("} gui_layout_probe;")
    a("")
    a("static const gui_layout_probe g_gui_layouts[] = {")
    for name, anchor, inter, ncount, disc, versus, corr in sorted(rows):
        cl = ", ".join(c_str(c) for c in corr)
        if len(corr) < CORROB:
            cl += ", " + ", ".join(["NULL"] * (CORROB - len(corr)))
        a("    { %-20s %-27s %-18s %-18s %d, %d, %d,"
          % (c_str(name) + ",",
             (c_str(anchor) + ",") if anchor else "NULL,",
             (c_str(disc) + ",") if disc else "NULL,",
             (c_str(versus) + ",") if versus else "NULL,",
             1 if inter else 0, ncount, len(corr)))
        a("      { %s } }," % cl)
    a("};")
    a("#define GUI_NLAYOUTS (int)(sizeof(g_gui_layouts) / "
      "sizeof(g_gui_layouts[0]))")
    a("")
    a("/* Focus-state vocabulary. 'kind' is one of the GUI_ST_* values.")
    a(" * ⚠ Do NOT replace this with a prefix test: SELECTED_UNFOCUS and")
    a(" * SELECTED_DEFAULT start with \"SELECT\" but are NOT selections, and")
    a(" * SELECTED_F0CUS is spelled with a digit zero. See tools/gui.py. */")
    a("#define GUI_ST_NONE     0")
    a("#define GUI_ST_DEFAULT  1")
    a("#define GUI_ST_SELECT   2")
    a("#define GUI_ST_DECIDE   3")
    a("#define GUI_ST_DISABLE  4")
    a("")
    a("typedef struct { const char *name; int kind; } gui_state_name;")
    a("")
    a("static const gui_state_name g_gui_states[] = {")
    kindmap = {"default": "GUI_ST_DEFAULT", "select": "GUI_ST_SELECT",
               "decide": "GUI_ST_DECIDE", "disable": "GUI_ST_DISABLE"}
    for n in sorted(states):
        a("    { %-22s %s }," % (c_str(n) + ",", kindmap[states[n]]))
    a("};")
    a("#define GUI_NSTATES (int)(sizeof(g_gui_states) / "
      "sizeof(g_gui_states[0]))")
    a("")
    a("/* State-prefixed names that must NEVER be treated as a selection.")
    a(" * Present so the runtime can assert it, not merely intend it. */")
    a("static const char *const g_gui_state_decoys[] = {")
    for n in decoys:
        a("    %s," % c_str(n))
    a("};")
    a("#define GUI_NDECOYS (int)(sizeof(g_gui_state_decoys) / "
      "sizeof(g_gui_state_decoys[0]))")
    a("")
    a("#endif /* CFC2_GUI_PROBES_H */")
    return "\n".join(L) + "\n", rows, states, decoys


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--game-dir", default=DEFAULT_GAME_DIR)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    dest = os.path.join(os.path.dirname(here), "src", "gui_probes.h")

    text, rows, states, decoys = build(args.game_dir)

    if args.check:
        if not os.path.exists(dest):
            sys.exit("FAIL: %s missing" % dest)
        if open(dest, encoding="utf-8").read() != text:
            sys.exit("FAIL: %s is STALE -- regenerate" % dest)
        print("OK: %s matches a fresh parse of menu.arc" % dest)
        return

    with open(dest, "w", encoding="utf-8") as f:
        f.write(text)

    print("wrote %s" % dest)
    print("  %d layouts (%d interactive), %d state names, %d decoys"
          % (len(rows), sum(1 for r in rows if r[2]), len(states), len(decoys)))

    fallback = [r for r in rows if not r[1] and r[4]]
    for name, _a, inter, _n, disc, versus, _c in fallback:
        print("  fallback anchor: %-16s %r (absent from %s)%s"
              % (name, disc, versus, "  [INTERACTIVE]" if inter else ""))

    weak = [(r[0], len(r[6])) for r in rows if len(r[6]) < 3]
    for name, n in weak:
        print("  ⚠ weak corroboration: %-16s only %d distinctive name(s)"
              % (name, n))
        print("    a match there rests mostly on the anchor alone")

    stuck = [r[0] for r in rows if not r[1] and not r[4]]
    if stuck:
        print("  ⚠ NO anchor at all for: %s" % ", ".join(stuck))
        print("    these screens cannot be identified at runtime")


if __name__ == "__main__":
    main()
