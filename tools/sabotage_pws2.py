"""Sabotage the Power Stone 2 adapter one mechanism at a time; each must fail its test.

Usage: python tools/sabotage_pws2.py   (from the repository root; restores every file)
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SEL, FRM = 'run_pws2_select_test.ps1', 'run_pws2_frame_test.ps1'
AFT, MAT = 'run_audio_fighter_test.ps1', 'run_match_audio_test.ps1'
F = 'src/pws2_frame.c'
CASES = [
    ('src/pws_select_decode.c', '} else if (id >= 16 || id == 12 || id == 13) {', '} else if (id >= 16) {', SEL, 'boss IDs spoken'),
    ('src/pws_select_decode.c', "s->phase[5] != 1 || s->phase[6] != 1 || s->phase[7] != 2 ||",
     "s->phase[6] != 1 || s->phase[7] != 2 ||", SEL, 'select step gate removed'),
    ('src/pws_select_decode.c', 's->state[p] != 1 || s->mask > 15', 's->mask > 15', SEL, 'confirmed side still spoken'),
    ('src/pws2_select.c', 'if (!channel.session.owned || root != owner_root || rom != owner_rom) {',
     'if (!channel.session.owned) {', SEL, 'new root not re-announced'),
    (F, '#define OFF_HP      0x1DCu', '#define OFF_HP      0x1E0u', FRM, 'health misread'),
    (F, '#define MAX_HP      1000', '#define MAX_HP      1200', FRM, 'wrong maximum'),
    (F, '        if (v == 0 && hp > 0.0f) v = 1;          /* alive is never "empty" */\n', '', FRM, 'alive rounds to empty'),
    (F, 'o->health.valid = hp >= 0.0f && hp <= (float)MAX_HP;   /* false for NaN too */',
     'o->health.valid = hp <= (float)MAX_HP;', FRM, 'negative health accepted'),
    (F, '#define OFF_STONES  0x4055u', '#define OFF_STONES  0x4056u', FRM, "stone's index read as the count"),
    (F, 'o->resource.valid = stones <= MAX_STONES;', 'o->resource.valid = true;', FRM, 'fourth stone accepted'),
    (F, '#define OFF_CPU     0x4028u', '#define OFF_CPU     0x4025u', FRM, 'CPU flag misread'),
    (F, "    return s[a + OFF_SLOT] == p && !s[a + OFF_CPU];", "    return !s[a + OFF_CPU];", FRM, 'slot byte unchecked'),
    (F, "    if (!in_fight_step(s) || (mode != MODE_READY && mode != MODE_FIGHT)) return;",
     "    if (!in_fight_step(s)) return;", FRM, 'continue screen counted as a match'),
    (F, "    if (!in_fight_step(s) || (mode != MODE_READY && mode != MODE_FIGHT)) return;",
     "    if (!in_fight_step(s) || mode != MODE_FIGHT) return;", FRM, 'ready mode dropped'),
    (F, "    return s[OFF_TOP] == 1 && s[OFF_NESTED] == 2 && s[OFF_GAME] == 1 && s[OFF_STEP] == STEP_FIGHT &&",
     "    return s[OFF_TOP] == 1 && s[OFF_GAME] == 1 && s[OFF_STEP] == STEP_FIGHT &&", FRM, 'nested phase unchecked'),
    (F, "           !s[OFF_GATE1] && !s[OFF_GATE2];", "           !s[OFF_GATE2];", FRM, 'dispatch gate unchecked'),
    (F, "    return in_fight_step(s) && s[OFF_MODE] == MODE_FIGHT && !s[OFF_SPLIT] && s[OFF_VIEWIX] < 4;",
     "    return in_fight_step(s) && !s[OFF_SPLIT] && s[OFF_VIEWIX] < 4;", FRM, 'pickups outside the fought round'),
    (F, "    return in_fight_step(s) && s[OFF_MODE] == MODE_FIGHT && !s[OFF_SPLIT] && s[OFF_VIEWIX] < 4;",
     "    return in_fight_step(s) && s[OFF_MODE] == MODE_FIGHT && s[OFF_VIEWIX] < 4;", FRM, 'two-pass draw projected'),
    (F, '#define OFF_CLOCK   0x52EE0u', '#define OFF_CLOCK   0x52EE2u', FRM, 'clock reads its sub-counter'),
    (F, '    if (mode == MODE_FIGHT) {\n        int clock', '    if (1) {\n        int clock', FRM, 'clock outside the fight'),
    (F, '#define OFF_FRAMES  0x52D34u', '#define OFF_FRAMES  0x52ED8u', FRM, 'counter that runs through the pause'),
    (F, '#define POOL_STRIDE 0x4C8u', '#define POOL_STRIDE 0x4B8u', FRM, 'wrong pool stride'),
    (F, '#define LIST_ARENA  9', '#define LIST_ARENA  5', FRM, 'wrong object list'),
    (F, '#define STATE_LYING 2', '#define STATE_LYING 5', FRM, 'held objects ping'),
    (F, '        for (unsigned p = 0; p < FIGHTERS; p++) held |= carried[p] == addr;\n', '', FRM, 'carried object pings'),
    (F, '#define OBJ_POS     0x3Cu', '#define OBJ_POS     0x40u', FRM, 'pickup position misaligned'),
    (F, '#define OFF_POS     0x3Cu', '#define OFF_POS     0x40u', FRM, 'fighter position misaligned'),
    (F, '        if (!human(s, p) || !s[a + OFF_IN]) continue;', '        if (!s[a + OFF_IN]) continue;', FRM, 'CPU fighter used for distance'),
    (F, '    if (cat == 4) { *family = AUDIO_PING_STONE; return true; }', '    if (cat == 5) { *family = AUDIO_PING_STONE; return true; }', FRM, 'wrong stone category'),
    (F, 'static const unsigned char g_food[] = { 0x10,0x11,0x12,0x53,0x54,0x55,0x56,0x57,0x58,0x68 };',
     'static const unsigned char g_food[] = { 0x10,0x11,0x12,0x4A,0x53,0x54,0x55,0x56,0x57,0x58,0x68 };', FRM, 'non-healing code called food'),
    (F, '    if (code == CODE_PROP || code == CODE_PROP2) {', '    if (code == CODE_PROP) {', FRM, 'second prop code lost'),
    (F, "    } else if (!(cat == 6 || cat == 9 || (cat == 0 && code < 0xC0))) {", "    } else if (!(cat == 6 || cat == 9 || cat == 0)) {", FRM, 'stage fixtures ping'),
    (F, '    *variant = (int)(code % 25u);', '    *variant = (int)code;', FRM, 'item type past two octaves'),
    (F, "    return v[3] == 0.0f && v[7] == 0.0f && v[11] == 0.0f && v[15] == 1.0f;", "    return true;", FRM, 'affine check removed'),
    (F, '#define OFF_VIEW    0x51D90u', '#define OFF_VIEW    0x51DD0u', FRM, 'wrong view record'),
    ('src/render_project.h', '    if (c3 <= 1e-3f || fabsf(c0) > c3 || fabsf(c1) > c3) return 0;', '    if (c3 <= 1e-3f) return 0;', FRM, 'off-screen pickups ping'),
    ('src/render_project.h', '    if (c3 <= 1e-3f || fabsf(c0) > c3 || fabsf(c1) > c3) return 0;', '    if (c3 <= 1e-3f || fabsf(c0) > c3) return 0;', FRM, 'above or below the screen pings'),
    (F, '        case OFF_SCREEN: continue;', '        case OFF_SCREEN: break;', FRM, 'off-screen fighter tones play'),
    (F, '        default:         break;            /* no camera: centred */', '        default:         continue;', FRM, 'no camera silences fighters'),
    (F, '    m->fighter_gauges = true;\n', '', FRM, 'CPU gauges not routed'),
    (F, '        case ON_SCREEN:  o->gauge_pan_valid = true; break;', '        case ON_SCREEN:  break;', FRM, 'fighter tones not placed'),
    ('src/audio.c', '            v->kind = st->kind;', '            v->kind = 0;', AFT, 'fighters share one articulation'),
    ('src/audio.c', '        g_gauge[g][player].pan = pan;\n', '', AFT, 'fighter pan ignored'),
    ('src/audio.c', '                else if (v->kind == 2) dt *= 1.0f + 0.07f * w;', '                else if (v->kind == 2) dt *= 1.0f;', AFT, 'warble lost'),
    # Not a case: a controller guard against HUD owners 2 and 3 was caught by
    # nothing, because audio_gauge_set_owned refuses them (case 13 checks the
    # behaviour). It was removed: one guard, one behaviour.
    ('src/audio.c', '    if ((int)g < 0 || g >= AUDIO_GAUGE_COUNT || player > 1 || maxv <= 0)\n        return;\n    lock_ensure();\n    EnterCriticalSection(&g_lock);\n    if (!g_scene_owned && owner_current(epoch))\n        gauge_set_locked(g, player, value, maxv);',
     '    if ((int)g < 0 || g >= AUDIO_GAUGE_COUNT || player >= AUDIO_PARTICIPANTS || maxv <= 0)\n        return;\n    lock_ensure();\n    EnterCriticalSection(&g_lock);\n    if (!g_scene_owned && owner_current(epoch))\n        gauge_set_locked(g, player, value, maxv);', MAT, 'HUD games sound owners 2 and 3'),
    ('src/match_audio.c', '        const match_audio_owner *fighter = s->fighter_gauges ? a : NULL;', '        const match_audio_owner *fighter = NULL;', MAT, 'fighter gauges sent to the HUD setter'),
    ('src/match_audio.c', 'fighter->gauge_pan_valid ? fighter->gauge_pan : 0.0f', 'fighter->gauge_pan', MAT, 'unplaced fighter keeps a stale pan'),
    (F, '            if (stones > w->stones[p] && stones <= MAX_STONES) {', '            if (0) {', FRM, 'stones gained not announced'),
    (F, '            if (c && c != w->carried[p] && (k = pws2_carried_code(s, root, c)) >= 0) {', '            if (c && (k = pws2_carried_code(s, root, c)) >= 0) {', FRM, 'held object announced every frame'),
    (F, '        if (before[i] && now[i] != before[i]) return (int)(before[i] & 0xFF);', '        if (before[i] && !before[i]) return (int)(before[i] & 0xFF);', FRM, 'touched pickup never named'),
    (F, '    if (off % POOL_STRIDE || off / POOL_STRIDE >= POOL_COUNT) return -1;', '    if (off / POOL_STRIDE >= POOL_COUNT) return -1;', FRM, 'misaligned carried pointer read'),
    (F, '    if (r == OFF_SCREEN && offscreen_ok && render_project_side(cam, pt, pan)) return ON_SCREEN;', '    (void)offscreen_ok;', FRM, 'off-screen switch ignored'),
    ('src/pickup_speech.c', '(mode == 1 && !human)', '0', SEL, 'players-only setting speaks CPUs'),
    ('src/pickup_speech.c', ' || !mode ||', ' ||', SEL, 'announcements spoken when off'),
    ('src/pws2_select.c', '    default:               return code == 0xC6 ? PICKUP_TREASURE : PICKUP_ITEM;', '    default:               return PICKUP_ITEM;', SEL, 'treasure called an item'),
    ('src/pws2_select.c', 'e.epoch != match_audio_current()', '0', SEL, "another owner's pickups spoken"),
    ('src/audio.c', '    v = o == AUDIO_OPT_PICKUP_SPEECH ? (audio_option_get(o) + n - 1) % n : (audio_option_get(o) + 1) % n;', '    v = (audio_option_get(o) + 1) % n;', FRM, 'announcement setting cycles the wrong way'),
    ('src/pws1_frame.c', '    if (r == 0 && offscreen_ok) return render_project_side(cam, pt, pan);', '', 'run_pws1_frame_test.ps1', 'PWS1 off-screen switch ignored'),
    (F, '           s[OFF_SUBPHASE] == 1 && s[OFF_SUBSTEP] == 0 && s[OFF_MODE] == MODE_READY &&', '           s[OFF_SUBPHASE] == 1 && s[OFF_SUBSTEP] == 0 &&', FRM, 'stage spots ping before landing'),
    (F, '    if (o[OBJ_LIST] != 5 || u64(o, 8) != SPOT_CB || o[2] >= SPOTS) return -1;', '    if (o[OBJ_LIST] != 5 || o[2] >= SPOTS) return -1;', FRM, 'any list-5 object taken for a stage'),
    (F, '        ez = d[0] * v[2] + d[2] * v[10];', '        ez = -(d[0] * v[2] + d[2] * v[10]);', FRM, 'stage directions front to back'),
    (F, '        q->variant = (int)(k == 0 ? 0 : k == 1 ? 4 : k == 2 ? 7 : k == 3 ? 11 : 14);', '        q->variant = 0;', FRM, 'stages share one pitch'),
    (F, '        q->distance = sqrtf(dx * dx + dz * dz) - SPOT_RADIUS;', '        q->distance = sqrtf(dx * dx + dz * dz);', FRM, 'stage distance ignores the reach'),
    (F, '    return s[OFF_STAGE_PICK] < SPOTS ? s[OFF_STAGE_PICK] : -1;', '    return 0;', FRM, 'chosen stage misread'),
    ('src/pws2_select.c', '                swprintf(say, 512, L"%ls%u", mt(MT_STAGE_PREFIX), e.code + 1);', '                swprintf(say, 512, L"%ls%u", mt(MT_STAGE_PREFIX), e.code);', SEL, 'stages numbered from 0'),
    # Not a case: the handler's stone skip (stones come from the count). Only
    # the hooked game shows double announcements; no replay calls the hook.
    # Not a case: the parent-matrix skip. No lying object in the capture had a
    # parent, so no replay can tell it from its absence.
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
    for runner in sorted({c[3] for c in CASES}):
        if run(runner) != 0:
            print(f'restored tree fails {runner}')
            bad += 1
    print('all sabotages caught' if not bad else f'{bad} problem(s)')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
