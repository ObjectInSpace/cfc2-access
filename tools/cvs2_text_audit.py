"""Reproduce the additional CVS2 text-source inventory from local game files.

RIP-relative LEA references are code-derived leads, not proof of display.
Archive printable runs outside the known script blocks are unverified and
often image data. No inference that a raster-only caption is absent is valid.
"""
import argparse
import json
import re
import struct
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from analyze_dump import read_manifest, DEFAULT_DUMP
from cvs2_quotes import extract_blob, DEFAULT_ARC


def audit(dump, arc):
    sections = read_manifest(dump)

    def read(address, size):
        for section in sections:
            offset = address - section.live
            if 0 <= offset and offset + size <= len(section.data):
                return section.data[offset:offset + size]
        return b''

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    md.skipdata = True
    refs = []
    for ins in md.disasm(read(0x1406A0000, 0xA0000), 0x1406A0000):
        if ins.mnemonic != 'lea':
            continue
        for operand in ins.operands:
            if operand.type != X86_OP_MEM or operand.mem.base != X86_REG_RIP:
                continue
            address = ins.address + ins.size + operand.mem.disp
            value = read(address, 256).split(b'\0')[0]
            if len(value) >= 3 and all(32 <= c < 127 for c in value) and re.search(rb'[A-Za-z]', value):
                refs.append(dict(instruction=hex(ins.address), address=hex(address), text=value.decode('ascii')))
    tables = []
    for base in (0x141282A60, 0x141282BA0, 0x141282CE0, 0x141282F60):
        for index in range(20):
            data = read(base + index * 64, 64)
            if not data:
                break
            x, y, style = struct.unpack_from('<hhi', data)
            if x < 0:
                break
            tables.append(dict(address=hex(base + index * 64), x=x, y=y, style=style,
                               text=data[8:].split(b'\0')[0].decode('ascii', 'replace')))
    blob = extract_blob(arc)
    counts = {'quotes': 0, 'story': 0, 'unverified_other_runs': 0}
    for match in re.finditer(rb'[\x20-\x7e]{12,}\x00', blob):
        if len(re.findall(rb'[A-Za-z]{3,}', match.group())) < 2:
            continue
        offset = match.start()
        if 0x1FD840 <= offset < 0x204040:
            counts['quotes'] += 1
        elif 0x204040 <= offset < 0x2082EC:
            counts['story'] += 1
        else:
            counts['unverified_other_runs'] += 1
    return dict(native_literal_references=refs, native_title_warning_records=tables,
                archive_candidate_counts=counts,
                caveat='Counts are printable-run search results, not display coverage. Non-string sprite text needs separate tracing.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump', default=DEFAULT_DUMP)
    parser.add_argument('--arc', default=DEFAULT_ARC)
    parser.add_argument('--out', type=Path, default=Path('build/cvs2-text-audit.json'))
    args = parser.parse_args()
    result = audit(args.dump, args.arc)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"Wrote {args.out}: {len(result['native_literal_references'])} native literal references, "
          f"{len(result['native_title_warning_records'])} title/warning records.")
    print(result['archive_candidate_counts'])
