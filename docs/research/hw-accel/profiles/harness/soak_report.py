#!/usr/bin/env python3
"""Summarises a soak.py run: python3 soak_report.py OUTDIR [VIEWER.jsonl]
Prints restarts and API errors, growth of memory, threads, fds and dma-buf over the run (first hour against last hour, and a least-squares slope),
the frame rate and stage timings of each node per quarter of the run, temperatures and clocks, error counts and the kinds of kernel/log
messages seen, and (given the viewer's log) the WebRTC viewer's minutes connected and frames received."""
import collections, json, os, re, sys


def slope_per_hour(xs, ys):
    n = len(xs)
    if n < 2: return 0.0
    mx, my = sum(xs) / n, sum(ys) / n
    d = sum((x - mx) ** 2 for x in xs)
    return 60 * sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / d if d else 0.0


def mean(v):
    v = [x for x in v if x is not None]
    return sum(v) / len(v) if v else float('nan')


def main():
    out = sys.argv[1]
    rows = [json.loads(l) for l in open(os.path.join(out, 'samples.jsonl')) if l.strip()]
    if not rows:
        print('no samples yet'); return
    n = len(rows)
    print('%d minutes sampled (%s to %s)' % (n, rows[0]['t'], rows[-1]['t']))
    print('service: restarts %d, PID changes %d, minutes not active %d, API errors %d, max API response %s ms' % (
        rows[-1].get('restarts', 0), sum(1 for r in rows if r.get('pid_changed')), sum(1 for r in rows if r.get('active') != 'active'),
        sum(1 for r in rows if 'api_error' in r), max((r.get('api_ms', 0) for r in rows), default=0)))

    hour = min(60, max(1, n // 4))
    print('\nresources (first %d min -> last %d min, slope per hour over the run):' % (hour, hour))
    for k in ('rss_mb', 'anon_mb', 'threads', 'fds', 'dmabuf_fds', 'dmabuf_objects', 'dmabuf_mb', 'mem_avail_mb', 'service_cores', 'busy_a76', 'busy_a55'):
        ys = [r.get(k) for r in rows]
        if any(y is None or y < 0 for y in ys): continue
        print('  %-15s %9.1f -> %9.1f   %+8.2f/h   (min %s, max %s)' % (k, mean(ys[:hour]), mean(ys[-hour:]), slope_per_hour(list(range(n)), ys), min(ys), max(ys)))

    print('\ntemperatures and clocks: max %s C (%s), A76 %s MHz, A55 %s MHz, GPU %s MHz, NPU %s MHz, DMC %s MHz; A76 governor %s' % (
        max(max(r['temp'].values()) for r in rows), ', '.join('%s %d' % (z, max(r['temp'].get(z, 0) for r in rows)) for z in rows[0]['temp']),
        sorted({m for r in rows for m in r['mhz_a76']}), sorted({r['mhz_a55'] for r in rows}), sorted({r.get('gpu_mhz') for r in rows}),
        sorted({r.get('npu_mhz') for r in rows}), sorted({r.get('dmc_mhz') for r in rows}), sorted({r.get('gov_a76') for r in rows})))
    print('loads (mean): GPU %.0f%%, DMC %.0f%%, NPU cores %s' % (mean([r.get('gpu_load') for r in rows]), mean([r.get('dmc_load') for r in rows]),
          [round(mean([r['npu_load'][i] for r in rows if len(r.get('npu_load', [])) > i])) for i in range(3)]))

    q = max(1, n // 4)
    quarters = [rows[i * q:(i + 1) * q] for i in range(4)] if n >= 4 else [rows]
    names = sorted({k for r in rows for k in r.get('nodes', {})})
    print('\nper node, by quarter of the run: fps | mean ms of the main stage | max ms of it')
    for name in names:
        cells = []
        for part in quarters:
            nodes = [r['nodes'].get(name, {}) for r in part if 'nodes' in r]
            fps = mean([x.get('frames', 0) / 60 for x in nodes]) if any('frames' in x for x in nodes) else None
            stage = next((s for s in ('detect', 'infer', 'jpeg', 'encode', 'decode-hw', 'decode') if any(s in x for x in nodes)), None)
            ms = mean([x[stage][0] for x in nodes if stage in x]) if stage else None
            mx = max((x[stage][2] for x in nodes if stage in x), default=0) if stage else None
            cells.append('%5s %s %5s %6s' % ('-' if fps is None else '%.1f' % fps, (stage or '')[:9].ljust(9), '-' if ms is None else '%.1f' % ms,
                                              '-' if mx is None else '%.0f' % mx))
        print('  %-18s %s' % (name, ' || '.join(cells)))
    zero = collections.Counter(name for r in rows for name, x in r.get('nodes', {}).items() if 'frames' in x and x['frames'] == 0)
    if zero:
        print('  minutes with no frames: %s' % dict(zero))

    mj = [r.get('mjpeg', [[0, 0, 0]] * 2) for r in rows]
    print('\nMJPEG pollers: frames %s, empty %s, bad %s; snapshots ok %d, failed %d; recordings now %d MB in %d files, disk free %d GB' % (
        [sum(m[i][0] for m in mj) for i in range(2)], [sum(m[i][1] for m in mj) for i in range(2)], [sum(m[i][2] for m in mj) for i in range(2)],
        sum(r['snapshots'][0] for r in rows), sum(r['snapshots'][1] for r in rows), rows[-1]['rec_mb'], rows[-1]['rec_files'], rows[-1]['disk_free_gb']))
    print('errors: journal lines %d (in %d minutes), service log errors/warnings %d, kernel messages %d (in %d minutes)' % (
        sum(r.get('journal_bad', 0) for r in rows), sum(1 for r in rows if r.get('journal_bad')), sum(r.get('log_errors') or 0 for r in rows),
        sum(r.get('kernel_msgs', 0) for r in rows), sum(1 for r in rows if r.get('kernel_msgs'))))

    kinds = collections.Counter()
    for line in open(os.path.join(out, 'events.log')):
        text = line.split(' ', 1)[1].strip() if ' ' in line else line
        text = re.sub(r'\[\s*\d+\.\d+\]', '', text)
        text = re.sub(r'\b\d+\b', 'N', text)
        kinds[text[:150]] += 1
    print('\nevent kinds (numbers masked; the soak logs at most 5 lines of each source a minute):')
    for k, c in kinds.most_common(15):
        print('  %5d  %s' % (c, k))

    if len(sys.argv) > 2 and os.path.exists(sys.argv[2]):
        v = [json.loads(l) for l in open(sys.argv[2]) if l.strip()]
        mins = [x for x in v if 'state' in x]
        if mins:
            print('\nWebRTC viewer: %d minutes, connected %d, frames/min mean %.0f (min %d), reconnects %d, packets lost %s, freezes %s' % (
                len(mins), sum(1 for x in mins if x['state'] == 'connected'), mean([x['framesPerMin'] for x in mins]),
                min(x['framesPerMin'] for x in mins), mins[-1]['reconnects'], mins[-1].get('lost'), mins[-1].get('freezes')))


if __name__ == '__main__':
    main()
