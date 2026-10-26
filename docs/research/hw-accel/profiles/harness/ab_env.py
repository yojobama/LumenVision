#!/usr/bin/env python3
"""Benchmark of the Vulkan AprilTag path on the Pi. Usage: ab_env.py V|W|B|C LABEL [NAME=VALUE ...]
V: one camera (1280x720@30) with one Vulkan detector.  W: three Vulkan detectors (that camera plus the stereo camera's two halves at 2560x720@60), nothing else.
B: W's three plus two YOLO detectors, two recordings and two MJPEG tiles polled at 10 fps.  C: B with the halves capped at 30 fps.
Optionally sets service environment variables (systemctl set-environment), restarts the service, builds the workload, measures service CPU and the stage timers for 40 s,
and prints one JSON line. All Vulkan sinks use the ultrafast refine preset with probeMargin 4."""
import sys
workload = sys.argv[1]
label = sys.argv[2]
env = [a for a in sys.argv[3:] if '=' in a]
sys.argv = ['x', 'ultrafast']
exec(open('/tmp/mc_test.py').read().split('\ndef main():')[0])
# V1 and W1 are V and W with the Vulkan detectors at decimation 1 (the default is 2)
variant = workload
if workload in ('V1', 'W1'):
    VK_EXTRA['quadDecimate'] = 1
    workload = workload[0]
    label = label + '-dec1'

STEREO_PATH = '/dev/v4l/by-path/platform-fc800000.usb-usb-0:1:1.0-video-index0'
SECONDS = 40


def jiffies(p):
    f = open('/proc/%s/stat' % p).read().rsplit(')', 1)[1].split()
    return int(f[11]) + int(f[12])  # utime + stime


def restart_service():
    for k in ('OPENCV_THREAD_POOL_ACTIVE_WAIT_WORKER', 'OPENCV_THREAD_POOL_ACTIVE_WAIT_MAIN'):
        sudo('systemctl unset-environment %s' % k)
    for e in env:
        sudo('systemctl set-environment %s' % e)
    sudo('systemctl restart lumenvision')
    for _ in range(90):
        time.sleep(1)
        try:
            req('GET', '/device/version')
            break
        except Exception:
            pass
    time.sleep(3)


def build():
    srcs, sinks, pollers = [], [], []
    lenovo = create_camera('ab-lenovo', LENOVO); srcs.append(lenovo)
    set_mode(lenovo, 1280, 720, 30)
    lenovoVk = vk('ab-lenovo-vk', lenovo, 1280, 720)
    sinks.append(lenovoVk)
    if workload == 'V':
        return srcs, sinks, pollers
    if workload in ('B', 'C'):
        sinks.append(npu('ab-lenovo-npu', lenovo))
    time.sleep(3)
    stereo = create_camera('ab-stereo', STEREO_PATH); srcs.append(stereo)
    set_mode(stereo, 2560, 720, 60)
    time.sleep(3)
    halves = req('POST', '/cameraSource/%d/split' % stereo)
    srcs += halves
    left, right = halves
    if workload == 'C':
        for h in halves:
            req('PATCH', '/source/fpsLimit?SourceID=%d&fps=30' % h)
    leftVk = vk('ab-left-vk', left, 1280, 720)
    rightVk = vk('ab-right-vk', right, 1280, 720)
    sinks += [leftVk, rightVk]
    if workload == 'W':
        return srcs, sinks, pollers
    sinks.append(npu('ab-left-npu', left))
    rec_l = record('ab-rec-lenovo', lenovo, '/tmp/ab-rec-lenovo')
    rec_s = record('ab-rec-stereo', stereo, '/tmp/ab-rec-stereo')
    mj_l = mjpeg('ab-mjpeg-l', lenovoVk)
    mj_r = mjpeg('ab-mjpeg-r', rightVk)
    sinks += [rec_l, rec_s, mj_l, mj_r]
    for s_ in (mj_l, mj_r):
        p = Poller(s_); p.start(); pollers.append(p)
    return srcs, sinks, pollers


def main():
    restart_service()
    srcs, sinks, pollers = [], [], []
    try:
        srcs, sinks, pollers = build()
        time.sleep(12)
        p = pid()
        req('POST', '/profile/reset')
        j0, t0 = jiffies(p), time.time()
        sampler = Sampler(); sampler.start()
        time.sleep(SECONDS)
        sampler.stop = True; sampler.join()
        j1, t1 = jiffies(p), time.time()
        prof = profile()
        cores = (j1 - j0) / 100.0 / (t1 - t0)

        def stage(name, st):
            v = prof.get(name, {}).get(st)
            return None if not v else [round(v['MeanMs'], 2), round(v['P95Ms'], 2)]

        res = {'label': label, 'env': env, 'workload': variant,
               'service_cores': round(cores, 3),
               'cpu_busy_A55_A76': ['%.0f' % (sum(c['A55'] for c in sampler.cpu) / len(sampler.cpu)), '%.0f' % (sum(c['A76'] for c in sampler.cpu) / len(sampler.cpu))],
               'gpu_load': round(sum(sampler.gpu) / len(sampler.gpu)),
               'mjpeg tile frames': [q.frames for q in pollers]}
        for name in ('ab-lenovo-vk', 'ab-left-vk', 'ab-right-vk'):
            short = name.replace('ab-', '')
            for st in ('detect', 'vk-gpu:submit+wait', 'vk-quads', 'vk-tags', 'process', 'age', 'interval'):
                v = stage(name, st)
                if v:
                    res['%s %s' % (short, st)] = v
        for name in ('ab-lenovo', 'ab-stereo'):
            v = stage(name, 'decode')
            if v:
                res['%s decode' % name.replace('ab-', '')] = v
        for name in ('ab-left-npu', 'ab-rec-stereo'):
            for st in ('infer', 'age', 'convert', 'encode'):
                v = stage(name, st)
                if v:
                    res['%s %s' % (name.replace('ab-', ''), st)] = v
        print(json.dumps(res))
    finally:
        for q in pollers:
            q.stop = True
        for sid in reversed(sinks):
            try: req('DELETE', '/sink/delete?SinkID=%d' % sid)
            except Exception as e: print('could not delete sink', sid, e)
        for sid in reversed(srcs):
            try: req('DELETE', '/source/delete?SourceID=%d' % sid)
            except Exception as e: print('could not delete source', sid, e)
        sh('rm -rf /tmp/ab-rec-*')


main()
