"""
Cross-check the C translator against the Python oracle.

⭐ WHY THIS EXISTS. There are two implementations of the same rules:

  - tools/markup.py  -- verified against the WHOLE corpus (58310 strings, 14
    languages, 0 residue) and against the real live captures. This is the
    oracle: it is the one with corpus-wide evidence behind it.
  - src/gui_markup.c -- what actually runs in the game, because the mod is C.

Two implementations of one rule set drift. The C suite pins its expectations as
literals, which catches a change in C but NOT a change in Python -- if the
oracle's rules move, the literals silently become stale and the C keeps passing
against yesterday's answer.

So this feeds the SAME inputs to both and requires identical output. It is the
only check that fails when the two disagree.

⚠ It shells out to a small C driver rather than reimplementing anything: a
checker that re-derives the behaviour it is checking proves nothing (this
project has shipped that mistake -- see harness-supplies-the-mechanism).

Run:  python tools/check_markup_parity.py
"""

import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

sys.path.insert(0, HERE)
from markup import (Translator, load_legends, load_legends_per_game,  # noqa: E402
                    strip_hint_stutter)

GCC = os.path.expandvars(
    r"%LOCALAPPDATA%\Microsoft\WinGet\Packages"
    r"\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe"
    r"\mingw64\bin\gcc.exe")

# A driver that reads one input per line and prints one output per line.
DRIVER = r"""
#include <stdio.h>
#include <string.h>
#include "../src/gui_markup.h"

int main(void)
{
    char line[2048];
    while (fgets(line, sizeof(line), stdin)) {
        size_t n = strlen(line);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
        char out[GM_OUT_MAX];
        gm_speak(line, out, sizeof(out));
        printf("%s\n", out);
    }
    return 0;
}
"""


def main():
    path = os.path.join(ROOT, "data", "text_english.json")
    if not os.path.isfile(path):
        sys.exit("run tools/extract_gmd.py first")
    with open(path, encoding="utf-8") as f:
        strings = json.load(f)

    tr = Translator(load_legends(strings), load_legends_per_game(strings))

    # Every corpus string that carries markup, plus the live captures.
    # ⚠ Multi-line strings are skipped: the driver is line-based, and a
    # line-based harness cannot represent them. They are COUNTED and reported
    # rather than quietly dropped -- "0 failed" must not hide "not checked".
    inputs, skipped = [], 0
    for label in sorted(strings):
        t = strings[label]
        if "<" not in t:
            continue
        if "\n" in t or "\r" in t:
            skipped += 1
            continue
        inputs.append(t)

    LIVE = [
        "<FONT 2>PRESS <SIZE 80><ICON DECIDE></SIZE> TO START</FONT>",
        "<SIZE 58><ICON DECIDE></SIZE>Confirm <SIZE 58><ICON CANCEL></SIZE>Back",
        "Choose the game you want to play.  <SIZE 58><ICON BTN_START></SIZE>"
        "Training <SIZE 58><ICON BTN_X></SIZE>Game Settings "
        "<SIZE 58><ICON CANCEL></SIZE>Back",
        "P1",
        # ⭐⭐ THE MOVE LIST'S x=633 COLUMN -- condition AND input in ONE string.
        # Verbatim from docs/run-2026-09-11-2228 and -2055 (untruncated).
        #
        # ⛔ These exist because the corpus has ZERO motion tokens, so the 239
        # corpus inputs left every C_236/C_63214/C_6248 UNTESTED -- and the
        # Python oracle turned out to have no motion table at all while parity
        # still read PASS. A superset vocabulary needs superset cases.
        " During Hyakkishu<SIZE 58><ICON C_K></SIZE>",
        " During Hyakkishu (No Additional Input)",
        " During Jump<SIZE 58><ICON C_236></SIZE>+<SIZE 58><ICON C_P></SIZE>",
        " Near Opponent<SIZE 58><ICON C_4></SIZE>/<SIZE 58><ICON C_6></SIZE>"
        "+<SIZE 58><ICON C_HK></SIZE>",
        " Near Opponent<SIZE 58><ICON C_6248></SIZE>+<SIZE 58><ICON C_P></SIZE>",
        " At Close Range<SIZE 58><ICON C_63214></SIZE>+<SIZE 58><ICON C_K></SIZE>",
        "<SIZE 58><ICON C_1T></SIZE><SIZE 58><ICON C_319></SIZE>"
        "+<SIZE 58><ICON C_K></SIZE>",
        "<SIZE 58><ICON C_412></SIZE>+<SIZE 58><ICON C_P></SIZE>",
        "<SIZE 58><ICON C_2141236></SIZE>+<SIZE 58><ICON C_P></SIZE>",
        "<SIZE 58><ICON C_2363214></SIZE>+<SIZE 58><ICON C_P></SIZE>",
        "<SIZE 58><ICON C_236></SIZE><SIZE 58><ICON C_23></SIZE>"
        "+<SIZE 58><ICON C_P></SIZE>",
        "<SIZE 58><ICON C_1></SIZE>+<SIZE 58><ICON C_HK></SIZE>",
        " During Cling to Mamahaha<SIZE 58><ICON C_HK></SIZE>",
        " At Height of Neutral/Forward Jump<SIZE 58><ICON C_3></SIZE>"
        "+<SIZE 58><ICON C_P></SIZE>",
        # A BLANKED pooled pair object: the game sets one space, not "".
        " ",
    ]
    inputs.extend(LIVE)

    build = os.path.join(ROOT, "build", "test")
    os.makedirs(build, exist_ok=True)
    drv_c = os.path.join(HERE, "_parity_driver.c")
    drv_exe = os.path.join(build, "parity_driver.exe")
    with open(drv_c, "w", encoding="ascii") as f:
        f.write(DRIVER)
    try:
        r = subprocess.run(
            [GCC, "-O2", "-w", "-o", drv_exe, drv_c,
             os.path.join(ROOT, "src", "gui_markup.c")],
            capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit("driver build failed:\n" + r.stderr)

        payload = "\n".join(inputs) + "\n"
        r = subprocess.run([drv_exe], input=payload,
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit("driver failed: %s" % r.stderr)
        c_out = r.stdout.split("\n")
    finally:
        if os.path.exists(drv_c):
            os.remove(drv_c)

    mismatches = []
    for i, src in enumerate(inputs):
        want = strip_hint_stutter(src, tr)
        got = c_out[i] if i < len(c_out) else "<MISSING>"
        if want != got:
            mismatches.append((src, want, got))

    print("markup parity: C vs the Python oracle")
    print("-------------------------------------")
    print("%d inputs checked (%d corpus + %d live), %d multi-line skipped"
          % (len(inputs), len(inputs) - len(LIVE), len(LIVE), skipped))

    for src, want, got in mismatches[:20]:
        print()
        print("  in     : %s" % src[:100])
        print("  python : %s" % want[:100])
        print("  c      : %s" % got[:100])

    print()
    if mismatches:
        print("RESULT: FAIL -- %d of %d disagree"
              % (len(mismatches), len(inputs)))
        sys.exit(1)
    print("RESULT: PASS -- the two implementations agree on every input")


if __name__ == "__main__":
    main()
