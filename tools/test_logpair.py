"""
Self-test for logpair.py, built from the REAL readings it exists to prevent.

⭐⭐ Every case here is a wrong answer I actually produced and acted on. A
helper that merely "looks careful" is worth nothing; these assert that the
specific artefacts are rejected and that the correct pairings still resolve.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import logpair  # noqa: E402

failures = 0


def check(name, cond, detail=""):
    global failures
    if cond:
        print(f"  PASS  {name}")
        return
    print(f"  FAIL  {name}" + (f"  {detail}" if detail else ""))
    failures += 1


def write(tmp, lines):
    with open(tmp, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

    tmp = os.path.join(ROOT, "build", "test_logpair.log")
    os.makedirs(os.path.dirname(tmp), exist_ok=True)

    # --- case 1: the row comes from the CURSOR ADDRESS, not from order ------
    # Real numbers: Casual Match, screen 0x2BDFDD60, values_off 0x344.
    # Row 1's value cursor is screen+0x344+4.
    write(tmp, [
        '[00:00:01.000] mc: horizontal cursor 0 -> 1  (item 2 of 3, range 0..2,'
        ' step +1)  caller=0000000140098986 screen=000000002BDFDD60'
        ' cursor=000000002BDFE0A8',
    ])
    mv = logpair.movements(tmp)[0]
    check("1. the row is derived from the cursor address",
          mv.row(0x344) == 1, f"got {mv.row(0x344)}")

    # --- case 2: misaligned arithmetic REFUSES rather than rounding ---------
    # ⛔ A wrong row names the wrong setting. Two bytes off must yield None,
    # not the nearest plausible row.
    write(tmp, [
        '[00:00:01.000] mc: horizontal cursor 0 -> 1  (item 2 of 3, range 0..2,'
        ' step +1)  caller=0000000140098986 screen=000000002BDFDD60'
        ' cursor=000000002BDFE0AA',
    ])
    mv = logpair.movements(tmp)[0]
    check("2. a misaligned cursor refuses instead of rounding",
          mv.row(0x344) is None, f"got {mv.row(0x344)}")

    # --- case 3: THE AXIS-BOUNDARY BUG -------------------------------------
    # ⛔⛔ The reading that nearly shipped a wrong game mapping. A caption
    # belonging to a 4-item VERTICAL list must not attach to the 8-item
    # HORIZONTAL strip that moved at a different instant.
    write(tmp, [
        '[00:00:01.000] mc: horizontal cursor 1 -> 2  (item 3 of 8, range 0..7,'
        ' step +1)  caller=0000000140098986 screen=000000002BDFDD60'
        ' cursor=000000002BDFE0A4',
        '[00:00:01.500] gt: * TEXT SET on 000000002C2B7C10: "Capcom Fighting'
        ' Evolution"',
        '[00:00:02.000] mc: vertical   cursor 0 -> 1  (item 2 of 4, range 0..3,'
        ' step +1)  caller=000000014009890D screen=000000002BDFDD60'
        ' cursor=000000002BDFDF94',
    ])
    pairs = list(logpair.paired(tmp))
    strip = [p for p in pairs if p[0].caller == "140098986"][0]
    check("3. text 0.5s later does NOT attach to the strip's movement",
          strip[2] == [], f"got {strip[2]}")

    # --- case 4: same-timestamp text DOES attach ---------------------------
    # The positive control: without this, case 3 would pass on a helper that
    # simply never pairs anything.
    write(tmp, [
        '[00:00:03.000] mc: horizontal cursor 1 -> 2  (item 3 of 9, range 0..8,'
        ' step +1)  caller=000000014009A482 screen=000000002C477020'
        ' cursor=000000002C47735C',
        '[00:00:03.000] gt: * TEXT SET on 000000002C47AF90: "Capcom Vs. SNK 2"',
    ])
    pairs = list(logpair.paired(tmp))
    check("4. text at the SAME timestamp does attach",
          pairs[0][2] == [("000000002C47AF90", "Capcom Vs. SNK 2")],
          f"got {pairs[0][2]}")

    # --- case 5: THE PICKER REVERSAL ---------------------------------------
    # ⛔⛔ "20 of 20 off by one" came from pairing a caption logged BEFORE the
    # movement line. Keyed on the timestamp the character matches exactly.
    # Real ordering from run-2026-09-10-1913: the caption line, the spoke line,
    # the mc: line and the TEXT SET all share one timestamp.
    write(tmp, [
        '[00:00:04.000] mcspeak: caption not in the corpus, not spoken: "X"',
        '[00:00:04.000] mc: vertical   cursor 34 -> 33  (item 34 of 36,'
        ' range 0..35, step -1)  caller=000000014008A8EF'
        ' screen=000000002C440110 cursor=000000002C440444',
        '[00:00:04.000] gt: * TEXT SET on 000000002C500920: "X"',
    ])
    mv, row, txt = list(logpair.paired(tmp))[0]
    K = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    drew = [t for _, t in txt]
    check("5. the picker's character matches its position exactly",
          mv.pos == 34 and drew == ["X"] and K[mv.pos - 1] == "X",
          f"pos={mv.pos} drew={drew}")

    print()
    print("ALL PASSED" if not failures else f"FAILED: {failures} case(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
