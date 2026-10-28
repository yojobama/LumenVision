#!/usr/bin/env python3
"""Long soak test on the Pi: builds a graph that loads every accelerator and pipeline at once, holds it for HOURS, samples the service and the board every
minute, and tears down what it built. Usage: soak.py HOURS OUTDIR [--keep]   (stdlib only; needs passwordless sudo for debugfs and dmesg)

Graph (every node it creates is named soak-*; the graph that was there before is left alone and is not counted):
  stereo camera 2560x720@60 MJPEG, split into halves
    left half  -> CPU AprilTag (libapriltag)         -> MJPEG preview, polled at 10 fps
    left half  -> RKNN detector, model 2 (YOLO11n COCO)
    right half -> Vulkan AprilTag (vkapriltag)       -> WebRTC preview (a viewer is run from another machine)
    whole      -> H.264 recording, 60 s segments, folder capped at 8 GB
  Lenovo camera (the existing source)
    -> RKNN detector, model 1 (apriltagV4)           -> MJPEG preview, polled at 10 fps
    -> H.264 recording, 60 s segments, folder capped at 8 GB
    -> a snapshot every 60 s (taken and deleted)
Every minute, one JSON line in OUTDIR/samples.jsonl: service PID/restarts/RSS/threads/fds, dma-buf totals, service CPU, cluster load, clocks,
temperatures, GPU/NPU/memory load, per-node frame counts and stage timings for that minute, MJPEG poller and snapshot counters, recording disk use,
error/warning lines logged by the service, kernel messages of interest, and the API's response time.
"""
import json, os, re, signal, subprocess, sys, threading, time, urllib.parse, urllib.request

A = 'http://127.0.0.1:5800/api'
STEREO_PATH = '/dev/v4l/by-path/platform-xhci-hcd.12.auto-usb-0:1.4:1.0-video-index0'
LENOVO_NAME = 'Lenovo0'
REC_ROOT = '/home/lumen/soak-rec'
REC_CAP = 8 * 1024 ** 3
VK_TUNING = {'refineEdges': 'true', 'refineMode': 'REFINE_ULTRAFAST', 'probeMargin': 4}


def req(method, path, body=None, timeout=30):
    data, h = None, {}
    if body is not None:
        data = json.dumps(body).encode()
        h['Content-Type'] = 'application/json'
    r = urllib.request.Request(A + path, data=data, method=method, headers=h)
    with urllib.request.urlopen(r, timeout=timeout) as resp:
        text = resp.read()
    if not text:
        return None
    try:
        return json.loads(text)
    except ValueError:
        return text.decode(errors='replace')


def sh(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True).stdout


def sudo(cmd):
    return sh('sudo -n sh -c %s' % json.dumps(cmd))


def read(path, default=''):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return default


def wait_for_api(seconds=120):
    for _ in range(seconds):
        try:
            req('GET', '/device/version', timeout=5)
            return True
        except Exception:
            time.sleep(1)
    return False


# ---------------------------------------------------------------- graph

def bind_enable(sid, src):
    req('PATCH', '/sink/bind?SinkID=%d&SourceID=%d' % (sid, src))
    req('PATCH', '/sink/toggle?SinkID=%d&Enabled=true' % sid)
    return sid


def apriltag(name, src, w, h, backend, extra=None):
    q = dict({'name': name, 'tagSize': 0.1651, 'backend': backend, 'frameWidth': w, 'frameHeight': h}, **(extra or {}))
    return bind_enable(int(req('POST', '/apriltagSink/createWithBackend?' + urllib.parse.urlencode(q))), src)


def detector(name, src, model):
    return bind_enable(int(req('POST', '/objectDetectionSink/create?name=%s&modelId=%d' % (name, model))), src)


def record(name, src, folder):
    q = {'name': name, 'dstFolder': folder, 'segmentSeconds': 60, 'maxFolderSizeBytes': REC_CAP}
    return bind_enable(int(req('POST', '/recordSink/create?' + urllib.parse.urlencode(q))), src)


def mjpeg(name, src):
    return bind_enable(int(req('POST', '/mjpegSink/create?' + urllib.parse.urlencode({'name': name}))), src)


def webrtc(name, src):
    return bind_enable(int(req('POST', '/webrtcSink/create?' + urllib.parse.urlencode({'name': name}))), src)


def lenovo_source_id():
    data = json.loads(sudo('cat /opt/lumenvision/data.json'))
    for s in data['Sinks']:
        src = s.get('Source') or {}
        if src.get('Name') == LENOVO_NAME:
            return src['Id']
    for s in data['Sources']:
        if s.get('Name') == LENOVO_NAME:
            return s['Id']
    raise SystemExit('no %s source in the graph' % LENOVO_NAME)


def build():
    ids = {'sources': [], 'sinks': {}}
    lenovo = lenovo_source_id()
    stereo = int(req('POST', '/cameraSource/create?name=soak-stereo', {'Name': 'soak-stereo', 'Path': STEREO_PATH}))
    ids['sources'].append(stereo)
    req('PATCH', '/cameraSource/%d/mode' % stereo, {'Width': 2560, 'Height': 720, 'Fps': 60, 'PixelFormat': 5, 'IsNative': False})
    time.sleep(2)
    left, right = req('POST', '/cameraSource/%d/split' % stereo)
    ids['sources'] += [left, right]
    s = ids['sinks']
    s['soak-cpu-tags'] = apriltag('soak-cpu-tags', left, 1280, 720, 'APRILTAG_BACKEND_CPU')
    s['soak-vk-tags'] = apriltag('soak-vk-tags', right, 1280, 720, 'APRILTAG_BACKEND_VULKAN', VK_TUNING)
    s['soak-npu-coco'] = detector('soak-npu-coco', left, 2)
    s['soak-npu-tags'] = detector('soak-npu-tags', lenovo, 1)
    s['soak-rec-stereo'] = record('soak-rec-stereo', stereo, REC_ROOT + '/stereo')
    s['soak-rec-lenovo'] = record('soak-rec-lenovo', lenovo, REC_ROOT + '/lenovo')
    s['soak-mjpeg-cpu'] = mjpeg('soak-mjpeg-cpu', s['soak-cpu-tags'])
    s['soak-mjpeg-npu'] = mjpeg('soak-mjpeg-npu', s['soak-npu-tags'])
    s['soak-webrtc'] = webrtc('soak-webrtc', s['soak-vk-tags'])
    ids['lenovo'] = lenovo
    return ids


def teardown(ids):
    # by name too, in case the service restarted and the ids changed
    for sid in reversed(list(ids['sinks'].values())):
        try: req('DELETE', '/sink/delete?SinkID=%d' % sid)
        except Exception: pass
    for sid in reversed(ids['sources']):
        try: req('DELETE', '/source/delete?SourceID=%d' % sid)
        except Exception: pass


# ---------------------------------------------------------------- load generators

class Poller(threading.Thread):
    """Fetches an MJPEG sink's latest frame at 10 fps like a browser tile."""
    def __init__(self, sink):
        super().__init__(daemon=True)
        self.sink, self.stop, self.frames, self.empty, self.bad = sink, False, 0, 0, 0

    def run(self):
        while not self.stop:
            try:
                with urllib.request.urlopen('http://127.0.0.1:5800/stream/mjpeg/frame?SinkID=%d' % self.sink, timeout=5) as r:
                    data = r.read()
                if r.status == 200 and data[:2] == b'\xff\xd8' and data[-2:] == b'\xff\xd9':
                    self.frames += 1
                elif r.status == 204:
                    self.empty += 1
                else:
                    self.bad += 1
            except Exception:
                self.bad += 1
            time.sleep(0.1)


class Snapshotter(threading.Thread):
    """Takes a snapshot of a node every 60 s and deletes it again."""
    def __init__(self, node):
        super().__init__(daemon=True)
        self.node, self.stop, self.ok, self.bad = node, False, 0, 0

    def run(self):
        while not self.stop:
            try:
                path = req('POST', '/snapshot/take?NodeID=%d' % self.node, timeout=20)
                if isinstance(path, str) and path:
                    req('DELETE', '/snapshot?Path=' + urllib.parse.quote(path))
                    self.ok += 1
                else:
                    self.bad += 1
            except Exception:
                self.bad += 1
            for _ in range(60):
                if self.stop: return
                time.sleep(1)


# ---------------------------------------------------------------- sampling

def service_pid():
    return sh('systemctl show -p MainPID --value lumenvision').strip()


def proc_stat(pid):
    try:
        f = open('/proc/%s/stat' % pid).read().rsplit(')', 1)[1].split()
        return int(f[11]) + int(f[12])
    except OSError:
        return 0


def proc_status(pid):
    out = {}
    for line in read('/proc/%s/status' % pid).splitlines():
        k, _, v = line.partition(':')
        if k in ('VmRSS', 'VmSize', 'Threads', 'RssAnon', 'RssFile'):
            out[k] = int(v.split()[0])
    return out


def cpu_times():
    cores = {}
    for line in read('/proc/stat').splitlines():
        m = re.match(r'cpu(\d+) (.*)', line)
        if m:
            v = list(map(int, m.group(2).split()))
            cores[int(m.group(1))] = (sum(v), v[3] + v[4])
    return cores


def busy(prev, cur, cores):
    tot = sum(cur[c][0] - prev[c][0] for c in cores)
    idle = sum(cur[c][1] - prev[c][1] for c in cores)
    return round(100.0 * (tot - idle) / tot, 1) if tot else 0


def temps():
    out = {}
    for z in sorted(os.listdir('/sys/class/thermal')):
        if z.startswith('thermal_zone'):
            t = read('/sys/class/thermal/%s/temp' % z)
            if t.isdigit():
                out[read('/sys/class/thermal/%s/type' % z)] = int(t) // 1000
    return out


def devfreq():
    out = {}
    for d in os.listdir('/sys/class/devfreq'):
        name = {'fb000000.gpu': 'gpu', 'dmc': 'dmc', 'fdab0000.npu': 'npu'}.get(d)
        if name:
            out[name + '_mhz'] = int(read('/sys/class/devfreq/%s/cur_freq' % d, '0') or 0) // 1000000
            load = read('/sys/class/devfreq/%s/load' % d)
            m = re.match(r'(\d+)@', load)
            if m: out[name + '_load'] = int(m.group(1))
    return out


def dma_totals():
    m = re.search(r'Total (\d+) objects, (\d+) bytes', sudo('tail -3 /sys/kernel/debug/dma_buf/bufinfo'))
    return (int(m.group(1)), int(m.group(2)) // 1048576) if m else (-1, -1)


def fds(pid):
    try:
        entries = os.listdir('/proc/%s/fd' % pid)
    except OSError:
        return -1, -1
    names = []
    for f in entries:
        try:
            names.append(os.readlink('/proc/%s/fd/%s' % (pid, f)))
        except OSError:
            pass  # closed since the listing
    return len(names), sum(1 for n in names if 'dmabuf' in n)


def dir_use(path):
    total, files = 0, 0
    for root, _, names in os.walk(path):
        for n in names:
            try:
                total += os.path.getsize(os.path.join(root, n)); files += 1
            except OSError:
                pass
    return total // 1048576, files


KERNEL_RE = re.compile(r'rga|mpp|iommu|dma.?buf|rkvdec|rkvenc|jpeg|rknpu|mali|panfrost|uvc|usb .*(disconnect|reset)|xhci|oom|segfault|'
                       r'hung|timeout|watchdog|throttl|over.?temp|error|fail', re.I)


def run(hours, outdir, keep):
    os.makedirs(outdir, exist_ok=True)
    samples = open(os.path.join(outdir, 'samples.jsonl'), 'a')
    events = open(os.path.join(outdir, 'events.log'), 'a')

    def event(text):
        events.write('%s %s\n' % (time.strftime('%Y-%m-%dT%H:%M:%S'), text)); events.flush()

    if not wait_for_api():
        raise SystemExit('service API not reachable')
    sudo('echo 60000 > /proc/mpp_service/load_interval')
    ids = build()
    json.dump(ids, open(os.path.join(outdir, 'graph.json'), 'w'))
    event('built graph %s' % json.dumps(ids))
    pollers = [Poller(ids['sinks']['soak-mjpeg-cpu']), Poller(ids['sinks']['soak-mjpeg-npu'])]
    snap = Snapshotter(ids['lenovo'])
    for t in pollers + [snap]:
        t.start()

    stop = {'flag': False}
    signal.signal(signal.SIGTERM, lambda *_: stop.update(flag=True))
    signal.signal(signal.SIGINT, lambda *_: stop.update(flag=True))

    end = time.time() + hours * 3600
    pid = service_pid()
    restarts0 = int(sh('systemctl show -p NRestarts --value lumenvision').strip() or 0)
    jif, cpu = proc_stat(pid), cpu_times()
    dmesg_mark = int(sudo('dmesg | wc -l').strip() or 0)
    journal_since = time.strftime('%Y-%m-%d %H:%M:%S')
    last = {'t': time.time(), 'poll': [(p.frames, p.empty, p.bad) for p in pollers], 'snap': (snap.ok, snap.bad)}
    time.sleep(5)
    req('POST', '/profile/reset')
    minute = 0
    while not stop['flag'] and time.time() < end:
        for _ in range(60):
            if stop['flag']: break
            time.sleep(1)
        minute += 1
        now = time.time()
        s = {'t': time.strftime('%Y-%m-%dT%H:%M:%S'), 'minute': minute}
        cur_pid = service_pid()
        if cur_pid != pid:
            event('service PID changed %s -> %s' % (pid, cur_pid))
            s['pid_changed'] = True
            pid, jif = cur_pid, proc_stat(cur_pid)
        s['restarts'] = int(sh('systemctl show -p NRestarts --value lumenvision').strip() or 0) - restarts0
        s['active'] = sh('systemctl is-active lumenvision').strip()
        st = proc_status(pid)
        s['rss_mb'], s['vsz_mb'], s['threads'] = st.get('VmRSS', 0) // 1024, st.get('VmSize', 0) // 1024, st.get('Threads', 0)
        s['anon_mb'] = st.get('RssAnon', 0) // 1024
        s['fds'], s['dmabuf_fds'] = fds(pid)
        s['dmabuf_objects'], s['dmabuf_mb'] = dma_totals()
        j = proc_stat(pid)
        s['service_cores'] = round((j - jif) / 100.0 / (now - last['t']), 3)
        jif = j
        c = cpu_times()
        s['busy_a55'], s['busy_a76'] = busy(cpu, c, [0, 1, 2, 3]), busy(cpu, c, [4, 5, 6, 7])
        cpu = c
        s['mhz_a55'] = int(read('/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq', '0')) // 1000
        s['mhz_a76'] = [int(read('/sys/devices/system/cpu/cpufreq/policy%d/scaling_cur_freq' % p, '0')) // 1000 for p in (4, 6)]
        s['gov_a76'] = read('/sys/devices/system/cpu/cpufreq/policy4/scaling_governor')
        s['temp'] = temps()
        s.update(devfreq())
        npu = sudo('cat /sys/kernel/debug/rknpu/load')
        s['npu_load'] = [int(x) for x in re.findall(r'(\d+)%', npu)]
        s['mem_avail_mb'] = int(re.search(r'MemAvailable:\s+(\d+)', read('/proc/meminfo')).group(1)) // 1024
        s['rec_mb'], s['rec_files'] = dir_use(REC_ROOT)
        s['disk_free_gb'] = int(sh("df -BG --output=avail / | tail -1").strip().rstrip('G') or 0)
        t0 = time.time()
        try:
            prof = req('GET', '/profile', timeout=20)
            s['api_ms'] = round((time.time() - t0) * 1000)
            nodes = {}
            for n in prof:
                if not n['Name'].startswith('soak-') and n['Name'] != LENOVO_NAME:
                    continue
                stages = {x['Stage']: x for x in n['Stages']}
                row = {}
                if 'interval' in stages:
                    row['frames'] = stages['interval']['Count']
                for k in ('decode', 'decode-hw', 'detect', 'vk-gpu:submit+wait', 'infer', 'convert', 'encode', 'jpeg', 'age', 'process'):
                    if k in stages and stages[k]['Count']:
                        row[k] = [round(stages[k]['MeanMs'], 2), round(stages[k]['P95Ms'], 2), round(stages[k]['MaxMs'], 1)]
                nodes[n['Name']] = row
            s['nodes'] = nodes
            req('POST', '/profile/reset')
        except Exception as e:
            s['api_error'] = str(e)[:200]
            event('API error: %s' % e)
        p = [(x.frames, x.empty, x.bad) for x in pollers]
        s['mjpeg'] = [[a[0] - b[0], a[1] - b[1], a[2] - b[2]] for a, b in zip(p, last['poll'])]
        s['snapshots'] = [snap.ok - last['snap'][0], snap.bad - last['snap'][1]]
        try:
            ws = req('GET', '/webrtcSink/status?sinkId=%d' % ids['sinks']['soak-webrtc'], timeout=10)
            s['webrtc_connected'] = bool(ws.get('Connected'))
        except Exception:
            s['webrtc_connected'] = None
        last.update(t=now, poll=p, snap=(snap.ok, snap.bad))
        journal = sudo("journalctl -u lumenvision --since '%s' --no-pager -o cat" % journal_since)
        journal_since = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(now))
        bad = [l for l in journal.splitlines() if re.search(r'error|warn|fail|exception|fatal|abort|segfault|core dump', l, re.I)
               and not re.search(r'pam_unix|sudo', l)]
        s['journal_bad'] = len(bad)
        for l in bad[:5]:
            event('journal: ' + l[:300])
        try:
            logs = req('GET', '/log/tail', timeout=10)
            lines = [l for l in logs if l[:19] >= time.strftime('%Y-%m-%dT%H:%M:%S', time.gmtime(now - 60))]
            errs = [l for l in lines if '[ERROR]' in l or '[WARN' in l or 'Unhandled' in l]
            s['log_errors'] = len(errs)
            for l in errs[:5]:
                event('log: ' + l[:300])
        except Exception:
            s['log_errors'] = None
        total = int(sudo('dmesg | wc -l').strip() or 0)
        if total < dmesg_mark:
            dmesg_mark = 0
        kern = [l for l in sudo('dmesg | tail -n +%d' % (dmesg_mark + 1)).splitlines() if KERNEL_RE.search(l)]
        dmesg_mark = total
        s['kernel_msgs'] = len(kern)
        for l in kern[:5]:
            event('kernel: ' + l[:300])
        samples.write(json.dumps(s) + '\n'); samples.flush()

    for t in pollers + [snap]:
        t.stop = True
    event('finished after %d minutes%s' % (minute, ' (stopped)' if stop['flag'] else ''))
    if not keep:
        teardown(ids)
        sh('rm -rf %s' % REC_ROOT)
        event('graph torn down, recordings removed')


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(2)
    run(float(sys.argv[1]), sys.argv[2], '--keep' in sys.argv)
