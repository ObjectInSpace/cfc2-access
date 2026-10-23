import json
import math
import struct
import unittest

from pws_camera_decode import ACTORS, LAYOUTS, decode


class CameraDecodeTests(unittest.TestCase):
    def state(self, game):
        layout = LAYOUTS[game]
        state = bytearray(layout['size'])
        for i in range(layout['count']):
            matrix = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., float(i), 0., 0., 1.]
            struct.pack_into('<16f', state, layout['matrix'] + i * layout['stride'], *matrix)
        return state

    def test_distinct_matrices_and_no_audio_authorization(self):
        for game, layout in LAYOUTS.items():
            result = decode(game, self.state(game))
            self.assertFalse(result['audio_eligible'])
            self.assertEqual(result['ordinary_selected_index'], 0)
            self.assertEqual([c['matrix'][12] for c in result['candidates']],
                             list(range(layout['count'])))
            self.assertTrue(all(c['affine'] for c in result['candidates']))

    def test_all_pws2_selectors(self):
        state = self.state('pws2')
        for selector in range(256):
            state[0x52E88] = selector
            result = decode('pws2', state)
            self.assertEqual(result['ordinary_selected_index'], selector if selector < 4 else None)

    def test_alternate_pass_does_not_guess_camera(self):
        for game, layout in LAYOUTS.items():
            state = self.state(game)
            for gate in (1, 2, 255):
                state[layout['gate']] = gate
                self.assertIsNone(decode(game, state)['ordinary_selected_index'])

    def test_wrong_size_rejected(self):
        for game in LAYOUTS:
            state = self.state(game)
            for bad in (state[:-1], state + b'\0', b''):
                with self.assertRaises(ValueError):
                    decode(game, bad)

    def test_nonfinite_retained_without_invalid_json(self):
        for value in (math.nan, math.inf, -math.inf):
            state = self.state('pws1')
            struct.pack_into('<f', state, 0x27974, value)
            result = decode('pws1', state)
            self.assertIsNone(result['candidates'][0]['matrix'])
            self.assertFalse(result['candidates'][0]['affine'])
            self.assertEqual(bytes.fromhex(result['candidates'][0]['raw_hex']), state[0x27974:0x279B4])
            json.dumps(result, allow_nan=False)

    def test_zero_and_projective_matrices_not_affine(self):
        state = bytearray(LAYOUTS['pws2']['size'])
        self.assertFalse(decode('pws2', state)['candidates'][0]['affine'])
        state = self.state('pws2')
        struct.pack_into('<f', state, 0x51D90 + 12, 1.)
        self.assertFalse(decode('pws2', state)['candidates'][0]['affine'])

    def test_actor_slots_stay_distinct_from_camera_selector(self):
        for game, actor in ACTORS.items():
            state = self.state(game)
            for i in range(actor['count']):
                base = actor['base'] + i * actor['stride']
                state[base + actor['slot']] = 3 - i
                struct.pack_into('<3f', state, base + actor['position'], i, -i, 10 + i)
            result = decode(game, state)
            for i, value in enumerate(result['actor_candidates']):
                self.assertEqual(value['fixed_index'], i)
                self.assertEqual(value['raw_slot'], 3 - i)
                self.assertEqual(value['position'], [i, -i, 10 + i])
            self.assertEqual(result['ordinary_selected_index'], 0)

    def test_nonfinite_actor_position_keeps_raw_evidence(self):
        state = self.state('pws1')
        struct.pack_into('<f', state, 0x18458, math.inf)
        result = decode('pws1', state)
        self.assertIsNone(result['actor_candidates'][0]['position'])
        self.assertEqual(bytes.fromhex(result['actor_candidates'][0]['position_raw_hex']),
                         state[0x18458:0x18464])
        json.dumps(result, allow_nan=False)


if __name__ == '__main__':
    unittest.main()
