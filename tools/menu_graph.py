"""
Extract the shell's MENU GRAPH from the code: which item leads where.

⭐⭐ WHY THIS EXISTS
Naming screens from candidate label runs is guesswork with a shortlist: the GMD
contains many runs of the same shape, so a screen's structure alone cannot say
which run is its own. Every screen therefore cost the user a confirmation.

But the game already encodes the answer. A menu's confirm handler dispatches on
the CURSOR through a jump table:

    movsxd rax, [rdi + 0x330]          ; the cursor
    cmp    eax, 5 ; ja  ...            ; the bound == item count - 1
    mov    ecx, [rbx + rax*4 + DISP]   ; table of RVA deltas
    add    rcx, rbx                    ; rbx = image base
    jmp    rcx

So the item count and one branch per item are both in the instructions, and the
branch bodies write the mode/screen IDs that say what the item DOES. That is a
measured fact about the game, not a shortlist.

⚠ WHAT THIS DOES AND DOES NOT GIVE YOU
It gives the graph: this item leads to that state. It does NOT give the label
text -- the branch writes a numeric screen ID, not a GMD index. Its value is as
a CONSTRAINT: knowing that main-menu item 2 leads to the state a 2-item screen
runs in tells you which screen that caller is, and then the label run follows.

⛔ Do not treat a decoded table as a naming. Confirm against a run, exactly as
every shipped g_screens row must be.

Usage:
    python tools/menu_graph.py            # every cursor-indexed dispatch
    python tools/menu_graph.py 0x140097b48   # one site, with branch bodies
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_dump import read_manifest, DEFAULT_DUMP  # noqa: E402

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
except ImportError:
    raise SystemExit("capstone is required:  pip install capstone")

IMAGE_BASE = 0x140000000
# The shell's menu code, the same region the cursor helpers' call sites occupy.
MENU_LO, MENU_HI = 0x140079000, 0x1400B6000

# Cursor fields seen in this engine's screen objects. A dispatch indexed by one
# of these is a MENU CHOICE; one indexed by [this+8] is the screen's own state
# machine phase and says nothing about menu items.
CURSOR_FIELDS = ("0x330]", "0x334]", "0x338]")


def sections():
    return read_manifest(DEFAULT_DUMP)


def read(secs, addr, n):
    for s in secs:
        if s.live <= addr < s.live + len(s.data):
            return s.data[addr - s.live:addr - s.live + n]
    return None


def find_dispatches(secs):
    """Every 'mov r32,[base+idx*4+disp] / add r64,base / jmp r64' in the menus.

    ⚠ Found by DECODING, not by matching a byte pattern: the first version
    matched fixed opcode bytes and found 2 of the 33 real sites, because it had
    baked in one particular register allocation.
    """
    sec = [s for s in secs if s.live == 0x140001000][0]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    out, seen = [], set()
    step = 0x2000
    for start in range(MENU_LO, MENU_HI, step):
        end = min(start + step + 64, MENU_HI)
        chunk = sec.data[start - sec.live:end - sec.live]
        window = []
        for ins in md.disasm(chunk, start):
            window.append(ins)
            if len(window) > 3:
                window.pop(0)
            if len(window) != 3:
                continue
            mv, add, jmp = window
            if jmp.mnemonic != "jmp" or add.mnemonic != "add":
                continue
            if mv.mnemonic != "mov" or "*4 +" not in mv.op_str:
                continue
            if jmp.op_str != add.op_str.split(",")[0].strip():
                continue
            try:
                disp = int(mv.op_str.split("*4 +")[1].split("]")[0].strip(), 0)
            except ValueError:
                continue
            if mv.address in seen:
                continue
            seen.add(mv.address)
            out.append((mv.address, IMAGE_BASE + disp))
    return out


def classify(secs, site):
    """Is this dispatch indexed by the CURSOR, and how many entries?

    Returns (is_cursor, count_or_None).
    """
    sec = [s for s in secs if s.live == 0x140001000][0]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    win = sec.data[site - sec.live - 72:site - sec.live + 8]
    text, count = [], None
    for ins in md.disasm(win, site - 72):
        text.append("%s %s" % (ins.mnemonic, ins.op_str))
        if ins.mnemonic == "cmp":
            parts = ins.op_str.split(",")
            if len(parts) == 2:
                try:
                    count = int(parts[1].strip(), 0) + 1
                except ValueError:
                    pass
    blob = " | ".join(text)
    return any(f in blob for f in CURSOR_FIELDS), count


def table_targets(secs, table, count):
    """Decode `count` RVA deltas into live addresses."""
    out = []
    for i in range(count):
        raw = read(secs, table + i * 4, 4)
        if raw is None or len(raw) < 4:
            return out
        out.append(IMAGE_BASE + struct.unpack("<i", raw)[0])
    return out


def branch_summary(secs, addr, limit=14):
    """What a branch DOES: the globals it writes, which identify the target."""
    sec = [s for s in secs if s.live == 0x140001000][0]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    win = sec.data[addr - sec.live:addr - sec.live + 0x60]
    notes = []
    for ins in md.disasm(win, addr):
        if ins.mnemonic == "mov" and "dword ptr [" in ins.op_str and \
                "+ 0xd0" in ins.op_str:
            notes.append(ins.op_str)
        if ins.mnemonic in ("jmp", "ret"):
            break
        if len(notes) >= limit:
            break
    return notes


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("site", nargs="?", help="one dispatch site, e.g. 0x140097b48")
    args = ap.parse_args()

    secs = sections()
    sites = find_dispatches(secs)

    # ⭐ A positive count of what was examined, not just of what matched.
    print("dispatch sites decoded in %#x..%#x: %d" %
          (MENU_LO, MENU_HI, len(sites)))

    rows = []
    for site, table in sites:
        is_cursor, count = classify(secs, site)
        if is_cursor:
            rows.append((site, table, count))
    print("of those, indexed by a CURSOR field (i.e. menu choices): %d\n"
          % len(rows))

    if args.site:
        want = int(args.site, 0)
        rows = [r for r in rows if r[0] == want]
        if not rows:
            sys.exit("%#x is not a cursor-indexed dispatch site" % want)

    for site, table, count in rows:
        print("site %#x  table %#x  entries=%s" % (site, table, count))
        if not count:
            print("   ⚠ no bound found; cannot decode the table safely\n")
            continue
        for i, tgt in enumerate(table_targets(secs, table, count)):
            notes = branch_summary(secs, tgt) if args.site else []
            extra = ("   " + "; ".join(notes)) if notes else ""
            print("   [%d] -> %#x%s" % (i, tgt, extra))
        print()


if __name__ == "__main__":
    main()
