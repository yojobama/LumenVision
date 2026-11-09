#!/usr/bin/env python3
"""What object tracking costs the running service. Run on the Pi: tracking_ab.py [SECONDS] [MODEL.onnx] [VIDEO]
Uploads a YOLOv8 ONNX model (CPU runtime; the board has no .rknn model on a fresh image), makes a video source from VIDEO (a photo with several people and a bus panned
back and forth, looping: made by make_bus_pan.py) and an Object Detection node on it, then measures the node's stage timers and the service's CPU for SECONDS (default 40) with tracking off
and with it on, and prints one JSON line. The model, source and node are removed afterwards."""
import sys, json, time, uuid, urllib.request
SECONDS = int(sys.argv[1]) if len(sys.argv) > 1 else 40
MODEL = sys.argv[2] if len(sys.argv) > 2 else '/home/runner/work/LumenVision/LumenVision/tests/data/yolov8n.onnx'
IMAGE = sys.argv[3] if len(sys.argv) > 3 else '/tmp/bus-pan.avi'
A = 'http://127.0.0.1:5800/api'
sys.argv = ['x']
exec(open('/tmp/mc_test.py').read().split('\ndef main():')[0])


def multipart(fields, files):
    boundary = uuid.uuid4().hex
    body = b''
    for k, v in fields.items():
        body += ('--%s\r\nContent-Disposition: form-data; name="%s"\r\n\r\n%s\r\n' % (boundary, k, v)).encode()
    for k, (name, data) in files.items():
        body += ('--%s\r\nContent-Disposition: form-data; name="%s"; filename="%s"\r\nContent-Type: application/octet-stream\r\n\r\n' % (boundary, k, name)).encode() + data + b'\r\n'
    body += ('--%s--\r\n' % boundary).encode()
    return body, 'multipart/form-data; boundary=' + boundary


def measure(sink_name, label):
    time.sleep(8)
    req('POST', '/profile/reset')
    p = pid()
    j0, t0 = jiffies(p), time.time()
    time.sleep(SECONDS)
    j1, t1 = jiffies(p), time.time()
    prof = profile()
    stages = {s: [round(v['MeanMs'], 3), round(v['P95Ms'], 3)] for s, v in prof.get(sink_name, {}).items()}
    return {'tracking': label, 'service_cores': round((j1 - j0) / 100.0 / (t1 - t0), 3), 'stages_mean_p95_ms': stages}


def jiffies(p):
    f = open('/proc/%s/stat' % p).read().rsplit(')', 1)[1].split()
    return int(f[11]) + int(f[12])


model_id = source = sink = None
try:
    body, ctype = multipart({'name': 'tracking-ab-yolo', 'variant': 0, 'inputSize': 640, 'confThreshold': 0.5, 'nmsThreshold': 0.45}, {'model': ('yolov8n.onnx', open(MODEL, 'rb').read())})
    model_id = int(req('POST', '/model/upload', body, {'Content-Type': ctype}))
    body, ctype = multipart({}, {'files': ('bus-pan.avi', open(IMAGE, 'rb').read())})
    ids = req('POST', '/videoFileSource/create', body, {'Content-Type': ctype})
    source = ids[0] if isinstance(ids, list) else int(ids)
    sink = int(req('POST', '/objectDetectionSink/create?name=tracking-ab&modelId=%d' % model_id))
    req('PATCH', '/sink/bind?SinkID=%d&SourceID=%d' % (sink, source))
    req('PATCH', '/sink/toggle?SinkID=%d&Enabled=true' % sink)
    results = [measure('tracking-ab', 'off')]
    req('PUT', '/objectDetectionSink/tracking?sinkId=%d&enabled=true' % sink)
    results.append(measure('tracking-ab', 'on'))
    last = req('GET', '/sink/getResult?SinkID=%d' % sink)
    print(json.dumps({'label': 'tracking-ab', 'results': results, 'sample_detection': (last or [None])[0] if isinstance(last, list) else last}))
finally:
    for call in (lambda: sink and req('DELETE', '/sink/delete?SinkID=%d' % sink), lambda: source and req('DELETE', '/source/delete?SourceID=%d' % source),
                 lambda: model_id and req('DELETE', '/model/delete?id=%d' % model_id)):
        try:
            call()
        except Exception:
            pass
