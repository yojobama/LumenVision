#!/usr/bin/env python3
"""Summarises hwcnt_timeline CSVs (one row per sampling period, counters are deltas over the period; per-core counters are summed over
the Mali-G610's four shader cores). Usage: gpu_counters.py DIR [DIR...] where each DIR holds hw.csv and freq_start (Hz)."""
import csv, os, sys

CORES = 4


def summarise(d):
    rows = list(csv.DictReader(open(os.path.join(d, 'hw.csv'))))[1:]  # the first row covers the time before sampling started
    if len(rows) < 2:
        return None
    t = [int(r['t_ns']) for r in rows]
    seconds = (t[-1] - t[0]) / 1e9 * len(rows) / max(1, len(rows) - 1)
    tot = {k: sum(float(r[k]) for r in rows) for k in rows[0] if k != 't_ns'}
    freq = float(open(os.path.join(d, 'freq_start')).read().strip())
    active = tot['GPU active cycles']
    core = tot['Programmable core active cycles']
    out = {
        'seconds': round(seconds, 1),
        'gpu busy %': round(100 * active / (freq * seconds), 1),
        'shader cores busy % of GPU-active': round(100 * core / (CORES * active), 1) if active else 0,
        'issue starvation % of core-active': round(100 * tot['Instruction issue starvation cycles'] / core, 1) if core else 0,
        'arithmetic issue % of core-active': round(100 * tot['Arithmetic unit issue cycles'] / core, 1) if core else 0,
        'load/store issue % of core-active': round(100 * tot['Load/store unit issue cycles'] / core, 1) if core else 0,
        'partial reads % of LS reads': round(100 * tot['Load/store unit partial read issues'] / tot['Load/store unit read issues'], 1) if tot['Load/store unit read issues'] else 0,
        'atomics per s (M)': round(tot['Load/store unit atomic issues'] / seconds / 1e6, 1),
        'LS reads from L2 GB/s': round(tot['Load/store unit read bytes from L2 cache'] / seconds / 1e9, 2),
        'LS reads from DRAM GB/s': round(tot['Load/store unit read bytes from external memory'] / seconds / 1e9, 2),
        'GPU DRAM read GB/s': round(tot['Output external read bytes'] / seconds / 1e9, 2),
        'GPU DRAM write GB/s': round(tot['Output external write bytes'] / seconds / 1e9, 2),
        'L3 TLB misses per s (M)': round(tot['MMU level 3 lookup TLB misses'] / seconds / 1e6, 1),
        'TLB misses per 1000 L2 reads': round(1000 * tot['MMU level 3 lookup TLB misses'] / tot['L2 cache read lookups'], 1) if tot['L2 cache read lookups'] else 0,
        'diverged % of arithmetic instr': round(100 * tot['Diverged instructions'] / tot['Arithmetic instructions'], 2) if tot['Arithmetic instructions'] else 0,
        'compute tasks per s (k)': round(tot['Compute tasks'] / seconds / 1e3, 1),
    }
    return out


if __name__ == '__main__':
    res = {d.rstrip('/').split('/')[-1]: summarise(d) for d in sys.argv[1:]}
    keys = next(v for v in res.values() if v).keys()
    print('%-38s' % '' + ''.join('%14s' % k for k in res))
    for k in keys:
        print('%-38s' % k + ''.join('%14s' % (v[k] if v else '-') for v in res.values()))
