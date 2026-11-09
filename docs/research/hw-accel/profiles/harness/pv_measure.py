#!/usr/bin/env python3
"""Measures a running PhotonVision (its web socket on the Pi) and the Pi's resource use while it does, from a PC on the same network.
  pv_measure.py LABEL SECONDS [NAME=VALUE ...]     NAME=VALUE pipeline settings to apply first (e.g. decimate=2 threads=4), then 15 s of warm-up
Needs: pip install websocket-client msgpack; plink/pscp with the board's credentials in PI_HOST / PI_PASS / PI_HOSTKEY, and sysmon.py already in /tmp on the Pi.
Prints one JSON line: the frame rate PhotonVision reports and the result stream really delivers, its capture-to-publish latency (mean, median, p95),
tags per frame and the ids seen, and the board's resource use (sysmon.py)."""
import sys, os, json, time, subprocess, statistics
import msgpack, websocket

HOST = os.environ.get('PI_HOST', '192.168.68.125')
PASS = os.environ.get('PI_PASS', 'vision')
HOSTKEY = os.environ.get('PI_HOSTKEY', 'SHA256:4YuJ3eVmyMI4D9G5NdbT2ztnqSBLmtuh/N+IR9MzdIg')
label, seconds = sys.argv[1], int(sys.argv[2])
settings = {}
for a in sys.argv[3:]:
    k, v = a.split('=', 1)
    settings[k] = json.loads(v)


def ssh(cmd, background=False):
    base = ['plink', '-batch', '-pw', PASS, '-hostkey', HOSTKEY, 'lumen@' + HOST, cmd]
    return subprocess.Popen(base, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) if background else subprocess.run(base, capture_output=True, text=True).stdout


def messages(ws, duration):
    end = time.time() + duration
    ws.settimeout(1.0)
    while time.time() < end:
        try:
            m = ws.recv()
        except Exception:
            continue
        if isinstance(m, bytes):
            yield msgpack.unpackb(m, raw=False)


ws = websocket.create_connection('ws://%s:5800/websocket_data' % HOST, timeout=10)
uid = None
for d in messages(ws, 5):
    if d.get('cameraSettings'):
        uid = d['cameraSettings'][0]['uniqueName']
        break
assert uid, 'PhotonVision has no active camera'
for k, v in settings.items():
    ws.send_binary(msgpack.packb({'changePipelineSetting': {'cameraUniqueName': uid, k: v}}, use_bin_type=True))
time.sleep(15)

mon = ssh('python3 /tmp/sysmon.py "photonvision.jar" %d /tmp/sysmon-pv.json' % seconds, background=True)
seen, latencies, per_frame, ids, reported_fps = {}, [], [], set(), []
for d in messages(ws, seconds):
    result = d.get('updatePipelineResult')
    if not result:
        continue
    for cam, v in result.items():
        if v['sequenceID'] in seen:
            continue
        seen[v['sequenceID']] = 1
        latencies.append(v['latency'])
        per_frame.append(len(v['targets']))
        reported_fps.append(v['fps'])
        ids.update(t['fiducialId'] for t in v['targets'])
mon.wait()
sysmon = json.loads(ssh('cat /tmp/sysmon-pv.json') or '{}')
latencies.sort()
res = {'system': 'photonvision v2026.3.4', 'label': label, 'settings': settings, 'seconds': seconds,
       'frames': len(seen), 'delivered_fps': round(len(seen) / seconds, 2), 'reported_fps': round(statistics.mean(reported_fps), 2) if reported_fps else None,
       'latency_ms': {'mean': round(statistics.mean(latencies), 2), 'median': round(latencies[len(latencies) // 2], 2), 'p95': round(latencies[int(len(latencies) * 0.95)], 2)} if latencies else None,
       'tags_per_frame': round(statistics.mean(per_frame), 2) if per_frame else None, 'distinct_ids': sorted(ids), 'board': sysmon}
print(json.dumps(res))
