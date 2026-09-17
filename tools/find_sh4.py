"""Find SH-4 executable code inside CFC2's shipped ROM blobs.

WHY THIS EXISTS
---------------
Eighteen live runs failed to find the in-game selection by scanning memory, and
run 18 established there is no plain SH-4 image anywhere in the process -- the
emulator recompiles or transforms guest memory.

But the community Z3U NAOMI port archive proved the SH-4 code IS present in
CFC2's SHIPPED FILES: a ROM reassembled from them scores rts=276 sts=186
lds=219, unmistakable real code. So the executable is extractable offline even
though it is unrecognisable in RAM.

If CVS2's executable can be located the same way, it can be DISASSEMBLED, and
the character-select handler names the cursor's guest address directly -- read
out of the instruction that writes it, rather than guessed at from a shape.

WHAT MAKES A HIT TRUSTWORTHY
----------------------------
Not rts count, and not "many PC-relative loads". Those fire on compressed data
and on sprite tables -- an earlier sweep in this project read "exactly 3
prologues across 11 entries" as a finding when it was a threshold artifact.

The discriminator is BALANCE. Real SH-4 functions open with `sts.l pr,@-Rn` and
close with `lds.l @Rn+,pr`, so a code region has roughly equal counts of both.
Data that happens to contain those bit patterns does not. Ranking is therefore
by min(sts_pr, lds_pr), not by any single count.

⚠ CONTROLS RUN IN THE SAME INVOCATION. A scanner that cannot find KNOWN code
proves nothing about unknown code, and this project has shipped that confusion
before. --self-test asserts a real SH-4 ROM scores as code and random bytes do
not, and refuses to report scan results if either control fails.
"""
import argparse
import os
import random
import re
import struct
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sh4det import sh4_score, verdict            # noqa: E402

WINDOW = 64 * 1024


def balanced(s):
    """The count that separates code from coincidence: matched prologue and
    epilogue pairs. A region with 400 prologues and 0 epilogues is not code."""
    return min(s['sts_pr'], s['lds_pr'])


# ⭐⭐ CALIBRATED AGAINST REAL CODE, not chosen by eye.
#
# The real Z3U ROM scores 427-486 balanced pairs per 64 KB window. cvs2.ibis
# tops out at 42 -- 8% of that. A threshold of 20 called 14 cvs2 windows "real
# SH-4 code", which was wrong: it was calibrated against nothing.
#
# ⚠ And note rts alone is USELESS here: cvs2 reaches 486 rts against the real
# ROM's 536, because 0x000B is a single pattern that dense data produces by
# chance. Only the PAIRED constraint discriminates. Reporting on rts would have
# declared the executable found.
CODE_MIN = 200          # half the weakest real-ROM window, still 5x cvs2's best


def scan(buf, window=WINDOW, step=None):
    """Score every window. Returns (results, windows_examined).

    ⚠ windows_examined is returned so a caller can print a POSITIVE count of
    work done: "found nothing" and "looked at nothing" must never read alike.
    """
    step = step or window
    out = []
    n = 0
    for off in range(0, max(0, len(buf) - window + 1), step):
        b = buf[off:off + window]
        n += 1
        if b.count(0) > len(b) * 0.9:            # unwritten / padding
            continue
        s = sh4_score(b)
        out.append((off, s, verdict(s)))
    return out, n


def report(title, results, examined, limit=15):
    ranked = sorted(results, key=lambda r: -balanced(r[1]))
    hits = [r for r in ranked if balanced(r[1]) >= CODE_MIN]
    print("--- %s" % title)
    print("    %d window(s) examined, %d scored, %d with %d+ BALANCED pairs"
          % (examined, len(results), len(hits), CODE_MIN))
    for off, s, v in ranked[:limit]:
        if balanced(s) == 0:
            break
        print("      0x%08x  rts=%-4d nop=%-4d movlpc=%-5d sts=%-4d lds=%-4d "
              "balanced=%-4d  %s"
              % (off, s['rts'], s['nop'], s['mov_l_pc'], s['sts_pr'],
                 s['lds_pr'], balanced(s), v))
    if not hits:
        print("      (nothing with balanced prologue/epilogue pairs)")
    return hits


def load_z3u_rom(zip_path, cap=8 * 1024 * 1024):
    """The known-good positive control: a real NAOMI ROM built from CFC2 data."""
    with zipfile.ZipFile(zip_path) as z:
        for name in z.namelist():
            if name.endswith('SFZ3U Naomi ROM.dat'):
                with z.open(name) as f:
                    return f.read(cap)
    return None


def synth_sh4(words=32768, seed=2):
    """Synthetic SH-4, for the control when the real ROM is unavailable."""
    rnd = random.Random(seed)
    w = []
    for _ in range(words):
        r = rnd.random()
        if r < 0.22:
            w.append(0xD000 | (rnd.randrange(16) << 8) | rnd.randrange(256))
        elif r < 0.30:
            w.append(0x6003 | (rnd.randrange(16) << 8) | (rnd.randrange(16) << 4))
        elif r < 0.34:
            w.append(0x4022 | (rnd.randrange(16) << 8))
        elif r < 0.38:
            w.append(0x4026 | (rnd.randrange(16) << 8))
        elif r < 0.42:
            w.append(0x000B)
        elif r < 0.46:
            w.append(0x0009)
        else:
            w.append(rnd.randrange(65536))
    return struct.pack('<%dH' % len(w), *w)


def controls(z3u_zip):
    """Both controls, in the same invocation as the scan.

    Returns True only if the detector demonstrably discriminates RIGHT NOW.
    """
    ok = True

    rnd = bytes(random.Random(1).randrange(256) for _ in range(WINDOW))
    v = verdict(sh4_score(rnd))
    print("  negative control (random bytes)      : %s" % v)
    if v != 'not code':
        print("    FAIL -- random data scored as code; no verdict is trustworthy")
        ok = False

    real = load_z3u_rom(z3u_zip) if z3u_zip and os.path.exists(z3u_zip) else None
    if real:
        res, _ = scan(real)
        best = max((balanced(s) for _, s, _ in res), default=0)
        hits = sum(1 for _, s, _ in res if balanced(s) >= CODE_MIN)
        print("  positive control (REAL Z3U ROM)      : %d window(s) with %d+ "
              "balanced pairs, best=%d" % (hits, CODE_MIN, best))
        if hits == 0:
            print("    FAIL -- cannot find code in a ROM known to contain it")
            ok = False
    else:
        v = verdict(sh4_score(synth_sh4()))
        print("  positive control (synthetic SH-4)    : %s  "
              "(⚠ real ROM not supplied)" % v)
        if v != 'SH-4 CODE':
            print("    FAIL -- synthetic SH-4 not recognised")
            ok = False
    return ok


def afs_entries(blob):
    """CFC2's IBIS wrapper puts an AFS directory at 0x40. Verified format."""
    if blob[:4] != b'IBIS' or blob[0x40:0x44] != b'AFS\x00':
        return []
    n = struct.unpack_from('<I', blob, 0x44)[0]
    return [(i,) + struct.unpack_from('<II', blob, 0x48 + i * 8)
            for i in range(n)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('blob', help='cvs2.ibis or another extracted ROM blob')
    ap.add_argument('--z3u-zip', default=r'C:\Users\amock\Downloads'
                                         r'\z3u Port for NAOMI - wip.zip')
    ap.add_argument('--entries', action='store_true',
                    help='also score each AFS entry separately')
    args = ap.parse_args()

    print("CONTROLS")
    if not controls(args.z3u_zip):
        print("\nREFUSING to report scan results: the detector failed its own "
              "controls.\nA negative from a broken instrument is worse than no "
              "answer.")
        return 2
    print()

    blob = open(args.blob, 'rb').read()
    print("SCANNING %s (%.1f MB), %d KB windows"
          % (os.path.basename(args.blob), len(blob) / 1048576, WINDOW // 1024))
    print()
    results, examined = scan(blob)
    hits = report('whole blob', results, examined)

    if args.entries:
        print()
        ents = afs_entries(blob)
        print("--- per AFS entry (%d entries)" % len(ents))
        scored = []
        for i, off, size in ents:
            if size < WINDOW:
                continue
            b = blob[off:off + min(size, 1024 * 1024)]
            if b.count(0) > len(b) * 0.9:
                continue
            s = sh4_score(b)
            scored.append((balanced(s), i, off, size, s))
        scored.sort(reverse=True)
        print("    %d entry(s) scored; top by balanced pairs:" % len(scored))
        for bal, i, off, size, s in scored[:12]:
            if bal == 0:
                break
            print("      [%3d] off=0x%08x size=%9d  sts=%-4d lds=%-4d "
                  "balanced=%-4d" % (i, off, size, s['sts_pr'], s['lds_pr'], bal))
        if not scored or scored[0][0] == 0:
            print("      (no entry has balanced prologue/epilogue pairs)")

    print()
    if hits:
        print("VERDICT: %d window(s) look like real SH-4 code. Disassembly is "
              "the next step." % len(hits))
        return 0
    print("VERDICT: no SH-4 code found in %d window(s) examined." % examined)
    print("  ⚠ This is a real negative ONLY because both controls passed above.")
    return 1


if __name__ == '__main__':
    sys.exit(main())
