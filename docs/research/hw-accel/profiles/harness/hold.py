#!/usr/bin/env python3
"""Builds one of ab_env.py's workloads, holds it while a profiler runs, then tears it down.
Usage: hold.py WORKLOAD OUTDIR -- command [args...]
The command runs through the shell after a 12 s warm-up (the stage timers are reset first); the stage timings of the held graph are written to
OUTDIR/stages.json afterwards. Unlike ab_env.py it does not restart the service. Needs /tmp/ab_env.py and /tmp/mc_test.py beside it."""
import sys
if '--' not in sys.argv or len(sys.argv) < 5:
    print(__doc__)
    sys.exit(2)
split = sys.argv.index('--')
workload, outdir = sys.argv[1], sys.argv[2]
command = ' '.join(sys.argv[split + 1:])
sys.argv = ['x', workload, 'hold']
exec(open('/tmp/ab_env.py').read().split('\ndef main():')[0])

import os
import subprocess
os.makedirs(outdir, exist_ok=True)
srcs, sinks, pollers = [], [], []
try:
    srcs, sinks, pollers = build()
    time.sleep(12)
    req('POST', '/profile/reset')
    t0 = time.time()
    rc = subprocess.run(command, shell=True).returncode
    json.dump({'workload': variant, 'command': command, 'seconds': round(time.time() - t0, 1), 'returncode': rc,
               'service_pid': pid(), 'stages': profile()}, open(os.path.join(outdir, 'stages.json'), 'w'), indent=1)
    print('held %s for %.1f s; command returned %d' % (variant, time.time() - t0, rc))
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
