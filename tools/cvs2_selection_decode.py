"""Decode the traced CVS2 character-select state from a bounded snapshot.

Research diagnostic: only the observed selection path is accepted. Names other
than the user-verified Ryu, Sagat and Yun are left unresolved. No input history
is required. See docs/cvs2-state-access-trace.md for instruction evidence.
"""
import argparse
import json
from pathlib import Path
import struct


def decode(data, player=0):
    if len(data) != 0x216B0:
        raise ValueError('Expected exactly 0x216B0 bytes of CVS2 state')
    if player not in (0, 1):
        raise ValueError('Player must be 0 or 1')
    phase = 0x78
    result = dict(player=player + 1, status='unsupported_or_inactive')
    # Restrict output to the actual dispatch path observed in the captures.
    if (data[phase:phase + 4] != bytes([2, 1, 1, 1])
            or data[phase + 0x24] != 0
            or data[phase + 0x128] != 0
            or data[phase + 0x104] != data[0x1FFEC]
            or not data[phase + 0x104] & (1 << player)
            or data[0x1FFD0] != 7
            or data[0x1FFD8] > 2
            or data[0x1FFD4 + player] not in (1, 4, 7)
            or data[0x1FFE4 + player] != 0):
        return result
    base = 0x1EBA8 + player * 0x678
    if data[base + 0x5EC] != player:
        return dict(result, status='inconsistent_player_index')
    column, row = struct.unpack_from('<bb', data, 0x1FFDE + player * 2)
    # The game's logical grid occupies 0x80 bytes before its input copies.
    if not (0 <= column < 16 and 0 <= row < 8):
        return dict(result, status='invalid_grid_position')
    grid_id = data[0x1FFF0 + row * 16 + column]
    stored_id = data[base + 1]
    x, y = struct.unpack_from('<ii', data, 0x20098 + player * 8)
    result.update(stored_character_id=stored_id, grid_character_id=grid_id,
                  grid_column=column, grid_row=row, cursor_x=x, cursor_y=y)
    if grid_id >= 48:
        return dict(result, status='special_or_empty_cell')
    if stored_id != grid_id:
        return dict(result, status='inconsistent_snapshot')
    result.update(status='traced_character_state',
                  verified_name={0: 'Ryu', 9: 'Sagat', 44: 'Yun'}.get(stored_id))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--player', type=int, choices=(1, 2), default=1)
    args = parser.parse_args()
    print(json.dumps(decode(args.snapshot.read_bytes(), args.player - 1), indent=2))


if __name__ == '__main__':
    main()
