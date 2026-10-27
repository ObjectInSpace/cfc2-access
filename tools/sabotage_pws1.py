"""Sabotage the Power Stone decoders one mechanism at a time; each must fail its test.

Usage: python tools/sabotage_pws1.py   (from the repository root; restores every file)
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SEL, FRM, DEC = 'run_pws1_select_test.ps1', 'run_pws1_frame_test.ps1', 'run_pws_select_test.ps1'
CUE = 'run_pickup_cues_test.ps1'
CASES = [
    ('src/pws_select_decode.c', 'static const unsigned char pws1_ids[11] = { 8,0,6,2,4,3,5,7,1,9,10 };',
     'static const unsigned char pws1_ids[11] = { 0,8,6,2,4,3,5,7,1,9,10 };', SEL, 'cursor map entries swapped'),
    ('src/pws_select_decode.c', 'if (s->slot[p] != p || s->shuffle[p]) return -2;', 'if (s->slot[p] != p) return -2;', SEL, 'shuffle spoken'),
    ('src/pws_select_decode.c', 's->state[p] != 2 || s->mask > 3', 's->mask > 3', SEL, 'side-state gate removed'),
    ('src/pws1_select.c', 'static const BYTE g_stage_ids[10] = { 3, 0, 8, 7, 2, 6, 1, 9, 5, 4 };',
     'static const BYTE g_stage_ids[10] = { 3, 0, 8, 7, 2, 1, 6, 9, 5, 4 };', SEL, 'stage table entries swapped'),
    ('src/pws1_select.c', '    if (phase != 3 || sub != 1 || step != 1) return -1;', '    if (phase != 3 || sub != 1) return -1;', SEL, 'stage spoken after it is chosen'),
    ('src/pws1_select.c', '    if (p != text && *p == \'.\') { p++; while (*p == \' \') p++; } else p = text;', '    p = text;', SEL, 'track number spoken'),
    ('src/pws1_select.c', 'static const int g_stage_label[10] = { 3816, 3817, 3818, 3819, 3820, 3823, 3824, 3825, 3827, 3828 };',
     'static const int g_stage_label[10] = { 3816, 3817, 3818, 3819, 3820, 3821, 3824, 3825, 3827, 3828 };', SEL, 'stage label off by one'),
    ('src/pws1_select.c', 'if (!channel.session.owned || root != owner_root || rom != owner_rom) {',
     'if (!channel.session.owned) {', SEL, 'new root not re-announced'),
    ('src/pws1_frame.c', '#define OFF_HP      0x7790u', '#define OFF_HP      0x7792u', FRM, 'recovery target read as health'),
    ('src/pws1_frame.c', '#define MAX_HP      240', '#define MAX_HP      255', FRM, 'wrong maximum'),
    ('src/pws1_frame.c', 'o->health.value = hp < 0 ? 0 : hp;', 'o->health.value = hp;', FRM, 'KO -1 unclamped'),
    ('src/pws1_frame.c', 'o->health.valid = live && hp >= -1 && hp <= MAX_HP;', 'o->health.valid = live && hp <= MAX_HP;', FRM, 'any negative accepted'),
    ('src/pws1_frame.c', 'live = sub >= 3;', 'live = true;', FRM, 'intro refill fed'),
    ('src/pws1_frame.c', "           s[OFF_STEP] == 0 && s[OFF_SPLIT] == 0;", "           s[OFF_SPLIT] == 0;", FRM, 'pickups after the round is decided'),
    ('src/pws1_frame.c', "           s[OFF_STEP] == 0 && s[OFF_SPLIT] == 0;", "           s[OFF_STEP] == 0;", FRM, 'two-view layout projected'),
    ('src/pws1_frame.c', "if (s[OFF_RUN] != 1 || s[OFF_PHASE] != 4 || sub < 2 || sub > 5) return;", "if (s[OFF_RUN] != 1 || sub < 2 || sub > 5) return;", FRM, 'attract demo admitted'),
    ('src/pws1_frame.c', "if (s[OFF_RUN] != 1 || s[OFF_PHASE] != 4 || sub < 2 || sub > 5) return;", "if (s[OFF_RUN] != 1 || s[OFF_PHASE] != 4) return;", FRM, 'sub-state gate removed'),
    ('src/pws1_frame.c', '#define OFF_STONES  0x77ECu', '#define OFF_STONES  0x77EDu', FRM, 'stone bits read as the count'),
    ('src/pws1_frame.c', 'o->resource.valid = live && stones <= MAX_STONES;', 'o->resource.valid = live;', FRM, 'fourth stone accepted'),
    ('src/pws1_frame.c', 'if (s[a + OFF_SLOT] != p) continue;', ';', FRM, 'side byte unchecked'),
    ('src/pws1_frame.c', '#define OFF_VIEW    0x27974u', '#define OFF_VIEW    0x27B84u', FRM, 'second camera record'),
    ('src/pws1_frame.c', '#define OFF_X       0x30u', '#define OFF_X       0x34u', FRM, 'fighter position misaligned'),
    ('src/pws1_frame.c', '#define OBJ_POS     0x30u', '#define OBJ_POS     0x34u', FRM, 'pickup position misaligned'),
    ('src/pws1_frame.c', 'if (cam[3] != 0.0f || cam[7] != 0.0f || cam[11] != 0.0f || cam[15] != 1.0f) return false;', ';', FRM, 'affine check removed'),
    ('src/pws1_frame.c', '#define POOL_STRIDE 0x3E0u', '#define POOL_STRIDE 0x3D0u', FRM, 'wrong pool stride'),
    ('src/pws1_frame.c', '#define TYPE_STONE  5', '#define TYPE_STONE  3', FRM, 'wrong stone type'),
    ('src/pws1_frame.c', '            if (!o[OBJ_SHOWN]) continue;\n            q->family = AUDIO_PING_STONE;', '            q->family = AUDIO_PING_STONE;', FRM, 'held stones ping'),
    ('src/pws1_frame.c', '            if (o[OBJ_CODE] >= ITEM_CODES) continue;\n', '', FRM, 'item code unchecked'),
    ('src/pws1_frame.c', '#define FOOD_FIRST  15', '#define FOOD_FIRST  16', FRM, 'food range shifted'),
    ('src/pws1_frame.c', '        if (addr == carried[0] || addr == carried[1]) continue;\n', '', FRM, 'carried item pings'),
    ('src/pws1_frame.c', "        if (!(humans >> p & 1u) || s[a + OFF_SLOT] != p) continue;", "        if (s[a + OFF_SLOT] != p) continue;", FRM, 'CPU fighter used for distance'),
    ('src/render_project.h', '    if (c3 <= 1e-3f || fabsf(c0) > c3 || fabsf(c1) > c3) return 0;', '    if (c3 <= 1e-3f) return 0;', FRM, 'off-screen pickups ping'),
    ('src/pws1_frame.c', '    return r == 1;', '    return r != 0;', FRM, 'unknown camera treated as on screen'),
    ('src/pickup_cues.c', '    float s = 0.20f + d * 0.0005f;              /* 0.2 s .. 1.2 s */', '    float s = 0.20f;', CUE, 'distance ignored for rate'),
    ('src/pickup_cues.c', '            c->countdown[i] = (int)(i % 8u) * 3;', '            c->countdown[i] = 0;', CUE, 'arrivals not staggered'),
    ('src/pickup_cues.c', '        if (!p->placed) continue;', '', CUE, 'unplaced pickup pings'),
    ('src/pickup_cues.c', '    if (epoch != c->epoch) {', '    if (0) {', CUE, 'new owner keeps countdowns'),
    # Not a case: deleting the owner check in audio_ping_owned is caught by
    # nothing, because the mixer drops a ping whose stamp is not the current
    # owner's before it sounds (the next case). Two guards, one behaviour.
    ('src/audio.c', "            if (g_scene_owned || !legacy_stamp_ok(p->stamp)) { InterlockedExchange(&p->armed, 0); continue; }", "            if (g_scene_owned) { InterlockedExchange(&p->armed, 0); continue; }", CUE, 'replaced owner ping keeps sounding'),
    ('src/audio.c', '    p->freq   = g_ping_family[family].freq * powf(2.0f, (float)variant / 12.0f);', '    p->freq   = g_ping_family[family].freq;', CUE, 'types share one pitch'),
    ('src/audio.c', '            l += ps * p->gl;\n            r += ps * p->gr;', '            l += ps;\n            r += ps;', CUE, 'pings not panned'),
    ('src/audio.c', '    p->vol    = gain * g_ping_family[family].level * cue_gain(AUDIO_CUE_BEACON);', '    p->vol    = gain * g_ping_family[family].level;', CUE, 'beacon volume ignored'),
    ('src/pws1_frame.c', '            if (stones > w->stones[p] && stones <= MAX_STONES) { who[n] = p; kind[n++] = PICKUP_STONE; }\n', '', FRM, 'stones gained not announced'),
    ('src/pws1_frame.c', '            if (hp - w->hp[p] >= 20 && hp <= MAX_HP && !w->food_frames[p])', '            if (hp - w->hp[p] >= 1 && hp <= MAX_HP && !w->food_frames[p])', FRM, 'slow recovery read as eating'),
    ('src/pws1_frame.c', ' && hp <= MAX_HP && !w->food_frames[p])', ' && hp <= MAX_HP)', FRM, 'lifted food announced twice'),
    ('src/pws1_frame.c', '                unsigned k = o[OBJ_TYPE] == TYPE_PROP ? PICKUP_THROWABLE :', '                unsigned k = o[OBJ_TYPE] == TYPE_PROP ? PICKUP_ITEM :', FRM, 'thing to throw called an item'),
    ('src/pws1_frame.c', '            if (c && c != w->carried[p] && c >= root', '            if (c && c >= root', FRM, 'held thing announced every frame'),
    ('src/pws1_select.c', '        if (!admitted || e.epoch != match_audio_current()) continue;', '        if (!admitted) continue;', SEL, "another owner's pickups spoken"),
    ('src/pws1_frame.c', '#define OFF_CLOCK   0x27FCDu', '#define OFF_CLOCK   0x27FCEu', FRM, 'clock reads its sub-counter'),
    ('src/pws1_frame.c', 'if (!s[OFF_FREEZE] && s[OFF_TIMER] != 4) {', 'if (!s[OFF_FREEZE]) {', FRM, 'no-timer clock ticks'),
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
        if '\r\n' in text:                  # patterns are written with \n
            old, new = old.replace('\n', '\r\n'), new.replace('\n', '\r\n')
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
    for runner in sorted({c[3] for c in CASES} | {DEC}):
        if run(runner) != 0:
            print(f'restored tree fails {runner}')
            bad += 1
    print('all sabotages caught' if not bad else f'{bad} problem(s)')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
