"""Read-only CVS1 name and in-match evidence, paired with window screenshots.

Two kinds of row, both written to <out>/session.jsonl:

- selection: whenever a side's selected native ID changes and holds for two
  consecutive polls, the game window is captured to a PNG beside the row.
  The ID comes from the traced selection byte (P+0x78+4*side+slot); the NAME
  comes later from reading the screenshot. Nothing here maps an ID to a name.
- snapshot: on each F9 press, the whole published primary block (0x32AB0
  bytes) is saved to a .bin with a screenshot, labelled with the next entry of
  the plan, so in-match fields can be located offline by labelled diffs.

Same gates as cvs1_selection_capture: reviewed code/data signatures, game 0,
ROM 0/1, callbacks, root/size publication and the phase pointer. No hooks, no
game-memory writes, no DLL changes. Screenshots are of the game WINDOW on the
desktop; exclusive fullscreen may capture black, which each row reports.
"""
import argparse
import ctypes as C
import ctypes.wintypes as W
import hashlib
import json
from pathlib import Path
import time

import collection_metadata_capture as metadata
import cvs1_selection_capture as selection

PRIMARY_SIZE = 0x32AB0
SNAPSHOT_KEY = 0x78          # VK_F9: free in CFC2, the mod (F5-F8, `) and Steam (F12)
DEFAULT_PLAN = [
    'P1 full health, both standing apart, round clock running',
    'P1 damaged (P1 health bar visibly lower)',
    'P1 crouching',
    'P1 in the air mid-jump',
    'P1 knocked down',
    'P1 power gauge full or one level stocked',
    'P1 power gauge empty after spending',
    'P1 in the left corner',
    'P1 in the right corner',
    'game paused on the pause menu',
]


def settled_selections(observation):
    """(side, slot, selected) for each side whose selection byte is a real ID.

    0xFF (read as -1) and an out-of-range slot mean no selection; they are
    never captured as an ID."""
    out = []
    for actor in observation.get('actors') or []:
        sel, slot = actor.get('selected'), actor.get('slot')
        if sel is None or sel < 0 or not 0 <= slot < 4:
            continue
        out.append((actor['side'], slot, sel))
    return out


class Session:
    """Polling state machine. Every side effect is injected for testing."""

    def __init__(self, read, lifecycle, out_dir, grab, key_down, say, plan, clock=time.time):
        self.read, self.lifecycle, self.out = read, lifecycle, Path(out_dir)
        self.grab, self.key_down, self.say, self.clock = grab, key_down, say, clock
        self.plan = list(plan)
        self.pending = {}            # side -> (slot, selected) seen once
        self.captured = {}           # side -> (slot, selected) already pictured
        self.key_was_down = False
        self.warned = False
        self.shots = 0
        self.snapshots = 0
        self.rows = []
        self.log = (self.out / 'session.jsonl').open('a', encoding='utf-8')

    def close(self):
        self.log.close()

    def _write(self, row):
        self.rows.append(row)
        self.log.write(json.dumps(row) + '\n')
        self.log.flush()

    def _image(self, stem):
        image = self.grab()
        if image is None:
            return dict(image=None, image_status='no game window found')
        path = self.out / f'{stem}.png'
        image.save(path)
        black = max(hi for _, hi in image.convert('RGB').getextrema()) < 8
        return dict(image=path.name, image_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    image_size=list(image.size),
                    image_status='black frame: use windowed or borderless' if black else 'ok')

    def poll(self):
        obs = selection.observe(self.read, self.lifecycle)
        now = metadata.utc()
        seen = settled_selections(obs) if 'actors' in obs else []
        current = {side: (slot, sel) for side, slot, sel in seen}
        for side in (0, 1):
            key = current.get(side)
            if key is None:
                self.pending.pop(side, None)
                continue
            if self.pending.get(side) != key:
                self.pending[side] = key          # first sighting: wait one poll
                continue
            if self.captured.get(side) == key:
                continue
            self.captured[side] = key
            self.shots += 1
            p = obs['records'].get('phase', {}).get('hex', '')
            row = dict(kind='selection', utc=now, side=side, slot=key[0], native_id=key[1],
                       mode=obs.get('mode'), phase_prefix=p[:10],
                       gate=obs.get('reason'), shot=self.shots)
            row.update(self._image(f'select-{self.shots:03d}-side{side}-id{key[1]}'))
            self._write(row)
            # The player is in the game, not the console: say a bad picture once.
            if row['image_status'] != 'ok' and not self.warned:
                self.warned = True
                self.say('Warning: the screenshot is unusable. ' +
                         ('Switch the display to windowed.' if row['image'] else
                          'The game window was not found.'))
            print(f"Side {side}: native ID {key[1]} pictured ({row['image_status']})", flush=True)

        down = bool(self.key_down(SNAPSHOT_KEY))
        if down and not self.key_was_down:
            self.snapshot(obs)
        self.key_was_down = down
        return obs

    def snapshot(self, obs):
        index = self.snapshots
        label = self.plan[index] if index < len(self.plan) else f'unplanned snapshot {index + 1}'
        root = int(obs['metadata']['decoded'].get('primary', '0x0'), 16) if obs.get('metadata') else 0
        row = dict(kind='snapshot', utc=metadata.utc(), index=index, label=label, root=hex(root))
        if 'actors' not in obs or not root:
            row['status'] = 'refused: CVS1 identity gate not satisfied (%s)' % obs.get('reason')
            self._write(row)
            self.say('Not captured. CVS1 is not active.')
            return
        try:
            slot_before = metadata.exact(self.read, selection.ROOT_SLOT, 8)
            raw = metadata.exact(self.read, root, PRIMARY_SIZE)
            slot_after = metadata.exact(self.read, selection.ROOT_SLOT, 8)
        except OSError as exc:
            row.update(status='unreadable', error=str(exc))
            self._write(row)
            self.say('Not captured. The state could not be read.')
            return
        if slot_before != slot_after or int.from_bytes(slot_before, 'little') != root:
            row['status'] = 'refused: root changed during the read'
            self._write(row)
            self.say('Not captured. Try again.')
            return
        self.snapshots += 1
        name = f'state-{self.snapshots:02d}.bin'
        (self.out / name).write_bytes(raw)
        row.update(status='read', block=name, size=PRIMARY_SIZE,
                   sha256=hashlib.sha256(raw).hexdigest(), mode=obs.get('mode'),
                   phase_prefix=obs['records'].get('phase', {}).get('hex', '')[:10])
        row.update(self._image(f'state-{self.snapshots:02d}'))
        self._write(row)
        nxt = self.plan[self.snapshots] if self.snapshots < len(self.plan) else None
        self.say(f'Captured {label}.' + (f' Next: {nxt}.' if nxt else ' Plan complete.'))
        print(f'Snapshot {self.snapshots}: {label}', flush=True)


# ---- Windows-only side effects ---------------------------------------------

def window_grabber(pid):
    from PIL import ImageGrab
    user = C.WinDLL('user32', use_last_error=True)
    try:
        C.WinDLL('shcore').SetProcessDpiAwareness(2)
    except OSError:
        pass
    proc = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)

    def find():
        best = []

        def visit(hwnd, _):
            owner = W.DWORD()
            user.GetWindowThreadProcessId(hwnd, C.byref(owner))
            if owner.value == pid and user.IsWindowVisible(hwnd):
                rect = W.RECT()
                user.GetClientRect(hwnd, C.byref(rect))
                area = rect.right * rect.bottom
                if area > 0:
                    best.append((area, hwnd, rect.right, rect.bottom))
            return True
        user.EnumWindows(proc(visit), 0)
        return max(best) if best else None

    def grab():
        found = find()
        if not found:
            return None
        _, hwnd, width, height = found
        origin = W.POINT(0, 0)
        user.ClientToScreen(hwnd, C.byref(origin))
        return ImageGrab.grab(bbox=(origin.x, origin.y, origin.x + width, origin.y + height),
                              all_screens=True).convert('RGB')
    return grab


def key_poller():
    user = C.WinDLL('user32')
    return lambda vk: user.GetAsyncKeyState(vk) & 0x8000


def speaker(game_dir):
    """NVDA through the game's own controller DLL; a beep when unavailable."""
    try:
        nvda = C.WinDLL(str(Path(game_dir) / 'nvdaControllerClient.dll'))
        nvda.nvdaController_speakText.argtypes = [C.c_wchar_p]
        if nvda.nvdaController_testIfRunning() == 0:
            return lambda text: nvda.nvdaController_speakText(text)
    except OSError:
        pass
    import winsound
    return lambda text: winsound.Beep(1200 if text.startswith('Captured') else 400, 120)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int)
    parser.add_argument('--out', type=Path, required=True, help='A new, empty directory')
    parser.add_argument('--label', required=True)
    parser.add_argument('--language', required=True, help='Shell language and ROM, or unknown')
    parser.add_argument('--minutes', type=float, default=10)
    parser.add_argument('--interval', type=float, default=0.1)
    parser.add_argument('--game-dir', default=r'C:\Program Files (x86)\Steam\steamapps\common\CAPCOM FIGHTING COLLECTION 2')
    args = parser.parse_args()
    if args.pid <= 0 or not 0.5 <= args.minutes <= 30 or not 0.05 <= args.interval <= 1:
        parser.error('Require PID>0, minutes 0.5..30, interval 0.05..1')
    if args.out.exists() and any(args.out.iterdir()):
        parser.error('Output directory is not empty; choose a fresh path')
    args.out.mkdir(parents=True, exist_ok=True)
    manifests = [p.read_bytes() for p in (metadata.MANIFEST, selection.ACQUISITION)]
    lifecycle, acquisition = map(json.loads, manifests)
    reader = metadata.ProcessReader(args.pid)
    say = speaker(args.game_dir)
    session = None
    try:
        checked = selection.verify(reader.read, lifecycle, acquisition)
        session = Session(reader.read, lifecycle, args.out, window_grabber(args.pid),
                          key_poller(), say, DEFAULT_PLAN)
        session._write(dict(kind='provenance', tool='cvs1-name-capture', schema=1,
                            label=args.label, language=args.language, created_utc=metadata.utc(),
                            checked_code_spans=checked, plan=DEFAULT_PLAN,
                            manifest_sha256=[hashlib.sha256(m).hexdigest() for m in manifests],
                            tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()))
        print(f'Capturing for up to {args.minutes} minutes. Press F9 in the game for each '
              f'snapshot. Ctrl+C stops.', flush=True)
        say(f'Capture started. First snapshot: {DEFAULT_PLAN[0]}.')
        end = time.time() + args.minutes * 60
        while time.time() < end:
            session.poll()
            time.sleep(args.interval)
        say('Capture finished.')
    except KeyboardInterrupt:
        say('Capture stopped.')
    finally:
        if session:
            session.close()
            print(f'{session.shots} selection picture(s), {session.snapshots} snapshot(s) in {args.out}')
        reader.close()


if __name__ == '__main__':
    main()
