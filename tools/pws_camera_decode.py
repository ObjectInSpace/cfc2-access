"""Inspect stored Power Stone view candidates in a captured root block.

Accepts raw or gzip-compressed state-NN.bin files from collection_state_capture.
This does not attach to the game or authorize audio. Only the ordinary draw
branch has a selected index here; alternate passes stay explicitly unresolved.
"""
import argparse
import gzip
import json
import math
from pathlib import Path
import struct

LAYOUTS = {
    'pws1': dict(size=0x428F0, matrix=0x27974, stride=0x210, count=2,
                 gate=0x280C4, selector=None),
    'pws2': dict(size=0x8CD40, matrix=0x51D90, stride=0x308, count=4,
                 gate=0x52FDC, selector=0x52E88),
}
ACTORS = {
    'pws1': dict(base=0x18428, stride=0x7890, count=2, position=0x30, slot=3),
    'pws2': dict(base=0x40818, stride=0x42B8, count=4, position=0x3C, slot=1),
}


def decode(game, state):
    layout = LAYOUTS[game]
    if len(state) != layout['size']:
        raise ValueError(f"Expected full {game} root of {layout['size']} bytes")
    gate = state[layout['gate']]
    selector = (struct.unpack_from('<b', state, layout['selector'])[0]
                if layout['selector'] is not None else 0)
    selected = selector if gate == 0 and 0 <= selector < layout['count'] else None
    candidates = []
    for index in range(layout['count']):
        offset = layout['matrix'] + index * layout['stride']
        raw = state[offset:offset + 64]
        matrix = struct.unpack('<16f', raw)
        finite = all(math.isfinite(v) for v in matrix)
        affine = finite and tuple(matrix[i] for i in (3, 7, 11, 15)) == (0, 0, 0, 1)
        candidates.append(dict(index=index, offset=hex(offset), raw_hex=raw.hex(),
                               finite=finite, affine=affine,
                               matrix=list(matrix) if finite else None))
    actors = []
    actor = ACTORS[game]
    for index in range(actor['count']):
        base = actor['base'] + index * actor['stride']
        raw = state[base + actor['position']:base + actor['position'] + 12]
        xyz = struct.unpack('<3f', raw)
        actors.append(dict(fixed_index=index, raw_slot=state[base + actor['slot']],
                           position_offset=hex(base + actor['position']),
                           position_raw_hex=raw.hex(),
                           position=list(xyz) if all(math.isfinite(v) for v in xyz) else None))
    return dict(game=game, audio_eligible=False, layout_gate=gate,
                raw_selector=selector, ordinary_selected_index=selected,
                candidates=candidates, actor_candidates=actors,
                limitations=['No match/visibility/ownership or sample-coherence proof.',
                             'Actor positions are traced candidates, not certified body anchors.',
                             'No projection matrix is included in the root decode.',
                             'Alternate draw passes are not resolved.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', choices=LAYOUTS, required=True)
    parser.add_argument('state', type=Path)
    args = parser.parse_args()
    raw = args.state.read_bytes()
    if args.state.suffix == '.gz':
        raw = gzip.decompress(raw)
    print(json.dumps(decode(args.game, raw), indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
