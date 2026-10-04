"""Retain actor/display ownership evidence from the pinned offline dump."""
import argparse
import hashlib
import json
from pathlib import Path

from collection_lifecycle_audit import CODE_SHA256, DEFAULT_DUMP, Dump


SPANS = (
    ('display-publication-and-layout-routing', 0x140AFA010, 0x140AFA45C),
    ('indexed-gauge-publication', 0x140AFB400, 0x140AFB695),
    ('indexed-gauge-update', 0x140AFAB70, 0x140AFB3F8),
    ('special-gauge-update', 0x140AF9A90, 0x140AFA006),
    ('display-allocation-wrapper', 0x140AEFC30, 0x140AEFC98),
    ('general-pool-list-publication', 0x140A4BC00, 0x140A4BCFB),
    ('included-actor-setup-loop', 0x140A667F0, 0x140A6683E),
    ('actor-resource-setup', 0x140A66020, 0x140A667E9),
    ('actor-reset-through-gauge-initialization-prefix', 0x140A669B0, 0x140A66AA8),
)

# Pin numerical operands outside .text independently. These are rendering
# operands, not an assertion of screen coordinates or an allowed health range.
DATA = (
    ('gauge-divisor', 0x141017210, '00007a44'),
    ('gauge-multiplier', 0x1410DDF80, '00008e42'),
    ('gauge-percent-divisor', 0x140FE5400, '0000c842'),
    ('gauge-offset', 0x1410A40F8, 'e17a943e'),
    ('layout-step', 0x1410DDF84, '00001f43'),
    ('layout-origin', 0x1410712D4, '00008c43'),
    ('layout-sixth-origin', 0x14106E870, '00007043'),
    ('special-gauge-multiplier', 0x1410DDF7C, '5c8f623f'),
    ('special-gauge-offset', 0x1410DDF78, '1f85eb3d'),
)


def produce(directory=DEFAULT_DUMP):
    dump = Dump(directory)
    data = []
    for label, address, expected in DATA:
        raw = dump.read(address, len(bytes.fromhex(expected)))
        if raw.hex() != expected:
            raise ValueError(f'HUD operand differs at {address:#x}')
        data.append(dict(label=label, address=hex(address), bytes=raw.hex(),
                         sha256=hashlib.sha256(raw).hexdigest()))
    return dict(schema=1, code_sha256=CODE_SHA256,
                evidence='offline pinned dump', audio_eligible=False,
                spans=[dump.span(*span) for span in SPANS], data=data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump', type=Path, default=DEFAULT_DUMP)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = produce(args.dump)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"Retained {len(result['spans'])} spans and {len(result['data'])} data ranges")
