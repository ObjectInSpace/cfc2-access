"""Sabotage the Plasma Sword decoders one mechanism at a time; each must fail its test.

Usage: python tools/sabotage_stg2.py   (from the repository root; restores every file)
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SEL, FRM = 'run_stg2_select_test.ps1', 'run_stg2_frame_test.ps1'
CASES = [
    ('src/stg2_select.c', '0, 1, 2, 3, 5, 4, 7, 8, 6, 10, 9, 1,', '0, 1, 2, 3, 4, 5, 7, 8, 6, 10, 9, 1,', SEL, 'native table entries swapped'),
    ('src/stg2_select.c', 'id = g_native[number] | (g_row[number] ? 0x10u : 0u);', 'id = g_native[number];', SEL, 'row flag ignored'),
    ('src/stg2_select.c', '(s->window[STG2_W_HUMANS] >> side & 1) && ', '', SEL, 'human gate removed'),
    ('src/stg2_select.c', ' && s->window[STG2_W_STATE + side] == 0;', ';', SEL, 'chosen-side gate removed'),
    ('src/stg2_select.c', ' && word_at(s, STG2_W_SUB) == 1', '', SEL, 'sub-state gate removed'),
    ('src/stg2_select.c', 'word_at(s, STG2_W_MODE) == 1', 'word_at(s, STG2_W_MODE) != 0', SEL, 'mode gate loosened'),
    ('src/stg2_select.c', 'if (s->roulette[side] != 0) return -2;', ';', SEL, 'shuffle spoken'),
    # Not a case: dropping `number == 0` is caught by nothing, because select
    # number 0 maps to native 0, whose shell name is empty, and the name check
    # refuses it anyway. Two guards, one behaviour.
    ('src/stg2_select.h', '#define STG2_W_NUMBER   0x002u', '#define STG2_W_NUMBER   0x003u', SEL, 'select number offset'),
    ('src/stg2_frame.c', '#define OFF_HP      0x57Cu', '#define OFF_HP      0x57Eu', FRM, 'wrong health offset'),
    ('src/stg2_frame.c', '#define MAX_HP      200', '#define MAX_HP      250', FRM, 'wrong maximum'),
    ('src/stg2_frame.c', 'o->health.value = hp < 0 ? 0 : hp;', 'o->health.value = hp;', FRM, 'KO overkill unclamped'),
    ('src/stg2_frame.c', 'live = mode == 4 || mode == 11;', 'live = true;', FRM, 'intro refill fed'),
    ('src/stg2_frame.c', 'fighting = mode == 4;', 'fighting = live;', FRM, 'beacons through the KO replay'),
    ('src/stg2_frame.c', "if (s[0] != 3 || (mode != 3 && mode != 4 && mode != 11)) return;", "if (s[0] != 3) return;", FRM, 'match gate loosened'),
    ('src/stg2_frame.c', '#define PLASMA_STEP 0x90', '#define PLASMA_STEP 0x80', FRM, 'wrong plasma level size'),
    ('src/stg2_frame.c', 'o->resource.value = level >= cap ? cap * PLASMA_STEP', 'o->resource.value = level >= cap ? cap * PLASMA_STEP + fill', FRM, 'fill added at the cap'),
    ('src/stg2_frame.c', 'p[0] = f32(s, a + OFF_X); p[1] = f32(s, a + OFF_Y); p[2] = f32(s, a + OFF_Z);', 'p[0] = -f32(s, a + OFF_X); p[1] = -f32(s, a + OFF_Y); p[2] = -f32(s, a + OFF_Z);', FRM, 'positions negated'),
    ('src/stg2_frame.c', 'e[c] = p[0] * g[c] + p[1] * g[4 + c] + p[2] * g[8 + c] + g[12 + c];', 'e[c] = p[0] * g[4 * c] + p[1] * g[4 * c + 1] + p[2] * g[4 * c + 2] + g[12 + c];', FRM, 'game view transposed'),
    ('src/stg2_frame.c', 'if (g[3] != 0.0f || g[7] != 0.0f || g[11] != 0.0f || g[15] != 1.0f) return FALSE;', ';', FRM, 'affine check removed'),
    ('src/stg2_frame.c', 'if (!fighting || !cam || !isfinite(y) || !project(s, cam, a, &pan)) continue;', 'if (!fighting || !isfinite(y)) continue; if (!cam || !project(s, cam, a, &pan)) pan = 0.0f;', FRM, 'beacon without a camera'),
    ('src/render_project.h', '*ndc_x = c0 / c3;', '*ndc_x = -c0 / c3;', FRM, 'projection mirrored'),
    ('src/render_project.h', 'if (!isfinite(c0) || !isfinite(c3) || c3 <= 1e-3f) return false;', 'if (!isfinite(c0) || !isfinite(c3) || c3 == 0.0f) return false;', FRM, 'point behind the camera'),
    ('src/stg2_frame.c', 'if (y < 0.0f) {', 'if (y > 0.0f) {', FRM, 'height sign inverted'),
    ('src/stg2_frame.c', '#define OFF_CLOCK   0x4DF33u', '#define OFF_CLOCK   0x4DF34u', FRM, 'clock reads the frame counter'),
    ('src/stg2_frame.c', 'if (!s[OFF_NOCLOCK]) {', 'if (1) {', FRM, 'infinite timer ticks'),
]


def run(runner):
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                        str(ROOT / 'tools' / runner)], cwd=ROOT, capture_output=True, text=True)
    return r.returncode


def main():
    bad = 0
    for path, old, new, runner, what in CASES:
        p = ROOT / path
        original = p.read_bytes()
        text = original.decode('utf-8')
        if text.count(old) != 1:
            print(f'?? {what}: pattern not found once in {path}')
            bad += 1
            continue
        try:
            p.write_bytes(text.replace(old, new).encode('utf-8'))
            code = run(runner)
        finally:
            p.write_bytes(original)
        ok = code != 0
        print(('caught  ' if ok else 'MISSED  ') + what)
        bad += not ok
    for runner in sorted({c[3] for c in CASES}):
        if run(runner) != 0:
            print(f'restored tree fails {runner}')
            bad += 1
    print('all sabotages caught' if not bad else f'{bad} problem(s)')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
