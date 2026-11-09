#!/usr/bin/env python3
"""Averages the runs of the PhotonVision / LumenVision comparison and prints the tables used in IMPLEMENTATION_STATUS.md.
Usage: compare_summary.py DIR     DIR holds pv-runs.jsonl, pv-od-runs.jsonl, pv-full-runs.jsonl, lv-vk-default.jsonl, lv-vk-tuned.jsonl, lv-cpu4.jsonl,
lv-npu-runs.jsonl, lv-full-vk.jsonl and lv-full-cpu.jsonl (the files the measurement scripts wrote; missing ones are skipped)."""
import json, os, sys, statistics as st

d = sys.argv[1]


def lines(name):
    path = os.path.join(d, name)
    return [json.loads(l) for l in open(path) if l.startswith('{')] if os.path.exists(path) else []


def mean(values):
    values = [v for v in values if v is not None]
    return st.mean(values) if values else None


def f(v, digits=1):
    return '-' if v is None else ('%.*f' % (digits, v))


print('## AprilTag on the Lenovo camera, 1280x720@30 (the scene held about 16-22 tags)')
print('system | decimation | result rate fps | latency ms (mean / p95) | process cores | RSS MB | GPU % | tags per frame')
pv = lines('pv-runs.jsonl')
for dec in (1, 2):
    rs = [r for r in pv if r['settings'].get('decimate') == dec]
    if rs:
        print('PhotonVision v2026.3.4 (CPU, 4 threads) | %d | %s | %s / %s | %s | %s | %s | %s' % (
            dec, f(mean(r['reported_fps'] for r in rs)), f(mean(r['latency_ms']['mean'] for r in rs)), f(mean(r['latency_ms']['p95'] for r in rs)),
            f(mean(r['board']['process_cores'] for r in rs), 2), f(mean(r['board']['rss_mb'] for r in rs), 0), f(mean(r['board']['gpu_load_pct'] for r in rs), 0), f(mean(r['tags_per_frame'] for r in rs))))
for name, label in (('lv-vk-default.jsonl', 'LumenVision Vulkan, shipped defaults'), ('lv-vk-tuned.jsonl', 'LumenVision Vulkan, ultrafast refine + margin 4'), ('lv-cpu4.jsonl', 'LumenVision CPU (libapriltag), 4 threads')):
    data = lines(name)
    for workload, dec in (('V', 2), ('V1', 1)):
        rs = [r for r in data if r['workload'] == workload]
        if not rs:
            continue
        interval = mean(r['lenovo-vk interval'][0] for r in rs)
        print('%s | %d | %s | %s / %s | %s | %s | %s | %s' % (
            label, dec, f(1000.0 / interval) if interval else '-', f(mean(r['lenovo-vk age'][0] for r in rs)), f(mean(r['lenovo-vk age'][1] for r in rs)),
            f(mean(r['service_cores'] for r in rs), 2), f(mean(r['rss_mb'] for r in rs), 0), f(mean(r['gpu_load'] for r in rs), 0), f(mean(r['tags_per_frame'] for r in rs))))

print()
print('## Object detection, YOLOv8n COCO (the same .rknn), Lenovo camera 1280x720@30')
print('system | result rate fps | latency ms | process cores | RSS MB | NPU % | detections per frame')
for r in lines('pv-od-runs.jsonl')[:1]:
    rs = lines('pv-od-runs.jsonl')
    print('PhotonVision | %s | %s | %s | %s | %s | %s' % (f(mean(x['reported_fps'] for x in rs)), f(mean(x['latency_ms']['mean'] for x in rs)), f(mean(x['board']['process_cores'] for x in rs), 2),
                                                          f(mean(x['board']['rss_mb'] for x in rs), 0), f(mean(x['board']['npu_load_pct'] for x in rs)), f(mean(x['tags_per_frame'] for x in rs), 2)))
rs = lines('lv-npu-runs.jsonl')
if rs:
    print('LumenVision | %s | %s | %s | %s | %s | %s' % (f(1000.0 / mean(x['stages_mean_p95_ms']['interval'][0] for x in rs)), f(mean(x['stages_mean_p95_ms']['age'][0] for x in rs)),
                                                         f(mean(x['board']['process_cores'] for x in rs), 2), f(mean(x['board']['rss_mb'] for x in rs), 0), f(mean(x['board']['npu_load_pct'] for x in rs)),
                                                         f(mean(x['detections_per_frame'] for x in rs), 2)))
    print('LumenVision stages (mean ms): ' + ', '.join('%s %s' % (k, f(mean(x['stages_mean_p95_ms'][k][0] for x in rs), 2)) for k in ('process', 'rknn-rga', 'rknn-run', 'rknn-decode+nms')))

print()
print('## AprilTag on the stereo camera, whole 2560x720@60 frame')
print('system | decimation | result rate fps | latency ms (mean / p95) | process cores | RSS MB | GPU %')
pv = lines('pv-full-runs.jsonl')
for dec in (1, 2):
    rs = [r for r in pv if r['settings'].get('decimate') == dec]
    if rs:
        print('PhotonVision (CPU, 4 threads) | %d | %s | %s / %s | %s | %s | %s' % (dec, f(mean(r['reported_fps'] for r in rs)), f(mean(r['latency_ms']['mean'] for r in rs)), f(mean(r['latency_ms']['p95'] for r in rs)),
                                                                            f(mean(r['board']['process_cores'] for r in rs), 2), f(mean(r['board']['rss_mb'] for r in rs), 0), f(mean(r['board']['gpu_load_pct'] for r in rs), 0)))
for name, label in (('lv-full-vk.jsonl', 'LumenVision Vulkan, shipped defaults'), ('lv-full-cpu.jsonl', 'LumenVision CPU (libapriltag), 4 threads')):
    data = lines(name)
    for workload, dec in (('V', 2), ('V1', 1)):
        rs = [r for r in data if r['workload'] == workload]
        if not rs:
            continue
        interval = mean(r['lenovo-vk interval'][0] for r in rs)
        print('%s | %d | %s | %s / %s | %s | %s | %s' % (label, dec, f(1000.0 / interval) if interval else '-', f(mean(r['lenovo-vk age'][0] for r in rs)), f(mean(r['lenovo-vk age'][1] for r in rs)),
                                                     f(mean(r['service_cores'] for r in rs), 2), f(mean(r['rss_mb'] for r in rs), 0), f(mean(r['gpu_load'] for r in rs), 0)))
