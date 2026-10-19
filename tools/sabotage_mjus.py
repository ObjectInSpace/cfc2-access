"""Sabotage the Project Justice decoders one mechanism at a time; each must fail its test.

Usage: python tools/sabotage_mjus.py   (from the repository root; restores every file)
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CASES = [
    ('src/mjus_select.c', '!on_screen(s, 2) || !(s->humans >> side & 1)', '!on_screen(s, 2) || !(s->humans >> (side + 1) & 1)', 'run_mjus_select_test.ps1', 'human bits shifted'),
    ('src/mjus_select.c', 'id = r[0x18 + slot];', 'id = r[0x18];', 'run_mjus_select_test.ps1', 'slot ignored'),
    ('src/mjus_select.c', "if (r[0x2A] != 1) return -1;", ';', 'run_mjus_select_test.ps1', 'free/story gate removed'),
    ('src/mjus_select.c', 'return s->task_state == state && s->task_step == 1;', 'return s->task_state == state;', 'run_mjus_select_test.ps1', 'step gate removed'),
    ('src/mjus_select.c', 'L"Taiyo High School", L"Gorin High School"', 'L"Gorin High School", L"Taiyo High School"', 'run_mjus_select_test.ps1', 'school order swapped'),
    ('src/mjus_select.c', 'unsigned visit = s->sides[side][0x17];', 'unsigned visit = 0;', 'run_mjus_select_test.ps1', 'pick visit ignored'),
    ('src/mjus_select.c', 'id = s->team[3 + cursor];', 'id = s->team[3];', 'run_mjus_select_test.ps1', 'team cursor ignored'),
    ('src/mjus_select.c', 'if (s->pick[2 + cursor]) return -1;', ';', 'run_mjus_select_test.ps1', 'picked entry spoken'),
    ('src/mjus_select.c', 'if (!s || side > 1 || s->phase != 1 || s->menu[2] != 0) return -1;', 'if (!s || side > 1) return -1;', 'run_mjus_select_test.ps1', 'team phase/done gate removed'),
    ('src/mjus_select.c', 'visit = 0x10u + mjus_team_picked(s);', 'visit = 0x10u + s->pick[0];', 'run_mjus_select_test.ps1', 'lagging pick count'),
    ('src/mjus_frame.c', '*ndc_x = c0 / c3;', '*ndc_x = -c0 / c3;', 'run_mjus_frame_test.ps1', 'projection mirrored'),
    ('src/mjus_frame.c', 'e[c] = pt[0] * v[c] + pt[1] * v[4 + c] + pt[2] * v[8 + c] + v[12 + c];', 'e[c] = pt[0] * v[4 * c] + pt[1] * v[4 * c + 1] + pt[2] * v[4 * c + 2] + v[4 * c + 3];', 'run_mjus_frame_test.ps1', 'view matrix transposed'),
    ('src/mjus_frame.c', '#define OFF_WORLD   0xD0u', '#define OFF_WORLD   0xECu', 'run_mjus_frame_test.ps1', 'midpoint-relative position projected'),
    ('src/mjus_frame.c', 'if (pr[11] != -1.0f ||', 'if (0 &&', 'run_mjus_frame_test.ps1', 'perspective check removed'),
    ('src/mjus_frame.c', 'if (!cam || !isfinite(y) || !mjus_project_x(cam, world, &pan)) continue;',
     'if (!isfinite(y)) continue; if (!cam) pan = 0.0f; else if (!mjus_project_x(cam, world, &pan)) continue;',
     'run_mjus_frame_test.ps1', 'beacon without a camera'),
    ('src/mjus_frame.c', '#define MAX_HP      200', '#define MAX_HP      250', 'run_mjus_frame_test.ps1', 'wrong maximum'),
    ('src/mjus_frame.c', '#define OFF_HP      0x2DEu', '#define OFF_HP      0x2E0u', 'run_mjus_frame_test.ps1', 'wrong health offset'),
    ('src/mjus_frame.c', 'if (s[PHASE_OFF] != 2) return;', '', 'run_mjus_frame_test.ps1', 'phase gate removed'),
    ('src/mjus_frame.c', '#define GUTS_LEVELS 5', '#define GUTS_LEVELS 6', 'run_mjus_frame_test.ps1', 'wrong guts cap'),
    ('src/mjus_frame.c', 'o->health.valid = hp >= 0 && hp <= MAX_HP;', 'o->health.valid = hp <= MAX_HP;', 'run_mjus_frame_test.ps1', 'KO sentinel accepted'),
    ('src/mjus_frame.c', 'if (y > 0.0f) {', 'if (y > 1e9f) {', 'run_mjus_frame_test.ps1', 'jump pitch removed'),
    ('src/mjus_text.c', "if (c == '\\\\') continue;", ';', 'run_mjus_text_test.ps1', 'copyright glyph spoken'),
    ('src/game_text.c', 'else if (!wcsncmp(t, L"(S)", 3)) t += 3;', ';', 'run_game_text_test.ps1', 'CREDIT(S) narrated'),
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
