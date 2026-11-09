#!/usr/bin/env python3
"""Resource use of one process (and the board) over a window, for comparing vision services. Run on the Pi:
  sysmon.py PATTERN SECONDS OUT.json      PATTERN is matched against the full command line (pgrep -f); the process with the most CPU time is taken
Writes one JSON object: the process's CPU in cores (user + system time over the window), its resident memory, the busy share of the little (A55) and big (A76)
core clusters, the GPU load and the hottest SoC temperature, each averaged over 1 s samples."""
import sys, time, json, subprocess, glob


def sh(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True).stdout


def pids(pattern):
    out = [int(p) for p in sh("pgrep -f '%s'" % pattern).split() if p.isdigit()]
    mine = int(sh('echo $PPID').strip() or 0)
    return [p for p in out if p != mine]


def proc_jiffies(pid):
    try:
        f = open('/proc/%d/stat' % pid).read().rsplit(')', 1)[1].split()
        return int(f[11]) + int(f[12])
    except Exception:
        return 0


def rss_mb(pid):
    try:
        for line in open('/proc/%d/status' % pid):
            if line.startswith('VmRSS'):
                return int(line.split()[1]) / 1024.0
    except Exception:
        pass
    return 0.0


def cpu_times():
    cores = {}
    for line in open('/proc/stat'):
        if line.startswith('cpu') and line[3].isdigit():
            p = line.split()
            vals = [int(x) for x in p[1:9]]
            cores[int(p[0][3:])] = (sum(vals), vals[3] + vals[4])  # total, idle+iowait
    return cores


def temps():
    best = 0.0
    for f in glob.glob('/sys/class/thermal/thermal_zone*/temp'):
        try:
            best = max(best, int(open(f).read()) / 1000.0)
        except Exception:
            pass
    return best


def gpu_load():
    for f in glob.glob('/sys/class/devfreq/*gpu*/load'):
        try:
            return int(open(f).read().split('@')[0])
        except Exception:
            pass
    return 0


def npu_load():
    # the NPU's three cores, from debugfs (root): "NPU load:  Core0: 12%, Core1:  0%, Core2:  0%,"
    import re
    loads = [int(x) for x in re.findall(r'(\d+)%', sh('sudo -n cat /sys/kernel/debug/rknpu/load 2>/dev/null'))]
    return sum(loads) / len(loads) if loads else 0.0


pattern, seconds, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
candidates = pids(pattern)
if not candidates:
    json.dump({'error': 'no process matches ' + pattern}, open(out, 'w'))
    sys.exit(1)
pid = max(candidates, key=proc_jiffies)
j0, t0 = proc_jiffies(pid), time.time()
c0 = cpu_times()
rss, gpu, temp, npu = [], [], [], []
while time.time() - t0 < seconds:
    time.sleep(1)
    rss.append(rss_mb(pid)); gpu.append(gpu_load()); temp.append(temps()); npu.append(npu_load())
j1, t1 = proc_jiffies(pid), time.time()
c1 = cpu_times()


def busy(cores):
    total = sum(c1[c][0] - c0[c][0] for c in cores)
    idle = sum(c1[c][1] - c0[c][1] for c in cores)
    return round(100.0 * (total - idle) / total, 1) if total else 0.0


result = {'pid': pid, 'seconds': round(t1 - t0, 1), 'process_cores': round((j1 - j0) / 100.0 / (t1 - t0), 3),
          'rss_mb': round(sum(rss) / len(rss), 0), 'npu_load_pct': round(sum(npu) / len(npu), 1), 'gpu_load_pct': round(sum(gpu) / len(gpu), 1), 'max_temp_c': max(temp),
          'busy_pct_A55': busy([0, 1, 2, 3]), 'busy_pct_A76': busy([4, 5, 6, 7]), 'busy_pct_all': busy(list(c1.keys()))}
json.dump(result, open(out, 'w'))
