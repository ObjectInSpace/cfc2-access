"""Reproduce collection initialization leads from the local unpacked dump.

Offline only. This records instructions, not validated gameplay fields.
Uses the same Capstone dependency and dump reader as tools/disasm.py.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
from analyze_dump import DEFAULT_DUMP, read_manifest


TAGS = ("CVS1", "CVS2", "CFJ", "Z3U", "PWS1", "PWS2", "MJUS", "STG2")
IMAGE_BASE = 0x140000000
DISPATCH = 0x14004C540
TABLE = 0x14004C674


def audit(dump):
    sections = read_manifest(dump)

    def read(address, size):
        for section in sections:
            offset = address - section.live
            if 0 <= offset and offset + size <= len(section.data):
                return section.data[offset:offset + size]
        raise ValueError(f"Missing dump range {address:#x}+{size:#x}")

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    def instructions(address, size):
        return list(md.disasm(read(address, size), address))

    def record(ins):
        row = dict(address=hex(ins.address), bytes=ins.bytes.hex(),
                   instruction=f"{ins.mnemonic} {ins.op_str}".strip())
        refs = [hex(ins.address + ins.size + op.mem.disp)
                for op in ins.operands
                if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP]
        if refs:
            row["rip_targets"] = refs
        return row

    # Refuse a different dispatcher rather than silently attaching familiar
    # game names to a changed jump table. Full section hashes are also retained.
    expected = bytes.fromhex("418b948074c604004903d0ffe2")
    if read(0x14004C575, len(expected)) != expected:
        raise ValueError("Dispatcher differs from the reviewed build")
    branches = struct.unpack("<8I", read(TABLE, 32))
    games = []
    for index, rva in enumerate(branches):
        address = IMAGE_BASE + rva
        if not DISPATCH <= address < TABLE:
            raise ValueError("Jump-table target outside reviewed dispatcher")
        # The table is not in address order. End at the next physical case,
        # or default case, keeping Z3U's conditional alternative together.
        upper = min([IMAGE_BASE + other for other in branches if other > rva]
                    + [0x14004C664])
        branch = instructions(address, upper - address)
        targets = []
        for ins in branch:
            if ins.mnemonic not in ("jmp", "call") or not ins.operands:
                continue
            operand = ins.operands[0]
            if operand.type != X86_OP_IMM or DISPATCH <= operand.imm < TABLE:
                continue
            target = operand.imm
            # Deliberately bounded excerpt, not a claim to disassemble the
            # whole function or recursively discover every initialization call.
            excerpt = instructions(target, 0x300)
            targets.append(dict(address=hex(target),
                                excerpt_size="0x300",
                                excerpt_sha256=hashlib.sha256(read(target, 0x300)).hexdigest(),
                                instructions=[record(i) for i in excerpt]))
        if not targets:
            raise ValueError(f"No direct initializer target for {TAGS[index]}")
        games.append(dict(game_index=index, tag=TAGS[index],
                          branch_address=hex(address),
                          branch=[record(i) for i in branch], initializers=targets))
    return dict(schema=1, evidence="offline instruction trace; no live validation",
                limitations=["Initializer excerpts are bounded, not complete control-flow graphs.",
                             "No player, phase, text, camera, or meter field is validated here.",
                             "Canonical names follow src/emu_ram.c; ROM variants need live cross-checks."],
                sections=[dict(name=s.name, live=hex(s.live), size=len(s.data),
                               sha256=hashlib.sha256(s.data).hexdigest()) for s in sections],
                dispatcher=hex(DISPATCH), table=hex(TABLE), games=games)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", default=DEFAULT_DUMP)
    parser.add_argument("--out", type=Path, default=Path("build/collection-port-audit.json"))
    args = parser.parse_args()
    result = audit(args.dump)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    for game in result["games"]:
        print(game["tag"] + ": " + ", ".join(i["address"] for i in game["initializers"]))
    print(f"Wrote {args.out}. Offline leads only; no gameplay fields validated.")


if __name__ == "__main__":
    main()
