#!/usr/bin/env python3
"""What the camera's exposure does to detection quality, on a static scene. Run on the Pi: exposure_accuracy.py [SECONDS_PER_STEP]
One Vulkan AprilTag detector (decimation 1, shipped tuning) on the Lenovo camera at 1280x720@30. For each step the camera's auto exposure is switched off and its
"Exposure Time, Absolute" set (units of 0.1 ms), or auto exposure is turned on; after 4 s the detector's latest result is polled five times a second. Per step it prints
the tags found per frame, how many tags were present in at least 90% of the polls, the share of polls those tags were missing from, and the corner jitter: the standard
deviation of each corner's position over the polls (pixels), averaged over those stable tags. The scene does not move, so the jitter is the repeatability of the corners
(noise, blur and flicker), not their absolute accuracy."""
import json, sys, time, statistics as st
seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 25
sys.argv = ['x']
exec(open('/tmp/mc_test.py').read().split('\ndef main():')[0])
LEN = '/dev/v4l/by-path/platform-fc880000.usb-usb-0:1:1.0-video-index0'
VK_EXTRA.clear()
VK_EXTRA['quadDecimate'] = 1
src = create_camera('exp-cam', LEN)
set_mode(src, 1280, 720, 30)
time.sleep(2)
sink = vk('exp-vk', src, 1280, 720)
time.sleep(4)
controls = {c['Name']: c for c in req('GET', '/cameraSource/%d/controlList' % src)}
auto, exposure = controls['Auto Exposure'], controls['Exposure Time, Absolute']
saved = (auto['Value'], exposure['Value'])


def poll(duration):
    polls = []
    end = time.time() + duration
    while time.time() < end:
        r = req('GET', '/sink/getResult?SinkID=%d' % sink)
        polls.append({t['id']: t['corners'] for t in (r or {}).get('tags', [])} if isinstance(r, dict) else {})
        time.sleep(0.2)
    return polls


def summarise(label, polls):
    n = len(polls)
    seen = {}
    for p in polls:
        for i, c in p.items():
            seen.setdefault(i, []).append(c)
    stable = [i for i, v in seen.items() if len(v) >= 0.9 * n]
    jitter = []
    for i in stable:
        for corner in range(4):
            xs = [c[corner][0] for c in seen[i]]
            ys = [c[corner][1] for c in seen[i]]
            jitter.append(((st.pstdev(xs) ** 2 + st.pstdev(ys) ** 2) / 2) ** 0.5)
    missing = 1 - (sum(len(seen[i]) for i in stable) / (n * len(stable))) if stable else None
    print(json.dumps({'step': label, 'polls': n, 'tags_per_frame': round(sum(len(p) for p in polls) / n, 2), 'stable_tags': len(stable), 'distinct_tags_ever': len(seen),
                      'stable_tags_missing_pct': None if missing is None else round(100 * missing, 2),
                      'corner_jitter_px': round(st.mean(jitter), 4) if jitter else None}), flush=True)


try:
    req('PATCH', '/cameraSource/%d/control?controlId=%d&value=3' % (src, auto['Id']))
    time.sleep(4)
    summarise('auto exposure', poll(seconds))
    req('PATCH', '/cameraSource/%d/control?controlId=%d&value=1' % (src, auto['Id']))
    for value in (5, 10, 20, 40, 80, 150, 312):
        req('PATCH', '/cameraSource/%d/control?controlId=%d&value=%d' % (src, exposure['Id'], value))
        time.sleep(4)
        summarise('manual %.1f ms' % (value / 10.0), poll(seconds))
finally:
    req('PATCH', '/cameraSource/%d/control?controlId=%d&value=%d' % (src, exposure['Id'], saved[1]))
    req('PATCH', '/cameraSource/%d/control?controlId=%d&value=%d' % (src, auto['Id'], 3))
    req('DELETE', '/sink/delete?SinkID=%d' % sink)
    req('DELETE', '/source/delete?SourceID=%d' % src)
