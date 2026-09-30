"""Reproduce bounded collection lifecycle evidence from the reviewed local dump.

Offline only. These are instruction/data records, not implemented game readers.
The reviewed spans deliberately stop short of claiming complete lifetime or ABI
proof. See docs/collection-lifecycle-trace.md for interpretation and open edges.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
from analyze_dump import DEFAULT_DUMP, read_manifest
from collection_port_audit import audit


CODE_SHA256 = "9c340b8ce40a0c15a3eccd3c47207b02a59e38365bf99ae7605afc26bb84f95b"
SESSION = 0x141C14F98
# Reviewed entry, exclusive initializer end, setup, native root slot, root size,
# update routine. Sizes are published state lengths, not guest RAM sizes.
PATHS = (
    ("CVS1", 0x1405A5A40, 0x1405A5C5C, 0x1405A6440, 0x1422D2B90, 0x32AB0, 0x1405A65F0),
    ("CVS2", 0x1406A97D0, 0x1406A9A0F, 0x1406AA320, 0x1422DEBD0, 0x216B0, 0x1406AA7A0),
    ("CFJ", 0x1407CACA0, 0x1407CAEDE, 0x1407CB660, 0x1422F3A80, 0x26E80, 0x1407CB8B0),
    ("Z3U", 0x14086BC20, 0x14086BE70, 0x14086CFC0, 0x1422FB960, 0x12F98, 0x14086D160),
    ("Z3U-alternate", 0x140DCB360, 0x140DCB968, 0x140DCC280, 0x142C44050, 0x1B528, 0x140DCC3F0),
    ("PWS1", 0x140972E50, 0x14097308F, 0x140973B10, 0x1423188C0, 0x428F0, 0x140974100),
    ("PWS2", 0x140A46190, 0x140A46415, 0x140A46EB0, 0x14262F0B0, 0x8CD40, 0x140A47290),
    ("MJUS", 0x140B926F0, 0x140B92929, 0x140B930D0, 0x142996830, 0x9E8A8, 0x140B93500),
    ("STG2", 0x140CCB1C0, 0x140CCB43C, 0x140CCC940, 0x142BE6370, 0x511B8, 0x140CCCB60),
)

# Explicit, instruction-aligned, exclusive ends; excerpts are not whole CFGs.
SHARED_SPANS = (
    ("game-screen-constructor", 0x140037000, 0x140037071),
    ("game-screen-update", 0x140038F90, 0x140039778),
    ("game-screen-release", 0x140037140, 0x140037292),
    ("post-update-shell-state", 0x140039ED0, 0x14003A284),
    ("initializer-dispatch-complete", 0x14004C540, 0x14004C674),
    ("initializer-entry-038af0-prefix", 0x140038AF0, 0x140038B21),
    ("initializer-entry-038f90-prefix", 0x140038F90, 0x140038FD6),
    ("initializer-entry-08c8a0-prefix", 0x14008C8A0, 0x14008C9E6),
    ("initializer-entry-0a65e0-prefix", 0x1400A65E0, 0x1400A6C45),
    ("initializer-entry-0b0d80-prefix", 0x1400B0D80, 0x1400B0FA8),
    ("initializer-entry-0b4900-prefix", 0x1400B4900, 0x1400B49E2),
    ("initializer-preparation-gate", 0x14004C6F0, 0x14004C706),
    ("shell-owner-constructor-prefix", 0x140047E00, 0x140047E61),
    ("shell-destructor-body", 0x140048380, 0x1400486B1),
    ("shell-deleting-destructor", 0x140048700, 0x140048762),
    ("CVS1-phase-publication", 0x1405A6522, 0x1405A6536),
    ("CVS1-native-update", 0x1405A65F0, 0x1405A665D),
    ("CVS1-phase-dispatch", 0x1405AE5F0, 0x1405AE675),
    ("CVS1-phase-1-subdispatch", 0x1405C4200, 0x1405C427C),
    ("CVS1-phase-2-subdispatch", 0x1405C5770, 0x1405C57A7),
    ("CVS1-secondary-copy-restore", 0x1405A6840, 0x1405A69B1),
    ("CVS1-copy-restore-caller", 0x1405C5D50, 0x1405C5E4B),
    ("copy-helper-dispatch", 0x140EEC5A0, 0x140EEC5E1),
    ("copy-helper-rep-movsb", 0x140EEC580, 0x140EEC593),
    ("copy-helper-vector-prefix", 0x140EEC780, 0x140EEC7C6),
    ("arena-allocation", 0x140048270, 0x1400482A9),
    ("base-ROM-routing", 0x140052470, 0x1400525A7),
    ("alternate-ROM-routing", 0x140049DA0, 0x140049E0F),
    ("ROM-name-lookup", 0x1400B6250, 0x1400B62B8),
    ("packed-option-writer", 0x14003FEB0, 0x14003FEC1),
    ("update-consumer", 0x14003A6FC, 0x14003A717),
    ("secondary-callback-consumer", 0x14003A767, 0x14003A78D),
    ("cleanup-before-reinitialize", 0x14004FFD0, 0x14005007B),
    ("cleanup-other-consumer", 0x14004ADC0, 0x14004ADE9),
    ("common-cleanup-thunk", 0x1405A5A30, 0x1405A5A35),
    ("common-cleanup-thunk-2", 0x1405A5D80, 0x1405A5D85),
    ("common-cleanup-body", 0x140F2F1F0, 0x140F2F269),
    ("cleanup-object-array-4096", 0x140048FC0, 0x140049008),
    ("cleanup-object-array-256", 0x140048F70, 0x140048FB5),
    ("CFJ-cleanup-thunk", 0x1407CAC90, 0x1407CAC95),
    ("CFJ-cleanup-body", 0x1407CB000, 0x1407CB089),
    ("PWS2-cleanup-thunk", 0x140A46180, 0x140A46185),
    ("PWS2-cleanup-body", 0x140A468D0, 0x140A468F1),
)


class Dump:
    def __init__(self, directory):
        self.sections = read_manifest(directory)
        self.md = Cs(CS_ARCH_X86, CS_MODE_64)
        self.md.detail = True
        code = next(s for s in self.sections if s.name == "sec_00001000_code.bin")
        if hashlib.sha256(code.data).hexdigest() != CODE_SHA256:
            raise ValueError("Code differs from reviewed build; retrace before using labels")

    def read(self, address, size):
        for section in self.sections:
            offset = address - section.live
            if 0 <= offset and offset + size <= len(section.data):
                return section.data[offset:offset + size]
        raise ValueError(f"Missing dump range {address:#x}+{size:#x}")

    def string(self, address):
        raw = self.read(address, 128)
        if b"\0" not in raw:
            raise ValueError("Unterminated ROM name")
        return raw.split(b"\0", 1)[0].decode("ascii")

    def pointer(self, address):
        return struct.unpack("<Q", self.read(address, 8))[0]

    def instructions(self, start, end):
        result = list(self.md.disasm(self.read(start, end - start), start))
        if not result or result[-1].address + result[-1].size != end:
            raise ValueError(f"Incomplete instruction span {start:#x}..{end:#x}")
        return result

    def span(self, label, start, end):
        return dict(label=label, start=hex(start), end_exclusive=hex(end),
                    sha256=hashlib.sha256(self.read(start, end - start)).hexdigest(),
                    instructions=[record(i) for i in self.instructions(start, end)])


def rip_targets(ins):
    return [ins.address + ins.size + op.mem.disp for op in ins.operands
            if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP]


def record(ins):
    row = dict(address=hex(ins.address), bytes=ins.bytes.hex(),
               instruction=f"{ins.mnemonic} {ins.op_str}".strip())
    refs = rip_targets(ins)
    if refs:
        row["rip_targets"] = [hex(ref) for ref in refs]
    return row


def callbacks(instructions):
    """Match reviewed local publication sequences, not generic dataflow analysis."""
    result = {}
    for index, ins in enumerate(instructions):
        if ins.mnemonic != "mov" or not ins.op_str.endswith("], rcx"):
            continue
        if not ins.op_str.startswith("qword ptr [rax +"):
            continue
        offset = ins.operands[0].mem.disp
        if offset not in (0x10, 0x18, 0x20, 0x28):
            continue
        # STG2 writes a context count between loading RCX and publishing it.
        window = instructions[max(0, index - 4):index]
        owners = [i for i in window if i.mnemonic == "mov" and
                  i.op_str.startswith("rax, qword ptr [rip")]
        values = [i for i in window if i.mnemonic == "lea" and
                  i.op_str.startswith("rcx, [rip")]
        if owners and rip_targets(owners[-1]) == [SESSION] and values:
            result[hex(offset)] = dict(target=hex(rip_targets(values[-1])[0]),
                                       publication=hex(ins.address))
    if set(result) != {"0x10", "0x18", "0x20", "0x28"}:
        raise ValueError("Expected four reviewed session callback publications")
    return result


def produce(directory):
    dump = Dump(directory)
    baseline = audit(directory)
    spans = [dump.span(*spec) for spec in SHARED_SPANS]
    paths = []
    for tag, entry, end, setup, root, size, update in PATHS:
        init = dump.instructions(entry, end)
        if not any(i.mnemonic == "call" and i.operands[0].type == X86_OP_IMM
                   and i.operands[0].imm == setup for i in init):
            raise ValueError(f"{tag}: setup call missing")
        spans.append(dump.span(tag + "-initializer", entry, end))
        cb = callbacks(init)
        # Bound the setup evidence at the second published size. No inferred
        # whole-function boundary, and no fallthrough into adjacent functions.
        prefix = []
        for ins in dump.md.disasm(dump.read(setup, 0x300), setup):
            prefix.append(ins)
            if ins.mnemonic == "mov" and ins.op_str.startswith("dword ptr [rax + 0x1c8],"):
                break
        else:
            raise ValueError(f"{tag}: secondary size publication missing")
        if not any(i.mnemonic == "mov" and rip_targets(i) == [root]
                   and i.operands[0].type == X86_OP_MEM for i in prefix):
            raise ValueError(f"{tag}: root publication missing")
        sizes = [i for i in prefix if i.mnemonic == "mov" and
                 i.op_str.startswith("dword ptr [rax + 0x1b8],")]
        if len(sizes) != 1 or sizes[0].operands[1].imm != size:
            raise ValueError(f"{tag}: primary size differs")
        spans.append(dump.span(tag + "-root-publication", setup,
                               prefix[-1].address + prefix[-1].size))
        wrapper = int(cb["0x10"]["target"], 16)
        body = []
        for ins in dump.md.disasm(dump.read(wrapper, 0x80), wrapper):
            body.append(ins)
            if ins.mnemonic == "jmp":
                break
        else:
            raise ValueError(f"{tag}: wrapper tail missing")
        if not any(i.mnemonic == "call" and i.operands[0].type == X86_OP_IMM
                   and i.operands[0].imm == update for i in body):
            raise ValueError(f"{tag}: update call missing")
        spans.append(dump.span(tag + "-update-wrapper", wrapper,
                               body[-1].address + body[-1].size))
        paths.append(dict(tag=tag, initializer=hex(entry), setup=hex(setup),
                          context_slot=hex(0x142CF9D48 if tag == "CFJ" else 0x142C79B40),
                          root_slot=hex(root), published_size=hex(size),
                          secondary_size=hex(prefix[-1].operands[1].imm),
                          callbacks=cb, update=hex(update),
                          wrapper_tail=hex(body[-1].operands[0].imm)))

    roms = []
    for index in range(18):
        address = dump.pointer(0x141208B00 + index * 8)
        name = dump.string(dump.pointer(address))
        roms.append(dict(index=index, record=hex(address), name=name))
    # Read the actual descriptor names; do not infer ROM routing by index/2.
    routes = {}
    for game in baseline["games"]:
        for region in range(2):
            slot = 0x1411F5F10 + game["game_index"] * 0x40 + 8 + region * 8
            name = dump.string(dump.pointer(slot))
            routes[name] = dict(game_index=game["game_index"], region=region,
                                name_pointer_slot=hex(slot),
                                initializer=game["initializers"][0]["address"],
                                alpha_discriminator="zero" if game["tag"] == "Z3U" else "not consulted")
    for region in range(2):
        slot = 0x1411F5FE8 + region * 8
        routes[dump.string(dump.pointer(slot))] = dict(
            game_index=3, region=region, name_pointer_slot=hex(slot),
            initializer=hex(0x140DCB360), alpha_discriminator="nonzero")
    if len(routes) != 18 or set(routes) != {r["name"] for r in roms}:
        raise ValueError("ROM descriptor routing does not cover exactly 18 names")
    for rom in roms:
        rom.update(routes[rom["name"]])
    phase_entries = [dict(index=index, slot=hex(0x141217568 + index * 8),
                          bytes=dump.read(0x141217568 + index * 8, 8).hex(),
                          target=hex(dump.pointer(0x141217568 + index * 8)))
                     for index in (1, 2)]
    destructor_slot = 0x140FE12F0
    if dump.pointer(destructor_slot) != 0x140048700:
        raise ValueError("Shell destructor vtable slot differs")
    screen_slots = ((0x140FDE728, 0x140038F90), (0x140FDE730, 0x140037140))
    for address, target in screen_slots:
        if dump.pointer(address) != target:
            raise ValueError("Game-screen vtable slot differs")
    return dict(schema=2, evidence="offline instruction/data trace; no live validation",
                limitations=["Bounded spans are not complete control-flow graphs.",
                             "Root publication is not readiness or lifetime proof.",
                             "No gameplay field, safe hook ABI, or callback cadence is validated.",
                             "CFJ cleanup callees, destructor entry paths and reset/load coverage remain open."],
                sections=baseline["sections"], paths=paths, roms=roms, spans=spans,
                game_screen_vtable=[dict(address=hex(a), target=hex(t)) for a,t in screen_slots],
                shell_destructor_vtable=dict(address=hex(destructor_slot),
                                            bytes=dump.read(destructor_slot, 8).hex(),
                                            target=hex(0x140048700)),
                cvs1_phase_table_leads=phase_entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", default=DEFAULT_DUMP)
    parser.add_argument("--out", type=Path, default=Path("build/collection-lifecycle-audit.json"))
    parser.add_argument("--manifest-out", type=Path,
                        help="Also write compact review manifest without instruction arrays")
    args = parser.parse_args()
    result = produce(args.dump)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if args.manifest_out:
        compact = dict(result)
        compact["spans"] = [{k: v for k, v in s.items() if k != "instructions"}
                            for s in result["spans"]]
        args.manifest_out.parent.mkdir(parents=True, exist_ok=True)
        args.manifest_out.write_text(json.dumps(compact, indent=2) + "\n", encoding="utf-8")
    print(f"Recorded {len(result['paths'])} paths, {len(result['roms'])} ROMs, "
          f"{len(result['spans'])} bounded instruction spans in {args.out}.")
    print("Offline evidence only; root lifetime and live behavior remain unvalidated.")


if __name__ == "__main__":
    main()
