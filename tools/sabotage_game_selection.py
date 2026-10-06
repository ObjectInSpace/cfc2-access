"""Falsify shared selection controls using disposable compiled source copies.

No game access, source edits, deployment or speech device. A mutant must build
successfully and then fail the intended behavioral assertion.
"""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GCC = (Path(os.environ['LOCALAPPDATA']) / 'Microsoft/WinGet/Packages' /
       'BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe' /
       'mingw64/bin/gcc.exe')
OUT = ROOT / 'build/test/game-selection-sabotage'
MUTATIONS = (
    ('one-sample-settles',
     'same(c, &p->pending[i]) && !same(c, &p->accepted[i])',
     'valid(c) && !same(c, &p->accepted[i])', 'calls == 0'),
    ('restore-erases-accepted-history',
     'p->revision = o->token.revision;',
     'memset(p->accepted, 0, sizeof p->accepted); p->revision = o->token.revision;',
     '!drain(1000)'),
    ('rejected-backend-marks-accepted',
     'game_session_accept(session, &p->latest.token, accepted)',
     'game_session_accept(session, &p->latest.token, accepted || true)',
     'calls == 1'),
    ('stale-token-reaches-backend',
     '!game_session_can_output(session, &p->latest.token)',
     '!session', 'calls == before'),
)


def run(name, source, expected=None):
    path = OUT / (name + '.c')
    exe = OUT / (name + '.exe')
    path.write_text(source, encoding='utf-8')
    build = subprocess.run([str(GCC), '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'src'), '-o', str(exe), str(path),
                            str(ROOT / 'src/game_session.c'),
                            str(ROOT / 'tools/test_game_selection.c')],
                           capture_output=True, text=True)
    if build.returncode:
        raise RuntimeError(f'{name}: compile failure is not a test rejection\n{build.stderr}')
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    (OUT / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    if expected is None:
        if result.returncode:
            raise RuntimeError('Original must pass before mutants run')
    elif result.returncode != 1 or not any(
            line.startswith('FAIL') and expected in line for line in result.stdout.splitlines()):
        raise RuntimeError(f'{name}: expected behavioral failure absent')
    print(f'{name}: {"passed" if expected is None else "rejected by behavioral test"}')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    original = (ROOT / 'src/game_selection.c').read_text(encoding='utf-8')
    run('original', original)
    for name, before, after, expected in MUTATIONS:
        if original.count(before) != 1:
            raise RuntimeError(f'{name}: mutation anchor must be unique')
        run(name, original.replace(before, after), expected)


if __name__ == '__main__':
    main()
