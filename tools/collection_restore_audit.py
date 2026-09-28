"""Pinned save/restore branches; offline evidence, never a hook installer."""
import argparse
import json
from pathlib import Path
from capstone.x86 import X86_OP_MEM, X86_REG_RCX, X86_REG_RBX, X86_REG_RDX

from collection_lifecycle_audit import Dump, DEFAULT_DUMP, CODE_SHA256, PATHS, rip_targets

# tag, entry, exclusive end, save copy, positive-control branch target,
# payload header, preparation entry, direct call/tail-jump instruction.
RESTORES = (
    ('CVS1', 0x1405A6840, 0x1405A69B1, 0x1405A688F, 0x1405A68EA, 8, 0x1405C5D50, 0x1405C5E46),
    ('CVS2', 0x1406AAB40, 0x1406AAC2C, 0x1406AAB8F, 0x1406AABE6, 8, 0x1406BABC0, 0x1406BAD41),
    ('CFJ', 0x1407CBB00, 0x1407CBBD1, 0x1407CBB4F, 0x1407CBBA6, 8, 0x1407DDD80, 0x1407DDEB8),
    ('Z3U', 0x14086D590, 0x14086DA97, 0x14086D5E4, 0x14086D63D, 8, 0x140899700, 0x140899780),
    ('Z3U-alternate', 0x140DCC840, 0x140DCCD57, 0x140DCC894, 0x140DCC8ED, 8, 0x140DFA750, 0x140DFA7C7),
    ('PWS1', 0x140974180, 0x1409742F2, 0x1409741CF, 0x14097422D, 16, 0x140976B50, 0x140976CEC),
    ('PWS2', 0x140A472F0, 0x140A47552, 0x140A4733F, 0x140A473A6, 16, 0x140A4CA30, 0x140A4CAF3),
    ('MJUS', 0x140B93630, 0x140B93AB2, 0x140B9367F, 0x140B936D6, 8, 0x140B9E170, 0x140B9E1E6),
    ('STG2', 0x140CCCCC0, 0x140CCCDA5, 0x140CCCD0F, 0x140CCCD69, 8, 0x140CDE150, 0x140CDE377),
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def produce(directory):
    dump = Dump(directory)
    paths = {p[0]: p for p in PATHS}
    rows, spans = [], []
    for tag, start, end, save, restore, header, caller, site in RESTORES:
        root, size = paths[tag][4:6]
        ins = dump.instructions(start, end)
        before_save = [i for i in ins if i.address < save]
        operations = [(i.mnemonic, i.op_str) for i in before_save]
        require(rip_targets(ins[1]) == [0x141C14F98], tag + ': session load differs')
        gates = []
        for offset, index in ((0x7b4, 2), (0x828, 4), (0x80c, 6)):
            cmp, branch = ins[index:index+2]
            require((cmp.mnemonic, cmp.op_str) == ('cmp', f'dword ptr [rax + {offset:#x}], 0')
                    and branch.mnemonic == 'jne' and branch.operands[0].imm == end-5,
                    tag + ': bypass gate differs')
            gates.append(dict(offset=hex(offset), compare=hex(cmp.address), branch=hex(branch.address)))
        require(('mov', f'r8d, {size:#x}') in operations, tag + ': save size differs')
        buffer_register = 'rbx' if tag.startswith('Z3U') else 'rdx'
        require(any(i.mnemonic == 'lea' and i.operands[0].reg == X86_REG_RCX and
                    i.operands[1].type == X86_OP_MEM and
                    i.operands[1].mem.base == (X86_REG_RBX if buffer_register == 'rbx' else X86_REG_RDX) and
                    i.operands[1].mem.disp == header for i in before_save),
                tag + ': save payload header differs')
        require(any(rip_targets(i) == [root] and i.op_str.startswith('rdx,') for i in before_save),
                tag + ': save source differs')
        require(any(rip_targets(i) == [root+8] and i.op_str.startswith(buffer_register + ',')
                    for i in before_save), tag + ': secondary pointer differs')
        branch_index = next((n for n,i in enumerate(before_save) if i.mnemonic == 'jg'), None)
        require(branch_index is not None, tag + ': signed restore branch missing')
        branch = before_save[branch_index]
        previous = before_save[branch_index-1]
        require(branch.operands[0].imm == restore and
                (previous.mnemonic, previous.op_str) == ('cmp', f'dword ptr [{buffer_register}], 0'),
                tag + ': restore control differs')
        save_ins = next(i for i in ins if i.address == save)
        require((save_ins.mnemonic, save_ins.op_str) == ('call', '0x140eec5a0'),
                tag + ': save helper differs')
        call_ins = dump.instructions(caller, site+5)[-1]
        require(call_ins.mnemonic in ('call','jmp') and call_ins.operands[0].imm == start,
                tag + ': preparation edge differs')
        rows.append(dict(game=tag, entry=hex(start), root_slot=hex(root),
                         secondary_slot=hex(root+8), save_bytes=hex(size),
                         payload_header_bytes=header, bypass_gates=gates,
                         save_call=hex(save), restore_branch=hex(branch.address),
                         restore_entry=hex(restore), preparation_edge=hex(site),
                         restore_kind='selective' if tag in ('Z3U','Z3U-alternate','MJUS') else 'full-copy'))
        spans.extend((dump.span(tag+'-save-restore',start,end),
                      dump.span(tag+'-preparation-prefix',caller,site+5)))
    return dict(schema=1, code_sha256=CODE_SHA256,
                evidence='offline code trace; no live event or hook ABI validation',
                limitations=['Preparation can change state before the restore routine.',
                             'Bypass and save paths must not be mistaken for restoration.',
                             'Selective restore ranges and downstream helper side effects are not a full write inventory.',
                             'Callers are bounded prefixes; indirect callers and UI event labels remain open.'],
                paths=rows, spans=spans)


def compact(report):
    result = dict(report)
    result['spans'] = [{k:v for k,v in s.items() if k != 'instructions'} for s in report['spans']]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump', default=DEFAULT_DUMP)
    parser.add_argument('--out', type=Path, default=Path('build/collection-restore-audit.json'))
    parser.add_argument('--manifest-out', type=Path)
    args = parser.parse_args()
    report = produce(args.dump)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    if args.manifest_out:
        args.manifest_out.write_text(json.dumps(compact(report), indent=2)+'\n', encoding='utf-8')
    print(f"Recorded {len(report['paths'])} save/restore paths and {len(report['spans'])} spans. Offline only.")


if __name__ == '__main__':
    main()
