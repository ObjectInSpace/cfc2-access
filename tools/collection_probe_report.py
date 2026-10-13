"""Validate boundary-probe CSV without mistaking a sample for lifetime proof."""
import argparse
import csv
import json
from pathlib import Path

FIELDS = ('sequence,ticks,thread,site,exit,depth,readable,session,root,game,rom,bytes,'
          'load_count,intermediate,transfer,argument,length,result').split(',')
SITE_NAMES = ('outer_update', 'snapshot_load', 'root_registration', 'game_cleanup', 'screen_release', 'quick_load')
SITE_NAMES += ('application_update', 'training_reset', 'game_initialize', 'pause_update',
               'wait_single', 'wait_multiple')
LIFETIME_FIELDS = ('invocation,application_invocation,return_site,versus,training,reset_state,'
                   'reset_request,character_gate,wait_count,wait_all,timeout,handles_readable,'
                   'last_error,handles').split(',')
WAIT_CALLERS = {0x1401f7da2: 'generic_wake', 0x1401f847f: 'batch_wake',
                0x1401f81b9: 'generic_retirement', 0x1401f8208: 'batch_retirement',
                0x1401f80ad: 'batch_join'}


def wait_outcome(row):
    """Classify raw API results, never promote them to allocation protection."""
    raw = row['result'] & 0xffffffff
    count = row['wait_count']
    if raw == 0xffffffff:
        return 'failed'
    if raw == 0x102:
        return 'timeout'
    if 1 <= count <= 64 and raw < count:
        return 'signaled'
    if 1 <= count <= 64 and 0x80 <= raw < 0x80 + count:
        return 'abandoned'
    return 'unknown'


def analyze(text):
    lines = text.splitlines()
    if len(lines) < 2:
        raise ValueError('Missing capture header')
    header = lines[0].split(',')
    if (len(header) != 6 or header[0] != '# boundary_probe' or header[1] not in ('1', '2', '3')
            or header[2] != 'qpc_frequency'
            or header[4:] != ['duration_ms', '120000'] or int(header[3]) <= 0):
        raise ValueError('Unsupported capture header')
    capture_schema = int(header[1])
    fields = FIELDS + (LIFETIME_FIELDS if capture_schema == 3 else [])
    if lines[1].split(',') != fields:
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
        if len(cells) != len(fields):
            raise ValueError('Partial event row')
        row = {key:int(value, 16 if key in ('session','root','argument','return_site') else 10)
               for key,value in zip(fields,cells) if key != 'handles'}
        if row['sequence'] != len(rows)+1 or row['thread'] <= 0 or row['ticks'] < 0:
            raise ValueError('Invalid sequence, thread or timestamp')
        if row['site'] not in range({1:5, 2:6, 3:12}[capture_schema]) or row['exit'] not in (0,1) or row['readable'] not in (0,1):
            raise ValueError('Invalid site or event flags')
        if not 0 <= row['depth'] <= 0xffffffff:
            raise ValueError('Invalid nesting depth')
        for key in ('thread','game','rom','bytes','load_count','intermediate','transfer'):
            if not 0 <= row[key] <= 0xffffffff:
                raise ValueError('Out-of-range dword field')
        for key in ('session','root','argument','ticks'):
            if not 0 <= row[key] <= 0xffffffffffffffff:
                raise ValueError('Out-of-range pointer or clock field')
        result_valid = (-(2**31) <= row['result'] < 2**31 if row['site'] in (5,10,11)
                        else -1 <= row['result'] <= 255)
        if not -(2**31) <= row['length'] < 2**31 or not result_valid:
            raise ValueError('Out-of-range argument or result')
        if capture_schema == 3:
            for key in LIFETIME_FIELDS[:3]:
                if not 0 <= row[key] <= 0xffffffffffffffff:
                    raise ValueError('Out-of-range invocation or return site')
            for key in LIFETIME_FIELDS[3:-1]:
                if not 0 <= row[key] <= 0xffffffff:
                    raise ValueError('Out-of-range lifetime dword')
            row['handles'] = [int(h, 16) for h in cells[-1].split(';')] if cells[-1] else []
            if (row['handles_readable'] not in (0,1) or len(row['handles']) > 64 or
                    any(not 0 <= h <= 0xffffffffffffffff for h in row['handles'])):
                raise ValueError('Invalid handle copy')
            if row['site'] >= 6 and not row['invocation']:
                raise ValueError('Missing invocation identity')
            if row['site'] >= 10:
                valid_callers = ({0x1401f80ad} if row['site'] == 11 else set(WAIT_CALLERS)-{0x1401f80ad})
                if row['return_site'] not in valid_callers:
                    raise ValueError('Unreviewed wait caller')
                if row['site'] == 10 and row['wait_count'] != 1:
                    raise ValueError('Invalid single wait count')
                if row['handles_readable']:
                    if not 1 <= row['wait_count'] <= 64 or len(row['handles']) != row['wait_count']:
                        raise ValueError('Incomplete handle copy')
                elif row['handles']:
                    raise ValueError('Unreadable handle copy contains handles')
            elif row['wait_count'] or row['handles_readable'] or row['handles']:
                raise ValueError('Handles on non-wait record')
        rows.append(row)

    stacks, pairs, unmatched_exits, threads = {}, [], [], {}
    prior_ticks, prior_invocations, waits = {}, {}, []
    for row in rows:
        tid = row['thread']
        if row['ticks'] < prior_ticks.get(tid, 0):
            raise ValueError('Timestamp reversed on one thread')
        prior_ticks[tid] = row['ticks']
        threads.setdefault(tid, set()).add(SITE_NAMES[row['site']])
        stack = stacks.setdefault(tid, [])
        if not row['exit']:
            if capture_schema == 3 and row['invocation']:
                if row['invocation'] <= prior_invocations.get(tid, 0):
                    raise ValueError('Reused or reversed invocation identity')
                prior_invocations[tid] = row['invocation']
            stack.append(row)
        elif (stack and stack[-1]['site'] == row['site']
              and stack[-1]['argument'] == row['argument']
              and stack[-1]['length'] == row['length']
              and stack[-1]['depth'] == row['depth']
              and (capture_schema != 3 or all(stack[-1][k] == row[k] for k in
                   ('invocation','application_invocation','return_site','wait_count',
                    'wait_all','timeout','handles_readable','handles')))):
            start = stack.pop()
            pairs.append(dict(site=SITE_NAMES[row['site']], thread=tid,
                              enter_sequence=start['sequence'], exit_sequence=row['sequence'],
                              duration_ticks=row['ticks']-start['ticks'],
                              depth=row['depth']))
            if capture_schema == 3 and row['site'] >= 10:
                waits.append(dict(purpose=WAIT_CALLERS[row['return_site']], thread=tid,
                                  invocation=row['invocation'], application_invocation=row['application_invocation'],
                                  enter_sequence=start['sequence'], exit_sequence=row['sequence'],
                                  return_site=hex(row['return_site']), raw_result=row['result'] & 0xffffffff,
                                  outcome=wait_outcome(row), last_error=row['last_error'],
                                  last_error_meaningful=(row['result'] & 0xffffffff) == 0xffffffff,
                                  count=row['wait_count'], wait_all=row['wait_all'], timeout=row['timeout'],
                                  handles_readable=bool(row['handles_readable']), handles=row['handles']))
        else:
            unmatched_exits.append(row['sequence'])

    unreadable = sum(not row['readable'] for row in rows)
    identities = sorted({(row['game'],row['rom']) for row in rows
                         if row['readable'] and row['session'] and row['root'] and row['bytes']})
    cautions = ['Observed ordering is not proof of future ordering or native memory synchronization.',
                'The observed sites do not cover every reset, native write or presentation event.',
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
    if capture_schema == 3:
        cautions += ['Wait results describe only matched reviewed calls; zero records does not mean successful wait.',
                     'Worker application_invocation=0 is not correlated to a caller application invocation.',
                     'Initialization and Training callback return are not validated readiness or lifetime protection.']
    return dict(schema=1, evidence='diagnostic call records; not an atomic snapshot or readiness proof',
                capture_schema=capture_schema,
                complete=complete, dropped=dropped, event_count=len(rows),
                qpc_frequency=int(header[3]), unreadable=unreadable,
                observed_identities=[dict(game=g, rom=r) for g,r in identities],
                threads=[dict(thread=t, sites=sorted(s)) for t,s in sorted(threads.items())],
                pairs=pairs, pairing_reliable=complete and dropped == 0 and
                    (capture_schema != 3 or not (unmatched_entries or unmatched_exits)),
                wait_outcomes=waits, lifetime_proven=False,
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
