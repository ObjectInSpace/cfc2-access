"""Reproduce PWS1 health and adjacent collectible/form resource evidence."""
import argparse
import json
from pathlib import Path

from collection_lifecycle_audit import CODE_SHA256, DEFAULT_DUMP, Dump

SPANS = (
    ('damage-and-negative-floor', 0x1409BE300, 0x1409BE82E),
    ('item-recovery-cap', 0x140986317, 0x14098639A),
    ('conditional-both-actor-refill', 0x140993775, 0x1409937D0),
    ('recoverable-and-trailing-values', 0x140991360, 0x1409913CC),
    ('fixed-actors-recovery-callers', 0x140993804, 0x14099384C),
    ('context-option-refill-cap', 0x1409B2412, 0x1409B2460),
    ('gauge-layout-branch', 0x1409AAB7D, 0x1409AABB1),
    ('gauge-reader-two-actor-loop', 0x1409AAE1C, 0x1409AB003),
    ('collectible-increment-and-bitset', 0x140986150, 0x140986260),
    ('collectible-threshold-caller', 0x1409ECB50, 0x1409ECBBF),
    ('three-collectible-action-transition', 0x1409ECD34, 0x1409ECD50),
    ('form-enable-and-resource-fill', 0x140986470, 0x140986497),
    ('form-dependent-model-setup', 0x1409E3410, 0x1409E34D0),
    ('resource-periodic-decrement', 0x1409ECD50, 0x1409ECD76),
)


def produce():
    dump = Dump(DEFAULT_DUMP)
    return dict(schema=1, code_sha256=CODE_SHA256,
                limitation='Static health evidence; no runtime eligibility or universal mode coverage.',
                root_slot='0x1423188c0', actor_base='0x18428', actor_stride='0x7890',
                health_offset='0x7790', encoding='signed little-endian int16',
                recovery_target_offset='0x7792', trailing_display_offset='0x7794',
                observed_refill_cap=240, spans=[dump.span(*span) for span in SPANS])


def compact(report):
    return {**report, 'spans': [{k: v for k, v in s.items() if k != 'instructions'}
                               for s in report['spans']]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    report = produce()
    target = Path('docs/pws1-health-manifest.json')
    if args.check:
        if json.loads(target.read_text()) != compact(report):
            raise SystemExit('Health evidence differs')
    else:
        target.write_text(json.dumps(compact(report), indent=2) + '\n')
    full = Path('build/pws1-health-audit.json')
    full.parent.mkdir(parents=True, exist_ok=True)
    full.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Verified {len(SPANS)} PWS1 health spans; offline only.')


if __name__ == '__main__':
    main()
