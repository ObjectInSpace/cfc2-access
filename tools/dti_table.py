"""
Walk MTFramework's DTI class table and print, for each class, the name and the
vtable that REALLY belongs to it.

⭐⭐ WHY THIS EXISTS. Four in-game runs of the GUI text reader failed because
every note in this project had cGUIObjText's vtable as 0x1410563a8. It is
cGUIObjRoot's. The table is a 0x38-byte stride array of records:

    +0x00  the class's vtable
    +0x08  a class NAME pointer -- but the name belongs to the NEIGHBOURING
           record, not to this one

So "read the name next to the vtable" shifts every class by one, silently and
plausibly. This tool walks the stride and pairs them correctly.

⚠ Two further traps it exists to stop repeating:

 1. The class names live in a packed STRING POOL with vtables interleaved
    between them. So an address adjacent to a name is not that class's vtable,
    and an address adjacent to a known vtable is not "a second vtable for the
    same class". ⭐ Membership is decided by REGISTRATION -- being a record's
    +0x00 -- never by adjacency.

 2. The DTI's vtable is the class DESCRIPTOR's (typically 2 slots: getDTI and
    a factory). What an INSTANCE carries is what the factory stamps in, which
    is a different, much larger table. --factory follows that chain, and prints
    the allocation size, which is a free identity check: cGUIObjText is
    documented at 0x210 bytes, and the wrong factory allocated 0xe0.

Usage:
    python tools/dti_table.py                    # every cGUIObj* class
    python tools/dti_table.py --grep Text        # filter by name
    python tools/dti_table.py --factory cGUIObjText
"""

import argparse
import os
import struct
import sys

# ⚠ This file's comments and output use non-ASCII markers, and Windows consoles
# default to cp1252 -- printing one raised UnicodeEncodeError and killed the
# tool AFTER it had already produced its answer. Reconfigure rather than strip
# the markers: a tool that crashes on its own output looks like a broken tool.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_dump import read_manifest, DEFAULT_DUMP  # noqa: E402

STRIDE = 0x38
# The table's extent, found by scanning for the run of records whose +0x08
# resolves to a "cGUIObj*" string. Kept wide; the walker stops on nonsense.
SCAN_LO = 0x1422CA000
SCAN_HI = 0x1422CC000


def load(dump):
    secs = read_manifest(dump)
    return [(s.live, s.data) for s in secs]


def read_q(mem, addr):
    for base, data in mem:
        if base <= addr < base + len(data) - 8:
            return struct.unpack_from("<Q", data, addr - base)[0]
    return None


def read_cstr(mem, addr, n=64):
    for base, data in mem:
        if base <= addr < base + len(data):
            raw = data[addr - base:addr - base + n].split(b"\0")[0]
            try:
                return raw.decode("ascii")
            except UnicodeDecodeError:
                return None
    return None


def walk(mem):
    """Yield (record_addr, vtable, name) for every plausible DTI record.

    ⭐ The alignment is DISCOVERED, not assumed: try every offset within one
    stride and keep whichever yields the most records whose +0x08 resolves to a
    class name. Assuming an alignment is how the 8-byte misread in the sPad
    work happened (memory: cfc2-pad-state)."""
    best, best_n = None, -1
    for off in range(0, STRIDE, 8):
        n = 0
        for rec in range(SCAN_LO + off, SCAN_HI, STRIDE):
            nm = read_cstr(mem, read_q(mem, rec + 8) or 0)
            if nm and nm.startswith("cGUIObj"):
                n += 1
        if n > best_n:
            best, best_n = SCAN_LO + off, n
    if best_n <= 0:
        return
    for rec in range(best, SCAN_HI, STRIDE):
        vt = read_q(mem, rec)
        nm = read_cstr(mem, read_q(mem, rec + 8) or 0)
        if vt and nm and nm.startswith("cGUIObj"):
            yield rec, vt, nm


def vtable_slots(mem, vt, limit=40):
    """Slots of a vtable, stopping at the first value that is not code.

    ⚠ The stop condition matters: these tables abut ASCII in the string pool,
    so a walker without one happily prints 'cGUIObjT' as a function pointer."""
    out = []
    for i in range(limit):
        v = read_q(mem, vt + i * 8)
        if v is None or not (0x140001000 <= v <= 0x141000000):
            break
        out.append(v)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", default=DEFAULT_DUMP)
    ap.add_argument("--grep", help="only classes whose name contains this")
    ap.add_argument("--factory", help="follow this class to its INSTANCE vtable")
    args = ap.parse_args()

    mem = load(args.dump)
    records = list(walk(mem))
    if not records:
        raise SystemExit("no DTI records found -- is the dump stale?")

    if args.factory:
        for rec, vt, nm in records:
            if nm != args.factory:
                continue
            print("%s" % nm)
            print("  DTI record        0x%x" % rec)
            print("  descriptor vtable 0x%x" % vt)
            slots = vtable_slots(mem, vt)
            print("  descriptor slots  %s" % [hex(s) for s in slots])
            if len(slots) < 2:
                print("  ⛔ no factory slot -- cannot reach an instance vtable")
                return
            print("  factory           0x%x" % slots[1])
            print()
            print("  ⭐ Disassemble the factory and read two things:")
            print("     - its `mov edx, <size>`: the allocation size, which")
            print("       must match the class's documented size. A mismatch")
            print("       means this is not the class you think it is.")
            print("     - its `lea rax,[rip+X]; mov [rbx],rax`: the INSTANCE")
            print("       vtable, which is what an object actually carries and")
            print("       what a vptr filter must compare against.")
            print("     python tools/disasm.py 0x%x --before 0 --count 16"
                  % slots[1])
            return
        raise SystemExit("class %r not found" % args.factory)

    print("%-28s %-14s %s" % ("class", "vtable", "DTI record"))
    for rec, vt, nm in records:
        if args.grep and args.grep.lower() not in nm.lower():
            continue
        print("%-28s 0x%-12x 0x%x" % (nm, vt, rec))

    print()
    print("⚠ These are DESCRIPTOR vtables. For the vtable an INSTANCE carries,")
    print("  run with --factory <class> and follow the chain.")


if __name__ == "__main__":
    main()
