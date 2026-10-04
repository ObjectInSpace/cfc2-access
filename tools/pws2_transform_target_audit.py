"""Retain offline transform-target evidence; never attach or authorize audio."""
import argparse
import hashlib
import json
from pathlib import Path

from collection_lifecycle_audit import CODE_SHA256, DEFAULT_DUMP, Dump


SPANS = (
    ("multi-point-target-production", 0x140AEA760, 0x140AEB44D),
    ("matrix-push-conditional-load", 0x140F331C0, 0x140F3324E),
    ("matrix-pop", 0x140F33130, 0x140F331BD),
    ("matrix-translation-read", 0x140F31F80, 0x140F31F99),
    ("matrix-template-reset", 0x140F33610, 0x140F33643),
    ("mode-zero-dispatch-and-pointer-publication", 0x140AE82B0, 0x140AE8A4C),
    ("script-update-gate", 0x140AECE00, 0x140AECE17),
    ("script-vector-production", 0x140AECE20, 0x140AEE000),
    ("component-interpolation", 0x140AEC780, 0x140AECABD),
    ("derive-target-ec", 0x140AEE180, 0x140AEE1EB),
    ("derive-target-bc", 0x140AEE1F0, 0x140AEE25B),
    ("resource-vector-thunk", 0x14099EF80, 0x14099EF85),
    ("resource-vector-wrapper", 0x1409BAD20, 0x1409BAD45),
)
DATA = (
    ("multi-point-direction-vectors", 0x1410AD680, 48),
    ("multi-point-target-separation", 0x140FE5400, 4),
    ("mode-zero-targets", 0x140AE8A4C, 56),
    ("mode-zero-remap", 0x140AE8A84, 36),
    ("initial-vector-a", 0x1410AD560, 12),
    ("initial-vector-b", 0x1410AD570, 12),
    ("published-vector-a", 0x14173E990, 12),
    ("published-vector-b", 0x14173E9A0, 12),
    ("target-low-nibble", 0x140AEE000, 60),
    ("actor-subdispatch", 0x140AEE03C, 44),
    ("target-high-nibble", 0x140AEE068, 24),
    ("target-high-remap", 0x140AEE080, 225),
    ("vector-production-dispatch", 0x140AEE164, 28),
)


def produce(directory):
    dump = Dump(directory)
    data = []
    for label, address, size in DATA:
        raw = dump.read(address, size)
        data.append(dict(label=label, address=hex(address), size=size,
                         bytes=raw.hex(), sha256=hashlib.sha256(raw).hexdigest()))
    return dict(code_sha256=CODE_SHA256, evidence="offline pinned dump",
                audio_eligible=False, spans=[dump.span(*s) for s in SPANS], data=data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", type=Path, default=DEFAULT_DUMP)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = produce(args.dump)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Retained {len(result['spans'])} spans and {len(result['data'])} data ranges")
