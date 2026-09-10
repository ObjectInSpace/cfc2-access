"""
Self-test for tools/name_screens.py.

⭐⭐ THE CONTROLS ARE FREE AND REAL. Two screens in the 22:50 run already have
VERIFIED bases in g_screens -- the main menu (37) and Select Game's row 0 (51).
So the tool can be checked against ground truth it did not supply, using a real
log rather than a synthetic one.

⛔ A namer that cannot reproduce a KNOWN base must not be trusted on an unknown
one. That is the whole point of this file: "it printed something plausible" is
not evidence, and this project has shipped a wrong screen name from exactly
that standard.

Run:  python tools/test_name_screens.py
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LOG = os.path.join(ROOT, "docs", "run-2026-09-09-2250.log")

# caller -> (expected base, expected minimum sightings)
# Both are MEASURED values already shipped in src/menu_speak.c as
# MC_VERIFIED_LIVE_TEXT rows.
EXPECTED = {
    "140097a59": 37,    # main menu -- 6 items from OFFLINE_PLAY
    "1400b1b69": 51,    # Select Game, row 0 = GAME_TITLE
}


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

    if not os.path.isfile(LOG):
        sys.exit("reference log missing: %s" % LOG)

    r = subprocess.run([sys.executable,
                        os.path.join(HERE, "name_screens.py"), LOG],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    if r.returncode != 0:
        print(r.stdout)
        print(r.stderr)
        sys.exit("name_screens.py failed")

    out = r.stdout
    failed = 0
    checked = 0

    # Parse "0x140097a59  (N sighting(s))" then its "=> base B" line.
    found = {}
    current = None
    for line in out.splitlines():
        m = re.match(r"^0x([0-9a-f]+)\s+\((\d+) sighting", line)
        if m:
            current = m.group(1)
            continue
        m = re.search(r"=> base (\d+)", line)
        if m and current:
            found[current] = int(m.group(1))
            current = None

    for caller, want in EXPECTED.items():
        checked += 1
        got = found.get(caller)
        if got == want:
            print("ok    0x%s -> base %d (matches the verified row)"
                  % (caller, got))
        else:
            print("FAIL  0x%s -> base %s, expected %d" % (caller, got, want))
            failed += 1

    # ⛔ THE DISCRIMINATING NEGATIVE. A spoken line can contain ". " inside a
    # game's own title ("Capcom vs. SNK 2: Millionaire Fighting 2001"), and
    # splitting on the first one invented a caption of "SNK 2: Millionaire..."
    # -- a sighting for a screen that had none. The parser must anchor on the
    # "N of M" position instead.
    checked += 1
    if "SNK 2" in out:
        print("FAIL  a game TITLE was parsed as a caption "
              "(the '. ' split bug is back)")
        failed += 1
    else:
        print("ok    a game title containing '. ' is not parsed as a caption")

    # Liveness: an empty result would pass every negative above vacuously.
    checked += 1
    if len(found) < 2:
        print("FAIL  only %d screen(s) resolved -- the tool found almost "
              "nothing, so the checks above prove little" % len(found))
        failed += 1
    else:
        print("ok    %d screens resolved from the log" % len(found))

    # ⛔ THE AMBIGUITY RULE, asserted against the CORPUS rather than the log.
    #
    # A caption shared by several items must be DROPPED from the index, not
    # resolved to one -- picking would reintroduce the guess the mod itself
    # refuses to make. The 22:50 log happens to contain no ambiguous caption,
    # so sabotaging that rule left this suite GREEN. Found by the sabotage
    # sweep; asserted here directly so the mechanism is actually covered.
    checked += 1
    sys.path.insert(0, HERE)
    from name_screens import load_corpus, build_caption_index  # noqa: E402
    table = load_corpus()
    index = build_caption_index(table)
    # "Choose a game version." is produced by both HELP_GAME_VERSION and
    # HELP_GAME_VERSION_ON -- the collision prompt_id also reports, and the one
    # the mod refused to speak in the live run.
    shared = "Choose a game version."
    if shared in index:
        print("FAIL  an AMBIGUOUS caption %r resolved to %s -- it must be "
              "dropped, not picked" % (shared, index[shared][0]))
        failed += 1
    else:
        print("ok    an ambiguous caption is dropped from the index, not "
              "resolved to one item")

    # ⛔⛔ DISAGREEING SIGHTINGS MUST NOT BE AVERAGED INTO AN ANSWER.
    #
    # If two captions on one screen imply different bases, the screen is not a
    # contiguous run and picking either would speak a wrong item name. The
    # 22:50 log has no such screen, so this path was completely untested --
    # sabotaging it left the suite green. A small synthetic log exercises it.
    #
    # ⚠ The fixture is synthetic ONLY because no real log has this shape yet.
    # Its captions and indices are real corpus values, so what it exercises is
    # the tool's logic, not a made-up corpus.
    checked += 1
    fixture = os.path.join(HERE, "fixtures", "disagree.log")
    if not os.path.isfile(fixture):
        print("FAIL  fixture missing: %s" % fixture)
        failed += 1
    else:
        rf = subprocess.run([sys.executable,
                             os.path.join(HERE, "name_screens.py"), fixture],
                            capture_output=True, text=True, encoding="utf-8",
                            errors="replace")
        # ⚠ The per-sighting lines legitimately end in "=> base N" -- that is
        # the working shown for each caption. What must be ABSENT is the
        # VERDICT line ("   => base N  (...)"), which is what a caller would
        # act on. Asserting on the raw substring failed a correct refusal.
        verdict = re.search(r"^\s+=> base \d+\s+\(", rf.stdout, re.M)
        if "SIGHTINGS DISAGREE" in rf.stdout and not verdict:
            print("ok    disagreeing sightings are reported, not resolved")
        else:
            print("FAIL  disagreeing sightings did not refuse: %s"
                  % rf.stdout.strip().splitlines()[-1:])
            failed += 1

    print()
    print("%d checked, %d failed" % (checked, failed))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
