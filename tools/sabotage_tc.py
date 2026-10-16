"""Applies each sabotage to the shared text layer and requires run_tc_test to FAIL.

A passing suite that has never been seen to fail is not evidence. Each entry
names the mechanism it removes; the case that must catch it is in the comment
beside that mechanism in test_text_channel.c.
"""
import subprocess, sys, os
S = 'src/text_channel.c'
C = 'src/text_capture.c'
SABS = [
 ("tc_say always reports success", S,
  'if (speech_say(text, interrupt)) {', 'if (speech_say(text, interrupt), 1) {'),
 ("rows are not sorted into reading order", S,
  'qsort(out->rows, out->count, sizeof out->rows[0], row_order);', ';'),
 ("session invalidation is a no-op", S,
  'p->published_at = 0; /* Require a frame produced after this change. */', ';'),
 ("text de-dup never re-arms", S,
  '} else if (now - d->slot[slot].seen > rearm_ms) {\n        d->slot[slot].spoken = FALSE;\n    }', '}'),
 ("queue drains newest-first", S,
  'memcpy(text, q->line[q->tail % TC_QUEUE_CAP], sizeof text);',
  'memcpy(text, q->line[(q->head-1) % TC_QUEUE_CAP], sizeof text);'),
 ("settling accepts a single read", S,
  'if (wcscmp(text, s->pending)) {\n        lstrcpynW(s->pending, text, TC_LINE_CAP);\n        return FALSE;\n    }',
  'lstrcpynW(s->pending, text, TC_LINE_CAP);'),
 ("an empty registry answers with silence", S,
  'speech_say(mt(MT_TEXT_NO_READER), TRUE);\n        return;', 'return;'),
 ("capture overflow is not flagged", C,
  'if (collecting->count >= TC_ROW_CAP) { collecting->overflow = TRUE; return; }',
  'if (collecting->count >= TC_ROW_CAP) { return; }'),
 ("invalid encodings convert to U+FFFD", S,
  'got = MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, in, -1,',
  'got = MultiByteToWideChar(code_page, 0, in, -1,'),
]

def read(p):  return open(p, encoding='utf-8', errors='surrogateescape').read()
def write(p, s): open(p, 'w', encoding='utf-8', errors='surrogateescape').write(s)

def run():
    r = subprocess.run(['powershell', '-NoProfile', '-File', 'tools/run_tc_test.ps1'],
                       capture_output=True, text=True)
    return r.returncode

originals = {S: read(S), C: read(C)}
if run() != 0:
    print('BASELINE FAILS -- fix the suite before trusting any sabotage')
    sys.exit(1)
print('baseline: PASS\n')
bad = []
try:
    for name, path, a, b in SABS:
        src = originals[path]
        assert src.count(a) == 1, f'anchor not unique: {name}'
        write(path, src.replace(a, b))
        rc = run()
        write(path, src)
        ok = rc != 0
        print(f'{"caught " if ok else "MISSED "} {name}')
        if not ok:
            bad.append(name)
finally:
    for p, s in originals.items():
        write(p, s)

print()
if bad:
    print(f'{len(bad)} sabotage(s) NOT caught -- those mechanisms are untested:')
    for n in bad: print('  ', n)
    sys.exit(1)
print(f'all {len(SABS)} sabotages caught; sources restored')
