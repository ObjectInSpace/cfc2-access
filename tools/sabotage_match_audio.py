"""Sabotage sweep for tools/test_match_audio.c.

Run: python tools\\sabotage_match_audio.py
Each case mutates a COPY of the sources under build/test and must fail the
named assertions; exit 1 if any sabotage goes uncaught.
"""
import os, shutil, subprocess, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
scratch = os.path.join(root, 'build', 'test', 'sabotage_match_audio')
gcc = os.path.expandvars(r'%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe')

cases = [
    ('stamp check always true', 'audio.c',
     '    LONG64 owner = g_owner_epoch;\n    if (!owner) return stamp == 0;',
     '    (void)stamp; return TRUE;\n    LONG64 owner = g_owner_epoch;\n    if (!owner) return stamp == 0;',
     ['9b: a beacon write that lost', '11: a tick armed before']),
    ('pause threshold huge', 'match_audio.c',
     'p->static_frames >= MATCH_AUDIO_PAUSE_FRAMES',
     'p->static_frames >= 100000', ['7: the pause is detected once']),
    ('clock without decrease', 'match_audio.c',
     'p->clock_seeded && t < p->clock &&', 'p->clock_seeded && t != p->clock &&',
     ['UPWARD jump onto 30']),
    ('gauge change check removed', 'match_audio.c',
     '    if (e->state == EDGE_VALID && e->value == v->value &&\n        e->maximum == v->maximum) return;',
     '', ['1b: unchanged health']),
    ('owner stop ignores epoch', 'audio.c',
     '    if (epoch && (LONG64)epoch == g_owner_epoch) {\n        InterlockedExchange(&g_owner_live, 0);',
     '    if (epoch) {\n        InterlockedExchange(&g_owner_live, 0);',
     ['10: a late stop']),
]
bad = 0
for name, fname, old, new, expect in cases:
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(scratch)
    for f in ('audio.c', 'audio.h', 'match_audio.c', 'match_audio.h', 'log.c', 'log.h'):
        shutil.copy(os.path.join(root, 'src', f), scratch)
    p = os.path.join(scratch, fname)
    s = open(p, encoding='utf-8', newline='').read().replace('\r\n', '\n')
    assert s.count(old) == 1, (name, s.count(old))
    open(p, 'w', encoding='utf-8', newline='').write(s.replace(old, new))
    exe = os.path.join(scratch, 't.exe')
    # The test includes ../src/*.h; point it at the scratch copies instead.
    t = open(os.path.join(root, 'tools', 'test_match_audio.c'), encoding='utf-8').read()
    t = t.replace('"../src/', '"')
    open(os.path.join(scratch, 'test.c'), 'w', encoding='utf-8').write(t)
    r = subprocess.run([gcc, '-O2', '-w', '-DAUDIO_OWNER_TEST', '-o', exe, os.path.join(scratch, 'test.c'),
                        os.path.join(scratch, 'match_audio.c'), os.path.join(scratch, 'audio.c'),
                        os.path.join(scratch, 'log.c'), '-lwinmm'], capture_output=True, text=True)
    if r.returncode:
        print('BUILD FAILED', name, r.stderr[:500]); bad += 1; continue
    out = subprocess.run([exe], capture_output=True, text=True).stdout
    fails = [l for l in out.splitlines() if l.startswith('FAIL')]
    hit = all(any(e in f for f in fails) for e in expect)
    print(('caught ' if hit else 'MISSED ') + name + ': ' + '; '.join(fails))
    bad += not hit
sys.exit(1 if bad else 0)
