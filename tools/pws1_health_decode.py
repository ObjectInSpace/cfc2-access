"""Inspect traced PWS1 health/resource fields; never authorize audio."""
import argparse
import gzip
import json
from pathlib import Path
import struct


def decode(state):
    if len(state) != 0x428F0:
        raise ValueError('Expected a full PWS1 root')
    actors = []
    for index in range(2):
        base = 0x18428 + index * 0x7890
        health, recoverable, trailing = struct.unpack_from('<3h', state, base + 0x7790)
        actors.append(dict(fixed_index=index, raw_slot=state[base+3],
            health_raw=health, recovery_target_raw=recoverable, trailing_display_raw=trailing,
            collectible_count_raw=state[base+0x77EC], collectible_bits_raw=state[base+0x77ED],
            model_form_raw=state[base+0x77AD],
            form_resource_raw=struct.unpack_from('<h', state, base+0x77B0)[0],
            within_traced_refill_range=-1 <= health <= 240,
            displayed_health_candidate=max(health, 0) if -1 <= health <= 240 else None))
    return dict(audio_eligible=False, phase_hex=state[0x27E58:0x27E60].hex(),
                observed_refill_cap=240, observed_form_resource_fill=2400, actors=actors,
                limitation='No match/visibility/lifetime admission; cap is from traced refill paths.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('state', type=Path)
    args = parser.parse_args()
    raw = args.state.read_bytes()
    if args.state.suffix == '.gz':
        raw = gzip.decompress(raw)
    print(json.dumps(decode(raw), indent=2))


if __name__ == '__main__':
    main()
