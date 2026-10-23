"""Reproduce bounded Power Stone cursor/phase/name-publication evidence."""
import argparse
import json
from pathlib import Path
from collection_lifecycle_audit import CODE_SHA256, DEFAULT_DUMP, Dump

SPANS = (
    ('pws1-outer-dispatch', 0x140976B50, 0x140976BDE),
    ('pws1-update-gates', 0x140976CF8, 0x140976D35),
    ('pws1-phase-table-use', 0x140976EFC, 0x140976F22),
    ('pws1-selection-phase-call', 0x140976FF8, 0x140977010),
    ('pws1-select-subphase', 0x1409A67E0, 0x1409A6820),
    ('pws1-select-owner-dispatch', 0x1409A69A1, 0x1409A69F0),
    ('pws1-cursor-and-commit', 0x1409A0960, 0x1409A1485),
    ('pws2-outer-dispatch', 0x140A48D3F, 0x140A48DA7),
    ('pws2-game-phase', 0x140A4CA30, 0x140A4CA60),
    ('pws2-mode-gate', 0x140A4CAFF, 0x140A4CB48),
    ('pws2-phase-table-use', 0x140A4CB48, 0x140A4CB6B),
    ('pws2-selection-phase-call', 0x140A4CD67, 0x140A4CD6E),
    ('pws2-selection-subphase', 0x140A692C0, 0x140A69327),
    ('pws2-select-owner-dispatch', 0x140A695F0, 0x140A696CD),
    ('pws2-cursor-and-commit', 0x140A6B120, 0x140A6B632),
)
DATA = (
    ('pws1-cursor-id-map', 0x14109C510, '080006020403050701090a'),
    ('pws1-phase-two-target', 0x1409772D8 + 2*4, 'f86f9700'),
    ('pws2-phase-one-target', 0x140A4CDB8 + 4, '67cda400'),
    ('pws2-grid-base', 0x141730140, '080201060b03040000080305010b070209070601060a02070a0500040409050302000b0a07040a0b050608090301090800000000000000000000000000000000'),
    ('pws2-grid-pride', 0x141730180, '08020f060b03040000080305010b070209070601060a02070a0500040409050302000b0a07040a0b05060809030109080000000000000000000000000f0f0f00'),
    ('pws2-grid-mel-pride', 0x1417301C0, '08020f060b03040e00080305010b070209070601060a02070a0500040409050302000b0a07040a0b050608090301090800000000000000000e0e010f0f0f0e00'),
)


def produce(directory=DEFAULT_DUMP):
    dump = Dump(directory)
    data = []
    for label, address, expected in DATA:
        raw = dump.read(address, len(bytes.fromhex(expected)))
        if raw.hex() != expected:
            raise ValueError(f'{label} differs from reviewed data')
        data.append(dict(label=label, address=hex(address), bytes=raw.hex()))
    return dict(schema=1, code_sha256=CODE_SHA256,
                evidence='static cursor path; live screen eligibility pending', data=data,
                spans=[dump.span(*span) for span in SPANS])


def compact(report):
    return {**report, 'spans': [{k: v for k, v in span.items() if k != 'instructions'}
                               for span in report['spans']]}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dump', default=DEFAULT_DUMP)
    p.add_argument('--check', action='store_true')
    args = p.parse_args()
    report = produce(args.dump)
    target = Path('docs/pws-selection-manifest.json')
    if args.check:
        if compact(report) != json.loads(target.read_text(encoding='utf-8')):
            raise SystemExit('Selection evidence differs')
    else:
        target.write_text(json.dumps(compact(report), indent=2) + '\n', encoding='utf-8')
    full = Path('build/pws-selection-audit.json')
    full.parent.mkdir(parents=True, exist_ok=True)
    full.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"Verified {len(SPANS)} spans and {len(DATA)} data anchors; offline only.")


if __name__ == '__main__':
    main()
