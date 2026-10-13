"""Controls for the cursor-candidate analyzer; never attaches to the game."""
import gzip
import json
import random
import tempfile
import unittest
from datetime import datetime
from pathlib import Path

import cursor_candidates as cc
from collection_state_capture import TraceWriter

FIXTURE = Path(__file__).resolve().parent / 'fixtures' / 'cvs1-movement-schema2-20260928.jsonl.gz'


def write_session(directory, samples, presses):
    """samples: [(t, root, bytes)]; presses: [(t, direction)]."""
    trace = TraceWriter(Path(directory) / 'trace.bin.gz')
    for t, root, raw in samples:
        trace.sample(t, root, raw)
    trace.close()
    with (Path(directory) / 'session.jsonl').open('w', encoding='utf-8') as f:
        for i, (t, d) in enumerate(presses, 1):
            f.write(json.dumps(dict(kind='input', t=t, direction=d, index=i)) + '\n')


def load(directory):
    rows = [json.loads(l) for l in (Path(directory) / 'session.jsonl').read_text().splitlines()]
    return cc.segments(Path(directory) / 'trace.bin.gz'), cc.presses(rows)


class SyntheticTests(unittest.TestCase):
    CURSOR, ROW, TIMER, ANIM, QUIET, SHADOW = 0x100, 0x101, 0x200, 0x300, 0x400, 0x500

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        rng = random.Random(7)
        self.presses = ([(2.0 + 2 * i, 'right') for i in range(4)] +
                        [(10.0, 'left'), (12.0, 'left'), (14.0, 'down'), (16.0, 'down')])
        state = bytearray(0x1000)
        samples = []
        col = row = 0
        t = 0.0
        while t < 30.0:
            for p, d in self.presses:
                if p <= t < p + 0.05:               # the game reacts within a frame
                    if d == 'right': col += 1
                    elif d == 'left': col -= 1
                    elif d == 'down' and p < 15: row += 1   # second down hits the edge
            state[self.CURSOR] = state[self.SHADOW] = col % 256
            state[self.ROW] = row
            state[self.TIMER] = int(t) % 256                 # ticks every second
            if round(t / 0.05) % 2 == 0:
                state[self.ANIM] = rng.randrange(256)        # always animating
            if t >= 20.0:
                state[self.QUIET] = int((t - 20) / 2.5)      # moves only in quiet time
            samples.append((t, 0xABC000, bytes(state)))
            t = round(t + 0.05, 2)
        # The tool polls input BEFORE reading state, so a press can be logged a
        # poll after the game already moved. Log one press that way.
        logged = [(p + 0.04 if p == 8.0 else p, d) for p, d in self.presses]
        write_session(self.tmp.name, samples, logged)

    def test_cursor_ranks_first_and_decoys_are_excluded(self):
        segs, presses = load(self.tmp.name)
        self.assertEqual(len(segs), 1)
        ranked = cc.rank(segs[0], presses, top=50)
        self.assertEqual(ranked[0]['offset'], self.CURSOR)
        self.assertEqual(ranked[1]['offset'], self.SHADOW, 'a copy is reported too, as a lead')
        top = {r['offset']: r for r in ranked}
        self.assertEqual(top[self.CURSOR]['axis'], 'horizontal')
        self.assertEqual((top[self.CURSOR]['hits'], top[self.CURSOR]['presses']), (6, 6))
        self.assertEqual((top[self.CURSOR]['agree'], top[self.CURSOR]['disagree']), (6, 0))
        self.assertEqual(top[self.ROW]['axis'], 'vertical')
        self.assertEqual((top[self.ROW]['hits'], top[self.ROW]['presses']), (1, 2),
                         'a press at the edge moves nothing and is not held against it')
        for decoy in (self.TIMER, self.ANIM):
            if decoy in top:
                self.assertLess(top[decoy]['score'], 0.3, hex(decoy))
        self.assertNotIn(self.QUIET, top, 'a byte that only moves in quiet time is no lead')

    def test_root_change_starts_a_new_segment(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        a, b = bytes(16), bytes([1]) + bytes(15)
        write_session(tmp.name, [(0.0, 1, a), (0.05, 1, b), (0.1, 2, a), (0.15, 2, b)], [(0.05, 'right')])
        segs, _ = load(tmp.name)
        self.assertEqual([s['root'] for s in segs], [1, 2])
        self.assertEqual(segs[0]['changes'], {0: [(0.05, 0, 1)]})


class Cvs1FixtureReplay(unittest.TestCase):
    """REAL CVS1 bytes: the 0x112-byte phase block of the retained movement
    fixture, sampled every 250 ms. Presses come from the game's OWN input
    record (actor 0 input words), not from the selection byte, so the test is
    not circular. The traced fields are the selection byte P+0x78 and the
    position byte P+0x104+group, i.e. P+0x105 for group 1 (docs/cvs1-*.md).
    The phase block also holds free-running frame counters: the decoys."""

    def test_traced_cursor_fields_rank_first(self):
        rows = [json.loads(l) for l in gzip.open(FIXTURE, 'rt')][1:]
        samples, presses, held = [], [], False
        for r in rows:
            t = datetime.fromisoformat(r['started_utc']).timestamp()
            rec = r['before']['records']
            samples.append((t, 1, bytes.fromhex(rec['phase']['hex'])))
            pressed = any(bytes.fromhex(rec['actor_0_inputs']['hex']))
            if pressed and not held:
                presses.append((t, 'press'))
            held = pressed
        self.assertGreaterEqual(len(presses), 2, 'the fixture records game-side presses')
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        write_session(tmp.name, samples, presses)
        segs, press_list = load(tmp.name)
        ranked = cc.rank(segs[0], press_list, top=10)
        order = [hex(r['offset']) for r in ranked]
        self.assertEqual({r['offset'] for r in ranked[:2]}, {0x78, 0x105}, order)
        self.assertTrue(all(r['score'] < ranked[1]['score'] / 2 for r in ranked[2:]),
                        'the frame counters stay well below the traced fields')


if __name__ == '__main__':
    unittest.main()
