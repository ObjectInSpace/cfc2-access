"""
Build the RIP-relative cross-reference index for the unpacked image.

WHY THIS IS A TOOL AND NOT A CHECKED-IN FILE
--------------------------------------------
The index maps every RIP-relative data reference in .text to the sites that
make it, and it is what turns "find the singleton pointer" from a memory search
into a lookup. Route 5 was finally cracked with it.

⚠⚠ BUT IT GOES STALE SILENTLY, AND THAT COST REAL TIME.

A previous index was built at 00:14 against one dump; the game then REWROTE
cfc2_dump/ at 00:55 on the next launch. The .text section is identical between
runs (ASLR off, no relocations), but the DATA sections are captured at a
different moment in startup and differ. Lookups against the stale index came
back empty and looked like negative findings rather than a tooling problem.

So the index now records the manifest it was built from and refuses to load if
the dump has changed underneath it. A stale index is worse than no index,
because it answers questions wrongly instead of failing.

Usage:
    python tools/build_riprefs.py            # build (or rebuild if stale)
    python tools/build_riprefs.py --check    # report freshness, build nothing

Then, from analysis code:
    from build_riprefs import load_refs
    refs = load_refs()      # raises if stale, never returns wrong data
"""

import argparse
import hashlib
import os
import pickle
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_dump import read_manifest, DEFAULT_DUMP  # noqa: E402

CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     "..", "riprefs.pkl")


def dump_fingerprint(dump_dir=DEFAULT_DUMP):
    """Identify the dump this index belongs to.

    Hashes the manifest plus each section's size and mtime. Cheap, and it
    changes whenever the game rewrites the dump -- which is exactly the event
    that invalidates the index.
    """
    man = os.path.join(dump_dir, "manifest.txt")
    h = hashlib.sha256()
    h.update(open(man, "rb").read())
    for name in sorted(os.listdir(dump_dir)):
        if not name.endswith(".bin"):
            continue
        p = os.path.join(dump_dir, name)
        st = os.stat(p)
        h.update(("%s:%d:%d" % (name, st.st_size, int(st.st_mtime))).encode())
    return h.hexdigest()


def build(dump_dir=DEFAULT_DUMP, out=CACHE):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM
    from capstone.x86 import X86_REG_RIP

    secs = {s.name: s for s in read_manifest(dump_dir)}
    code = secs["sec_00001000_code.bin"]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    data, base = code.data, code.live
    refs = {}
    off, L, n = 0, len(data), 0
    t0 = time.time()
    # ⚠ Capstone's linear disasm STOPS at the first undecodable byte. An early
    # version of this sweep covered 5716 instructions of a 16 MB section and
    # looked complete. Restart past bad bytes; the real sweep is ~4.1M
    # instructions and takes about a minute.
    while off < L:
        got = 0
        for insn in md.disasm(data[off:], base + off):
            got = insn.address - base - off + insn.size
            n += 1
            for op in insn.operands:
                if op.type == CS_OP_MEM and op.mem.base == X86_REG_RIP:
                    t = insn.address + insn.size + op.mem.disp
                    refs.setdefault(t, []).append(
                        (insn.address, insn.mnemonic, insn.op_str))
        off += got if got else 1

    payload = {"fingerprint": dump_fingerprint(dump_dir),
               "dump_dir": dump_dir,
               "built": time.strftime("%Y-%m-%d %H:%M:%S"),
               "insns": n,
               "refs": refs}
    with open(out, "wb") as f:
        pickle.dump(payload, f)
    print("disassembled %d instructions in %.0fs; %d distinct RIP targets"
          % (n, time.time() - t0, len(refs)))
    print("wrote %s" % os.path.normpath(out))
    return payload


def load_refs(dump_dir=DEFAULT_DUMP, path=CACHE):
    """Return the reference map, or raise if it is missing or stale.

    ⚠ Raises rather than rebuilding silently: a rebuild takes a minute, and a
    caller that expected an instant lookup should know why it is waiting.
    """
    if not os.path.exists(path):
        raise SystemExit("no riprefs index -- run: python tools/build_riprefs.py")
    payload = pickle.load(open(path, "rb"))
    if not isinstance(payload, dict) or "fingerprint" not in payload:
        raise SystemExit("riprefs index predates freshness checking -- "
                         "rebuild it: python tools/build_riprefs.py")
    now = dump_fingerprint(dump_dir)
    if payload["fingerprint"] != now:
        raise SystemExit(
            "riprefs index is STALE -- it was built %s against a different "
            "dump, and the game has rewritten cfc2_dump since.\n"
            "Rebuild it: python tools/build_riprefs.py\n"
            "(.text is identical between runs, but the DATA sections are "
            "captured at a different moment and differ, so offsets derived "
            "from a stale index are wrong rather than merely missing.)"
            % payload["built"])
    return payload["refs"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", default=DEFAULT_DUMP)
    ap.add_argument("--check", action="store_true",
                    help="report freshness only")
    args = ap.parse_args()

    if args.check:
        if not os.path.exists(CACHE):
            print("no index present")
            return
        payload = pickle.load(open(CACHE, "rb"))
        if not isinstance(payload, dict) or "fingerprint" not in payload:
            print("index present but has no fingerprint -- STALE by definition")
            return
        fresh = payload["fingerprint"] == dump_fingerprint(args.dump)
        print("index built %s, %d targets -- %s"
              % (payload["built"], len(payload["refs"]),
                 "FRESH" if fresh else "STALE, rebuild it"))
        return

    build(args.dump)


if __name__ == "__main__":
    main()
