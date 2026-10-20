"""Read-only state capture for any traced CFC2 game path, with screenshots.

One tool for every path in docs/collection-lifecycle-manifest.json, chosen
with --game. While the game's published primary block is identified, it:

- traces the block: a full keyframe, then only changed bytes, ~20 Hz, into
  trace.bin.gz (a new root starts a new keyframe and segment);
- logs direction presses from the keyboard arrows and XInput pads;
- with --screenshots only, pictures the game window once the cursor stops;
- on F9, saves the whole block plus a screenshot under the next plan label,
  and says through NVDA what was captured and what comes next;
- where a game has a TRACED cursor decoder (CVS1 only, today), pictures each
  newly settled selection automatically.

Rows go to session.jsonl. tools/cursor_candidates.py ranks trace bytes that
move with the presses; its output is a lead to confirm in code, never a
decoder. No hooks, no game-memory writes, no DLL changes.
"""
import argparse
import ctypes as C
import ctypes.wintypes as W
import gzip
import hashlib
import json
from pathlib import Path
import struct
import time

import collection_metadata_capture as metadata

SNAPSHOT_KEY = 0x78           # VK_F9: free in CFC2, the mod (F5-F8, `) and Steam (F12)
DIRECTION_KEYS = {0x25: 'left', 0x26: 'up', 0x27: 'right', 0x28: 'down'}
SETTLE_SECONDS = 0.35         # picture the cursor this long after the last press
TRACE_MAGIC = b'CFC2TRC1'
CHUNK = 256

IN_MATCH_PLAN = [
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


# ---- the mod's own hooks -----------------------------------------------------

# Entry points CFC2 Access itself detours with MinHook (a 5-byte jmp rel32),
# with the reviewed original bytes. Signature checks see through exactly these
# patches and nothing else: any other difference still refuses.
OWN_HOOKS = {
    0x1406AA7A0: bytes.fromhex('48895c2408'),   # cvs2_frame.c
    0x1405A65F0: bytes.fromhex('4883ec28e8'),   # cvs1_frame.c
    0x1407CB8B0: bytes.fromhex('4883ec28e8'),   # cfj_frame.c
    0x14086D160: bytes.fromhex('48895c2408'),   # z3u_frame.c
    0x1408797D0: bytes.fromhex('488bc44889'),   # z3u_frame.c (attract text)
    0x140DCC3F0: bytes.fromhex('48895c2408'),   # z3u_frame.c (alternate copy)
    0x140DD92B0: bytes.fromhex('488bc44889'),   # z3u_frame.c (alternate attract text)
    0x140B93500: bytes.fromhex('48895c2408'),   # mjus_frame.c
    0x140B96130: bytes.fromhex('4c8bdc4989'),   # mjus_text.c (immediate text draw)
    0x140CCCB60: bytes.fromhex('40534883ec'),   # stg2_frame.c
}


def unhooked(read):
    """A read that shows the reviewed bytes at the mod's own hook sites, only
    where those sites currently hold a jmp rel32 (MinHook's patch)."""
    def view(address, size):
        raw = bytearray(read(address, size))
        for site, original in OWN_HOOKS.items():
            if site + 5 <= address or site >= address + size:
                continue
            patch = read(site, 5)
            if patch[0] != 0xE9 or patch == original:
                continue
            for i in range(5):
                if address <= site + i < address + size:
                    raw[site + i - address] = original[i]
        return bytes(raw)
    return view


# ---- identity ---------------------------------------------------------------

def game_paths(lifecycle):
    return {p['tag'].lower(): p for p in lifecycle['paths']}


def identify(read, lifecycle, path):
    """(root, size, info) when `path` is the published active game, else
    (None, None, reason). Observational gates, not a lifetime guarantee."""
    obs = metadata.observe(read, lifecycle)
    d = obs['decoded']
    if obs['errors']:
        return None, None, 'unreadable identity'
    rom = next((r for r in lifecycle['roms'] if r['index'] == d.get('rom_index')), None)
    callbacks = {off: row['target'] for off, row in path['callbacks'].items()}
    root = int(d.get('primary') or '0x0', 16)
    size = int(path['published_size'], 16)
    if not (rom and d.get('game') == rom['game_index'] and d.get('rom_name') == rom['name'] and
            rom['initializer'] == path['initializer'] and d.get('callbacks') == callbacks and
            root and d.get('primary_size') == size and
            d['slots'].get(path['root_slot']) == hex(root)):
        return None, None, f"{path['tag']} identity/publication gate not satisfied"
    return root, size, dict(rom_index=rom['index'], rom_name=rom['name'], game=rom['game_index'],
                            alpha_discriminator=d.get('alpha_discriminator'))


# ---- trace file ---------------------------------------------------------------

class TraceWriter:
    """Keyframe on the first sample of each root, then changed bytes only."""

    def __init__(self, path):
        self.f = gzip.open(path, 'wb', compresslevel=1)
        self.f.write(TRACE_MAGIC)
        self.prev, self.root, self.samples, self.keyframes = None, None, 0, 0

    def sample(self, t, root, raw):
        self.samples += 1
        # A killed capture keeps everything up to the last flush (~1 s).
        if self.samples % 20 == 0:
            self.f.flush()
        if self.prev is None or root != self.root or len(raw) != len(self.prev):
            self.f.write(struct.pack('<dQBI', t, root, 1, len(raw)) + raw)
            self.prev, self.root = raw, root
            self.keyframes += 1
            return
        offsets, values = [], bytearray()
        prev, view = memoryview(self.prev), memoryview(raw)
        for start in range(0, len(raw), CHUNK):
            if prev[start:start + CHUNK] != view[start:start + CHUNK]:
                for i in range(start, min(start + CHUNK, len(raw))):
                    if raw[i] != self.prev[i]:
                        offsets.append(i)
                        values.append(raw[i])
        self.f.write(struct.pack('<dQBI', t, root, 0, len(offsets)) +
                     struct.pack(f'<{len(offsets)}I', *offsets) + bytes(values))
        self.prev = raw

    def close(self):
        self.f.close()


def read_trace(path):
    """Yield (t, root, keyframe, payload): payload is the whole block for a
    keyframe, else a list of (offset, value). A capture that was killed ends
    without a gzip trailer; every complete record before that is still
    yielded, and the partial tail is dropped."""
    try:
        yield from _read_trace(path)
    except EOFError:
        return


def _read_trace(path):
    with gzip.open(path, 'rb') as f:
        if f.read(len(TRACE_MAGIC)) != TRACE_MAGIC:
            raise ValueError('Not a CFC2 state trace')
        head = struct.calcsize('<dQBI')
        while True:
            raw = f.read(head)
            if not raw:
                return
            if len(raw) != head:
                raise ValueError('Truncated trace record')
            t, root, key, n = struct.unpack('<dQBI', raw)
            if key:
                block = f.read(n)
                if len(block) != n:
                    raise ValueError('Truncated keyframe')
                yield t, root, True, block
            else:
                offsets = struct.unpack(f'<{n}I', f.read(4 * n))
                values = f.read(n)
                if len(values) != n:
                    raise ValueError('Truncated delta')
                yield t, root, False, list(zip(offsets, values))


# ---- traced cursor decoders (leads become entries here only after tracing) ----

def cvs1_selections(read, lifecycle):
    """CVS1 branch-A selection byte P+0x78+4*side+slot; see
    docs/cvs1-selection-visits.md. 0xFF and bad slots are not selections."""
    import cvs1_selection_capture as selection
    obs = selection.observe(read, lifecycle)
    out = []
    for actor in obs.get('actors') or []:
        sel, slot = actor.get('selected'), actor.get('slot')
        if sel is None or sel < 0 or not 0 <= slot < 4:
            continue
        out.append((actor['side'], slot, sel))
    return out


DECODERS = {'cvs1': cvs1_selections}


# ---- capture -----------------------------------------------------------------

class Capture:
    """Polling state machine. Every side effect is injected for testing."""

    def __init__(self, read, lifecycle, game, out_dir, grab, key_down, pad_dirs, say,
                 plan, clock=time.monotonic):
        self.read, self.lifecycle, self.game = read, lifecycle, game
        self.path = game_paths(lifecycle)[game]
        self.decoder = DECODERS.get(game)
        self.out = Path(out_dir)
        self.grab, self.key_down, self.pad_dirs, self.say = grab, key_down, pad_dirs, say
        self.plan, self.clock = list(plan), clock
        self.log = (self.out / 'session.jsonl').open('a', encoding='utf-8')
        self.trace = TraceWriter(self.out / 'trace.bin.gz')
        self.rows = []
        self.held, self.key_was_down = set(), False
        self.last_press, self.pictured_since_press, self.inputs = None, True, 0
        self.pending, self.captured = {}, {}
        self.pictures = self.selection_pictures = self.snapshots = 0
        self.warned = False
        self.active_reason = None

    def close(self):
        self.trace.close()
        self.log.close()

    def write(self, row):
        self.rows.append(row)
        self.log.write(json.dumps(row) + '\n')
        self.log.flush()

    def image(self, stem):
        if self.grab is None:                      # screenshots are opt-in
            return dict(image=None, image_status='screenshots off')
        image = self.grab()
        if image is None:
            result = dict(image=None, image_status='no game window found')
        else:
            path = self.out / f'{stem}.png'
            image.save(path)
            black = max(hi for _, hi in image.convert('RGB').getextrema()) < 8
            result = dict(image=path.name, image_size=list(image.size),
                          image_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                          image_status='black frame: use windowed or borderless' if black else 'ok')
        # The player is in the game, not the console: say a bad picture once.
        if result['image_status'] != 'ok' and not self.warned:
            self.warned = True
            self.say('Warning: the screenshot is unusable. ' +
                     ('Switch the display to windowed.' if result['image'] else
                      'The game window was not found.'))
        return result

    def block(self):
        """Bracketed read of the identified block, or (None, reason)."""
        root, size, info = identify(self.read, self.lifecycle, self.path)
        if root is None:
            return None, None, info
        try:
            before = metadata.exact(self.read, int(self.path['root_slot'], 16), 8)
            raw = metadata.exact(self.read, root, size)
            after = metadata.exact(self.read, int(self.path['root_slot'], 16), 8)
        except OSError as exc:
            return None, None, f'unreadable block: {exc}'
        if before != after or int.from_bytes(before, 'little') != root:
            return None, None, 'root changed during the read'
        return root, raw, info

    def poll_inputs(self, now):
        current = {(d, 'keyboard') for vk, d in DIRECTION_KEYS.items() if self.key_down(vk)}
        current |= {(d, 'pad') for d in self.pad_dirs()}
        for direction, source in sorted(current - self.held):
            self.inputs += 1
            self.last_press, self.pictured_since_press = now, False
            self.write(dict(kind='input', t=now, utc=metadata.utc(), direction=direction,
                            source=source, index=self.inputs))
        self.held = current

    def poll(self):
        now = self.clock()
        self.poll_inputs(now)
        root, raw, info = self.block()
        if root is None:
            if info != self.active_reason:
                self.write(dict(kind='status', t=now, utc=metadata.utc(), active=False, reason=info))
                self.active_reason = info
            self.pending.clear()
        else:
            if self.active_reason is not None or not self.trace.samples:
                self.write(dict(kind='status', t=now, utc=metadata.utc(), active=True,
                                root=hex(root), **info))
                self.active_reason = None
            self.trace.sample(now, root, raw)
            if self.decoder:
                self.decode_selections(now)

        if (self.grab is not None and self.last_press is not None and not self.pictured_since_press and
                now - self.last_press >= SETTLE_SECONDS):
            self.pictured_since_press = True
            self.pictures += 1
            row = dict(kind='cursor_picture', t=now, utc=metadata.utc(),
                       after_input=self.inputs, active=root is not None)
            row.update(self.image(f'cursor-{self.pictures:03d}'))
            self.write(row)

        down = bool(self.key_down(SNAPSHOT_KEY))
        if down and not self.key_was_down:
            self.snapshot(now, root, raw, info)
        self.key_was_down = down

    def decode_selections(self, now):
        current = {side: (slot, sel) for side, slot, sel in self.decoder(self.read, self.lifecycle)}
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
            self.selection_pictures += 1
            row = dict(kind='selection', t=now, utc=metadata.utc(), side=side, slot=key[0],
                       native_id=key[1], decoder=self.game)
            row.update(self.image(f'select-{self.selection_pictures:03d}-side{side}-id{key[1]}'))
            self.write(row)

    def snapshot(self, now, root, raw, info):
        index = self.snapshots
        label = self.plan[index] if index < len(self.plan) else f'unplanned snapshot {index + 1}'
        row = dict(kind='snapshot', t=now, utc=metadata.utc(), index=index, label=label)
        if root is None:
            row['status'] = f'refused: {info}'
            self.write(row)
            self.say(f"Not captured. {self.path['tag']} is not active.")
            return
        self.snapshots += 1
        name = f'state-{self.snapshots:02d}.bin'
        (self.out / name).write_bytes(raw)
        row.update(status='read', root=hex(root), block=name, size=len(raw),
                   sha256=hashlib.sha256(raw).hexdigest(), **info)
        row.update(self.image(f'state-{self.snapshots:02d}'))
        self.write(row)
        nxt = self.plan[self.snapshots] if self.snapshots < len(self.plan) else None
        self.say(f'Captured {label}.' + (f' Next: {nxt}.' if nxt else ' Plan complete.'))


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
                if rect.right * rect.bottom > 0:
                    best.append((rect.right * rect.bottom, hwnd, rect.right, rect.bottom))
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


class _Pad(C.Structure):
    _fields_ = [('packet', W.DWORD), ('buttons', W.WORD), ('lt', C.c_ubyte), ('rt', C.c_ubyte),
                ('lx', C.c_short), ('ly', C.c_short), ('rx', C.c_short), ('ry', C.c_short)]


def pad_poller():
    """D-pad and left stick on any of the four XInput pads; none if absent."""
    try:
        xinput = C.WinDLL('xinput1_4')
    except OSError:
        return lambda: set()

    def poll():
        dirs = set()
        for i in range(4):
            state = _Pad()
            if xinput.XInputGetState(i, C.byref(state)):
                continue
            b = state.buttons
            if b & 1 or state.ly > 16000: dirs.add('up')
            if b & 2 or state.ly < -16000: dirs.add('down')
            if b & 4 or state.lx < -16000: dirs.add('left')
            if b & 8 or state.lx > 16000: dirs.add('right')
        return dirs
    return poll


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
    lifecycle_raw = metadata.MANIFEST.read_bytes()
    lifecycle = json.loads(lifecycle_raw)
    games = sorted(game_paths(lifecycle))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int)
    parser.add_argument('--game', required=True, choices=games)
    parser.add_argument('--out', type=Path, required=True, help='A new, empty directory')
    parser.add_argument('--label', required=True)
    parser.add_argument('--language', required=True, help='Shell language and ROM version, or unknown')
    parser.add_argument('--plan', type=Path, help='Snapshot labels, one per line (default: in-match plan)')
    parser.add_argument('--screenshots', action='store_true',
                        help='Also picture the window (needs windowed display mode)')
    parser.add_argument('--minutes', type=float, default=10)
    parser.add_argument('--interval', type=float, default=0.05)
    parser.add_argument('--game-dir', default=r'C:\Program Files (x86)\Steam\steamapps\common\CAPCOM FIGHTING COLLECTION 2')
    args = parser.parse_args()
    if args.pid <= 0 or not 0.5 <= args.minutes <= 40 or not 0.02 <= args.interval <= 1:
        parser.error('Require PID>0, minutes 0.5..40, interval 0.02..1')
    if args.out.exists() and any(args.out.iterdir()):
        parser.error('Output directory is not empty; choose a fresh path')
    plan = ([l.strip() for l in args.plan.read_text(encoding='utf-8').splitlines() if l.strip()]
            if args.plan else IN_MATCH_PLAN)
    args.out.mkdir(parents=True, exist_ok=True)
    reader = metadata.ProcessReader(args.pid)
    say = speaker(args.game_dir)
    cap = None
    try:
        checked = metadata.verify_build(unhooked(reader.read), lifecycle)
        if args.game == 'cvs1':
            import cvs1_selection_capture as selection
            checked += selection.verify(unhooked(reader.read), lifecycle,
                                        json.loads(selection.ACQUISITION.read_bytes()))
        cap = Capture(reader.read, lifecycle, args.game, args.out,
                      window_grabber(args.pid) if args.screenshots else None,
                      key_poller(), pad_poller(), say, plan)
        cap.write(dict(kind='provenance', tool='collection-state-capture', schema=1,
                       game=args.game, label=args.label, language=args.language,
                       screenshots=args.screenshots,
                       created_utc=metadata.utc(), interval=args.interval, plan=plan,
                       decoder=args.game if cap.decoder else None, checked_code_spans=checked,
                       manifest_sha256=hashlib.sha256(lifecycle_raw).hexdigest(),
                       tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()))
        print(f'Capturing {args.game} for up to {args.minutes} minutes. F9 = snapshot. '
              f'Ctrl+C stops.', flush=True)
        say(f'Capture started. First snapshot: {plan[0]}.' if plan else 'Capture started.')
        end = time.monotonic() + args.minutes * 60
        while time.monotonic() < end:
            cap.poll()
            time.sleep(args.interval)
        say('Capture finished.')
    except KeyboardInterrupt:
        say('Capture stopped.')
    finally:
        if cap:
            cap.close()
            print(f'{cap.trace.samples} trace samples, {cap.inputs} presses, {cap.pictures} cursor '
                  f'pictures, {cap.selection_pictures} selection pictures, {cap.snapshots} '
                  f'snapshots in {args.out}')
        reader.close()


if __name__ == '__main__':
    main()
