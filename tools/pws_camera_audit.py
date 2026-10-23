"""Retain the Power Stone stored-view route in the reviewed unpacked image.

Offline evidence only; neither selected matrix nor slot identity proves a
visible fighter, active match, or coherent live sample.
"""
import argparse
import json
from pathlib import Path

from collection_lifecycle_audit import CODE_SHA256, DEFAULT_DUMP, Dump

SPANS = (
    ('pws1-camera-update', 0x140996860, 0x140996B56),
    ('pws1-single-camera-builder', 0x1409971D0, 0x140997403),
    ('pws1-camera-fighter-position-consumer', 0x1409974F3, 0x140997561),
    ('pws1-draw-view-selection', 0x1409A8809, 0x1409A89A1),
    ('pws2-camera-builder', 0x140A5BBE0, 0x140A5BCA4),
    ('pws2-ordinary-view-selection', 0x140A5D649, 0x140A5D6D1),
    ('pws2-fighter-position-render-consumer', 0x140A5E0C7, 0x140A5E153),
    ('renderer-slot-selection', 0x140F326E0, 0x140F32724),
    ('renderer-slot-copy-in', 0x140F32440, 0x140F32487),
    ('renderer-slot-copy-out', 0x140F31F40, 0x140F31F72),
    ('renderer-compose', 0x140F33830, 0x140F3388B),
)


def produce(directory=DEFAULT_DUMP):
    dump = Dump(directory)  # rejects an unreviewed code image
    return dict(schema=1, code_sha256=CODE_SHA256,
                evidence='offline instructions; no live camera acceptance',
                context_slot='0x142c79b40', view_offset='0x1f812c',
                projection_offset='0x1f816c',
                spans=[dump.span(*span) for span in SPANS])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump', default=DEFAULT_DUMP)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    report = produce(args.dump)
    if args.check:
        if json.loads(args.out.read_text(encoding='utf-8')) != report:
            raise SystemExit('Camera evidence differs')
    else:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"Verified {len(report['spans'])} instruction spans; offline only.")


if __name__ == '__main__':
    main()
