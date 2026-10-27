#!/usr/bin/env python3
"""Two-camera load and behaviour test, run on the Pi. Drives the REST API, samples the accelerators and the dma-buf accounting, prints a report."""
import json, os, re, subprocess, sys, threading, time, urllib.request, urllib.parse

A = 'http://127.0.0.1:5800/api'
LENOVO = '/dev/v4l/by-path/platform-fc880000.usb-usb-0:1:1.0-video-index0'
VK_EXTRA = {'refineEdges': 'true', 'refineMode': 'REFINE_ULTRAFAST', 'probeMargin': 4} if 'ultrafast' in sys.argv else {}
STEREO_SOURCE_ID = 50123440  # the existing LenovoCamera0 source, which currently holds the stereo camera


def req(method, path, body=None, headers=None, timeout=30):
    data = None
    h = dict(headers or {})
    if body is not None:
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        h.setdefault('Content-Type', 'application/json')
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


def pid():
    return sh('pgrep -x Server | head -1').strip()


def dma():
    out = sudo('tail -3 /sys/kernel/debug/dma_buf/bufinfo')
    m = re.search(r'Total (\d+) objects, (\d+) bytes', out)
    return (int(m.group(1)), int(m.group(2)) // 1024) if m else (-1, -1)


def fds():
    p = pid()
    return int(sh('ls -l /proc/%s/fd 2>/dev/null | grep -c dmabuf' % p).strip() or 0), int(sh('ls /proc/%s/fd | wc -l' % p).strip() or 0)


def rss_mb():
    return int(sh("awk '/VmRSS/ {print $2}' /proc/%s/status" % pid()).strip() or 0) // 1024


class Sampler(threading.Thread):
    """1 Hz samples of RGA and MPP load, CPU frequency and the hottest SoC temperature while a phase runs."""
    def __init__(self):
        super().__init__(daemon=True)
        self.stop = False
        self.rga, self.mpp, self.temp, self.dma, self.gpu, self.cpu = [], [], [], [], [], []
        self.last_stat = None

    def run(self):
        while not self.stop:
            rga = sudo('cat /sys/kernel/debug/rkrga/load')
            self.rga.append([int(x) for x in re.findall(r'load = (\d+)%', rga)])
            mpp = sudo('cat /proc/mpp_service/load')
            self.mpp.append(dict((k.split('.')[-1] + '@' + k.split('.')[0][-4:], float(v)) for k, v in re.findall(r'(\S+)\s+load:\s+([\d.]+)%', mpp)))
            self.dma.append(dma()[0])
            self.gpu.append(int((sh('cat /sys/class/devfreq/fb000000.gpu/load').split('@')[0] or '0').strip() or 0))
            stat = {}
            for line in sh("grep '^cpu[0-9]' /proc/stat").splitlines():
                f = line.split()
                vals = [int(x) for x in f[1:9]]
                stat[int(f[0][3:])] = (sum(vals) - vals[3] - vals[4], sum(vals))
            if self.last_stat:
                busy = {}
                for cluster, cpus in (('A55', range(0, 4)), ('A76', range(4, 8))):
                    b = sum(stat[c][0] - self.last_stat[c][0] for c in cpus)
                    t = sum(stat[c][1] - self.last_stat[c][1] for c in cpus)
                    busy[cluster] = 100.0 * b / t if t else 0
                self.cpu.append(busy)
            self.last_stat = stat
            temps = [int(x) for x in sh('cat /sys/class/thermal/thermal_zone*/temp').split()]
            self.temp.append(max(temps) / 1000.0 if temps else 0)
            time.sleep(1)


def summarise_samples(s):
    out = {}
    if s.rga and s.rga[0]:
        cols = list(zip(*[r for r in s.rga if len(r) == len(s.rga[0])]))
        out['rga load % (rga3, rga3, rga2) mean/max'] = ['%.0f/%d' % (sum(c) / len(c), max(c)) for c in cols]
    keys = set(k for m in s.mpp for k in m)
    busy = {k: (sum(m.get(k, 0) for m in s.mpp) / len(s.mpp), max(m.get(k, 0) for m in s.mpp)) for k in keys}
    out['mpp load % mean/max'] = {k: '%.1f/%.0f' % v for k, v in sorted(busy.items()) if v[1] > 0}
    out['dma-buf objects over the phase min..max'] = '%d..%d' % (min(s.dma), max(s.dma)) if s.dma else ''
    out['gpu load % mean/max'] = '%.0f/%d' % (sum(s.gpu) / len(s.gpu), max(s.gpu)) if s.gpu else ''
    if s.cpu:
        out['cpu busy % A55 / A76 (mean)'] = '%.0f / %.0f' % (sum(c['A55'] for c in s.cpu) / len(s.cpu), sum(c['A76'] for c in s.cpu) / len(s.cpu))
    out['max temp C'] = max(s.temp) if s.temp else 0
    return out


def profile():
    return {n['Name']: {s['Stage']: s for s in n['Stages']} for n in req('GET', '/profile')}


def show(names, stages, prof):
    for name in names:
        st = prof.get(name)
        if not st:
            print('   %-20s (no timings)' % name)
            continue
        cells = []
        for stage in stages:
            if stage in st:
                cells.append('%s %.2f/%.2f' % (stage, st[stage]['MeanMs'], st[stage]['P95Ms']))
        print('   %-20s %s' % (name, '  '.join(cells)))


def dmesg_count():
    return int(sudo('dmesg | wc -l').strip() or 0)


def dmesg_new(since):
    out = sudo('dmesg | tail -n +%d' % (since + 1))
    return [l for l in out.splitlines() if re.search(r'rga|mpp|iommu|dma-buf|dma_buf|rkvdec|rknpu|vpu|jpeg|DMA|unsupported|failed|error|timeout|hung|reset', l, re.I)]


def create_camera(name, path):
    return int(req('POST', '/cameraSource/create?name=' + name, {'Name': name, 'Path': path}))


def set_mode(src, w, h, fps, fmt=5):
    return req('PATCH', '/cameraSource/%d/mode' % src, {'Width': w, 'Height': h, 'Fps': fps, 'PixelFormat': fmt, 'IsNative': False})


def vk(name, src, w, h):
    sid = int(req('POST', '/apriltagSink/createWithBackend?' + urllib.parse.urlencode(
        dict({'name': name, 'tagSize': 0.1651, 'backend': 'APRILTAG_BACKEND_VULKAN', 'frameWidth': w, 'frameHeight': h}, **VK_EXTRA))))
    req('PATCH', '/sink/bind?SinkID=%d&SourceID=%d' % (sid, src))
    req('PATCH', '/sink/toggle?SinkID=%d&Enabled=true' % sid)
    return sid


def npu(name, src):
    sid = int(req('POST', '/objectDetectionSink/create?name=%s&modelId=1' % name))
    req('PATCH', '/sink/bind?SinkID=%d&SourceID=%d' % (sid, src))
    req('PATCH', '/sink/toggle?SinkID=%d&Enabled=true' % sid)
    return sid


def record(name, src, folder):
    sid = int(req('POST', '/recordSink/create?' + urllib.parse.urlencode({'name': name, 'dstFolder': folder, 'segmentSeconds': 60})))
    req('PATCH', '/sink/bind?SinkID=%d&SourceID=%d' % (sid, src))
    req('PATCH', '/sink/toggle?SinkID=%d&Enabled=true' % sid)
    return sid


def mjpeg(name, src):
    sid = int(req('POST', '/mjpegSink/create?' + urllib.parse.urlencode({'name': name})))
    req('PATCH', '/sink/bind?SinkID=%d&SourceID=%d' % (sid, src))
    req('PATCH', '/sink/toggle?SinkID=%d&Enabled=true' % sid)
    return sid


class Poller(threading.Thread):
    """Polls an MJPEG sink at 10 fps like a browser tile, counting frames and bad responses."""
    def __init__(self, sink):
        super().__init__(daemon=True)
        self.sink, self.stop, self.frames, self.bad, self.sizes = sink, False, 0, 0, []

    def run(self):
        while not self.stop:
            try:
                with urllib.request.urlopen('http://127.0.0.1:5800/stream/mjpeg/frame?SinkID=%d' % self.sink, timeout=5) as r:
                    data = r.read()
                if r.status == 200 and data[:2] == b'\xff\xd8':
                    self.frames += 1
                    if len(self.sizes) < 3:
                        self.sizes.append(jpeg_size(data))
                elif r.status != 204:
                    self.bad += 1
            except Exception:
                self.bad += 1
            time.sleep(0.1)


def jpeg_size(data):
    i = 2
    while i + 9 < len(data):
        if data[i] != 0xFF:
            i += 1
            continue
        marker = data[i + 1]
        if marker in (0xC0, 0xC1, 0xC2):
            return (int.from_bytes(data[i + 7:i + 9], 'big'), int.from_bytes(data[i + 5:i + 7], 'big'))
        i += 2 + int.from_bytes(data[i + 2:i + 4], 'big')
    return None


def phase(title, names, seconds=45):
    print('\n=== %s' % title)
    req('POST', '/profile/reset')
    base = dmesg_count()
    sampler = Sampler()
    sampler.start()
    time.sleep(seconds)
    sampler.stop = True
    sampler.join()
    prof = profile()
    show(names, ['decode', 'decode-hw', 'detect', 'vk-gpu:submit+wait', 'vk-quads', 'vk-tags', 'infer', 'rknn-rga', 'rknn-run', 'convert', 'encode', 'age', 'interval'], prof)
    for k, v in summarise_samples(sampler).items():
        print('   %s: %s' % (k, v))
    n, kb = dma()
    d, total = fds()
    print('   dma-buf objects %d (%d KiB), process dma-buf fds %d of %d, RSS %d MiB' % (n, kb, d, total, rss_mb()))
    new = dmesg_new(base)
    print('   kernel messages of interest: %s' % ('none' if not new else ''))
    for l in new[:8]:
        print('     ' + l[:200])
    return prof


def main():
    print('Pi: %s   VK sinks: %s' % (sh('uname -r').strip(), VK_EXTRA or 'default tuning'))
    sudo('echo 1000 > /proc/mpp_service/load_interval')
    original_mode = req('GET', '/cameraSource/%d/currentMode' % STEREO_SOURCE_ID)
    created_sinks, created_sources, pollers = [], [], []
    base_dma, base_fds = dma(), fds()
    print('baseline: dma-buf %s, process fds %s, RSS %d MiB' % (base_dma, base_fds, rss_mb()))
    sud = sh('ls /tmp/mc-rec* -d 2>/dev/null')
    sh('rm -rf /tmp/mc-rec-*')

    try:
        # ---- phase 1: the Lenovo camera alone with two detectors
        lenovo = create_camera('mc-lenovo', LENOVO)
        created_sources.append(lenovo)
        set_mode(lenovo, 1280, 720, 30)
        created_sinks += [vk('mc-lenovo-vk', lenovo, 1280, 720), npu('mc-lenovo-npu', lenovo)]
        time.sleep(4)
        phase('1. Lenovo camera 1280x720@30: Vulkan AprilTag + YOLO11', ['mc-lenovo', 'mc-lenovo-vk', 'mc-lenovo-npu'])

        # ---- phase 2: the stereo camera side by side at 2560x720@60, split into two halves, each with detectors
        print('\n(stereo camera: %s)' % req('GET', '/cameraSource/%d/currentMode' % STEREO_SOURCE_ID))
        ok = set_mode(STEREO_SOURCE_ID, 2560, 720, 60)
        time.sleep(3)
        print('(stereo mode now %s)' % req('GET', '/cameraSource/%d/currentMode' % STEREO_SOURCE_ID))
        halves = req('POST', '/cameraSource/%d/split' % STEREO_SOURCE_ID)
        created_sources += halves
        left, right = halves
        if 'limit30' in sys.argv:
            for half in halves:
                req('PATCH', '/source/fpsLimit?SourceID=%d&fps=30' % half)
            print('(the two halves are limited to 30 fps)')
        created_sinks += [vk('mc-left-vk', left, 1280, 720), vk('mc-right-vk', right, 1280, 720), npu('mc-left-npu', left)]
        time.sleep(4)
        names = ['LenovoCamera0', 'mc-lenovo', 'mc-lenovo-vk', 'mc-lenovo-npu', 'mc-left-vk', 'mc-right-vk', 'mc-left-npu']
        phase('2. + stereo camera 2560x720@60 split into halves: 2x Vulkan, 1x YOLO11 on the halves', names)

        # ---- phase 3: add encoders and tile polling
        rec_l = record('mc-rec-lenovo', lenovo, '/tmp/mc-rec-lenovo')
        rec_s = record('mc-rec-stereo', STEREO_SOURCE_ID, '/tmp/mc-rec-stereo')
        mj_l = mjpeg('mc-mjpeg-lenovo-vk', created_sinks[0])
        mj_r = mjpeg('mc-mjpeg-right-vk', created_sinks[3])
        created_sinks += [rec_l, rec_s, mj_l, mj_r]
        for sink in (mj_l, mj_r):
            p = Poller(sink)
            p.start()
            pollers.append(p)
        time.sleep(4)
        prof = phase('3. + two recordings (encoders) and two MJPEG tiles polled at 10 fps', names + ['mc-rec-lenovo', 'mc-rec-stereo', 'mc-mjpeg-lenovo-vk', 'mc-mjpeg-right-vk'])
        for p, label in zip(pollers, ('lenovo-vk tile', 'right-vk tile')):
            print('   %s: %d frames, %d bad responses, first frame sizes %s' % (label, p.frames, p.bad, p.sizes))

        # ---- behaviour: transforms and snapshots on NV12 frames
        print('\n=== 4. behaviour on NV12 frames: transform, snapshot, crop')
        req('PATCH', '/cameraSource/%d/transform' % lenovo, {'Rotation': 90, 'FlipHorizontal': True, 'FlipVertical': False, 'CropX': 0, 'CropY': 0, 'CropWidth': 0, 'CropHeight': 0})
        time.sleep(3)
        snap = req('POST', '/snapshot/take?NodeID=%d' % lenovo)
        print('   rotated+mirrored camera snapshot: %s' % snap)
        req('PATCH', '/cameraSource/%d/transform' % lenovo, {'Rotation': 0, 'FlipHorizontal': False, 'FlipVertical': False, 'CropX': 100, 'CropY': 50, 'CropWidth': 640, 'CropHeight': 360})
        time.sleep(3)
        snap2 = req('POST', '/snapshot/take?NodeID=%d' % lenovo)
        print('   cropped camera snapshot: %s' % snap2)
        req('PATCH', '/cameraSource/%d/transform' % lenovo, {'Rotation': 0, 'FlipHorizontal': False, 'FlipVertical': False, 'CropX': 0, 'CropY': 0, 'CropWidth': 0, 'CropHeight': 0})
        snap3 = req('POST', '/snapshot/take?NodeID=%d' % created_sinks[0])
        print('   annotated detector snapshot: %s' % snap3)
        snaps = [s for s in (snap, snap2, snap3) if isinstance(s, str) and s]
        for s in snaps:
            print('   saved', s)
        time.sleep(4)
        tail = profile()
        show(['mc-lenovo', 'mc-lenovo-vk'], ['decode', 'detect', 'age'], tail)
    finally:
        for p in pollers:
            p.stop = True
        print('\n=== teardown')
        for sid in reversed(created_sinks):
            try:
                req('DELETE', '/sink/delete?SinkID=%d' % sid)
            except Exception as e:
                print('   could not delete sink', sid, e)
        for sid in reversed(created_sources):
            try:
                req('DELETE', '/source/delete?SourceID=%d' % sid)
            except Exception as e:
                print('   could not delete source', sid, e)
        try:
            set_mode(STEREO_SOURCE_ID, original_mode['Width'], original_mode['Height'], original_mode['Fps'], original_mode['PixelFormat'])
        except Exception as e:
            print('   could not restore the stereo mode', e)
        time.sleep(20)
        n, kb = dma()
        d, total = fds()
        print('   after teardown: dma-buf objects %d (%d KiB) [baseline %s], process dma-buf fds %d [baseline %s], RSS %d MiB' % (n, kb, base_dma, d, base_fds[0], rss_mb()))
        print('   stereo mode restored to %s' % req('GET', '/cameraSource/%d/currentMode' % STEREO_SOURCE_ID))
        sh('rm -rf /tmp/mc-rec-*')


main()
