"""What actually stops changing when CVS2 is PAUSED?

WHY THIS EXISTS
---------------
Pause suppression has now failed twice on reasoning rather than measurement:

  1. Watched the frame counter at phase+0x18 -- but the HOOKED FUNCTION is
     what increments it, so the test was false by construction every frame.
  2. Watched a composite of fighter X, round timer and animation bytes. The
     liveness counter proves that fires (9 episodes in one session), but the
     user's actual pauses are still not suppressed -- so during a real pause
     something in that signature is still moving, and the 9 were probably
     round-start/KO freezes instead.

⭐ Stop guessing which field freezes. MEASURE which bytes of the state block
stop changing while paused, and compare against unpaused play.

HOW TO USE IT
-------------
    python tools/probe_pause.py <pid> --label unpaused    # during normal play
    python tools/probe_pause.py <pid> --label paused      # while PAUSED

Each run samples the whole state block repeatedly over a few seconds and
records, per byte, whether it EVER changed. Then:

    python tools/probe_pause.py --compare

prints bytes that change while unpaused and are FROZEN while paused. Those
are the candidates for a pause signal -- chosen by evidence, not by which
field name sounds right.

This only READS memory; it never writes to the game.
"""
import argparse
import ctypes
import json
import os
import struct
import sys
import time

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
k = ctypes.windll.kernel32

ROOT_PTR = 0x1422DEBD0
SESSION_PTR = 0x141C14F98
STATE_SIZE = 0x216B0
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "build", "cvs2-state")


def read(h, addr, size):
    buf = ctypes.create_string_buffer(size)
    got = ctypes.c_size_t(0)
    ok = k.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size,
                             ctypes.byref(got))
    return buf.raw if ok and got.value == size else None


def sample(pid, label, seconds, hz):
    h = k.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        raise SystemExit("OpenProcess failed")
    root_b = read(h, ROOT_PTR, 8)
    if not root_b:
        raise SystemExit("cannot read the root pointer")
    root = struct.unpack("<Q", root_b)[0]
    if not root:
        raise SystemExit("root pointer is null -- is a CVS2 match running?")
    print("root = 0x%X" % root)

    first = read(h, root, STATE_SIZE)
    if not first:
        raise SystemExit("cannot read the state block")
    changed = bytearray(STATE_SIZE)      # 1 = this byte moved at least once
    n = 0
    t_end = time.time() + seconds
    while time.time() < t_end:
        cur = read(h, root, STATE_SIZE)
        if cur is None:
            continue
        n += 1
        for i in range(STATE_SIZE):
            if cur[i] != first[i]:
                changed[i] = 1
        time.sleep(1.0 / hz)

    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, "pauseprobe-%s.bin" % label)
    open(path, "wb").write(bytes(changed))
    moved = sum(changed)
    print("%d samples over %.1fs; %d of %d bytes changed (%.2f%%)"
          % (n, seconds, moved, STATE_SIZE, 100.0 * moved / STATE_SIZE))
    print("wrote %s" % path)


def compare():
    up = os.path.join(OUT_DIR, "pauseprobe-unpaused.bin")
    pa = os.path.join(OUT_DIR, "pauseprobe-paused.bin")
    for p in (up, pa):
        if not os.path.exists(p):
            raise SystemExit("missing %s -- run both labels first" % p)
    u = open(up, "rb").read()
    q = open(pa, "rb").read()

    # Bytes that move during play but are frozen while paused.
    cands = [i for i in range(min(len(u), len(q))) if u[i] and not q[i]]
    print("bytes changing UNPAUSED: %d" % sum(u))
    print("bytes changing PAUSED  : %d" % sum(q))
    print("FROZEN BY PAUSE        : %d" % len(cands))
    print()
    if not cands:
        print("⛔ Nothing freezes. Either the pause menu keeps the whole block")
        print("   ticking, or the samples were mislabelled. Do NOT invent a")
        print("   signal -- re-run and check the labels first.")
        return

    B, S = 0x1EBA8, 0x678
    print("First 40 candidates (player-struct offsets called out):")
    for i in cands[:40]:
        tag = ""
        for pl in (0, 1):
            lo = B + pl * S
            if lo <= i < lo + S:
                tag = "  P%d B+0x%03X" % (pl + 1, i - lo)
        print("  R+0x%05X%s" % (i, tag))
    if len(cands) > 40:
        print("  ... and %d more" % (len(cands) - 40))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pid", nargs="?", type=int)
    ap.add_argument("--label", default="unpaused")
    ap.add_argument("--seconds", type=float, default=3.0)
    ap.add_argument("--hz", type=float, default=30.0)
    ap.add_argument("--compare", action="store_true")
    a = ap.parse_args()
    if a.compare:
        compare()
    elif a.pid:
        sample(a.pid, a.label, a.seconds, a.hz)
    else:
        ap.error("give a pid, or --compare")


if __name__ == "__main__":
    main()
