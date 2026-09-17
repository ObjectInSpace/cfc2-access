"""Analyse a captured emulator framebuffer.

WHY THIS EXISTS
---------------
Every attempt to READ the in-game selection out of memory has failed across 18
runs, and the accumulated evidence says the emulator does not keep a plain guest
image at all. But the SELECTION IS VISIBLE: the user reports "the pulsing red or
blue outline around the character moves to the other character".

Run 10 proved arena+0x8900000 is ARGB1555 pixel data -- 438 distinct colours,
alpha set throughout, 74% of bytes changing per frame, with a second copy at
+0xa400000 matching at shift -0x18. That is the live screen.

So: find the outline, read its grid cell, index the roster, speak the name. No
dead reckoning, no drift, and it re-reads the truth every frame.

WHAT THIS TOOL DECIDES, AND WHAT IT MUST NOT ASSUME
---------------------------------------------------
⛔ It must NOT assume the buffer is a linear left-to-right image. Dreamcast/NAOMI
video memory is frequently TWIDDLED (Morton order) or tiled, and a twiddled
buffer read linearly produces a plausible-looking but wrong picture -- which
would put the cursor in the wrong cell and speak the wrong character. That is
the worst failure available here.

So the tool reports EVIDENCE about layout rather than rendering a picture and
inviting a human to squint at it:

  - the colour histogram (is it really ARGB1555?)
  - ROW AUTOCORRELATION across candidate widths: a linear image of a screen has
    strong correlation at its true stride and weak correlation elsewhere. A
    twiddled buffer has no such peak.
  - where the saturated reds and blues are, since the user named those.

⚠ A width that "looks about right" is not a measurement. The autocorrelation
peak is, and if there is no clear peak this tool says so rather than picking the
best of a bad set.
"""
import argparse
import collections
import os
import struct
import sys


def words(buf):
    n = len(buf) // 2
    return struct.unpack('<%dH' % n, buf[:n * 2])


def argb1555(w):
    """(a, r, g, b) with r/g/b in 0..31."""
    return (w >> 15) & 1, (w >> 10) & 31, (w >> 5) & 31, w & 31


def describe(buf, label):
    w = words(buf)
    c = collections.Counter(w)
    alpha = sum(1 for x in w if x & 0x8000)
    print("--- %s" % label)
    print("    %d words, %d distinct values, %.1f%% with bit15 set"
          % (len(w), len(c), 100.0 * alpha / max(len(w), 1)))
    top = c.most_common(4)
    print("    most common: %s"
          % ', '.join('0x%04x x%d' % (k, v) for k, v in top))

    # ⚠ THE CHECK THAT CAUGHT A FALSE POSITIVE BEFORE. 0xCDCD is MSVC's
    # uninitialised-heap fill: 100% of words have bit15 set, exactly like
    # ARGB1555, and a naive reader calls it "dense pixel data". One distinct
    # value across thousands of words is a fill pattern, not an image.
    if len(c) <= 4:
        print("    ⛔ ONLY %d DISTINCT VALUE(S) -- this is a FILL PATTERN, not "
              "an image (0xCDCD = never-written heap)." % len(c))
        return False
    if len(c) < 64:
        print("    ⚠ very few distinct colours; unlikely to be a real frame")
    return True


def row_autocorr(buf, width_words):
    """Pearson correlation between each row and the one above it, per channel.

    ⛔⛔ AN EARLIER VERSION OF THIS FAILED ITS OWN POSITIVE CONTROL, and the way
    it failed is worth keeping. It used mean absolute difference on the raw
    15-bit word, and on a known 640px test image it confidently picked 800px.
    Two reasons:

      - a SMOOTH image has small row-to-row differences at EVERY stride, so
        low difference does not indicate the right one;
      - packing r,g,b into one integer makes |a-b| meaningless -- one step of
        red is 1024, one step of blue is 1, so the metric only ever saw red.

    Correlation fixes both. At the TRUE stride, pixel x of row r sits directly
    below pixel x of row r-1, so the two rows correlate strongly. At a wrong
    stride they are unrelated samples and correlation collapses toward zero --
    a contrast that does not depend on the image being smooth.

    Higher score = more likely the true width.
    """
    w = words(buf)
    if width_words < 8 or len(w) < width_words * 8:
        return None
    rows = min(len(w) // width_words, 256)
    if rows < 4:
        return None

    xs, ys = [], []
    for r in range(1, rows):
        a = r * width_words
        b = (r - 1) * width_words
        for i in range(0, width_words, 2):
            for shift, mask in ((10, 31), (5, 31), (0, 31)):
                xs.append((w[a + i] >> shift) & mask)
                ys.append((w[b + i] >> shift) & mask)

    n = len(xs)
    if n < 32:
        return None
    mx = sum(xs) / n
    my = sum(ys) / n
    num = sum((xs[i] - mx) * (ys[i] - my) for i in range(n))
    dx = sum((v - mx) ** 2 for v in xs)
    dy = sum((v - my) ** 2 for v in ys)
    if dx <= 0 or dy <= 0:
        return None
    return num / ((dx ** 0.5) * (dy ** 0.5))


def find_width(buf, candidates):
    print("--- row correlation (HIGHER = better; a real image correlates "
          "strongly with itself one stride apart)")
    scores = []
    for width in candidates:
        s = row_autocorr(buf, width)
        if s is None:
            continue
        scores.append((s, width))
    scores.sort(reverse=True)          # higher correlation is better
    for s, width in scores[:10]:
        print("      width %4d px  r = %+.4f" % (width, s))

    if len(scores) < 2:
        print("      (not enough data)")
        return None

    best, second = scores[0], scores[1]
    # ⚠ A "best" that is barely better than the next is not a measurement.
    # Require a real margin AND a meaningful absolute correlation: an image
    # correlates strongly with itself, so a winner at r=0.05 is noise even if
    # it beats the field.
    margin = (best[0] - second[0]) / max(abs(best[0]), 1e-9)
    if best[0] < 0.30:
        print()
        print("    ⛔ NO CANDIDATE CORRELATES (best r = %+.4f at %d px)."
              % (best[0], best[1]))
        print("       A linear image scores far higher. This slice is probably "
              "TWIDDLED/tiled, or is not a frame.")
        return None
    print()
    if margin < 0.05:
        print("    ⛔ NO CLEAR PEAK (best %d px, r=%+.4f, beats next by only "
              "%.1f%%)." % (best[1], best[0], margin * 100))
        print("       The buffer is probably TWIDDLED/tiled rather than a "
              "linear image, or this slice is not a frame.")
        print("       ⚠ Do NOT pick a width from this -- a wrong stride puts "
              "the cursor in the wrong cell.")
        return None
    print("    ⭐ width %d px stands out: r = %+.4f, %.0f%% above the next "
          "candidate" % (best[1], best[0], margin * 100))
    return best[1]


def find_saturated(buf, width_words, limit=12):
    """Where are the strong reds and blues? The user named those colours."""
    w = words(buf)
    reds, blues = [], []
    for i, x in enumerate(w):
        _, r, g, b = argb1555(x)
        if r >= 24 and g <= 10 and b <= 10:
            reds.append(i)
        elif b >= 24 and r <= 10 and g <= 14:
            blues.append(i)
    print("--- saturated pixels (the user reports a RED or BLUE outline)")
    for name, lst in (('red', reds), ('blue', blues)):
        print("    %-5s %d pixel(s)" % (name, len(lst)))
        if lst and width_words:
            xs = [i % width_words for i in lst[:5000]]
            ys = [i // width_words for i in lst[:5000]]
            print("          x %d..%d   y %d..%d"
                  % (min(xs), max(xs), min(ys), max(ys)))
    return reds, blues


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+', help='cfc2_ram_fb_a.bin etc')
    ap.add_argument('--widths', default='320,352,384,400,512,600,640,720,800',
                    help='candidate widths in PIXELS')
    args = ap.parse_args()

    cands = [int(x) for x in args.widths.split(',')]

    for path in args.files:
        if not os.path.exists(path):
            print("missing: %s" % path)
            continue
        buf = open(path, 'rb').read()
        print("=" * 70)
        print("%s  (%d KB)" % (os.path.basename(path), len(buf) // 1024))
        print("=" * 70)
        if not describe(buf, 'content'):
            print()
            continue
        print()
        width = find_width(buf, cands)
        print()
        find_saturated(buf, width)
        print()

    return 0


if __name__ == '__main__':
    sys.exit(main())
