"""Generate src/mjus_story_text.h from data/mjus_story/.

Project Justice draws its story dialogue as pictures: one 128x128 cell per
message in AFS entry 34 (Japanese) and 35 (English) of game_60.arc. The
pictures were transcribed by hand into data/mjus_story/{en,ja}.tsv, one line
per cell, keyed "SS.CC" (scene ID, message index):

    SS.CC<TAB>text            text, with {} where the game overlays a word
    SS.CC<TAB>#WORDS<TAB>a|b  the scene's words cell (never shown alone)
    SS.CC<TAB>#GLYPH<TAB>x    a lone icon cell (never shown alone)

fills.tsv supplies each {} in order ("lang<TAB>SS.CC<TAB>w1|w2"). The fills
were read off the word pieces the game itself overlays (per-scene struct
+0x20, see docs/port-tasks/mjus-text.md), not guessed.

Run:  python tools/make_mjus_story.py   (also --check: fail if stale)
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'data', 'mjus_story')
OUT = os.path.join(ROOT, 'src', 'mjus_story_text.h')


def load(lang):
    rows = {}
    with open(os.path.join(DATA, lang + '.tsv'), encoding='utf-8') as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip('\n')
            if not line:
                continue
            key, text = line.split('\t', 1)
            sc, msg = key.split('.')
            k = (int(sc), int(msg))
            if not (0 <= k[0] < 100 and 0 <= k[1] < 256) or len(key) != 5:
                raise SystemExit('%s.tsv:%d bad key %r' % (lang, n, key))
            if k in rows:
                raise SystemExit('%s.tsv:%d duplicate %s' % (lang, n, key))
            rows[k] = text
    return rows


def load_fills():
    fills = {}
    with open(os.path.join(DATA, 'fills.tsv'), encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if line:
                lang, key, words = line.split('\t')
                sc, msg = key.split('.')
                fills[(lang, int(sc), int(msg))] = words.split('|')
    return fills


def resolve(lang, rows, fills):
    out = {}
    used = set()
    for k, text in sorted(rows.items()):
        if text.startswith('#'):
            continue                      # words cell / icon: never spoken alone
        holes = text.count('{}')
        words = fills.get((lang,) + k)
        if holes or words:
            if not words or len(words) != holes:
                raise SystemExit('%s %02d.%02d: %d blanks, fills %r' % (lang, k[0], k[1], holes, words))
            for w in words:
                text = text.replace('{}', w, 1)
            used.add(k)
        if '{' in text or '}' in text or '\t' in text:
            raise SystemExit('%s %02d.%02d: stray markup in %r' % (lang, k[0], k[1], text))
        out[k] = text
    stale = [k for (L, *k) in fills if L == lang and tuple(k) not in used]
    if stale:
        raise SystemExit('%s: fills for messages without blanks: %r' % (lang, stale))
    return out


def c_wide(s):
    """L"..." pieces; non-ASCII as \\x escapes, each closed so a following hex
    digit cannot extend it."""
    parts, run = [], ''
    for ch in s:
        o = ord(ch)
        if 0x20 <= o < 0x7f and ch not in '"\\?':
            run += ch
        else:
            if run:
                parts.append('L"%s"' % run)
                run = ''
            parts.append('L"\\x%x"' % o)
    if run:
        parts.append('L"%s"' % run)
    return ' '.join(parts) if parts else 'L""'


def table(name, lines):
    body = ['static const mjus_story_line %s[] = {' % name]
    for (sc, msg), text in sorted(lines.items()):
        body.append('    { 0x%02x%02x, %s },' % (sc, msg, c_wide(text)))
    body.append('};')
    return '\n'.join(body)


def load_narration(lang):
    """nar_<lang>.tsv: P00..P14, one narration screen per line (AFS entry 27
    English / 26 Japanese; a screen is two 256x256 pictures side by side)."""
    pages = {}
    with open(os.path.join(DATA, 'nar_%s.tsv' % lang), encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if line:
                key, text = line.split('\t', 1)
                pages[int(key[1:])] = text
    if sorted(pages) != list(range(15)):
        raise SystemExit('nar_%s.tsv: want pages P00..P14, have %r' % (lang, sorted(pages)))
    return [pages[i] for i in range(15)]


# Speaker names: native cast ID (shell ID - 1) -> the collection's own
# STATS_MJUS_NN_NAME label, in English and Japanese. Costume variants speak
# as the character (Powered Akira, Burning Batsu, Demon Hyo, Wild Daigo).
# Vatsu (0x1C) is Kurow's Batsu disguise; until he unmasks, a sighted player
# sees Batsu, so it is spoken as Batsu rather than giving the twist away.
SPEAKER = {
    0x00: 1, 0x01: 2, 0x02: 3, 0x03: 9, 0x04: 10, 0x05: 11, 0x06: 23, 0x07: 24,
    0x08: 22, 0x09: 15, 0x0A: 19, 0x0B: 16, 0x0C: 25, 0x0D: 26, 0x0E: 27, 0x0F: 28,
    0x10: 19, 0x11: 4, 0x12: 14, 0x13: 6, 0x14: 5, 0x15: 13, 0x16: 20, 0x17: 12,
    0x18: 21, 0x19: 1, 0x1A: 28, 0x1B: 14, 0x1C: 1,
}


def load_speakers():
    out = {}
    for lang, f in (('en', 'text_english.json'), ('ja', 'text_japanese.json')):
        with open(os.path.join(ROOT, 'data', f), encoding='utf-8') as fh:
            text = json.load(fh)
        out[lang] = [text['STATS_MJUS_%02d_NAME' % SPEAKER[i]] for i in range(len(SPEAKER))]
    return out


def load_cast():
    """cast.tsv (tools/extract_mjus_cast.py): per scene, each side's slots."""
    cast = {}
    with open(os.path.join(DATA, 'cast.tsv'), encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if line:
                sc, s0, s1 = line.split('\t')
                sides = [[int(x, 16) for x in s.split(',')] if s else [] for s in (s0, s1)]
                for ids in sides:
                    if len(ids) > 8 or any(i not in SPEAKER for i in ids):
                        raise SystemExit('cast.tsv scene %s: unnamed or too many: %r' % (sc, ids))
                cast[int(sc)] = sides
    if sorted(cast) != list(range(100)):
        raise SystemExit('cast.tsv: want scenes 0..99')
    return cast


def check_lengths(name, texts):
    for t in texts:
        if len(t) >= 511:                 # TC_LINE_CAP 512 wchars incl. NUL
            raise SystemExit('%s: a line of %d characters exceeds TC_LINE_CAP' % (name, len(t)))


def generate():
    fills = load_fills()
    en = resolve('en', load('en'), fills)
    ja = resolve('ja', load('ja'), fills)
    nar_en, nar_ja = load_narration('en'), load_narration('ja')
    speakers, cast = load_speakers(), load_cast()
    for name, texts in (('en', en.values()), ('ja', ja.values()), ('nar_en', nar_en), ('nar_ja', nar_ja)):
        check_lengths(name, texts)
    return '\n'.join([
        '/* Generated by tools/make_mjus_story.py from data/mjus_story/ -- do not',
        ' * edit. Project Justice story dialogue, transcribed from the pictures the',
        ' * game draws, keyed (scene ID << 8) | message index, sorted by key. */',
        '#ifndef CFC2_MJUS_STORY_TEXT_H',
        '#define CFC2_MJUS_STORY_TEXT_H',
        '#include <wchar.h>',
        'typedef struct { unsigned short key; const wchar_t *text; } mjus_story_line;',
        '#define MJUS_STORY_EN_COUNT %d' % len(en),
        '#define MJUS_STORY_JA_COUNT %d' % len(ja),
        table('mjus_story_en', en),
        table('mjus_story_ja', ja),
        '/* Story-intro narration screens P00..P14 (see mjus_story.h). */',
        '#define MJUS_NARRATION_PAGES 15',
        'static const wchar_t *const mjus_narration_en[MJUS_NARRATION_PAGES] = {',
        ] + ['    %s,' % c_wide(t) for t in nar_en] + [
        '};',
        'static const wchar_t *const mjus_narration_ja[MJUS_NARRATION_PAGES] = {',
        ] + ['    %s,' % c_wide(t) for t in nar_ja] + [
        '};',
        '/* Speakers: per scene and side, the character in each portrait slot',
        ' * (0xFF none), and the name of each character ID. */',
        '#define MJUS_SPEAKER_IDS %d' % len(SPEAKER),
        'static const unsigned char mjus_cast[100][2][8] = {',
        ] + ['    { %s },' % ', '.join(
            '{ %s }' % ', '.join('0x%02x' % i for i in (side + [0xFF] * (8 - len(side))))
            for side in cast[sc]) for sc in range(100)] + [
        '};',
        'static const wchar_t *const mjus_speaker_en[MJUS_SPEAKER_IDS] = {',
        ] + ['    %s,' % c_wide(t) for t in speakers['en']] + [
        '};',
        'static const wchar_t *const mjus_speaker_ja[MJUS_SPEAKER_IDS] = {',
        ] + ['    %s,' % c_wide(t) for t in speakers['ja']] + [
        '};',
        '#endif',
        ''])


if __name__ == '__main__':
    text = generate()
    if '--check' in sys.argv:
        # newline='' keeps CRLF, then normalise: git may check it out either way.
        cur = open(OUT, encoding='utf-8', newline='').read().replace('\r\n', '\n') if os.path.exists(OUT) else ''
        if cur != text:
            raise SystemExit('src/mjus_story_text.h is stale: run tools/make_mjus_story.py')
        print('mjus_story_text.h is current')
    else:
        with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        print('wrote', OUT)
