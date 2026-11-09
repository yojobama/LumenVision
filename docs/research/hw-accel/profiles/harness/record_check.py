#!/usr/bin/env python3
"""Does a recording play back at the speed it was captured at? Run on the Pi: record_check.py [SECONDS] [WIDTH HEIGHT FPS]
Records SECONDS (default 20) of the stereo camera with a RecordSink (the service's default encoder and bitrate), then reads the finished MP4's own timing
(fragment durations and sample counts) and prints its duration, frame count, real frame rate and size next to the wall time recorded."""
import sys, json, time, glob, os, struct, urllib.parse
SECONDS = int(sys.argv[1]) if len(sys.argv) > 1 else 20
W, H, FPS = (int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else (1280, 480, 60)
sys.argv = ['x']
exec(open('/tmp/mc_test.py').read().split('\ndef main():')[0])
STEREO = '/dev/v4l/by-path/platform-fc800000.usb-usb-0:1:1.0-video-index0'
FOLDER = '/tmp/rec-check'


def boxes(data, start, end):
    pos = start
    while pos + 8 <= end:
        size, kind = struct.unpack('>I4s', data[pos:pos + 8])
        header = 8
        if size == 1:
            size = struct.unpack('>Q', data[pos + 8:pos + 16])[0]
            header = 16
        elif size == 0:
            size = end - pos
        yield kind.decode('latin1'), pos + header, pos + size
        pos += size


def mp4_timing(path):
    data = open(path, 'rb').read()
    timescale, end_ticks, samples = None, 0, 0
    for kind, s, e in boxes(data, 0, len(data)):
        if kind == 'moov':
            for k1, s1, e1 in boxes(data, s, e):
                if k1 == 'trak':
                    for k2, s2, e2 in boxes(data, s1, e1):
                        if k2 == 'mdia':
                            for k3, s3, e3 in boxes(data, s2, e2):
                                if k3 == 'mdhd':
                                    version = data[s3]
                                    timescale = struct.unpack('>I', data[s3 + (20 if version else 12):s3 + (24 if version else 16)])[0]
        if kind == 'moof':
            for k1, s1, e1 in boxes(data, s, e):
                if k1 != 'traf':
                    continue
                base, default_dur = 0, 0
                for k2, s2, e2 in boxes(data, s1, e1):
                    if k2 == 'tfhd':
                        flags = struct.unpack('>I', b'\0' + data[s2 + 1:s2 + 4])[0]
                        p = s2 + 8
                        if flags & 1: p += 8
                        if flags & 2: p += 4
                        if flags & 8: default_dur = struct.unpack('>I', data[p:p + 4])[0]
                    elif k2 == 'tfdt':
                        version = data[s2]
                        base = struct.unpack('>Q', data[s2 + 4:s2 + 12])[0] if version else struct.unpack('>I', data[s2 + 4:s2 + 8])[0]
                    elif k2 == 'trun':
                        flags = struct.unpack('>I', b'\0' + data[s2 + 1:s2 + 4])[0]
                        count = struct.unpack('>I', data[s2 + 4:s2 + 8])[0]
                        p = s2 + 8
                        if flags & 1: p += 4
                        if flags & 4: p += 4
                        total = 0
                        for _ in range(count):
                            dur = default_dur
                            if flags & 0x100:
                                dur = struct.unpack('>I', data[p:p + 4])[0]; p += 4
                            if flags & 0x200: p += 4
                            if flags & 0x400: p += 4
                            if flags & 0x800: p += 4
                            total += dur
                        samples += count
                        end_ticks = max(end_ticks, base + total)
    return timescale, end_ticks, samples


os.system('rm -rf %s' % FOLDER)
src = sink = None
try:
    src = create_camera('rec-check', STEREO)
    set_mode(src, W, H, FPS)
    time.sleep(3)
    sink = int(req('POST', '/recordSink/create?' + urllib.parse.urlencode({'name': 'rec-check', 'dstFolder': FOLDER, 'segmentSeconds': 600})))
    req('PATCH', '/sink/bind?SinkID=%d&SourceID=%d' % (sink, src))
    t0 = time.time()
    req('PATCH', '/sink/toggle?SinkID=%d&Enabled=true' % sink)
    time.sleep(SECONDS)
    req('PATCH', '/sink/toggle?SinkID=%d&Enabled=false' % sink)
    wall = time.time() - t0
    time.sleep(2)
finally:
    for call in (lambda: sink and req('DELETE', '/sink/delete?SinkID=%d' % sink), lambda: src and req('DELETE', '/source/delete?SourceID=%d' % src)):
        try:
            call()
        except Exception:
            pass
for path in sorted(glob.glob(FOLDER + '/*.mp4')):
    timescale, ticks, samples = mp4_timing(path)
    duration = ticks / float(timescale) if timescale else 0
    print(json.dumps({'file': os.path.basename(path), 'mode': '%dx%d@%d' % (W, H, FPS), 'wall_s': round(wall, 2), 'clip_duration_s': round(duration, 2),
                      'frames': samples, 'clip_fps': round(samples / duration, 1) if duration else None, 'size_mb': round(os.path.getsize(path) / 1e6, 2),
                      'mbit_per_s': round(os.path.getsize(path) * 8 / 1e6 / max(duration, 1e-9), 2)}))
