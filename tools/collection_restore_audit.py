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

PREPARATION_SPANS = (
    ('CVS1-preparation-dispatch', 0x1405C5D30, 0x1405C5D47),
    ('CVS2-preparation-dispatch', 0x1406BABA0, 0x1406BABB7),
    ('CFJ-preparation-dispatch', 0x1407DDD60, 0x1407DDD77),
    ('Z3U-phase-task', 0x140895C50, 0x140895D88),
    ('Z3U-task-scheduler', 0x14086E700, 0x14086E786),
    ('Z3U-alternate-phase-task', 0x140DF6C50, 0x140DF6DA5),
    ('Z3U-alternate-task-scheduler', 0x140DCD5B0, 0x140DCD65C),
    ('PWS1-outer-update', 0x140973300, 0x1409735AB),
    ('PWS1-preparation-complete', 0x140976B50, 0x140976D5E),
    ('PWS2-outer-update', 0x140A48CC0, 0x140A48DD0),
    ('PWS2-preparation-code', 0x140A4CA30, 0x140A4CDB8),
    ('MJUS-task-scheduler', 0x140B93B60, 0x140B93BF8),
    ('MJUS-preparation-code', 0x140B9E170, 0x140B9E40F),
    ('STG2-native-update', 0x140CCCB60, 0x140CCCC12),
)

# Only the identified slot is claimed; these are not inferred table extents.
PREPARATION_SLOTS = (
    ('CVS1', 0x141222430, 0x1405C5D50),
    ('CVS2', 0x14128CD78, 0x1406BABC0),
    ('CFJ', 0x1412B6920, 0x1407DDD80),
    ('Z3U', 0x1412D7A48, 0x140899700),
    ('Z3U-task', 0x1412D0358, 0x140895C50),
    ('Z3U-alternate', 0x1419E3BB8, 0x140DFA750),
    ('Z3U-alternate-task', 0x1419DC638, 0x140DF6C50),
    ('MJUS-task', 0x141787288, 0x140B9E170),
)

PREPARATION_EDGES = (
    (0x140973523, 'call', '0x140976b50'),
    (0x140A48D80, 'call', '0x140a4ca30'),
    (0x140CCCBA4, 'call', '0x140cde150'),
    (0x140895CF1, 'call', 'qword ptr [rsi + rax*8 + 0x12d7a48]'),
    (0x140DF6CF1, 'call', 'qword ptr [rsi + rax*8 + 0x19e3bb8]'),
    (0x140976D54, 'jmp', '0x1409a8760'),
    (0x140B9E40A, 'jmp', '0x140b93ce0'),
    (0x140CCCC0D, 'jmp', '0x140ccdc60'),
)

# Wrapper work runs after the native update. Mode 3 copies a smaller state
# region back from S+0x465c. Ends are the non-mode-3 join, not whole functions.
TAIL_TRANSFERS = (
    ('CVS1', 0x1405A5D90, 0x1405A6060, 0x1405A5F24, 0x1405A5F2C, 0x480),
    ('CVS2', 0x1406A9DE0, 0x1406A9F5E, 0x1406A9E59, 0x1406A9E60, 0xB00),
    ('CFJ', 0x1407CB090, 0x1407CB2BE, 0x1407CB169, 0x1407CB170, 0x420),
    ('Z3U', 0x14086C870, 0x14086CB49, 0x14086C8EE, 0x14086C8F5, 0x1100),
    ('Z3U-alternate', 0x140DCBAF0, 0x140DCBE05, 0x140DCBB6E, 0x140DCBB75, 0x17C0),
    ('PWS1', 0x140973670, 0x1409737EF, 0x140973753, 0x14097375A, 0xB4),
    ('PWS2', 0x140A46900, 0x140A46A43, 0x140A469BE, 0x140A469C5, 0x5A),
    ('MJUS', 0x140B92A30, 0x140B92D09, 0x140B92BD5, 0x140B92BDD, 0x280),
    ('STG2', 0x140CCC310, 0x140CCC5E3, 0x140CCC4A6, 0x140CCC4AD, 0x190),
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
    preparation_ins = {i.address:i for _, start, end in PREPARATION_SPANS
                       for i in dump.instructions(start, end)}
    for address, mnemonic, operands in PREPARATION_EDGES:
        ins = preparation_ins[address]
        require((ins.mnemonic, ins.op_str) == (mnemonic, operands),
                f'{address:#x}: preparation continuation differs')
    for label, address, target in PREPARATION_SLOTS:
        require(dump.pointer(address) == target, label + ': preparation table slot differs')
    spans.extend(dump.span(*span) for span in PREPARATION_SPANS)
    transfers = []
    for tag, start, end, control, branch, size in TAIL_TRANSFERS:
        ins = dump.instructions(start, end)
        by_address = {i.address:i for i in ins}
        cmp, jump = by_address[control], by_address[branch]
        require(cmp.mnemonic == 'cmp' and cmp.op_str.endswith(' + 0x4654], 3')
                and jump.mnemonic == 'jne' and jump.operands[0].imm == end,
                tag + ': post-update transfer control differs')
        require(any(i.mnemonic == 'mov' and i.op_str.endswith(f' + 0x4658], {size:#x}')
                    for i in ins), tag + ': post-update transfer size differs')
        require(any(rip_targets(i) == [paths[tag][4]] for i in ins),
                tag + ': post-update root differs')
        require(any(rip_targets(i) == [0x141C14F98] for i in ins),
                tag + ': post-update session differs')
        spans.append(dump.span(tag+'-post-update-transfer-prefix', start, end))
        transfers.append(dict(game=tag, entry=hex(start), control=hex(control),
                              branch=hex(branch), join=hex(end), published_bytes=hex(size)))
    return dict(schema=3, code_sha256=CODE_SHA256,
                evidence='offline code trace; no live event or hook ABI validation',
                limitations=['Preparation can change state before the restore routine.',
                             'Bypass and save paths must not be mistaken for restoration.',
                             'Selective restore ranges and downstream helper side effects are not a full write inventory.',
                             'Preparation dispatch slots do not establish all callers or UI event labels.',
                             'Preparation and native update can continue after restoration.'],
                post_update_transfers=transfers,
                preparation_slots=[dict(label=label, address=hex(address), target=hex(target))
                                   for label,address,target in PREPARATION_SLOTS],
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
