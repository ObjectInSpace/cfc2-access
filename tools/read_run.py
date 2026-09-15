"""
Read a run's log ONCE and report everything a screen investigation needs.

⭐⭐ WHY THIS EXISTS. Working out what the 2026-09-15 Game Settings run
contained took ~20 separate ad-hoc greps and Python one-liners: which screens
were unnamed, what the cursor callers and counts were, which rows sat in which
column, that the screen had TWO TABS, and which values each row had shown. Every
one of those is a mechanical read of the same log. Doing them by hand is slow
and -- worse -- each one is a chance to mis-parse and build on the mistake.

Usage:
    python tools/read_run.py [path-to-CFC2Access.log]

It prints, in one pass:
  * the build stamp, and whether the log is FRESH (⚠ a stale log is the single
    most expensive mistake available here: it looks exactly like a real one)
  * speech backend and any hard errors
  * every cursor caller with its axis, item counts and movement tally
  * every screen the mod reported as UNKNOWN
  * the settings census, if cfc2_census.on was set
  * per-screen COLUMN CONTENTS from the layout dumps, grouped by X, so a
    row/value/heading layout is visible without decoding floats by hand
  * ⭐ TAB DETECTION: when one caller's column shows two different label sets,
    that is a tabbed screen, and naming it from one sighting would be wrong.

⚠ THE FLOATS IN A LAYOUT LINE ARE BIG-ENDIAN. Decoding them little-endian
yields all-zero coordinates -- a wrong answer that reads exactly like a real
measurement rather than an error. Cost a full detour on 2026-09-15.
"""

import os
import re
import sys
import struct
import collections

DEFAULT_LOG = (r"C:\Program Files (x86)\Steam\steamapps\common"
               r"\CAPCOM FIGHTING COLLECTION 2\CFC2Access.log")

MOVE = re.compile(
    r'mc: (?P<axis>vertical|horizontal)\s+cursor (?P<from>-?\d+) -> (?P<to>-?\d+)'
    r'\s+\(item (?P<pos>\d+) of (?P<total>\d+).*?'
    r'caller=(?P<caller>[0-9A-Fa-f]+) screen=(?P<screen>[0-9A-Fa-f]+)'
    r'(?: cursor=(?P<cursor>[0-9A-Fa-f]+))?')
SPOKE = re.compile(r'mcspeak: spoke "(?P<line>.*)"\s*$')
UNKNOWN = re.compile(r'mcspeak: \[DISCOVERY\] unknown screen -- '
                     r'caller=(?P<caller>0x[0-9A-Fa-f]+)\s+items=(?P<items>\d+)'
                     r'\s+axis=(?P<axis>\w+)')
LAYOUT = re.compile(r'layout:\s+(?P<obj>[0-9A-F]{16}) "(?P<text>.*?)"'
                    r'(?P<fields>.*)$')
FIELD = re.compile(r'\+0x(?P<off>[0-9a-f]{3}):(?P<val>[0-9a-f]{8})')
CENSUS = re.compile(r'gt:   (?P<label>.{1,28}?)\s+= (?P<value>.{1,28}?)\s+'
                    r'x(?P<hits>\d+)')
BUILD = re.compile(r'build: compiled (?P<when>.*?)\s+--')


def be_float(hexword):
    """⚠ BIG-endian. See the module docstring: '<f' here silently yields 0.0."""
    return struct.unpack('>f', bytes.fromhex(hexword))[0]


def parse(path):
    runs = {'lines': 0, 'build': None, 'speech': None, 'errors': [],
            'moves': [], 'unknown': [], 'dumps': [], 'census': [],
            'spoke': collections.Counter()}
    cur_dump = None
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            runs['lines'] += 1
            ts = line[1:13]

            m = BUILD.search(line)
            if m and not runs['build']:
                runs['build'] = m.group('when')
            if 'speech: ready' in line:
                runs['speech'] = line.split('backend=')[-1].strip()
            if '⛔' in line and 'could not' in line.lower():
                runs['errors'].append((ts, line.strip()))

            m = MOVE.search(line)
            if m:
                d = m.groupdict()
                scr = int(d['screen'], 16)
                cur = int(d['cursor'], 16) if d['cursor'] else None
                runs['moves'].append({
                    'ts': ts, 'axis': d['axis'],
                    'caller': int(d['caller'], 16), 'screen': scr,
                    'total': int(d['total']), 'pos': int(d['pos']),
                    'off': (cur - scr) if cur else None})

            m = UNKNOWN.search(line)
            if m:
                runs['unknown'].append((ts, int(m.group('caller'), 16),
                                        int(m.group('items')),
                                        m.group('axis')))

            m = SPOKE.search(line)
            if m:
                runs['spoke'][m.group('line')] += 1

            m = CENSUS.search(line)
            if m and '=' in line and 'gt:   ' in line:
                runs['census'].append((m.group('label').strip(),
                                       m.group('value').strip(),
                                       int(m.group('hits'))))

            if 'FIELD DUMP' in line:
                cur_dump = {'ts': ts, 'objs': []}
                runs['dumps'].append(cur_dump)
            m = LAYOUT.search(line)
            if m and cur_dump is not None:
                fields = dict(FIELD.findall(m.group('fields')))
                if '040' in fields and '044' in fields:
                    cur_dump['objs'].append({
                        'text': m.group('text'),
                        'x': be_float(fields['040']),
                        'y': be_float(fields['044']),
                        'parent': fields.get('088')})
    return runs


def report(runs, path):
    print(f"=== {path}")
    print(f"    {runs['lines']} line(s)")
    mtime = os.path.getmtime(path)
    import time
    age_h = (time.time() - mtime) / 3600.0
    stamp = time.strftime('%Y-%m-%d %H:%M', time.localtime(mtime))
    flag = "" if age_h < 6 else "   ⚠⚠ STALE -- is this the run you mean?"
    print(f"    last written {stamp}  ({age_h:.1f} h ago){flag}")
    print(f"    build stamp  {runs['build']}")
    print(f"    speech       {runs['speech'] or '⛔ never became ready'}")
    for ts, e in runs['errors'][:5]:
        print(f"    ⛔ {ts} {e[:100]}")

    print("\n--- CURSOR SITES (caller, axis, item counts seen) ---")
    sites = collections.defaultdict(lambda: collections.Counter())
    offs = collections.defaultdict(set)
    for mv in runs['moves']:
        sites[(mv['caller'], mv['axis'])][mv['total']] += 1
        if mv['off'] is not None:
            offs[(mv['caller'], mv['axis'])].add(mv['off'])
    if not sites:
        print("  (none -- no cursor movement was recorded in this run)")
    for (caller, axis), counts in sorted(sites.items()):
        tot = sum(counts.values())
        cs = ", ".join(f"{k} items x{v}" for k, v in sorted(counts.items()))
        o = sorted(offs[(caller, axis)])
        # ⚠ A cursor OUTSIDE its screen object is not a field at all -- the
        # Yes/No dialog's cursor is a STACK LOCAL, which is why its "2 of 2"
        # once appeared on unrelated rows. Printing a huge or negative number
        # as an offset invites reading it as a struct member.
        inside = [v for v in o if 0 <= v < 0x4000]
        outside = len(o) - len(inside)
        parts = []
        if inside:
            parts.append("cursor offsets " +
                         ", ".join(f"+0x{v:X}" for v in inside))
        if outside:
            parts.append(f"⚠ {outside} cursor(s) NOT inside the screen object "
                         f"(a stack local, not a field -- it cannot key a row)")
        os_ = "; ".join(parts) if parts else "no cursor offset"
        print(f"  0x{caller:X}  {axis:10} {tot:3d} move(s): {cs}")
        print(f"      {os_}")
        if len(counts) > 1:
            print("      ⚠ MORE THAN ONE COUNT on one caller -- either a value "
                  "axis serving several rows, or TWO SCREENS sharing a call "
                  "site. Do not key a label map on this caller alone.")

    print("\n--- SCREENS THE MOD COULD NOT NAME ---")
    if not runs['unknown']:
        print("  (none reported)")
    seen = set()
    for ts, caller, items, axis in runs['unknown']:
        if (caller, items, axis) in seen:
            continue
        seen.add((caller, items, axis))
        print(f"  {ts}  caller=0x{caller:X}  items={items}  axis={axis}")

    print("\n--- COLUMNS, per dump (grouped by X, ordered by Y) ---")
    if not runs['dumps']:
        print("  (no layout dumps -- was cfc2_layout.on set?)")
    colsets = collections.defaultdict(list)
    for d in runs['dumps']:
        bycol = collections.defaultdict(list)
        for o in d['objs']:
            bycol[round(o['x'])].append(o)
        for x, objs in bycol.items():
            objs.sort(key=lambda o: o['y'])
            sig = tuple(o['text'] for o in objs)
            colsets[x].append((d['ts'], sig))
    for x in sorted(colsets):
        variants = collections.OrderedDict()
        for ts, sig in colsets[x]:
            variants.setdefault(sig, []).append(ts)
        print(f"\n  x={x}  ({len(colsets[x])} dump(s), "
              f"{len(variants)} distinct content(s))")
        for sig, tss in variants.items():
            print(f"    seen {len(tss)}x (first {tss[0]}):")
            for t in sig[:12]:
                print(f"        {t[:66]}")
            if len(sig) > 12:
                print(f"        ... and {len(sig)-12} more")
        if len(variants) > 1 and all(len(s) == len(list(variants)[0])
                                     for s in variants):
            print("    ⭐⭐ TABS: this column shows more than one label set at "
                  "the same length. That is one screen with several PAGES, and "
                  "a label map keyed on (caller, count) would speak one page's "
                  "names on another. Quote the live column instead.")

    if runs['census']:
        print("\n--- SETTINGS CENSUS (cfc2_census.on) ---")
        byrow = collections.OrderedDict()
        for label, value, hits in runs['census']:
            byrow.setdefault(label, [])
            if value not in [v for v, _ in byrow[label]]:
                byrow[label].append((value, hits))
        for label, vals in byrow.items():
            vs = ", ".join(v for v, _ in vals)
            print(f"  {label:28} {len(vals):2d} value(s): {vs}")
        print("  ▶ Compare each row's value COUNT against that row's "
              "horizontal cursor total above. A row showing fewer values than "
              "its cursor reports was not walked all the way -- that is a gap "
              "in the WALK, not a short list.")

    print("\n--- WHAT WAS SPOKEN (distinct) ---")
    named = [s for s in runs['spoke'] if not re.match(r'^\d+ of \d+$', s)]
    bare = [s for s in runs['spoke'] if re.match(r'^\d+ of \d+$', s)]
    print(f"  {len(named)} line(s) carried a NAME, "
          f"{len(bare)} were bare positions")
    for s in sorted(bare)[:8]:
        print(f"    ⚠ bare: \"{s}\"  x{runs['spoke'][s]}")
    if bare:
        print("  ▶ A bare position means the screen is unnamed OR a guard "
              "refused. Check the unknown-screen list above first.")


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_LOG
    if not os.path.exists(path):
        print(f"⛔ no log at {path}")
        return 2
    report(parse(path), path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
