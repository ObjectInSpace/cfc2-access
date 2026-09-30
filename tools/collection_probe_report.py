"""Validate boundary-probe CSV without mistaking a sample for lifetime proof."""
import argparse
import csv
import json
from pathlib import Path

FIELDS = ('sequence,ticks,thread,site,exit,depth,readable,session,root,game,rom,bytes,'
          'load_count,intermediate,transfer,argument,length,result').split(',')
SITE_NAMES = ('outer_update', 'snapshot_load', 'root_registration', 'game_cleanup', 'screen_release')


def analyze(text):
    lines = text.splitlines()
    if len(lines) < 2:
        raise ValueError('Missing capture header')
    header = lines[0].split(',')
    if (len(header) != 6 or header[:3] != ['# boundary_probe', '1', 'qpc_frequency']
            or header[4:] != ['duration_ms', '120000'] or int(header[3]) <= 0):
        raise ValueError('Unsupported capture header')
    if lines[1].split(',') != FIELDS:
        raise ValueError('Unexpected event columns')
    rows, dropped, complete = [], None, False
    for line in lines[2:]:
        if line.startswith('#'):
            footer = line.split(',')
            if footer[:2] != ['# complete', 'dropped'] or len(footer) != 3 or complete:
                raise ValueError('Install failure, duplicate footer or unknown metadata')
            dropped = int(footer[2])
            if dropped < 0:
                raise ValueError('Invalid dropped count')
            complete = True
            continue
        if complete:
            raise ValueError('Events after completion footer')
        cells = next(csv.reader([line]))
        if len(cells) != len(FIELDS):
            raise ValueError('Partial event row')
        row = {key:int(value, 16 if key in ('session','root','argument') else 10)
               for key,value in zip(FIELDS,cells)}
        if row['sequence'] != len(rows)+1 or row['thread'] <= 0 or row['ticks'] < 0:
            raise ValueError('Invalid sequence, thread or timestamp')
        if row['site'] not in range(len(SITE_NAMES)) or row['exit'] not in (0,1) or row['readable'] not in (0,1):
            raise ValueError('Invalid site or event flags')
        if not 0 <= row['depth'] <= 0xffffffff:
            raise ValueError('Invalid nesting depth')
        for key in ('thread','game','rom','bytes','load_count','intermediate','transfer'):
            if not 0 <= row[key] <= 0xffffffff:
                raise ValueError('Out-of-range dword field')
        for key in ('session','root','argument','ticks'):
            if not 0 <= row[key] <= 0xffffffffffffffff:
                raise ValueError('Out-of-range pointer or clock field')
        if not -(2**31) <= row['length'] < 2**31 or not -1 <= row['result'] <= 255:
            raise ValueError('Out-of-range argument or result')
        rows.append(row)

    stacks, pairs, unmatched_exits, threads = {}, [], [], {}
    prior_ticks = {}
    for row in rows:
        tid = row['thread']
        if row['ticks'] < prior_ticks.get(tid, 0):
            raise ValueError('Timestamp reversed on one thread')
        prior_ticks[tid] = row['ticks']
        threads.setdefault(tid, set()).add(SITE_NAMES[row['site']])
        stack = stacks.setdefault(tid, [])
        if not row['exit']:
            stack.append(row)
        elif (stack and stack[-1]['site'] == row['site']
              and stack[-1]['argument'] == row['argument']
              and stack[-1]['length'] == row['length']
              and stack[-1]['depth'] == row['depth']):
            start = stack.pop()
            pairs.append(dict(site=SITE_NAMES[row['site']], thread=tid,
                              enter_sequence=start['sequence'], exit_sequence=row['sequence'],
                              duration_ticks=row['ticks']-start['ticks'],
                              depth=row['depth']))
        else:
            unmatched_exits.append(row['sequence'])

    unreadable = sum(not row['readable'] for row in rows)
    identities = sorted({(row['game'],row['rom']) for row in rows
                         if row['readable'] and row['session'] and row['root'] and row['bytes']})
    cautions = ['Observed ordering is not proof of future ordering or native memory synchronization.',
                'The five sites do not observe every reset, native write or presentation event.',
                'Entry/exit durations include native work and diagnostic overhead.']
    if not complete:
        cautions.append('No completion footer: interrupted or failed write; dropped count is unknown.')
    if dropped:
        cautions.append('Events were dropped; missing boundaries cannot be reconstructed.')
    if unreadable:
        cautions.append('Unreadable records contain no usable state identity, even if some fields are nonzero.')
    unmatched_entries = [r['sequence'] for stack in stacks.values() for r in stack]
    if unmatched_entries or unmatched_exits:
        cautions.append('Unmatched boundaries can reflect window edges, event loss or abnormal return; do not infer completion.')
    return dict(schema=1, evidence='diagnostic call records; not an atomic snapshot or readiness proof',
                complete=complete, dropped=dropped, event_count=len(rows),
                qpc_frequency=int(header[3]), unreadable=unreadable,
                observed_identities=[dict(game=g, rom=r) for g,r in identities],
                threads=[dict(thread=t, sites=sorted(s)) for t,s in sorted(threads.items())],
                pairs=pairs, pairing_reliable=complete and dropped == 0,
                unmatched_entries=sorted(unmatched_entries),
                unmatched_exits=unmatched_exits, cautions=cautions)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    report = analyze(args.capture.read_text(encoding='utf-8'))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f"{report['event_count']} events, {len(report['threads'])} thread(s), "
          f"{len(report['pairs'])} candidate call pairs; complete={report['complete']}, "
          f"dropped={report['dropped']}, unreadable={report['unreadable']}.")
    for caution in report['cautions']:
        print(caution)


if __name__ == '__main__':
    main()
