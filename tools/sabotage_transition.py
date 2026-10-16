"""Sabotage sweep for tools/test_transition.c.

Run: python tools\\sabotage_transition.py
Each case mutates a COPY of transition_channel.c under build/test and must
fail the named assertions; exit 1 if any sabotage goes uncaught.
"""
import os, shutil, subprocess, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
scratch = os.path.join(root, 'build', 'test', 'sabotage_transition')
gcc = os.path.expandvars(r'%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe')

LATCH = '''    for (unsigned p = 0; p < TRANSITION_OWNERS; p++) if (include[p]) {
        c->announced[p] = true;
        c->announced_id[p] = o->side[p].current.id;
    }
'''
cases = [
    ('fighter latch before acceptance',
     '    if (!tc_say(line, TRUE, c->source)) { c->refused++; return false; }\n    c->spoken++;\n' + LATCH,
     LATCH + '    if (!tc_say(line, TRUE, c->source)) { c->refused++; return false; }\n    c->spoken++;\n',
     ['3: a refused change is retried']),
    ('intro keyed on introduced only',
     "if (line[0] && (!c->introduced || wcscmp(line, c->intro))) {",
     "if (line[0] && !c->introduced) {",
     ['8: a new opponent team is introduced']),
    ('ownership always You',
     '        mt_fmt(out, cap, MT_PLAYER_N, n, NULL, NULL);',
     '        swprintf(out, cap, L"%ls", mt(MT_YOU)); (void)n;',
     ['6: unknown ownership speaks']),
    ('depart leaves channel begun',
     '    memset(c, 0, sizeof *c);\n    c->spoken = spoken; c->refused = refused;\n}\n\nstatic void member_text',
     '    memset(c, 0, sizeof *c);\n    c->spoken = spoken; c->refused = refused;\n    c->begun = true;\n}\n\nstatic void member_text',
     ['4: departure cancels']),
]
bad = 0
for name, old, new, expect in cases:
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(scratch)
    for f in ('transition_channel.c', 'transition_channel.h', 'text_channel.c', 'text_channel.h',
              'mod_text.c'):
        shutil.copy(os.path.join(root, 'src', f), scratch)
    for f in os.listdir(os.path.join(root, 'src')):
        if f.endswith('.h') and not os.path.exists(os.path.join(scratch, f)):
            shutil.copy(os.path.join(root, 'src', f), scratch)
    p = os.path.join(scratch, 'transition_channel.c')
    s = open(p, encoding='utf-8', newline='').read().replace('\r\n', '\n')
    assert s.count(old) == 1, (name, s.count(old))
    open(p, 'w', encoding='utf-8', newline='').write(s.replace(old, new))
    t = open(os.path.join(root, 'tools', 'test_transition.c'), encoding='utf-8').read()
    open(os.path.join(scratch, 'test.c'), 'w', encoding='utf-8').write(t.replace('"../src/', '"'))
    exe = os.path.join(scratch, 't.exe')
    r = subprocess.run([gcc, '-O2', '-w', '-o', exe, os.path.join(scratch, 'test.c'), p,
                        os.path.join(scratch, 'text_channel.c'),
                        os.path.join(scratch, 'mod_text.c')], capture_output=True, text=True)
    if r.returncode:
        print('BUILD FAILED', name, r.stderr[:500]); bad += 1; continue
    out = subprocess.run([exe], capture_output=True, text=True).stdout
    fails = [l for l in out.splitlines() if l.startswith('FAIL')]
    hit = all(any(e in f for f in fails) for e in expect)
    print(('caught ' if hit else 'MISSED ') + name + ': ' + '; '.join(fails))
    bad += not hit
sys.exit(1 if bad else 0)
