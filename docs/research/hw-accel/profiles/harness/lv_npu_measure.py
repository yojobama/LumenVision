#!/usr/bin/env python3
"""An object-detection node on the NPU, measured the way pv_measure.py measures PhotonVision's. Run on the Pi: lv_npu_measure.py LABEL SECONDS MODEL.rknn
Uploads the model (YOLOv8, 640 input, the Rockchip split-head export), makes the Lenovo camera source (1280x720@30 MJPEG), an Object Detection node with 0.25 / 0.45
cutoffs, waits 15 s, then measures SECONDS: the node's stage timers, the detections per frame (polled at 5 Hz), and the process / board resource use (sysmon.py)."""
import sys, json, time, uuid, urllib.parse, subprocess
label, seconds, model_path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
sys.argv = ['x']
exec(open('/tmp/mc_test.py').read().split('\ndef main():')[0])
LEN = '/dev/v4l/by-path/platform-fc880000.usb-usb-0:1:1.0-video-index0'


def multipart(fields, files):
    boundary = uuid.uuid4().hex
    body = b''
    for k, v in fields.items():
        body += ('--%s\r\nContent-Disposition: form-data; name="%s"\r\n\r\n%s\r\n' % (boundary, k, v)).encode()
    for k, (name, data) in files.items():
        body += ('--%s\r\nContent-Disposition: form-data; name="%s"; filename="%s"\r\nContent-Type: application/octet-stream\r\n\r\n' % (boundary, k, name)).encode() + data + b'\r\n'
    return body + ('--%s--\r\n' % boundary).encode(), 'multipart/form-data; boundary=' + boundary


model = src = sink = None
try:
    body, ctype = multipart({'name': 'bench-yolov8n', 'variant': 0, 'inputSize': 640, 'confThreshold': 0.25, 'nmsThreshold': 0.45}, {'model': ('yolov8nCOCO.rknn', open(model_path, 'rb').read())})
    model = int(req('POST', '/model/upload', body, {'Content-Type': ctype}))
    src = create_camera('bench-lenovo', LEN)
    set_mode(src, 1280, 720, 30)
    time.sleep(3)
    sink = int(req('POST', '/objectDetectionSink/create?name=bench-npu&modelId=%d' % model))
    req('PATCH', '/sink/bind?SinkID=%d&SourceID=%d' % (sink, src))
    req('PATCH', '/sink/toggle?SinkID=%d&Enabled=true' % sink)
    time.sleep(15)
    req('POST', '/profile/reset')
    mon = subprocess.Popen(['python3', '/tmp/sysmon.py', 'Server', str(seconds), '/tmp/sysmon-lv.json'])
    counts, classes = [], {}
    end = time.time() + seconds
    while time.time() < end:
        try:
            r = req('GET', '/sink/getResult?SinkID=%d' % sink)
            if isinstance(r, list):
                counts.append(len(r))
                for d in r:
                    classes[d.get('className')] = classes.get(d.get('className'), 0) + 1
        except Exception:
            pass
        time.sleep(0.2)
    mon.wait()
    prof = profile()
    stages = {s: [round(v['MeanMs'], 2), round(v['P95Ms'], 2)] for s, v in prof.get('bench-npu', {}).items()}
    board = json.load(open('/tmp/sysmon-lv.json'))
    print(json.dumps({'system': 'lumenvision', 'label': label, 'seconds': seconds, 'stages_mean_p95_ms': stages,
                      'detections_per_frame': round(sum(counts) / len(counts), 2) if counts else None, 'classes': classes, 'board': board}))
finally:
    for call in (lambda: sink and req('DELETE', '/sink/delete?SinkID=%d' % sink), lambda: src and req('DELETE', '/source/delete?SourceID=%d' % src),
                 lambda: model and req('DELETE', '/model/delete?id=%d' % model)):
        try:
            call()
        except Exception:
            pass
