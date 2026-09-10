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
# ⭐ Reuse the generator's platform-suffix rule rather than restating it: the
# two must agree about which labels are the same screen, and a second copy is a
# second thing to drift.
from gen_prompt_table import canonical  # noqa: E402

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
    # ⭐ COLLAPSE PLATFORM VARIANTS BEFORE COUNTING CLAIMANTS.
    #
    # HELP_OP_MENU_BTN / _PS4 / _XBO / _NX are FOUR labels carrying the same
    # text for the SAME row -- they differ only by which controller glyph the
    # screen draws. Counting them as four claimants made the caption look
    # shared and dropped it, so the Options > System submenu lost a row name it
    # could have had. gen_prompt_table.py already collapses these; this did
    # not, and the two must agree.
    #
    # ⚠ The count is over CANONICAL names, so genuinely different screens
    # sharing a text (HELP_OPTION / HELP_OP_SYSTEM) are still dropped.
    claimants = collections.Counter()
    for k in labels:
        if k.startswith("HELP_") and "<" not in table[k]:
            claimants[table[k]] = 0
    seen_canon = collections.defaultdict(set)
    for k in labels:
        if k.startswith("HELP_") and "<" not in table[k]:
            seen_canon[table[k]].add(canonical(k))
    for text, names in seen_canon.items():
        claimants[text] = len(names)

    for k in labels:
        if not k.startswith("HELP_") or "<" in table[k]:
            continue
        if claimants[table[k]] > 1:
            continue                    # shared by several screens -- drop it
        stem = k[5:]
        # The stem may name the item directly, or with a MENU_ prefix; both
        # forms occur (OFFLINE_PLAY, but MENU_OP_SYSTEM for HELP_OP_SYSTEM).
        hit = None
        for cand in (stem, "MENU_" + stem):
            if cand in idx:
                hit = (cand, idx[cand])
                break

        # ⭐ THE HELP LABEL IS SOMETIMES ABBREVIATED. Measured in the corpus:
        #     HELP_OP_BGM_VOL   -> MENU_OP_BGM_VOLUME
        #     HELP_OP_SE_VOL    -> MENU_OP_SE_VOLUME
        #     HELP_OP_CHAT_VOL  -> MENU_OP_CHAT_VOLUME
        # Four sound rows lost their names to this, and no amount of extra
        # play would have fixed it -- the join rule was wrong, not the
        # evidence.
        #
        # ⛔ ONLY when the prefix matches EXACTLY ONE item. Several matches is
        # an ambiguity, not a lead, and picking one is the guess this whole
        # tool exists to avoid. HELP_OP_NETWORK stays unresolved on purpose:
        # its item is MENU_OP_NET, which no prefix of the stem reaches.
        if hit is None:
            pref = "MENU_" + stem
            cands = [k2 for k2 in labels if k2.startswith(pref)]
            if len(cands) == 1:
                hit = (cands[0], idx[cands[0]])

        if hit is not None:
            by_text[table[k]].add(hit)

    out = {}
    for text, hits in by_text.items():
        # ⚠ COLLAPSE THE RESOLVED ITEMS TOO, for the same reason as the
        # claimants above: HELP_OP_MENU_BTN{,_PS4,_XBO,_NX} resolve to
        # MENU_OP_MENU_BTN{,_PS4,_XBO,_NX} -- four labels, ONE row, differing
        # only by controller glyph. Counting them as four distinct items
        # dropped the caption at the last step even after the claimant fix.
        #
        # ⭐ Keep the BASE variant's index (the shortest canonical name), which
        # is the form a screen without a platform suffix uses.
        by_canon = {}
        for lab, i in hits:
            by_canon.setdefault(canonical(lab), []).append((lab, i))
        if len(by_canon) != 1:
            continue                    # genuinely different items -- drop
        group = next(iter(by_canon.values()))
        lab, i = min(group, key=lambda p: (len(p[0]), p[0]))
        out[text] = (lab, i)
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
            labels = list(table.keys())
            total = seen[0][1]

            # ⭐⭐ VALIDATE THE WHOLE RUN, not just the sighted position.
            #
            # ⛔ Two sightings AGREED on base 546 for the Options > System
            # submenu and the base was still WRONG: that screen's rows
            # interleave with their own help text (547, 549, 551 -- stride 2),
            # so position 1 would have spoken raw markup and position 3 a
            # HELP_ description. Agreement is not correctness when both
            # sightings are the SAME caption at the same position.
            #
            # A base is only usable if EVERY position it implies looks like a
            # menu item -- no markup, no terminal punctuation, not a HELP_
            # label. That is the same predicate the discovery sweep uses, and
            # it is what separates "a real run" from "a coincidence at one
            # index".
            bad = []
            for p in range(total):
                j = base + p
                if not (0 <= j < len(labels)):
                    bad.append((p + 1, "<out of range>"))
                    continue
                lab, txt = labels[j], table[labels[j]]
                if lab.startswith("HELP_"):
                    bad.append((p + 1, "%s is a DESCRIPTION" % lab))
                elif "<" in txt:
                    bad.append((p + 1, "%s carries MARKUP" % lab))
                elif not txt.strip():
                    bad.append((p + 1, "%s is blank" % lab))

            if bad:
                print("   ⛔ base %d REJECTED -- it does not describe a "
                      "contiguous run of menu items:" % base)
                for p, why in bad[:6]:
                    print("        position %d: %s" % (p, why))
                print("      This screen needs a label MAP (rows interleaved "
                      "with their own help text, like Select Game), or more "
                      "sightings at other positions. Do NOT ship this base.")
                print()
                continue

            mark = "⭐ CONSISTENT" if n > 1 else "⚠ single sighting -- a LEAD"
            print("   => base %d  (%s across %d sighting(s))" % (base, mark, n))
            # Show what the whole screen would say, for eyeball comparison.
            print("      it would speak:")
            for p in range(total):
                j = base + p
                if 0 <= j < len(labels):
                    print("        %d: %s" % (p + 1, table[labels[j]][:44]))

            # ⚠⚠ THE CHECK ABOVE CANNOT SEE A VALUE LABEL.
            #
            # "Windowed" and "Borderless Windowed" are item-shaped in every way
            # -- short, no markup, no punctuation -- but they are the VALUES of
            # the Display setting, not rows of the screen. base 218 on PC
            # Settings passes validation and is still wrong past position 2.
            #
            # ⭐ So the only positions this evidence actually establishes are
            # the ones a caption was SEEN at. Say which those are, so the base
            # is not read as verified for the whole screen.
            sighted = sorted({p for p, _, txt in seen if captions.get(txt)})
            unsighted = [p for p in range(1, total + 1) if p not in sighted]
            if unsighted:
                print("      ⚠ ONLY position(s) %s are established by a "
                      "caption. %s unverified -- a VALUE label ('Windowed') is "
                      "item-shaped and passes the check above, so visit those "
                      "positions before shipping this."
                      % (", ".join(map(str, sighted)),
                         "Position(s) " + ", ".join(map(str, unsighted)) +
                         " are"))
        else:
            # ⛔ Never average. Disagreement means the model is wrong here.
            print("   ⛔ SIGHTINGS DISAGREE: bases %s. This screen is NOT a "
                  "contiguous run from one base -- it needs a label MAP, like "
                  "Select Game. Do not pick one."
                  % ", ".join("%d(x%d)" % (b, n) for b, n in bases.most_common()))
        print()


if __name__ == "__main__":
    main()
