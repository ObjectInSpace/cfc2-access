"""Rank state bytes that move with direction presses: cursor LEADS only.

Reads a collection_state_capture session (trace.bin.gz + session.jsonl) and,
per root segment, scores every byte offset that changed:

- recall: of the presses on an axis, how many were followed by a change of
  this byte (a press at a grid edge may move nothing, so recall < 1 is normal);
- precision: of this byte's changes, how many sat next to a press. Quiet
  stretches are the control: timers and animation counters change there too,
  and score near zero.

A ranked byte is a lead. It becomes a decoder only after the instruction that
writes it is found and the screenshots agree (docs/port-tasks/state-capture-tool.md).

Usage: python tools/cursor_candidates.py build/captures/<session> [--top 15]
"""
import argparse
import bisect
import json
from pathlib import Path

from collection_state_capture import read_trace

LEAD = 0.1       # a press is logged up to one poll after the game moved
WINDOW = 0.3     # a cursor moves within a few frames of the press
AXES = {'left': 'horizontal', 'right': 'horizontal', 'up': 'vertical', 'down': 'vertical'}


def segments(trace_path):
    """[{root, start, end, changes: {offset: [(t, old, new)]}}], one per root."""
    out, state, seg = [], None, None
    for t, root, key, payload in read_trace(trace_path):
        if key:
            if seg is not None and root == seg['root'] and len(payload) == len(state):
                # Same root re-keyed: diff it like a delta so no change is lost.
                payload = [(i, b) for i, b in enumerate(payload) if b != state[i]]
            else:
                seg = dict(root=root, start=t, end=t, changes={})
                out.append(seg)
                state = bytearray(payload)
                continue
        for offset, value in payload:
            seg['changes'].setdefault(offset, []).append((t, state[offset], value))
            state[offset] = value
        seg['end'] = t
    return out


def presses(session_rows):
    return [(r['t'], r['direction']) for r in session_rows if r.get('kind') == 'input']


def score(changes, press_list, lead=LEAD, window=WINDOW):
    """Per-offset evidence; changes = [(t, old, new)], press_list = [(t, dir)]."""
    times = [t for t, _, _ in changes]
    press_times = sorted(t for t, _ in press_list)
    axes = {}
    agree = disagree = 0
    for p, direction in press_list:
        lo = bisect.bisect_left(times, p - lead)
        hi = bisect.bisect_right(times, p + window)
        axis = AXES.get(direction, 'any')
        hit_n, total_n = axes.get(axis, (0, 0))
        axes[axis] = (hit_n + (hi > lo), total_n + 1)
        if hi > lo and direction in ('left', 'right', 'up', 'down'):
            _, old, new = changes[lo]
            step = (new - old) % 256
            if step and step != 128:
                forward = step < 128
                if forward == (direction in ('right', 'down')):
                    agree += 1
                else:
                    disagree += 1
    near = 0
    for t in times:
        i = bisect.bisect_left(press_times, t - window)
        if i < len(press_times) and press_times[i] <= t + lead:
            near += 1
    best_axis, (hits, total) = max(axes.items(), key=lambda kv: (kv[1][0] / kv[1][1], kv[1][1]),
                                   default=('none', (0, 1)))
    precision = near / len(times) if times else 0.0
    return dict(axis=best_axis, hits=hits, presses=total, recall=hits / total,
                near=near, changes=len(times), precision=precision,
                score=(hits / total) * precision, agree=agree, disagree=disagree,
                axes={a: list(v) for a, v in axes.items()},
                values=[changes[0][1]] + [n for _, _, n in changes][:12])


def rank(segment, press_list, top=15):
    inside = [(t, d) for t, d in press_list
              if segment['start'] - LEAD <= t <= segment['end'] + WINDOW]
    if not inside:
        return []
    rows = []
    for offset, changes in segment['changes'].items():
        s = score(changes, inside)
        if s['hits']:
            s['offset'] = offset
            rows.append(s)
    rows.sort(key=lambda r: (-r['score'], -r['hits'], r['offset']))
    return rows[:top]


def describe(row):
    axes = ', '.join(f'{a} {h} of {n}' for a, (h, n) in sorted(row['axes'].items()))
    return (f"root+{row['offset']:#x}: score {row['score']:.2f}. Moved after {axes} presses. "
            f"{row['near']} of {row['changes']} changes were next to a press. "
            f"Direction agreed {row['agree']}, disagreed {row['disagree']}. "
            f"Values {' '.join(str(v) for v in row['values'])}.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    parser.add_argument('--top', type=int, default=15)
    parser.add_argument('--json', type=Path, help='Also write the ranking as JSON')
    args = parser.parse_args()
    rows = [json.loads(l) for l in (args.session / 'session.jsonl').read_text(encoding='utf-8').splitlines()]
    press_list = presses(rows)
    report = []
    for n, seg in enumerate(segments(args.session / 'trace.bin.gz'), 1):
        ranked = rank(seg, press_list, args.top)
        print(f"Segment {n}: root {seg['root']:#x}, {seg['end'] - seg['start']:.1f} seconds, "
              f"{len(seg['changes'])} bytes changed, "
              f"{sum(1 for t, _ in press_list if seg['start'] <= t <= seg['end'])} presses.")
        if not ranked:
            print('  No byte moved with a press in this segment.')
        for i, row in enumerate(ranked, 1):
            print(f'  {i}. {describe(row)}')
        report.append(dict(root=hex(seg['root']), start=seg['start'], end=seg['end'], ranked=ranked))
    print('These are leads. Confirm each against the instruction that writes it before use.')
    if args.json:
        args.json.write_text(json.dumps(report, indent=1), encoding='utf-8')


if __name__ == '__main__':
    main()
