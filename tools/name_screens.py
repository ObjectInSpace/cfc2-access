"""
Name unknown screens from a run's log, using the LIVE CAPTION channel.

⭐⭐ THE MECHANISM, and why it is general rather than one screen at a time.

The mod now captures the focused row's help caption live and logs it. The
corpus pairs a caption to its item by STEM -- `HELP_OFFLINE_PLAY` describes the
item `OFFLINE_PLAY` -- for 133 of the 187 prose captions. So a single caption
sighting yields the item's GMD INDEX directly:

    caption "Play offline."  ->  HELP_OFFLINE_PLAY  ->  OFFLINE_PLAY  ->  37

and the log states the POSITION the cursor was on when it arrived. Index minus
position gives the screen's base:

    idx 37, position 1  ->  base 37   (main menu -- the VERIFIED value)
    idx 51, position 1  ->  base 51   (Select Game row 0 -- also VERIFIED)

⛔ WHY THIS BEATS THE OLD ROUTE. `resolve_screens.py` intersects candidate bases
by plausibility and typically leaves several survivors for a human to match
against the screen; on the 6-item Online Play menu it left SIX, and the
corpus's own obvious 5-item run was not even among them. This route does not
guess: the caption NAMES the item, and the base falls out by subtraction.

⚠ WHAT IT STILL DOES NOT DO -- and this is deliberate.
It does not write a `g_screens` row. Every shipped row needs
MC_VERIFIED_LIVE_TEXT, and the discipline that earned that rule stands: a row
added on plausibility once made a different 6-item screen speak the main menu's
labels AND suppressed the discovery that would have exposed it.

What this gives is a base derived from EVIDENCE (a caption the game itself set
on a row the log timestamps), plus a CONSISTENCY CHECK across every sighting on
that screen. Two sightings agreeing on a base is strong; one is a lead.

⚠ It also reports screens whose sightings DISAGREE, which is the case that must
never be silently averaged into a plausible answer.

Usage:
    python tools/name_screens.py [path-to-CFC2Access.log]
"""

import os
import re
import sys
import collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmd import iter_gmds, parse_gmd  # noqa: E402

DEFAULT_GAME_DIR = (r"C:\Program Files (x86)\Steam\steamapps\common"
                    r"\CAPCOM FIGHTING COLLECTION 2")
DEFAULT_LOG = os.path.join(DEFAULT_GAME_DIR, "CFC2Access.log")

# "mcspeak: spoke "Offline Play, 1 of 6. Play offline.""
SPOKE = re.compile(r'mcspeak: spoke "(?P<line>.*)"\s*$')
# "mc: vertical   cursor 0 -> 1  (item 2 of 6, ...) caller=00000001400....."
MOVE = re.compile(r'mc: \w+\s+cursor .*?\(item (?P<pos>\d+) of (?P<total>\d+),'
                  r'.*?caller=(?P<caller>[0-9A-Fa-f]+)')
# Captions the mod refused to speak are still evidence of what was on screen.
REFUSED = re.compile(r'mcspeak: caption (?:AMBIGUOUS|not in the corpus), '
                     r'not spoken: "(?P<text>.*)"\s*$')


def load_corpus():
    arc = os.path.join(DEFAULT_GAME_DIR, "nativeDX11x64", "arc", "pc",
                       "msg.arc")
    if not os.path.isfile(arc):
        sys.exit("msg.arc not found: %s" % arc)
    for name, raw in iter_gmds(arc):
        if name.endswith("menu_eng"):
            _, _, table = parse_gmd(raw)
            return table
    sys.exit("menu_eng not found")


def build_caption_index(table):
    """{caption text: (item_label, item_index)} for captions that pair by stem.

    ⚠ A caption shared by several items is DROPPED, not resolved to one. Those
    are the same collisions prompt_id reports as AMBIGUOUS, and picking one
    here would reintroduce exactly the guess the mod refuses to make.
    """
    labels = list(table.keys())
    idx = {k: i for i, k in enumerate(labels)}

    by_text = collections.defaultdict(set)
    # ⛔⛔ COUNT THE CAPTION LABELS, NOT THE RESOLVED ITEMS.
    #
    # `HELP_GAME_VERSION` and `HELP_GAME_VERSION_ON` carry the SAME text
    # ("Choose a game version.") on two different screens -- offline and
    # online Select Game. Only the first has a matching item label, so counting
    # resolved items collapsed them to ONE and the caption looked unambiguous.
    # Using it would attribute the ONLINE screen's row to the OFFLINE one:
    # a wrong base, derived confidently, on a screen the user is standing on.
    #
    # ⭐ Caught by asserting the rule against the corpus rather than the log --
    # the 22:50 log contains no ambiguous caption, so the log-driven cases were
    # all green while this was broken. The mod's own prompt_id already refuses
    # this exact string; the two must agree.
    claimants = collections.Counter()
    for k in labels:
        if k.startswith("HELP_") and "<" not in table[k]:
            claimants[table[k]] += 1

    for k in labels:
        if not k.startswith("HELP_") or "<" in table[k]:
            continue
        if claimants[table[k]] > 1:
            continue                    # shared by several screens -- drop it
        stem = k[5:]
        # The stem may name the item directly, or with a MENU_ prefix; both
        # forms occur (OFFLINE_PLAY, but MENU_OP_SYSTEM for HELP_OP_SYSTEM).
        for cand in (stem, "MENU_" + stem):
            if cand in idx:
                by_text[table[k]].add((cand, idx[cand]))
                break

    out = {}
    for text, hits in by_text.items():
        if len(hits) == 1:
            out[text] = next(iter(hits))
    return out


def main():
    # ⚠ Windows consoles default to cp1252, which cannot encode the marker
    # glyphs used throughout this project's output. Reconfigure rather than
    # drop them: a tool that crashes on its own success message is worse than
    # useless, and stripping the markers would make this output inconsistent
    # with every other tool here.
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

    log_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_LOG
    if not os.path.isfile(log_path):
        sys.exit("log not found: %s" % log_path)

    table = load_corpus()
    captions = build_caption_index(table)

    # Walk the log. A caption is attributed to the movement it was spoken
    # with (same line) or the refusal immediately preceding a movement.
    sightings = collections.defaultdict(list)   # caller -> [(pos,total,text)]
    pending = None
    with open(log_path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            m = REFUSED.search(raw)
            if m:
                pending = m.group("text")
                continue
            m = SPOKE.search(raw)
            if m:
                line = m.group("line")
                # "Item, 1 of 6. Caption." -- the caption follows the POSITION.
                #
                # ⛔ Splitting on the first ". " is WRONG and produced a bogus
                # sighting: "Game, Capcom vs. SNK 2: Millionaire Fighting 2001,
                # 2 of 8" split inside the game's own title, yielding
                # "SNK 2: Millionaire..." as a caption. Anchor on the
                # "N of M" position instead, which is the actual delimiter.
                pending = None
                pm = re.search(r"\d+ of \d+\.\s+(?P<cap>.+)$", line)
                if pm:
                    pending = pm.group("cap")
                continue
            m = MOVE.search(raw)
            if m and pending is not None:
                sightings[m.group("caller").lstrip("0").lower()].append(
                    (int(m.group("pos")), int(m.group("total")), pending))
                pending = None

    print("screens with caption evidence in this log: %d" % len(sightings))
    print()

    for caller, seen in sorted(sightings.items()):
        print("0x%s  (%d sighting(s))" % (caller, len(seen)))
        bases = collections.Counter()
        unresolved = []
        for pos, total, text in seen:
            hit = captions.get(text)
            if hit is None:
                unresolved.append((pos, total, text))
                continue
            label, index = hit
            base = index - (pos - 1)
            bases[base] += 1
            print("   pos %d of %-2d  %-38r -> %-18s idx %d  => base %d"
                  % (pos, total, text[:36], label, index, base))
        for pos, total, text in unresolved:
            print("   pos %d of %-2d  %-38r -> (no unique item pairs with it)"
                  % (pos, total, text[:36]))

        if not bases:
            print("   ⚠ NO base derived: no sighting paired to an item.")
        elif len(bases) == 1:
            base, n = bases.most_common(1)[0]
            mark = "⭐ CONSISTENT" if n > 1 else "⚠ single sighting -- a LEAD"
            print("   => base %d  (%s across %d sighting(s))" % (base, mark, n))
            # Show what the whole screen would say, for eyeball comparison.
            labels = list(table.keys())
            total = seen[0][1]
            print("      it would speak:")
            for p in range(total):
                j = base + p
                if 0 <= j < len(labels):
                    print("        %d: %s" % (p + 1, table[labels[j]][:44]))
        else:
            # ⛔ Never average. Disagreement means the model is wrong here.
            print("   ⛔ SIGHTINGS DISAGREE: bases %s. This screen is NOT a "
                  "contiguous run from one base -- it needs a label MAP, like "
                  "Select Game. Do not pick one."
                  % ", ".join("%d(x%d)" % (b, n) for b, n in bases.most_common()))
        print()


if __name__ == "__main__":
    main()
