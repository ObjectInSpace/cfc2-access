"""Resolve worker IAT targets against exports of DLLs loaded by a live process.

Read-only evidence; verifies target prefixes, not wait outcomes or full module integrity.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import pefile

from collection_metadata_capture import ProcessReader, exact, utc

SLOTS = (0x140FDC0C8, 0x140FDC350, 0x140FDC3A0, 0x140FDC3A8,
         0x140FDC3B0, 0x140FDC3E0, 0x140FDC3C0, 0x140FDC3D0, 0x140FDC1D0)


def resolve(read, modules):
    rows = []
    for slot in SLOTS:
        target = struct.unpack('<Q', exact(read, slot, 8))[0]
        row = dict(slot=hex(slot), target=hex(target), resolved=False)
        rows.append(row)
        for module in modules:
            rva = target - module['BaseAddress']
            if not 0 <= rva < module['ModuleMemorySize']:
                continue
            data = Path(module['FileName']).read_bytes()
            pe = pefile.PE(data=data, fast_load=True)
            try:
                pe.parse_data_directories(directories=[0])
                symbols = getattr(getattr(pe, 'DIRECTORY_ENTRY_EXPORT', None), 'symbols', ())
                names = [e.name.decode('ascii') for e in symbols
                         if e.address == rva and e.name and not e.forwarder]
                live = exact(read, target, 16)
                file_prefix = pe.get_data(rva, 16)
                row.update(module=module['ModuleName'], module_base=hex(module['BaseAddress']),
                           module_sha256=hashlib.sha256(data).hexdigest(), rva=hex(rva),
                           exports=names, live_prefix=live.hex(), file_prefix=file_prefix.hex(),
                           prefix_matches=live == file_prefix,
                           resolved=bool(names) and live == file_prefix)
            finally:
                pe.close()
            break
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.pid <= 0 or args.out.exists():
        parser.error('Require positive PID and a new output path')
    # PID is parsed as an integer; no user-supplied shell text is interpolated.
    command = (f'Get-Process -Id {args.pid} -Module -ErrorAction Stop | '
               'Select-Object ModuleName,FileName,'
               '@{Name="BaseAddress";Expression={$_.BaseAddress.ToInt64()}},'
               'ModuleMemorySize | ConvertTo-Json -Compress')
    output = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-Command', command],
                            check=True, capture_output=True, text=True, timeout=20)
    modules = json.loads(output.stdout.lstrip('\ufeff'))
    if isinstance(modules, dict):
        modules = [modules]
    reader = ProcessReader(args.pid)
    try:
        report = dict(schema=1, pid=args.pid, created_utc=utc(), imports=resolve(reader.read, modules),
                      tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      wait_outcomes_observed=False)
    finally:
        reader.close()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x', encoding='utf-8') as target:
        json.dump(report, target, indent=2)
        target.write('\n')
    print(f"Resolved {sum(r['resolved'] for r in report['imports'])}/{len(SLOTS)} targets; wait outcomes unobserved.")


if __name__ == '__main__':
    main()
